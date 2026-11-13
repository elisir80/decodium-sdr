// SPDX-License-Identifier: GPL-3.0-or-later
#include "Vdl2IqPipeline.h"
#include <algorithm>
#include <stdexcept>
#include <utility>
namespace dsdr::core {
Vdl2IqPipeline::Vdl2IqPipeline(unsigned rate,double offset,double floor,
                               vdl2::WindowStream::Callback callback,std::size_t slots)
    :m_frontend(output,this) {
    if(!m_frontend.configure(rate,offset)) throw std::invalid_argument("invalid VDL2 source configuration");
    m_receiver=std::make_unique<Vdl2AsyncStream>(m_frontend.outputRate(),floor,std::move(callback),slots,1024);
    m_raw=std::make_unique<AsyncIqDecoder>(slots,4096,AsyncIqDecoder::Sink{this,resetRaw,nullptr,processRaw});
}
Vdl2IqPipeline::~Vdl2IqPipeline() { stop(false); }
void Vdl2IqPipeline::start() {
    if(m_running) return;
    m_receiver->start();
    try { m_raw->start(); } catch(...) { m_receiver->stop(false); throw; }
    m_running=true;
}
void Vdl2IqPipeline::stop(bool drain) noexcept {
    if(!m_running) return;
    m_raw->stop(drain); m_receiver->stop(drain); m_running=false;
}
void Vdl2IqPipeline::process(const dsp::Complex* data,std::size_t count) noexcept {
    processForGeneration(data,count,sourceGeneration());
}
void Vdl2IqPipeline::processForGeneration(const dsp::Complex* data,std::size_t count,std::uint64_t generation) noexcept {
    if(!m_running || !count) return;
    if(!data) { discontinuity(); return; }
    while(count) {
        if(generation!=sourceGeneration()) return;
        const auto n=std::min<std::size_t>(count,4096);
        if(m_raw->submit(data,n,generation)!=AsyncIqDecoder::SubmitResult::Accepted) {
            m_rawGeneration.fetch_add(1,std::memory_order_acq_rel); m_receiver->invalidate(); return;
        }
        data+=n; count-=n;
    }
}
void Vdl2IqPipeline::discontinuity() noexcept {
    invalidateResults(); m_raw->discontinuity();
}
void Vdl2IqPipeline::invalidateResults() noexcept {
    m_rawGeneration.fetch_add(1,std::memory_order_acq_rel); m_receiver->invalidate();
}
AsyncIqDecoder::Stats Vdl2IqPipeline::stats() const noexcept {
    const auto a=m_raw->stats(),b=m_receiver->stats();
    return {a.accepted+b.accepted,a.droppedBlocks+b.droppedBlocks,a.droppedSamples+b.droppedSamples};
}
void Vdl2IqPipeline::resetRaw(void* context) noexcept {
    auto& self=*static_cast<Vdl2IqPipeline*>(context);
    self.m_frontend.reset(); self.m_receiver->discontinuity(); self.m_workerGeneration=0;
}
void Vdl2IqPipeline::processRaw(void* context,const dsp::Complex* data,std::size_t count,std::uint64_t generation,std::chrono::steady_clock::time_point) noexcept {
    auto& self=*static_cast<Vdl2IqPipeline*>(context);
    if(generation!=self.m_rawGeneration.load(std::memory_order_acquire)) return;
    if(self.m_workerGeneration!=generation) { self.m_frontend.reset(); self.m_receiver->discontinuity(); self.m_workerGeneration=generation; }
    self.m_frontend.process(data,count);
}
void Vdl2IqPipeline::output(void* context,const vdl2::Frontend::Sample* data,std::size_t count) noexcept {
    auto& self=*static_cast<Vdl2IqPipeline*>(context);
    if(!data && !count) { self.m_receiver->discontinuity(); return; }
    self.m_receiver->submit(data,count); // rejected blocks mark a gap in the queue
}
}
