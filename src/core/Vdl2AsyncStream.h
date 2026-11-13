// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "core/AsyncIqDecoder.h"
#include "dsp/vdl2/Vdl2WindowStream.h"
#include <atomic>
namespace dsdr::core {
// Experimental single-producer queue for already filtered VDL2 IQ. The
// callback runs on the worker; the owner must marshal results to the UI.
// Start/stop/destruction require a quiescent producer, and stop joins.
class Vdl2AsyncStream final {
public:
    using SubmitResult=AsyncIqDecoder::SubmitResult;
    using WorkerHook=void (*)(void*) noexcept;
    Vdl2AsyncStream(double rate,double floor,vdl2::WindowStream::Callback callback,
                    std::size_t slots=64,std::size_t maxFrames=4096,
                    WorkerHook hook=nullptr,void* hookContext=nullptr);
    ~Vdl2AsyncStream();
    Vdl2AsyncStream(const Vdl2AsyncStream&)=delete;
    Vdl2AsyncStream& operator=(const Vdl2AsyncStream&)=delete;
    void start();
    void stop(bool drain=true) noexcept;
    SubmitResult submit(const dsp::Complex* data,std::size_t count) noexcept;
    void discontinuity() noexcept;
    // May be called by the owner to fence results immediately; unlike
    // discontinuity(), does not touch the producer-owned queue state.
    void invalidate() noexcept { m_generation.fetch_add(1,std::memory_order_acq_rel); }
    AsyncIqDecoder::Stats stats() const noexcept;
    std::uint64_t errors() const noexcept { return m_errors.load(); }
    std::uint64_t generation() const noexcept { return m_generation.load(std::memory_order_acquire); }
private:
    static void resetWorker(void*) noexcept;
    static void processWorker(void*,const dsp::Complex*,std::size_t,std::uint64_t,std::chrono::steady_clock::time_point) noexcept;
    WorkerHook m_hook;
    void* m_hookContext;
    vdl2::WindowStream::Callback m_callback;
    vdl2::WindowStream m_stream;
    std::atomic<std::uint64_t> m_generation{1};
    std::uint64_t m_workerGeneration=0;
    std::atomic<std::uint64_t> m_errors{0};
    AsyncIqDecoder m_async;
    bool m_started=false; // lifecycle calls serialized with a quiescent producer
};
}
