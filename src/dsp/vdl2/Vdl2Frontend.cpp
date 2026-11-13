// SPDX-License-Identifier: GPL-3.0-or-later
#include "Vdl2Frontend.h"
#include <algorithm>
#include <cmath>
namespace dsdr::vdl2 {
namespace { constexpr double pi=3.14159265358979323846; }
Frontend::Frontend(Callback callback,void* context) noexcept:m_callback(callback),m_context(context) {}
bool Frontend::configure(unsigned rate,double offset) {
    if(!m_callback || rate<84000 || rate>2400000 || !std::isfinite(offset) ||
       std::abs(offset)>double(rate)/2-18000) {
        m_rate=0; m_factor=0; m_taps.clear(); m_history.clear(); reset(); return false;
    }
    m_rate=rate; m_factor=std::max(1u,rate/84000);
    const std::size_t length=32*m_factor+1;
    m_taps.resize(length); m_history.assign(length,{});
    double sum=0;
    for(std::size_t i=0;i<length;++i) {
        const double n=double(i)-double(length-1)/2;
        const double sinc=n==0 ? 2*14500.0/rate : std::sin(2*pi*14500.0*n/rate)/(pi*n);
        const double window=0.54-0.46*std::cos(2*pi*i/(length-1));
        m_taps[i]=float(sinc*window); sum+=m_taps[i];
    }
    for(auto& tap:m_taps) tap=float(tap/sum);
    const double angle=-2*pi*offset/rate; m_step={std::cos(angle),std::sin(angle)};
    reset(); return true;
}
void Frontend::reset() noexcept {
    std::fill(m_history.begin(),m_history.end(),Sample{});
    m_cursor=0; m_phase=0; m_osc={1,0};
}
void Frontend::process(const Sample* samples,std::size_t count) noexcept {
    if(!m_rate) return;
    if(!samples) { if(count) { m_callback(m_context,nullptr,0); reset(); } return; }
    std::size_t used=0;
    for(std::size_t i=0;i<count;++i) {
        const auto z=samples[i];
        if(!std::isfinite(z.real()) || !std::isfinite(z.imag())) {
            if(used) { m_callback(m_context,m_output.data(),used); used=0; }
            m_callback(m_context,nullptr,0); reset(); continue;
        }
        m_history[m_cursor]=z*Sample(float(m_osc.real()),float(m_osc.imag()));
        m_cursor=(m_cursor+1)%m_history.size(); m_osc*=m_step;
        const auto power=std::norm(m_osc);
        if(power<0.999999 || power>1.000001) m_osc/=std::sqrt(power);
        if(++m_phase<m_factor) continue;
        m_phase=0; Sample filtered{}; auto at=m_cursor;
        for(float tap:m_taps) { at=at?at-1:m_history.size()-1; filtered+=m_history[at]*tap; }
        m_output[used++]=filtered;
        if(used==m_output.size()) { m_callback(m_context,m_output.data(),used); used=0; }
    }
    if(used) m_callback(m_context,m_output.data(),used);
}
}
