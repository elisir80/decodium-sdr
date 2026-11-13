// SPDX-License-Identifier: GPL-3.0-or-later
#include "Vdl2AsyncStream.h"
#include <stdexcept>
#include <utility>
namespace dsdr::core {
Vdl2AsyncStream::Vdl2AsyncStream(double rate,double floor,vdl2::WindowStream::Callback callback,
                                 std::size_t slots,std::size_t maxFrames,WorkerHook hook,void* hookContext)
    :m_hook(hook),m_hookContext(hookContext),m_callback(std::move(callback)),
     m_stream(rate,floor,[this](const auto& burst){
         if(m_workerGeneration==m_generation.load(std::memory_order_acquire) && m_callback) {
             auto tagged=burst; tagged.generation=m_workerGeneration; m_callback(tagged);
         }
     }),m_async(slots,maxFrames,{this,resetWorker,nullptr,processWorker}) {
    if(!m_callback) throw std::invalid_argument("missing VDL2 result callback");
}
Vdl2AsyncStream::~Vdl2AsyncStream() { stop(false); }
void Vdl2AsyncStream::start() { if(m_started) return; m_generation.fetch_add(1,std::memory_order_acq_rel); m_async.start(); m_started=true; }
void Vdl2AsyncStream::stop(bool drain) noexcept { if(!m_started) return; m_async.stop(drain); m_started=false; }
Vdl2AsyncStream::SubmitResult Vdl2AsyncStream::submit(const dsp::Complex* data,std::size_t count) noexcept {
    const auto result=m_async.submit(data,count,m_generation.load(std::memory_order_acquire));
    if(result!=SubmitResult::Accepted) m_generation.fetch_add(1,std::memory_order_acq_rel);
    return result;
}
void Vdl2AsyncStream::discontinuity() noexcept { m_generation.fetch_add(1,std::memory_order_acq_rel); m_async.discontinuity(); }
AsyncIqDecoder::Stats Vdl2AsyncStream::stats() const noexcept { return m_async.stats(); }
void Vdl2AsyncStream::resetWorker(void* p) noexcept { auto& self=*static_cast<Vdl2AsyncStream*>(p); self.m_stream.reset(); self.m_workerGeneration=0; }
void Vdl2AsyncStream::processWorker(void* p,const dsp::Complex* data,std::size_t count,std::uint64_t generation,std::chrono::steady_clock::time_point) noexcept {
    auto& self=*static_cast<Vdl2AsyncStream*>(p);
    if(self.m_hook) self.m_hook(self.m_hookContext);
    if(generation!=self.m_generation.load(std::memory_order_acquire)) return;
    if(self.m_workerGeneration!=generation) { self.m_stream.reset(); self.m_workerGeneration=generation; }
    try { self.m_stream.feed(data,count); }
    catch(...) { ++self.m_errors; self.m_stream.reset(); }
}
}
