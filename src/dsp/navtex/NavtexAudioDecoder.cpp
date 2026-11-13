// SPDX-License-Identifier: GPL-3.0-or-later
#include "NavtexAudioDecoder.h"
#include <cmath>
#include <utility>
namespace dsdr::navtex {
namespace { constexpr double pi=3.14159265358979323846; }
NavtexAudioDecoder::NavtexAudioDecoder(Callback cb):m_callback(std::move(cb)) {
    auto emit=[this](const NavtexMessage& m){if(m.complete && m_callback) m_callback(m);};
    m_normal.setCallback(emit); m_inverted.setCallback(emit);
}
void NavtexAudioDecoder::reset() {
    m_normal.reset(); m_inverted.reset(); m_high={}; m_low={}; m_highSum={}; m_lowSum={};
    m_index=m_count=0; m_highPhase=m_lowPhase=m_clock=m_smooth=0; m_lastSign=0;
}
void NavtexAudioDecoder::process(const float* samples,std::size_t count) {
    for(std::size_t n=0;n<count;++n) {
        const double x=std::isfinite(samples[n])?samples[n]:0;
        IQ high{x*std::cos(m_highPhase),x*std::sin(m_highPhase)};
        IQ low{x*std::cos(m_lowPhase),x*std::sin(m_lowPhase)};
        m_highPhase+=2*pi*1085/24000; m_lowPhase+=2*pi*915/24000;
        if(m_highPhase>=2*pi) m_highPhase-=2*pi;
        if(m_lowPhase>=2*pi) m_lowPhase-=2*pi;
        m_highSum.i+=high.i-m_high[m_index].i; m_highSum.q+=high.q-m_high[m_index].q;
        m_lowSum.i+=low.i-m_low[m_index].i; m_lowSum.q+=low.q-m_low[m_index].q;
        m_high[m_index]=high; m_low[m_index]=low; m_index=(m_index+1)%m_high.size();
        if(m_count<m_high.size()) ++m_count;
        if(m_count<m_high.size()) continue;
        const double eh=m_highSum.i*m_highSum.i+m_highSum.q*m_highSum.q;
        const double el=m_lowSum.i*m_lowSum.i+m_lowSum.q*m_lowSum.q;
        const double discr=(eh-el)/(el+eh+1e-12);
        m_smooth+=.12*(discr-m_smooth);
        const int sign=m_smooth>.10?1:m_smooth<-.10?-1:m_lastSign;
        if(m_lastSign && sign!=m_lastSign) {
            double error=m_clock-.25; if(error>.5) error-=1; if(error<-.5) error+=1;
            // A gentler phase correction prevents individual noisy transitions
            // from pulling the sampling point away from the bit centre.
            m_clock-=.15*error; if(m_clock<0) m_clock+=1; if(m_clock>=1) m_clock-=1;
        }
        m_lastSign=sign; m_clock+=1.0/240;
        if(m_clock>=1) { m_clock-=1; m_normal.pushBit(float(m_smooth)); m_inverted.pushBit(float(m_smooth)); }
    }
}
}
