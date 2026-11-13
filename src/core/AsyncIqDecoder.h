// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "dsp/DspTypes.h"
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <thread>

namespace dsdr::core {

// One DSP producer and one internal consumer. Control methods require the
// producer to be quiescent and must be serialized by the owner. The decoder
// and its context must outlive stop(); callbacks run only on the worker.
class AsyncIqDecoder final {
public:
    struct Sink {
        void *context;
        void (*reset)(void *) noexcept;
        void (*process)(void *, const dsp::Complex *, std::size_t) noexcept;
        // Optional tagged callback; when set, used instead of process.
        void (*processTagged)(void *, const dsp::Complex *, std::size_t, std::uint64_t, std::chrono::steady_clock::time_point) noexcept = nullptr;
    };
    enum class SubmitResult { Accepted, Full, TooLarge, Invalid, Stopped };
    struct Stats { std::uint64_t accepted, droppedBlocks, droppedSamples; };

    // Allocates all queue storage. slots and maxFrames must be nonzero.
    AsyncIqDecoder(std::size_t slots, std::size_t maxFrames, Sink sink);
    ~AsyncIqDecoder();
    AsyncIqDecoder(const AsyncIqDecoder &) = delete;
    AsyncIqDecoder &operator=(const AsyncIqDecoder &) = delete;

    // start may throw (thread creation). stop joins; it can either drain or
    // discard queued blocks. A callback that never returns prevents stop.
    void start();
    void stop(bool drain = true) noexcept;

    // Producer only: no allocation, locks, wakeups, or waiting. Input is copied.
    // Zero-length input is a no-op; invalid/oversized blocks create a gap.
    SubmitResult submit(const dsp::Complex *samples, std::size_t count, std::uint64_t tag = 0,
                        std::chrono::steady_clock::time_point timestamp = {}) noexcept;
    // Producer only, before the first block after a source/rate/tuning gap.
    void discontinuity() noexcept { m_gap = true; }
    Stats stats() const noexcept;

private:
    struct Slot { std::size_t count = 0; bool reset = false; std::uint64_t tag = 0; std::chrono::steady_clock::time_point timestamp{}; };
    void run() noexcept;
    const std::size_t m_slots, m_maxFrames;
    const Sink m_sink;
    std::unique_ptr<Slot[]> m_meta;
    std::unique_ptr<dsp::Complex[]> m_samples;
    alignas(64) std::atomic<std::uint64_t> m_write{0};
    alignas(64) std::atomic<std::uint64_t> m_read{0};
    std::atomic<std::uint64_t> m_accepted{0}, m_droppedBlocks{0}, m_droppedSamples{0};
    std::atomic<bool> m_running{false}, m_drain{true};
    bool m_gap = true; // producer only, reset while producer is stopped
    std::thread m_worker;
};
} // namespace dsdr::core
