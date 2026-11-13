// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/PocsagReceiver.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace dsdr::core {
static_assert(std::atomic<double>::is_always_lock_free,
              "managed POCSAG parameters require lock-free atomics");
namespace {
std::size_t checkedCapacity(std::size_t capacity) {
    if (!capacity || capacity > 4096) throw std::invalid_argument("invalid message capacity");
    return capacity;
}
}
PocsagReceiver::PocsagReceiver(std::size_t slots, std::size_t maxFrames,
        std::size_t capacity, WorkerHook hook, void *context)
    : m_hook(hook), m_hookContext(context), m_messages(checkedCapacity(capacity)),
      m_decoder(&PocsagReceiver::receive, this),
      m_async(slots, maxFrames, {this, &PocsagReceiver::resetWorker, nullptr, &PocsagReceiver::processWorker}) {}
PocsagReceiver::~PocsagReceiver() { stop(); }
bool PocsagReceiver::configure(unsigned rate, unsigned baud, double hz) {
    if (m_started) return false;
    ++m_generation;
    m_configured = false;
    m_configured = m_decoder.configure(rate, baud, hz);
    return m_configured;
}
bool PocsagReceiver::start() {
    if (!m_configured || m_started) return false;
    ++m_generation;
    m_messages.clear(); // consumer is quiescent during lifecycle transitions
    m_workerGeneration = m_generation.load();
    m_async.start(); m_started = true; return true;
}
bool PocsagReceiver::startManaged() {
    if (m_started) return false;
    ++m_generation;
    m_managedEnabled.store(false);
    m_managedValid.store(false);
    m_managedApplied.store(0);
    m_managedFailed.store(0);
    m_managed.store(true);
    try { m_async.start(); }
    catch (...) { m_managed.store(false); throw; }
    m_started = true;
    return true;
}
bool PocsagReceiver::configureManaged(unsigned rate, unsigned baud, double hz) noexcept {
    if (!m_managed.load()) return false;
    const bool valid = rate >= 48000 && rate <= 2000000 &&
        (baud == 512 || baud == 1200 || baud == 2400) && std::isfinite(hz) &&
        std::abs(hz) + 12000 < rate / 2.0;
    m_managedUpdating.store(true);
    ++m_generation;
    m_managedRate.store(rate); m_managedBaud.store(baud); m_managedOffset.store(hz);
    m_managedValid.store(valid);
    ++m_generation;
    m_managedUpdating.store(false);
    return valid;
}
void PocsagReceiver::setManagedEnabled(bool enabled) noexcept {
    if (!m_managed.load() || m_managedEnabled.load() == enabled) return;
    m_managedUpdating.store(true); ++m_generation;
    m_managedEnabled.store(enabled);
    ++m_generation; m_managedUpdating.store(false);
}
PocsagReceiver::ManagedStatus PocsagReceiver::managedStatus() const noexcept {
    if (!m_managed.load()) return ManagedStatus::Stopped;
    if (!m_managedEnabled.load()) return ManagedStatus::Disabled;
    if (m_managedUpdating.load()) return ManagedStatus::Configuring;
    if (!m_managedValid.load()) return ManagedStatus::InvalidConfiguration;
    const auto epoch = m_generation.load();
    if (m_managedFailed.load() == epoch) return ManagedStatus::Error;
    return m_managedApplied.load() == epoch ? ManagedStatus::Active : ManagedStatus::Configuring;
}
void PocsagReceiver::stop(bool drain) noexcept {
    if (!m_started) return;
    if (!drain) ++m_generation; // invalidate worker and unread results immediately
    m_async.stop(drain);
    m_started = false;
    m_managed.store(false);
}
AsyncIqDecoder::SubmitResult PocsagReceiver::submit(const dsp::Complex *samples, std::size_t count) noexcept {
    return submitForGeneration(samples, count, m_generation.load());
}
AsyncIqDecoder::SubmitResult PocsagReceiver::submitForGeneration(const dsp::Complex *samples, std::size_t count, std::uint64_t epoch) noexcept {
    if (m_managed.load() && (m_managedUpdating.load() || !m_managedEnabled.load() ||
        !m_managedValid.load() || epoch != m_generation.load())) return AsyncIqDecoder::SubmitResult::Stopped;
    const auto result = m_async.submit(samples, count, epoch);
    if (result == AsyncIqDecoder::SubmitResult::Full || result == AsyncIqDecoder::SubmitResult::TooLarge || result == AsyncIqDecoder::SubmitResult::Invalid)
        ++m_generation;
    return result;
}
void PocsagReceiver::discontinuity() noexcept { ++m_generation; m_async.discontinuity(); }
PocsagReceiver::ReadResult PocsagReceiver::readOne(Message &message) noexcept {
    if (!m_messages.read(&message, 1)) { message = {}; return ReadResult::Empty; }
    if (message.generation != m_generation.load()) { message = {}; return ReadResult::Stale; }
    return ReadResult::Message;
}
bool PocsagReceiver::read(Message &message) noexcept {
    for (;;) {
        const auto result = readOne(message);
        if (result == ReadResult::Message) return true;
        if (result == ReadResult::Empty) return false;
    }
}
void PocsagReceiver::resetWorker(void *ctx) noexcept {
    auto *self = static_cast<PocsagReceiver *>(ctx);
    self->m_decoder.reset(); ++self->m_resetCount;
}
void PocsagReceiver::processWorker(void *ctx, const dsp::Complex *samples, std::size_t count,
                                  std::uint64_t epoch, std::chrono::steady_clock::time_point) noexcept {
    auto *self = static_cast<PocsagReceiver *>(ctx);
    if (self->m_hook) self->m_hook(self->m_hookContext);
    if (epoch != self->m_generation.load()) return;
    try {
        if (self->m_workerGeneration != epoch) {
            if (self->m_managed.load()) {
                if (self->m_managedUpdating.load() || !self->m_managedEnabled.load() || !self->m_managedValid.load()) return;
                const auto rate=self->m_managedRate.load(), baud=self->m_managedBaud.load();
                const auto offset=self->m_managedOffset.load();
                if (epoch != self->m_generation.load()) return;
                if (!self->m_decoder.configure(rate,baud,offset)) { self->m_managedFailed.store(epoch); return; }
                if (epoch != self->m_generation.load()) return;
                self->m_managedApplied.store(epoch);
            } else self->m_decoder.reset();
            self->m_workerGeneration = epoch;
        }
        self->m_decoder.process(samples, count);
    } catch (...) { self->m_decoder.reset(); ++self->m_workerErrors; self->m_managedFailed.store(epoch); }
}
void PocsagReceiver::receive(void *ctx, const dsp::PocsagDecoder::Message &decoded) {
    auto *self = static_cast<PocsagReceiver *>(ctx);
    if (self->m_workerGeneration != self->m_generation.load()) return;
    Message msg;
    if (decoded.alpha.size() > msg.text.size()) { ++self->m_droppedMessages; return; }
    msg.address = decoded.address; msg.function = decoded.function;
    msg.correctedBits = decoded.correctedBits; msg.generation = self->m_workerGeneration;
    msg.size = decoded.alpha.size();
    std::copy(decoded.alpha.begin(), decoded.alpha.end(), msg.text.begin());
    if (!self->m_messages.write(&msg, 1)) ++self->m_droppedMessages;
}
}
