// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "core/AsyncIqDecoder.h"
#include "dsp/SpscRing.h"
#include "dsp/acars/acars_chan.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <vector>
namespace dsdr::core {
// One producer and one result consumer. Controls serialized by owner; lifecycle
// requires producer and consumer quiescence. stop joins outside DSP/UI threads.
class AcarsReceiver final {
public:
    using SubmitResult=AsyncIqDecoder::SubmitResult;
    using WorkerHook=void (*)(void *) noexcept;
    struct Message { std::array<std::uint8_t, 256> bytes{}; std::size_t size=0; float levelDb=0; std::uint64_t generation=0; };
    enum class Status { Disabled, Stopped, NoSource, WrongRate, WrongOffset, Active };
    AcarsReceiver(std::size_t slots=64, std::size_t maxFrames=4096, std::size_t messages=128,
                  WorkerHook hook=nullptr, void *hookContext=nullptr);
    ~AcarsReceiver();
    AcarsReceiver(const AcarsReceiver&)=delete;
    AcarsReceiver& operator=(const AcarsReceiver&)=delete;
    void start();
    void stop() noexcept;
    void setEnabled(bool enabled) noexcept;
    // IQ must contain a single channel within the input passband.
    void setSource(bool present, double rate, double offsetHz) noexcept;
    void discontinuity() noexcept;
    Status status() const noexcept;
    std::uint64_t generation() const noexcept { return m_generation.load(); }
    SubmitResult submitForGeneration(const dsp::Complex*, std::size_t, std::uint64_t) noexcept;
    SubmitResult submit(const dsp::Complex *p,std::size_t n) noexcept { return submitForGeneration(p,n,generation()); }
    enum class ReadResult { Empty, Stale, Message };
    ReadResult readOne(Message&) noexcept;
    bool read(Message&) noexcept;
    std::uint64_t droppedMessages() const noexcept { return m_dropped.load(); }
    std::uint64_t workerErrors() const noexcept { return m_errors.load(); }
    AsyncIqDecoder::Stats stats() const noexcept { return m_async.stats(); }
private:
    static void resetWorker(void*) noexcept;
    static void processWorker(void*,const dsp::Complex*,std::size_t,std::uint64_t,std::chrono::steady_clock::time_point) noexcept;
    const WorkerHook m_hook;
    void *const m_hookContext;
    std::atomic<bool> m_enabled{false},m_running{false},m_present{false},m_updating{false};
    std::atomic<double> m_rate{0},m_offset{0};
    std::atomic<std::uint64_t> m_generation{1},m_dropped{0},m_errors{0};
    std::uint64_t m_workerGeneration=0;
    dsp::SpscRing<Message> m_messages;
    dect2::AcarsChannelRx m_decoder;
    std::vector<dect2::AcarsRawBlock> m_blocks;
    AsyncIqDecoder m_async;
};
}
