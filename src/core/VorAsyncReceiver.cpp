// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/VorAsyncReceiver.h"
namespace dsdr::core {
VorAsyncReceiver::VorAsyncReceiver(std::size_t slots,std::size_t maxFrames,WorkerHook hook,void *context)
 : m_maxFrames(maxFrames),m_hook(hook),m_hookContext(context),m_audioBuffer(maxFrames),m_workerAudio(maxFrames),
   m_decoder(&VorAsyncReceiver::receive,this),
   m_async(slots,maxFrames,{this,&VorAsyncReceiver::workerReset,nullptr,&VorAsyncReceiver::workerProcess}) {}
VorAsyncReceiver::~VorAsyncReceiver() { stop(); }
bool VorAsyncReceiver::configure(Input input,unsigned rate) {
    if(m_running) return false;
    ++m_generation; m_input=input;
    m_configured=(input==Input::Audio || input==Input::FilteredIq) && m_decoder.configure(rate);
    std::lock_guard<std::mutex> lock(m_resultMutex); m_hasResult=false;
    return m_configured;
}
bool VorAsyncReceiver::start() {
    if(!m_configured || m_running) return false;
    ++m_generation;
    try { m_async.start(); } catch (...) { return false; }
    m_running=true; return true;
}
void VorAsyncReceiver::stop() noexcept {
    if(!m_running) return;
    ++m_generation; m_async.stop(false); m_running=false;
    std::lock_guard<std::mutex> lock(m_resultMutex); m_hasResult=false;
}
void VorAsyncReceiver::discontinuity() noexcept { ++m_generation; m_async.discontinuity(); }
VorAsyncReceiver::SubmitResult VorAsyncReceiver::submit(const dsp::Complex* p,std::size_t n,std::uint64_t gen) noexcept {
    if(gen!=m_generation.load()) return SubmitResult::Stopped;
    auto r=m_async.submit(p,n,gen);
    if(r==SubmitResult::Full || r==SubmitResult::TooLarge || r==SubmitResult::Invalid) discontinuity();
    return r;
}
VorAsyncReceiver::SubmitResult VorAsyncReceiver::submitAudio(const float* p,std::size_t n) noexcept {
    if(!m_running) return SubmitResult::Stopped;
    if(m_input!=Input::Audio || (n && !p) || n>m_maxFrames) { discontinuity(); return n>m_maxFrames?SubmitResult::TooLarge:SubmitResult::Invalid; }
    for(std::size_t i=0;i<n;++i) m_audioBuffer[i]={p[i],0};
    return submit(m_audioBuffer.data(),n,m_generation.load());
}
VorAsyncReceiver::SubmitResult VorAsyncReceiver::submitIq(const dsp::Complex* p,std::size_t n) noexcept {
    return submitIqForGeneration(p,n,m_generation.load());
}
VorAsyncReceiver::SubmitResult VorAsyncReceiver::submitIqForGeneration(const dsp::Complex* p,std::size_t n,std::uint64_t gen) noexcept {
    if(!m_running) return SubmitResult::Stopped;
    if(m_input!=Input::FilteredIq || (n && !p) || n>m_maxFrames) { discontinuity(); return n>m_maxFrames?SubmitResult::TooLarge:SubmitResult::Invalid; }
    return submit(p,n,gen);
}
void VorAsyncReceiver::workerReset(void* p) noexcept { static_cast<VorAsyncReceiver*>(p)->m_decoder.reset(); }
void VorAsyncReceiver::workerProcess(void* p,const dsp::Complex* s,std::size_t n,std::uint64_t gen,std::chrono::steady_clock::time_point) noexcept {
    auto* self=static_cast<VorAsyncReceiver*>(p);
    if(self->m_hook) self->m_hook(self->m_hookContext);
    if(gen!=self->m_generation.load()) return;
    if(self->m_workerGeneration!=gen) { self->m_decoder.reset(); self->m_workerGeneration=gen; }
    if(self->m_input==Input::FilteredIq) self->m_decoder.processIq(s,n);
    else { for(std::size_t i=0;i<n;++i) self->m_workerAudio[i]=s[i].real(); self->m_decoder.processAudio(self->m_workerAudio.data(),n); }
}
void VorAsyncReceiver::receive(const dsp::VorReceiver::Result& value,void* p) {
    auto* self=static_cast<VorAsyncReceiver*>(p);
    if(self->m_workerGeneration!=self->m_generation.load()) return;
    std::lock_guard<std::mutex> lock(self->m_resultMutex);
    if(self->m_hasResult) ++self->m_resultDrops;
    self->m_result={value,self->m_workerGeneration}; self->m_hasResult=true;
}
bool VorAsyncReceiver::read(Result& result) noexcept {
    std::lock_guard<std::mutex> lock(m_resultMutex);
    if(!m_hasResult || m_result.generation!=m_generation.load()) { m_hasResult=false; return false; }
    result=m_result; m_hasResult=false; return true;
}
}
