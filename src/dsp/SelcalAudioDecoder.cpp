// SPDX-License-Identifier: GPL-3.0-or-later
// SELCAL detection and thresholds adapted from OnAir tones_rx.cpp,
// Kristijonas Adomaitis and contributors, commit 15a2907f02f7486fc64b8e439a1645eb7373171f.
// Based on OnAir by Kristijonas Adomaitis - https://on-air.dev
// Additional terms: docs/decoders/acars-onair/ADDITIONAL-TERMS.md.
#include "dsp/SelcalAudioDecoder.h"
#include <algorithm>
#include <cmath>

namespace dsdr::dsp {
namespace {
constexpr double pi=3.14159265358979323846;
constexpr double hz[16]={312.6,346.7,384.6,426.6,473.2,524.8,582.1,645.7,
                         716.1,794.3,881.0,977.2,1083.9,1202.3,1333.5,1479.1};
constexpr char letters[]="ABCDEFGHJKLMPQRS";
}
SelcalAudioDecoder::SelcalAudioDecoder(Callback cb, void *ctx) noexcept : m_callback(cb),m_context(ctx) {}
void SelcalAudioDecoder::reset() noexcept {
    m_samples.fill(0); m_written=m_filled=m_hop=0;
    m_current=-1; m_pending=-2; m_pendingRuns=m_run=0; m_first=-1; m_hops=m_firstEnd=0;
}
void SelcalAudioDecoder::process(const float *samples,std::size_t count) noexcept {
    if(!samples) { if(count) reset(); return; }
    for(std::size_t i=0;i<count;++i) {
        if(!std::isfinite(samples[i])) { reset(); continue; }
        m_samples[m_written]=samples[i]; m_written=(m_written+1)%m_samples.size();
        m_filled=std::min(m_filled+1,m_samples.size());
        if(++m_hop==400) { m_hop=0; if(m_filled==m_samples.size()) analyze(); }
    }
}
void SelcalAudioDecoder::analyze() noexcept {
    ++m_hops;
    double energy=0, power[16]{};
    std::array<double,800> window{};
    for(std::size_t i=0;i<window.size();++i) {
        const double x=m_samples[(m_written+i)%m_samples.size()]*(0.5-0.5*std::cos(2*pi*i/799.0));
        window[i]=x; energy+=x*x;
    }
    for(int k=0;k<16;++k) {
        const double coeff=2*std::cos(2*pi*hz[k]/8000);
        double a=0,b=0;
        for(double x : window) {
            const double v=x+coeff*a-b; b=a; a=v;
        }
        power[k]=a*a+b*b-coeff*a*b;
    }
    int pair=-1;
    if(energy/800.0>1e-8) {
        int a=0; for(int i=1;i<16;++i) if(power[i]>power[a]) a=i;
        int b=a==0?1:0; for(int i=0;i<16;++i) if(i!=a && power[i]>power[b]) b=i;
        double third=0; for(int i=0;i<16;++i) if(i!=a && i!=b) third=std::max(third,power[i]);
        const double normal=3.0/(800.0*energy);
        // Real captures can have unequal tone levels. Retain the total-energy
        // and third-tone checks while allowing the quieter member of a pair.
        if((power[a]+power[b])*normal>=0.55 && power[b]*normal>=0.07 &&
           power[b]>=0.08*power[a] && third<=0.25*power[b]) pair=std::min(a,b)*16+std::max(a,b);
    }
    if(pair==m_current) { ++m_run; m_pending=-2; m_pendingRuns=0; }
    else if(pair==m_pending) {
        if(++m_pendingRuns>=2) {
            if(m_current>=0 && m_run>=11) {
                const auto start=m_hops>=std::uint64_t(m_run+2)?m_hops-m_run-2:0;
                if(m_first>=0 && start>=m_firstEnd && start-m_firstEnd<16 && m_first!=m_current) {
                    const std::array<char,5> code{letters[m_first/16],letters[m_first%16],'-',letters[m_current/16],letters[m_current%16]};
                    if(m_callback) m_callback(m_context,code);
                    m_first=-1;
                } else { m_first=m_current; m_firstEnd=m_hops-2; }
            }
            m_current=pair; m_run=m_pendingRuns; m_pending=-2; m_pendingRuns=0;
        }
    } else { m_pending=pair; m_pendingRuns=1; }
    if(m_first>=0 && m_hops-m_firstEnd>60) m_first=-1;
}
}
