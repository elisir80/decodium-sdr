// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "core/AsyncIqDecoder.h"
#include "dsp/SpscRing.h"
#include "dsp/navtex/NavtexAudioDecoder.h"
#include <array>
#include <atomic>
#include <cstdint>
namespace dsdr::core {
// Experimental NAVTEX; one producer supplies 48 kHz mono audio at 1000 Hz
// centre. Owner serializes controls, fences producer before stop and joins off
// the DSP/UI threads. Results are bounded; text may be explicitly truncated.
class NavtexReceiver final {
public:
    using WorkerHook=void (*)(void*) noexcept;
    struct Message {
        std::array<char,5> header{};
        std::array<char,2049> text{};
        std::uint64_t generation=0;
        std::uint32_t errors=0;
        bool truncated=false;
    };
    enum class ReadResult { Empty, Stale, Message };
    explicit NavtexReceiver(std::size_t slots=64,std::size_t maxFrames=4096,std::size_t capacity=32,
                            WorkerHook hook=nullptr,void* hookContext=nullptr);
    ~NavtexReceiver();
    NavtexReceiver(const NavtexReceiver&)=delete;
    NavtexReceiver& operator=(const NavtexReceiver&)=delete;
    void start(); void stop() noexcept;
    void setEnabled(bool) noexcept;
    bool enabled() const noexcept { return m_enabled.load(); }
    void selectChannel(int) noexcept;
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
    void resetFilter() noexcept;
    void receive(const navtex::NavtexMessage&);
    const WorkerHook m_hook; void* const m_hookContext;
    std::atomic<bool> m_enabled{false}; std::atomic<int> m_channel{-1};
    std::atomic<std::uint64_t> m_generation{1},m_dropped{0};
    std::uint64_t m_workerGeneration=0;
    std::array<float,65> m_coeff{},m_history{}; std::size_t m_pos=0,m_phase=0;
    dsp::SpscRing<Message> m_messages;
    navtex::NavtexAudioDecoder m_decoder;
    AsyncIqDecoder m_async;
};
}
