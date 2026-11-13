// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "core/AsyncIqDecoder.h"
#include "dsp/VorReceiver.h"
#include <atomic>
#include <mutex>
#include <vector>
namespace dsdr::core {
// One producer, one consumer. Lifecycle requires both quiescent; stop joins.
class VorAsyncReceiver final {
public:
    using WorkerHook=void (*)(void *) noexcept;
    enum class Input { Audio, FilteredIq };
    struct Result { dsp::VorReceiver::Result value{}; std::uint64_t generation = 0; };
    explicit VorAsyncReceiver(std::size_t slots=64, std::size_t maxFrames=4096,
                              WorkerHook hook=nullptr, void *hookContext=nullptr);
    ~VorAsyncReceiver();
    VorAsyncReceiver(const VorAsyncReceiver&)=delete;
    VorAsyncReceiver& operator=(const VorAsyncReceiver&)=delete;
    bool configure(Input input, unsigned sampleRate);
    bool start();
    void stop() noexcept;
    AsyncIqDecoder::SubmitResult submitAudio(const float*, std::size_t) noexcept;
    AsyncIqDecoder::SubmitResult submitIq(const dsp::Complex*, std::size_t) noexcept;
    AsyncIqDecoder::SubmitResult submitIqForGeneration(const dsp::Complex*, std::size_t, std::uint64_t) noexcept;
    // Thread-safe invalidation; unlike discontinuity(), does not touch the
    // producer-owned queue state and may be called by a source-control thread.
    void invalidateResults() noexcept { ++m_generation; }
    void discontinuity() noexcept;
    bool read(Result&) noexcept;
    bool configured() const noexcept { return m_configured; }
    bool running() const noexcept { return m_running; }
    Input input() const noexcept { return m_input; }
    std::uint64_t generation() const noexcept { return m_generation.load(); }
    std::uint64_t droppedResults() const noexcept { return m_resultDrops.load(); }
    AsyncIqDecoder::Stats stats() const noexcept { return m_async.stats(); }
private:
    using SubmitResult=AsyncIqDecoder::SubmitResult;
    SubmitResult submit(const dsp::Complex*,std::size_t,std::uint64_t) noexcept;
    static void workerReset(void*) noexcept;
    static void workerProcess(void*,const dsp::Complex*,std::size_t,std::uint64_t,std::chrono::steady_clock::time_point) noexcept;
    static void receive(const dsp::VorReceiver::Result&,void*);
    const std::size_t m_maxFrames;
    const WorkerHook m_hook;
    void *const m_hookContext;
    Input m_input=Input::Audio; bool m_configured=false, m_running=false;
    std::atomic<std::uint64_t> m_generation{1},m_resultDrops{0};
    std::uint64_t m_workerGeneration=0;
    std::vector<dsp::Complex> m_audioBuffer;
    std::vector<float> m_workerAudio;
    std::mutex m_resultMutex;
    Result m_result{}; bool m_hasResult=false;
    dsp::VorReceiver m_decoder;
    AsyncIqDecoder m_async;
};
}
