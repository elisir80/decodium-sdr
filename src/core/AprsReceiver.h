// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "core/AsyncIqDecoder.h"
#include "dsp/AprsAfskDecoder.h"
#include "dsp/SpscRing.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

namespace dsdr::core {
// One audio producer, one UI consumer. Controls may be called on the UI thread and must be serialized with each other.
// start/stop and destruction require the producer to be quiescent.
class AprsReceiver final {
public:
    // Optional worker-side instrumentation; never called on the producer or UI.
    using WorkerHook = void (*)(void *) noexcept;
    enum class Status { Disabled, NoChannel, NoSource, NotNfm, WrongRate, Active };
    enum class ReadResult { Empty, Stale, Message };
    struct Message {
        std::array<std::uint8_t, 328> bytes{};
        std::size_t size = 0;
        std::uint64_t generation = 0;
    };
    explicit AprsReceiver(std::size_t slots = 64, std::size_t maxFrames = 4096,
                          std::size_t frameCapacity = 128,
                          WorkerHook workerHook = nullptr, void *hookContext = nullptr);
    ~AprsReceiver();
    void start();
    void stop() noexcept;
    void setEnabled(bool enabled) noexcept;
    bool enabled() const noexcept { return m_enabled.load(); }
    void selectChannel(int id) noexcept;
    int selectedChannel() const noexcept { return m_selected.load(); }
    // Publish selected channel state after selection and on every relevant change.
    // id prevents a late notification for an old selection from activating it.
    void setChannelState(int id, bool present, bool nfm, double audioRate) noexcept;
    Status status() const noexcept;
    // Producer only. samples contain stereo L/R in real/imaginary components.
    AsyncIqDecoder::SubmitResult submit(int channelId, const dsp::Complex *samples, std::size_t count) noexcept;
    AsyncIqDecoder::SubmitResult submitForGeneration(int channelId, const dsp::Complex *samples,
        std::size_t count, std::uint64_t generation) noexcept;
    // Producer or control thread: call on source/tuning/audio discontinuity.
    void discontinuity() noexcept;
    ReadResult readOne(Message &message) noexcept; // UI consumer only; at most one queue item
    bool read(Message &message) noexcept; // UI consumer only
    std::uint64_t generation() const noexcept { return m_generation.load(); }
    std::uint64_t droppedFrames() const noexcept { return m_droppedFrames.load(); }
    AsyncIqDecoder::Stats stats() const noexcept { return m_async.stats(); }
private:
    static void resetWorker(void *) noexcept;
    static void processWorker(void *, const dsp::Complex *, std::size_t, std::uint64_t, std::chrono::steady_clock::time_point) noexcept;
    static void receive(void *, const dsp::AprsAfskDecoder::Frame &);
    const WorkerHook m_workerHook;
    void *const m_hookContext;
    std::atomic<bool> m_enabled{false}, m_present{false}, m_nfm{false}, m_updating{false};
    std::atomic<int> m_selected{-1}, m_stateChannel{-1};
    std::atomic<double> m_rate{0};
    std::atomic<std::uint64_t> m_generation{1}, m_droppedFrames{0};
    std::vector<float> m_mono;        // worker-owned preallocated downmix
    std::uint64_t m_workerGeneration = 0;
    dsp::SpscRing<Message> m_messages;
    dsp::AprsAfskDecoder m_decoder;
    AsyncIqDecoder m_async;
};
}
