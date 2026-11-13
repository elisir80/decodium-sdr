// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "core/AsyncIqDecoder.h"
#include "dsp/SpscRing.h"
#include "dsp/dsc/FskAudioRx.h"
#include <atomic>
#include <cstdint>

namespace dsdr::core {
// Experimental MF/HF DSC decoder for distress, all-ships and individual calls.
// The producer provides 48 kHz
// mono channel audio in the real component, centred at 1700 Hz. The owner serializes
// controls, quiesces the producer before stop, and joins outside the UI/DSP.
class DscReceiver final {
public:
    using WorkerHook=void (*)(void*) noexcept;
    struct Message { dsc_rx::Distress call{}; std::uint64_t generation=0; };
    enum class ReadResult { Empty, Stale, Message };
    explicit DscReceiver(std::size_t slots=64,std::size_t maxFrames=4096,std::size_t capacity=64,
                         WorkerHook hook=nullptr,void* hookContext=nullptr);
    ~DscReceiver();
    DscReceiver(const DscReceiver&)=delete;
    DscReceiver& operator=(const DscReceiver&)=delete;
    void start();
    void stop() noexcept;
    void setEnabled(bool) noexcept;
    bool enabled() const noexcept { return m_enabled.load(); }
    void selectChannel(int channel) noexcept;
    int selectedChannel() const noexcept { return m_channel.load(); }
    void discontinuity() noexcept { ++m_generation; }
    std::uint64_t generation() const noexcept { return m_generation.load(); }
    AsyncIqDecoder::SubmitResult submitForGeneration(const dsp::Complex*,std::size_t,std::uint64_t) noexcept;
    ReadResult readOne(Message&) noexcept;
    AsyncIqDecoder::Stats stats() const noexcept { return m_async.stats(); }
    std::uint64_t droppedMessages() const noexcept { return m_dropped.load(); }
private:
    static void resetWorker(void*) noexcept;
    static void processWorker(void*,const dsp::Complex*,std::size_t,std::uint64_t,std::chrono::steady_clock::time_point) noexcept;
    void receive(const dsc_rx::Distress&);
    void resetFilter() noexcept;
    const WorkerHook m_hook;
    void* const m_hookContext;
    std::atomic<bool> m_enabled{false};
    std::atomic<int> m_channel{-1};
    std::atomic<std::uint64_t> m_generation{1},m_dropped{0};
    std::uint64_t m_workerGeneration=0;
    std::array<float,65> m_coeff{},m_history{};
    std::size_t m_pos=0,m_phase=0;
    dsp::SpscRing<Message> m_messages;
    dsc_rx::FskAudioRx m_decoder;
    AsyncIqDecoder m_async;
};
}
