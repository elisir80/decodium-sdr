// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "dsp/AdsbDecoder.h"
#include "core/AsyncIqDecoder.h"
#include "dsp/SpscRing.h"
#include <atomic>
#include <chrono>
#include <cstdint>

namespace dsdr::core {
// UI controls and DSP producer communicate only through atomics and an SPSC queue.
class AdsbReceiver {
public:
    enum class Status { Disabled, NoSource, AudioSource, WrongRate, WrongFrequency, Active };
    struct Message { dsp::AdsbDecoder::Frame frame; std::uint64_t generation; std::chrono::steady_clock::time_point timestamp; };
    // Optional worker-side hook permits deterministic latency testing; it must
    // outlive the receiver and never run on the DSP or GUI thread.
    using WorkerHook = void (*)(void *) noexcept;
    explicit AdsbReceiver(WorkerHook workerHook = nullptr, void *hookContext = nullptr);
    ~AdsbReceiver();
    void setEnabled(bool enabled) noexcept;
    bool enabled() const noexcept { return m_enabled.load(); }
    void setSource(bool present, bool audio, double rate, std::int64_t center) noexcept;
    void setCenter(std::int64_t center) noexcept;
    Status status() const noexcept;
    double sampleRate() const noexcept { return m_rate.load(); }
    void process(const dsp::Complex *iq, std::size_t count) noexcept;
    void process(const dsp::Complex *iq, std::size_t count,
                 std::chrono::steady_clock::time_point ingress) noexcept;
    void discontinuity() noexcept { m_generation.fetch_add(1); }
    bool read(dsp::AdsbDecoder::Frame &frame) noexcept;
    bool read(Message &message) noexcept;
    std::uint64_t generation() const noexcept { return m_generation.load(); }
    std::uint64_t dropped() const noexcept { return m_dropped.load(); }
    AsyncIqDecoder::Stats iqStats() const noexcept { return m_async.stats(); }
private:
    static void receive(void *, const dsp::AdsbDecoder::Frame &) noexcept;
    static void resetDecoder(void *) noexcept;
    static void processBlock(void *, const dsp::Complex *, std::size_t, std::uint64_t, std::chrono::steady_clock::time_point) noexcept;
    std::atomic<bool> m_enabled{false}, m_present{false}, m_audio{false};
    std::atomic<double> m_rate{0};
    std::atomic<std::int64_t> m_center{0};
    std::atomic<std::uint64_t> m_generation{1}, m_dropped{0};
    std::uint64_t m_decoderGeneration = 0;
    std::chrono::steady_clock::time_point m_decoderTimestamp{};
    WorkerHook m_workerHook;
    void *m_hookContext;
    dsp::SpscRing<Message> m_messages{256};
    dsp::AdsbDecoder m_decoder;
    // Last member: stopped and joined before decoder and queues are destroyed.
    AsyncIqDecoder m_async;
};
}
