// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/AprsReceiver.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace dsdr::core {
AprsReceiver::AprsReceiver(std::size_t slots, std::size_t maxFrames, std::size_t frameCapacity, WorkerHook hook, void *hookContext)
    : m_workerHook(hook), m_hookContext(hookContext), m_mono(maxFrames),
      m_messages(frameCapacity), m_decoder(receive, this),
      m_async(slots, maxFrames, {this, resetWorker, nullptr, processWorker}) {
    if (!frameCapacity || !maxFrames || maxFrames > 1048576) throw std::invalid_argument("invalid APRS queue capacity");
}
AprsReceiver::~AprsReceiver() { stop(); }
void AprsReceiver::start() { m_async.start(); }
void AprsReceiver::stop() noexcept { discontinuity(); m_async.stop(false); }
void AprsReceiver::discontinuity() noexcept { m_generation.fetch_add(1); }
void AprsReceiver::setEnabled(bool enabled) noexcept {
    if (m_enabled.load() == enabled) return;
    m_updating.store(true); discontinuity(); m_enabled.store(enabled); discontinuity(); m_updating.store(false);
}
void AprsReceiver::selectChannel(int id) noexcept {
    if (m_selected.load() == id) return;
    m_updating.store(true); discontinuity(); m_present.store(false); m_selected.store(id);
    discontinuity(); m_updating.store(false);
}
void AprsReceiver::setChannelState(int id, bool present, bool nfm, double rate) noexcept {
    if (id != m_selected.load()) return;
    if (m_stateChannel.load() == id && m_present.load() == present && m_nfm.load() == nfm && m_rate.load() == rate) return;
    m_updating.store(true); discontinuity(); m_present.store(false);
    m_stateChannel.store(id); m_nfm.store(nfm); m_rate.store(rate);
    m_present.store(present); discontinuity(); m_updating.store(false);
}
AprsReceiver::Status AprsReceiver::status() const noexcept {
    if (!m_enabled.load()) return Status::Disabled;
    if (m_updating.load()) return Status::NoSource;
    if (m_selected.load() < 0) return Status::NoChannel;
    if (!m_present.load() || m_stateChannel.load() != m_selected.load()) return Status::NoSource;
    if (!m_nfm.load()) return Status::NotNfm;
    const auto r = m_rate.load();
    if (!std::isfinite(r) || r < 8000 || r > 192000) return Status::WrongRate;
    return Status::Active;
}
AsyncIqDecoder::SubmitResult AprsReceiver::submit(int id, const dsp::Complex *samples, std::size_t count) noexcept {
    return submitForGeneration(id, samples, count, m_generation.load());
}
AsyncIqDecoder::SubmitResult AprsReceiver::submitForGeneration(int id, const dsp::Complex *samples, std::size_t count, std::uint64_t epoch) noexcept {
    using R = AsyncIqDecoder::SubmitResult;
    if (id != m_selected.load() || status() != Status::Active || epoch != m_generation.load()) return R::Stopped;
    if (!count) return R::Accepted;
    const auto result = m_async.submit(samples, count, epoch);
    if (result != R::Accepted) discontinuity();
    return result;
}
void AprsReceiver::resetWorker(void *ctx) noexcept {
    auto &self = *static_cast<AprsReceiver *>(ctx);
    self.m_decoder.reset(); self.m_workerGeneration = 0;
}
void AprsReceiver::processWorker(void *ctx, const dsp::Complex *samples, std::size_t count,
                                 std::uint64_t epoch, std::chrono::steady_clock::time_point) noexcept {
    auto &s = *static_cast<AprsReceiver *>(ctx);
    if (s.m_workerHook) s.m_workerHook(s.m_hookContext);
    if (!count) return;
    if (epoch != s.m_generation.load() || s.status() != Status::Active) return;
    try {
        if (s.m_workerGeneration != epoch) {
            if (!s.m_decoder.configure(s.m_rate.load())) return;
            s.m_workerGeneration = epoch;
        }
        for (std::size_t i = 0; i < count; ++i)
            s.m_mono[i] = 0.5f * samples[i].real() + 0.5f * samples[i].imag();
        s.m_decoder.process(s.m_mono.data(), count);
    } catch (...) { s.m_decoder.reset(); s.m_workerGeneration = 0; }
}
void AprsReceiver::receive(void *ctx, const dsp::AprsAfskDecoder::Frame &frame) {
    auto &s = *static_cast<AprsReceiver *>(ctx);
    if (s.m_workerGeneration != s.m_generation.load() || s.status() != Status::Active) return;
    Message m; m.generation = s.m_workerGeneration; m.size = frame.size();
    if (m.size > m.bytes.size()) return;
    std::copy(frame.begin(), frame.end(), m.bytes.begin());
    if (s.m_messages.write(&m, 1) != 1) s.m_droppedFrames.fetch_add(1);
}
AprsReceiver::ReadResult AprsReceiver::readOne(Message &m) noexcept {
    if (!m_messages.read(&m, 1)) { m = {}; return ReadResult::Empty; }
    if (m.generation != m_generation.load() || status() != Status::Active) {
        m = {}; return ReadResult::Stale;
    }
    return ReadResult::Message;
}
bool AprsReceiver::read(Message &m) noexcept {
    for (;;) {
        const auto result = readOne(m);
        if (result == ReadResult::Message) return true;
        if (result == ReadResult::Empty) return false;
    }
}
}
