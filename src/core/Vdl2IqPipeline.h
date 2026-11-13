// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "core/Vdl2AsyncStream.h"
#include "dsp/vdl2/Vdl2Frontend.h"
#include <memory>
namespace dsdr::core {
// Experimental one-channel pipeline. Construction configures the filter;
// producer must be stopped during start, stop, destruction and retuning.
// Filtering and decoding run on workers; results never run on the IQ producer.
class Vdl2IqPipeline final {
public:
    Vdl2IqPipeline(unsigned sourceRate,double offset,double floor,
                   vdl2::WindowStream::Callback callback,std::size_t slots=64);
    ~Vdl2IqPipeline();
    Vdl2IqPipeline(const Vdl2IqPipeline&)=delete;
    Vdl2IqPipeline& operator=(const Vdl2IqPipeline&)=delete;
    void start();
    void stop(bool drain=true) noexcept;
    void process(const dsp::Complex*,std::size_t) noexcept;
    void processForGeneration(const dsp::Complex*,std::size_t,std::uint64_t) noexcept;
    std::uint64_t sourceGeneration() const noexcept { return m_rawGeneration.load(std::memory_order_acquire); }
    void discontinuity() noexcept;
    void invalidateResults() noexcept;
    double outputRate() const noexcept { return m_frontend.outputRate(); }
    AsyncIqDecoder::Stats stats() const noexcept;
    std::uint64_t errors() const noexcept { return m_receiver->errors()+m_rawErrors.load(); }
    std::uint64_t generation() const noexcept { return m_receiver->generation(); }
private:
    static void output(void*,const vdl2::Frontend::Sample*,std::size_t) noexcept;
    static void resetRaw(void*) noexcept;
    static void processRaw(void*,const dsp::Complex*,std::size_t,std::uint64_t,std::chrono::steady_clock::time_point) noexcept;
    vdl2::Frontend m_frontend;
    std::unique_ptr<Vdl2AsyncStream> m_receiver;
    std::unique_ptr<AsyncIqDecoder> m_raw;
    std::atomic<std::uint64_t> m_rawGeneration{1},m_rawErrors{0};
    std::uint64_t m_workerGeneration=0;
    bool m_running=false;
};
}
