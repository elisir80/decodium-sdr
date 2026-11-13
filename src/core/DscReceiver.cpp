// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/DscReceiver.h"
#include <cmath>
#include <stdexcept>
namespace dsdr::core {
namespace { std::size_t validCapacity(std::size_t n) { if(!n || n>4096) throw std::invalid_argument("invalid DSC queue capacity"); return n; } }
DscReceiver::DscReceiver(std::size_t slots,std::size_t maxFrames,std::size_t capacity,WorkerHook hook,void* context)
    : m_hook(hook),m_hookContext(context),m_messages(validCapacity(capacity)),m_decoder([this](const auto& call){ receive(call); }),
      m_async(slots,maxFrames,{this,resetWorker,nullptr,processWorker}) {
    // 48 -> 24 kHz anti-alias low-pass, evaluated only on output samples.
    constexpr double pi=3.14159265358979323846, cutoff=5000.0/48000.0;
    double sum=0;
    for(std::size_t i=0;i<m_coeff.size();++i) {
        const double k=double(i)-32;
        const double sinc=k==0 ? 2*cutoff : std::sin(2*pi*cutoff*k)/(pi*k);
        const double window=.42-.5*std::cos(2*pi*i/64)+.08*std::cos(4*pi*i/64);
        m_coeff[i]=float(sinc*window); sum+=m_coeff[i];
    }
    for(auto& c:m_coeff) c=float(c/sum);
}
DscReceiver::~DscReceiver() { stop(); }
void DscReceiver::start() { discontinuity(); m_async.start(); }
void DscReceiver::stop() noexcept { discontinuity(); m_async.stop(false); }
void DscReceiver::setEnabled(bool value) noexcept {
    if(m_enabled.load()==value) return;
    discontinuity(); m_enabled.store(value); discontinuity();
}
void DscReceiver::selectChannel(int channel) noexcept {
    if(m_channel.load()==channel) return;
    discontinuity(); m_channel.store(channel); discontinuity();
}
AsyncIqDecoder::SubmitResult DscReceiver::submitForGeneration(const dsp::Complex* samples,std::size_t count,std::uint64_t generation) noexcept {
    using R=AsyncIqDecoder::SubmitResult;
    if(!enabled() || generation!=m_generation.load()) return R::Stopped;
    const auto result=m_async.submit(samples,count,generation);
    if(result!=R::Accepted) discontinuity();
    return result;
}
DscReceiver::ReadResult DscReceiver::readOne(Message& m) noexcept {
    if(!m_messages.read(&m,1)) { m={}; return ReadResult::Empty; }
    if(!enabled() || m.generation!=m_generation.load()) { m={}; return ReadResult::Stale; }
    return ReadResult::Message;
}
void DscReceiver::resetFilter() noexcept { m_history.fill(0); m_pos=m_phase=0; m_decoder.reset(); }
void DscReceiver::resetWorker(void* p) noexcept {
    auto& s=*static_cast<DscReceiver*>(p); s.resetFilter(); s.m_workerGeneration=0;
}
void DscReceiver::processWorker(void* p,const dsp::Complex* samples,std::size_t count,std::uint64_t generation,std::chrono::steady_clock::time_point) noexcept {
    auto& s=*static_cast<DscReceiver*>(p);
    if(s.m_hook) s.m_hook(s.m_hookContext);
    if(!s.enabled() || generation!=s.generation()) return;
    if(s.m_workerGeneration!=generation) { s.resetFilter(); s.m_workerGeneration=generation; }
    try {
        for(std::size_t i=0;i<count;++i) {
            const float value=samples[i].real();
            if(!std::isfinite(value)) { s.discontinuity(); s.resetFilter(); return; }
            s.m_history[s.m_pos]=value; s.m_pos=(s.m_pos+1)%s.m_history.size();
            if(++s.m_phase!=2) continue;
            s.m_phase=0; float out=0; std::size_t pos=s.m_pos;
            for(float coeff:s.m_coeff) { pos=pos?pos-1:s.m_history.size()-1; out+=s.m_history[pos]*coeff; }
            s.m_decoder.process(&out,1);
        }
    } catch(...) { s.discontinuity(); s.resetFilter(); }
}
void DscReceiver::receive(const dsc_rx::Distress& call) {
    if(!enabled() || m_workerGeneration!=generation()) return;
    Message m{call,m_workerGeneration}; if(!m_messages.write(&m,1)) ++m_dropped;
}
}
