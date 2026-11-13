// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "core/AsyncIqDecoder.h"
#include "dsp/SelcalAudioDecoder.h"
#include "dsp/SpscRing.h"
#include <array>
#include <atomic>
#include <cstdint>

namespace dsdr::core {
// One 48 kHz mono-audio producer, one result consumer. Controls are
// serialized by the owner; stop/destruction join and require quiescence.
class SelcalReceiver final {
public:
    using WorkerHook=void (*)(void *) noexcept;
    struct Message { std::array<char,5> code{}; std::uint64_t generation=0; };
    enum class ReadResult { Empty, Stale, Message };
    explicit SelcalReceiver(std::size_t slots=64,std::size_t maxFrames=4096,std::size_t capacity=64,
                            WorkerHook hook=nullptr,void *hookContext=nullptr);
    ~SelcalReceiver();
    void start();
    void stop() noexcept;
    void setEnabled(bool value) noexcept;
    bool enabled() const noexcept { return m_enabled.load(); }
    void selectChannel(int channel) noexcept;
    int selectedChannel() const noexcept { return m_channel.load(); }
    void discontinuity() noexcept { ++m_generation; }
    std::uint64_t generation() const noexcept { return m_generation.load(); }
    AsyncIqDecoder::SubmitResult submitForGeneration(const dsp::Complex *samples,std::size_t count,std::uint64_t generation) noexcept;
    ReadResult readOne(Message &message) noexcept;
    std::uint64_t droppedMessages() const noexcept { return m_dropped.load(); }
    AsyncIqDecoder::Stats stats() const noexcept { return m_async.stats(); }
private:
    static void resetWorker(void *) noexcept;
    static void processWorker(void *,const dsp::Complex *,std::size_t,std::uint64_t,std::chrono::steady_clock::time_point) noexcept;
    static void receive(void *,const std::array<char,5> &);
    void resetFilter() noexcept;
    const WorkerHook m_hook;
    void *const m_hookContext;
    std::atomic<bool> m_enabled{false};
    std::atomic<int> m_channel{-1};
    std::atomic<std::uint64_t> m_generation{1},m_dropped{0};
    std::uint64_t m_workerGeneration=0;
    std::array<float,97> m_coeff{},m_ring{};
    std::size_t m_pos=0,m_phase=0;
    dsp::SpscRing<Message> m_messages;
    dsp::SelcalAudioDecoder m_decoder;
    AsyncIqDecoder m_async;
};
}
