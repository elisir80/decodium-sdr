// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/AdsbReceiver.h"
namespace dsdr::core {
AdsbReceiver::AdsbReceiver(WorkerHook workerHook, void *hookContext)
    : m_workerHook(workerHook), m_hookContext(hookContext)
    , m_decoder(receive, this)
    , m_async(32, dsp::kMaxBlockFrames, {this, resetDecoder, nullptr, processBlock})
{
    m_async.start();
}
AdsbReceiver::~AdsbReceiver() { m_async.stop(false); }
void AdsbReceiver::setEnabled(bool enabled) noexcept {
    if (m_enabled.exchange(enabled) != enabled) discontinuity();
}
void AdsbReceiver::setSource(bool present, bool audio, double rate, std::int64_t center) noexcept {
    m_present.store(false); discontinuity();
    m_audio.store(audio); m_rate.store(rate); m_center.store(center);
    m_present.store(present);
}
void AdsbReceiver::setCenter(std::int64_t center) noexcept {
    if (m_center.exchange(center) != center) discontinuity();
}
AdsbReceiver::Status AdsbReceiver::status() const noexcept {
    if (!m_enabled.load()) return Status::Disabled;
    if (!m_present.load()) return Status::NoSource;
    if (m_audio.load()) return Status::AudioSource;
    if (m_rate.load() != 2000000.0) return Status::WrongRate;
    if (m_center.load() != 1090000000) return Status::WrongFrequency;
    return Status::Active;
}
void AdsbReceiver::process(const dsp::Complex *iq, std::size_t count) noexcept {
    process(iq, count, std::chrono::steady_clock::now());
}
void AdsbReceiver::process(const dsp::Complex *iq, std::size_t count, std::chrono::steady_clock::time_point ingress) noexcept {
    const auto generation = m_generation.load(std::memory_order_acquire);
    if (status() != Status::Active || generation != m_generation.load(std::memory_order_acquire)) return;
    const auto result = m_async.submit(iq, count, generation, ingress);
    if (result != AsyncIqDecoder::SubmitResult::Accepted && result != AsyncIqDecoder::SubmitResult::Stopped)
        discontinuity();
}
void AdsbReceiver::resetDecoder(void *ctx) noexcept {
    auto *self = static_cast<AdsbReceiver *>(ctx);
    self->m_decoder.reset();
    self->m_decoderGeneration = 0;
}
void AdsbReceiver::processBlock(void *ctx, const dsp::Complex *iq, std::size_t count, std::uint64_t generation, std::chrono::steady_clock::time_point ingress) noexcept {
    auto *self = static_cast<AdsbReceiver *>(ctx);
    if (self->m_workerHook) self->m_workerHook(self->m_hookContext);
    if (generation != self->m_generation.load(std::memory_order_acquire)) return;
    if (generation != self->m_decoderGeneration) {
        self->m_decoder.reset();
        self->m_decoderGeneration = generation;
    }
    self->m_decoderTimestamp = ingress;
    self->m_decoder.process(iq, count);
}
void AdsbReceiver::receive(void *ctx, const dsp::AdsbDecoder::Frame &frame) noexcept {
    auto *self = static_cast<AdsbReceiver *>(ctx);
    Message message{frame, self->m_decoderGeneration, self->m_decoderTimestamp};
    if (self->m_messages.write(&message, 1) != 1) self->m_dropped.fetch_add(1);
}
bool AdsbReceiver::read(dsp::AdsbDecoder::Frame &frame) noexcept {
    Message message;
    if (!read(message)) return false;
    frame = message.frame; return true;
}
bool AdsbReceiver::read(Message &message) noexcept {
    // The producer may keep filling the queue while stale frames are being
    // discarded. Bound work on the caller's thread to one queue capacity.
    for (int i=0; i<256 && m_messages.read(&message, 1); ++i) {
        if (message.generation == m_generation.load() && status() == Status::Active) {
            return true;
        }
    }
    return false;
}
}
