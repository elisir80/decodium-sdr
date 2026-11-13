// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/AsyncIqDecoder.h"
#include <chrono>
#include <limits>
#include <stdexcept>

namespace dsdr::core {
static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
              "DSP queue requires lock-free 64-bit atomics");
static_assert(std::atomic<bool>::is_always_lock_free,
              "DSP queue requires lock-free boolean atomics");

AsyncIqDecoder::AsyncIqDecoder(std::size_t slots, std::size_t maxFrames, Sink sink)
    : m_slots(slots), m_maxFrames(maxFrames), m_sink(sink)
{
    if (!slots || !maxFrames || !sink.reset || (!sink.process && !sink.processTagged) ||
        slots > std::numeric_limits<std::size_t>::max() / maxFrames ||
        slots > std::numeric_limits<std::uint64_t>::max() / 2)
        throw std::invalid_argument("invalid async IQ decoder configuration");
    m_meta = std::make_unique<Slot[]>(slots);
    m_samples = std::make_unique<dsp::Complex[]>(slots * maxFrames);
}
AsyncIqDecoder::~AsyncIqDecoder() { stop(); }
void AsyncIqDecoder::start()
{
    if (m_worker.joinable()) return;
    m_write.store(0, std::memory_order_relaxed);
    m_read.store(0, std::memory_order_relaxed);
    m_gap = true;
    m_drain.store(true, std::memory_order_relaxed);
    m_running.store(true, std::memory_order_release);
    try { m_worker = std::thread(&AsyncIqDecoder::run, this); }
    catch (...) { m_running.store(false, std::memory_order_release); throw; }
}
void AsyncIqDecoder::stop(bool drain) noexcept
{
    m_drain.store(drain, std::memory_order_relaxed);
    m_running.store(false, std::memory_order_release);
    if (m_worker.joinable()) m_worker.join();
}
AsyncIqDecoder::SubmitResult AsyncIqDecoder::submit(const dsp::Complex *samples, std::size_t count, std::uint64_t tag, std::chrono::steady_clock::time_point timestamp) noexcept
{
    if (!m_running.load(std::memory_order_acquire)) return SubmitResult::Stopped;
    if (!count) return SubmitResult::Accepted;
    SubmitResult error = SubmitResult::Accepted;
    if (!samples) error = SubmitResult::Invalid;
    else if (count > m_maxFrames) error = SubmitResult::TooLarge;
    const auto w = m_write.load(std::memory_order_relaxed);
    if (error == SubmitResult::Accepted && w - m_read.load(std::memory_order_acquire) >= m_slots)
        error = SubmitResult::Full;
    if (error != SubmitResult::Accepted) {
        m_gap = true;
        m_droppedBlocks.fetch_add(1, std::memory_order_relaxed);
        m_droppedSamples.fetch_add(count, std::memory_order_relaxed);
        return error;
    }
    const auto index = static_cast<std::size_t>(w % m_slots);
    auto *dst = m_samples.get() + index * m_maxFrames;
    for (std::size_t i = 0; i < count; ++i) dst[i] = samples[i];
    m_meta[index] = {count, m_gap, tag, timestamp};
    m_gap = false;
    m_write.store(w + 1, std::memory_order_release);
    m_accepted.fetch_add(1, std::memory_order_relaxed);
    return SubmitResult::Accepted;
}
AsyncIqDecoder::Stats AsyncIqDecoder::stats() const noexcept
{
    return {m_accepted.load(std::memory_order_relaxed), m_droppedBlocks.load(std::memory_order_relaxed),
            m_droppedSamples.load(std::memory_order_relaxed)};
}
void AsyncIqDecoder::run() noexcept
{
    for (;;) {
        if (!m_running.load(std::memory_order_acquire) && !m_drain.load(std::memory_order_relaxed)) break;
        const auto r = m_read.load(std::memory_order_relaxed);
        if (r == m_write.load(std::memory_order_acquire)) {
            if (!m_running.load(std::memory_order_acquire)) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        const auto index = static_cast<std::size_t>(r % m_slots);
        if (m_meta[index].reset) m_sink.reset(m_sink.context);
        if (m_sink.processTagged)
            m_sink.processTagged(m_sink.context, m_samples.get() + index * m_maxFrames, m_meta[index].count, m_meta[index].tag, m_meta[index].timestamp);
        else
            m_sink.process(m_sink.context, m_samples.get() + index * m_maxFrames, m_meta[index].count);
        m_read.store(r + 1, std::memory_order_release);
    }
}
} // namespace dsdr::core
