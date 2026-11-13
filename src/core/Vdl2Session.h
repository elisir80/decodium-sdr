// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Vdl2IqPipeline.h"
#include "Vdl2ResultQueue.h"
namespace dsdr::core {
// Experimental single-source owner. One IQ producer and one result consumer;
// start/stop/destruction require both to be quiescent. Not a QObject/UI model.
class Vdl2Session final {
public:
    Vdl2Session(unsigned rate,double offset,double floor,std::size_t results=64,std::size_t slots=64);
    void start() { m_pipeline.start(); }
    void stop(bool drain=true) noexcept { m_pipeline.stop(drain); }
    void process(const dsp::Complex* iq,std::size_t count) noexcept { m_pipeline.process(iq,count); }
    void processForGeneration(const dsp::Complex* iq,std::size_t count,std::uint64_t generation) noexcept { m_pipeline.processForGeneration(iq,count,generation); }
    std::uint64_t sourceGeneration() const noexcept { return m_pipeline.sourceGeneration(); }
    void discontinuity() noexcept { m_pipeline.discontinuity(); }
    void invalidateResults() noexcept { m_pipeline.invalidateResults(); }
    bool read(Vdl2ResultQueue::Result&) noexcept;
    std::uint64_t generation() const noexcept { return m_pipeline.generation(); }
    std::uint64_t droppedResults() const noexcept { return m_results.dropped(); }
    std::uint64_t errors() const noexcept { return m_pipeline.errors(); }
    AsyncIqDecoder::Stats stats() const noexcept { return m_pipeline.stats(); }
private:
    Vdl2ResultQueue m_results;
    Vdl2IqPipeline m_pipeline;
};
}
