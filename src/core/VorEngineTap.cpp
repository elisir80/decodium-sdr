// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/VorEngineTap.h"
#include <algorithm>
#include <cmath>
namespace dsdr::core {
namespace { constexpr double pi=3.14159265358979323846; }
bool VorEngineTap::configure(unsigned rate,double tune) {
    m_rate=0;
    if(rate<MinInputRate || rate>MaxInputRate || !std::isfinite(tune) || std::abs(tune)+15000>=rate/2.) return false;
    m_rate=rate; m_decimation=rate/48000; m_step=2*pi*tune/rate;
    m_highRate=rate>960000;
    if(m_highRate) {
        // Avoid large prime factors (e.g. 2 MHz / 48 kHz -> 41) so the
        // existing decimator can use inexpensive stages. The final output
        // stays at or above 48 kHz and is interpolated to exactly 48 kHz.
        auto smooth=[](unsigned value) {
            for(unsigned factor:{2u,3u,5u,7u}) while(value%factor==0) value/=factor;
            return value==1;
        };
        while(m_decimation>2 && !smooth(m_decimation)) --m_decimation;
        if(!m_highRateChain.configure(rate,int(m_decimation),14000,80)) { m_rate=0; return false; }
        reset(); return true;
    }
    m_taps=rate<=384000 ? 193 : 48*m_decimation+1;
    // Blackman FIR, 14 kHz cutoff; unity DC gain.
    double sum=0;
    for(std::size_t i=0;i<m_taps;++i) {
        const double k=double(i)-double(m_taps-1)/2;
        const double h=k==0 ? 28000./rate : std::sin(2*pi*14000*k/rate)/(pi*k);
        const double w=.42-.5*std::cos(2*pi*i/(m_taps-1))+.08*std::cos(4*pi*i/(m_taps-1));
        m_coeff[i]=float(h*w); sum+=m_coeff[i];
    }
    for(std::size_t i=0;i<m_taps;++i) m_coeff[i]=float(m_coeff[i]/sum);
    reset(); return true;
}
void VorEngineTap::reset() noexcept {
    m_ring.fill({}); m_pos=m_out=m_phase=0; m_osc=0; m_generation=0;
    m_resamplePhase=0; m_previous={}; m_havePrevious=false; m_highRateChain.reset(); m_receiver.discontinuity();
}
void VorEngineTap::deliver(dsp::Complex sum,std::uint64_t generation) noexcept {
    if(m_rate!=48000*m_decimation) {
        if(!m_havePrevious) { m_previous=sum; m_havePrevious=true; return; }
        const auto step=std::uint64_t(48000)*m_decimation;
        const auto before=m_resamplePhase; m_resamplePhase+=step;
        if(m_resamplePhase<m_rate) { m_previous=sum; return; }
        const float fraction=float(double(m_rate-before)/double(step));
        const auto interpolated=m_previous+(sum-m_previous)*fraction;
        m_previous=sum; m_resamplePhase-=m_rate; sum=interpolated;
    }
    m_output[m_out++]=sum;
    if(m_out==chunk) { auto result=m_receiver.submitIqForGeneration(m_output.data(),m_out,generation); if(result==AsyncIqDecoder::SubmitResult::Accepted) ++m_submitted; m_out=0; }
}
void VorEngineTap::process(const dsp::Complex* samples,std::size_t count) noexcept {
    processForGeneration(samples,count,m_receiver.generation());
}
void VorEngineTap::processForGeneration(const dsp::Complex* samples,std::size_t count,std::uint64_t generation) noexcept {
    if(!m_rate || !samples) return;
    if(generation!=m_receiver.generation()) return;
    if(m_generation!=generation) {
        m_ring.fill({}); m_pos=m_out=m_phase=0; m_osc=0; m_generation=generation;
        m_resamplePhase=0; m_previous={}; m_havePrevious=false; m_highRateChain.reset();
    }
    if(m_highRate) {
        for(std::size_t start=0;start<count;) {
            const auto n=std::min(count-start,dsp::kMaxBlockFrames);
            for(std::size_t i=0;i<n;++i) {
                const auto sample=samples[start+i];
                if(!std::isfinite(sample.real()) || !std::isfinite(sample.imag())) { reset(); return; }
                m_mixed[i]=sample*dsp::Complex(float(std::cos(m_osc)),float(-std::sin(m_osc)));
                m_osc+=m_step; if(m_osc>pi) m_osc-=2*pi; else if(m_osc < -pi) m_osc+=2*pi;
            }
            const auto produced=m_highRateChain.process(m_mixed.data(),n,m_decimated.data());
            for(std::size_t i=0;i<produced;++i) deliver(m_decimated[i],generation);
            start+=n;
        }
        return;
    }
    for(std::size_t n=0;n<count;++n) {
        if(!std::isfinite(samples[n].real()) || !std::isfinite(samples[n].imag())) {
            // A corrupt sample breaks timing and may poison the interpolator.
            // Invalidate queued results and wait for the next input block.
            reset();
            return;
        }
        m_ring[m_pos]=samples[n]*dsp::Complex(float(std::cos(m_osc)),float(-std::sin(m_osc)));
        m_osc+=m_step; if(m_osc>pi) m_osc-=2*pi; else if(m_osc < -pi) m_osc+=2*pi;
        m_pos=(m_pos+1)%m_taps;
        if(++m_phase<m_decimation) continue;
        m_phase=0; dsp::Complex sum{};
        std::size_t newest=m_pos ? m_pos-1 : m_taps-1, oldest=m_pos;
        for(std::size_t j=0;j<m_taps/2;++j) {
            sum+=(m_ring[newest]+m_ring[oldest])*m_coeff[j];
            newest=newest ? newest-1 : m_taps-1;
            if(++oldest==m_taps) oldest=0;
        }
        sum+=m_ring[newest]*m_coeff[m_taps/2];
        deliver(sum,generation);
    }
}
}
