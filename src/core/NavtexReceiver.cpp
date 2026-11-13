// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/NavtexReceiver.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace dsdr::core {
namespace { std::size_t capacity(std::size_t n) { if(!n || n>256) throw std::invalid_argument("invalid NAVTEX queue capacity"); return n; } }
NavtexReceiver::NavtexReceiver(std::size_t slots,std::size_t maxFrames,std::size_t count,WorkerHook hook,void* ctx)
    : m_hook(hook),m_hookContext(ctx),m_messages(capacity(count)),
      m_decoder([this](const auto& message){receive(message);}),m_async(slots,maxFrames,{this,resetWorker,nullptr,processWorker}) {
    constexpr double pi=3.14159265358979323846, cutoff=5000.0/48000.0;
    double sum=0;
    for(std::size_t i=0;i<m_coeff.size();++i) {
        const double k=double(i)-32, sinc=k==0?2*cutoff:std::sin(2*pi*cutoff*k)/(pi*k);
        const double window=.42-.5*std::cos(2*pi*i/64)+.08*std::cos(4*pi*i/64);
        m_coeff[i]=float(sinc*window); sum+=m_coeff[i];
    }
    for(auto& c:m_coeff) c=float(c/sum);
}
NavtexReceiver::~NavtexReceiver() { stop(); }
void NavtexReceiver::start() { discontinuity(); m_async.start(); }
void NavtexReceiver::stop() noexcept { discontinuity(); m_async.stop(false); }
void NavtexReceiver::setEnabled(bool v) noexcept { if(enabled()==v) return; discontinuity(); m_enabled.store(v); discontinuity(); }
void NavtexReceiver::selectChannel(int id) noexcept { if(selectedChannel()==id) return; discontinuity(); m_channel.store(id); discontinuity(); }
AsyncIqDecoder::SubmitResult NavtexReceiver::submitForGeneration(const dsp::Complex* data,std::size_t n,std::uint64_t generation) noexcept {
    using R=AsyncIqDecoder::SubmitResult;
    if(!enabled() || generation!=m_generation.load()) return R::Stopped;
    const auto result=m_async.submit(data,n,generation); if(result!=R::Accepted) discontinuity(); return result;
}
NavtexReceiver::ReadResult NavtexReceiver::readOne(Message& m) noexcept {
    if(!m_messages.read(&m,1)) { m={}; return ReadResult::Empty; }
    if(!enabled() || m.generation!=m_generation.load()) { m={}; return ReadResult::Stale; }
    return ReadResult::Message;
}
void NavtexReceiver::resetFilter() noexcept { m_history.fill(0); m_pos=m_phase=0; m_decoder.reset(); }
void NavtexReceiver::resetWorker(void* p) noexcept { auto& s=*static_cast<NavtexReceiver*>(p); s.resetFilter(); s.m_workerGeneration=0; }
void NavtexReceiver::processWorker(void* p,const dsp::Complex* samples,std::size_t n,std::uint64_t g,std::chrono::steady_clock::time_point) noexcept {
    auto& s=*static_cast<NavtexReceiver*>(p); if(s.m_hook) s.m_hook(s.m_hookContext);
    if(!s.enabled() || g!=s.generation()) return;
    if(s.m_workerGeneration!=g) { s.resetFilter(); s.m_workerGeneration=g; }
    try {
        for(std::size_t i=0;i<n;++i) {
            const float v=samples[i].real();
            if(!std::isfinite(v)) { s.discontinuity(); s.resetFilter(); return; }
            s.m_history[s.m_pos]=v; s.m_pos=(s.m_pos+1)%s.m_history.size();
            if(++s.m_phase!=2) continue;
            s.m_phase=0; float out=0; std::size_t pos=s.m_pos;
            for(float coeff:s.m_coeff) { pos=pos?pos-1:s.m_history.size()-1; out+=s.m_history[pos]*coeff; }
            s.m_decoder.process(&out,1);
        }
    } catch(...) { s.discontinuity(); s.resetFilter(); }
}
void NavtexReceiver::receive(const navtex::NavtexMessage& v) {
    if(!enabled() || m_workerGeneration!=generation() || !v.complete) return;
    Message m; m.generation=m_workerGeneration; m.errors=v.errors;
    std::copy_n(v.header.begin(),std::min(v.header.size(),m.header.size()-1),m.header.begin());
    const auto n=std::min(v.text.size(),m.text.size()-1);
    std::copy_n(v.text.begin(),n,m.text.begin()); m.truncated=v.textCut || n<v.text.size();
    if(!m_messages.write(&m,1)) ++m_dropped;
}
}
