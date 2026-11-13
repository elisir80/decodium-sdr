// SPDX-License-Identifier: GPL-3.0-or-later
#include "VorIdentDecoder.h"
#include <cmath>
#include <string_view>

namespace dsdr::dsp {
namespace {
constexpr double pi=3.14159265358979323846;
constexpr std::string_view patterns[]={".-","-...","-.-.","-..",".","..-.","--.","....","..",".---","-.-",".-..","--","-.","---",".--.","--.-",".-.","...","-","..-","...-",".--","-..-","-.--","--.."};
constexpr std::string_view digits[]={"-----",".----","..---","...--","....-",".....","-....","--...","---..","----."};
}
void VorIdentDecoder::reset() { *this=VorIdentDecoder{}; }
void VorIdentDecoder::process(const float *samples,std::size_t count) {
    if(!samples) return;
    for(std::size_t n=0;n<count;++n) {
        const double x=std::isfinite(samples[n]) ? samples[n] : 0;
        m_dc+=.002*(x-m_dc);
        const double ac=x-m_dc;
        const double i=ac*std::cos(m_phase), q=ac*std::sin(m_phase);
        m_i+=i-m_iHistory[m_historyIndex]; m_q+=q-m_qHistory[m_historyIndex];
        m_iHistory[m_historyIndex]=i; m_qHistory[m_historyIndex]=q;
        m_historyIndex=(m_historyIndex+1)%m_iHistory.size();
        if(m_historyCount<m_iHistory.size()) ++m_historyCount;
        m_energy+=x*x;
        m_phase+=2*pi*1020/48000; if(m_phase>=2*pi) m_phase-=2*pi;
        if(++m_samples==480) block();
    }
}
void VorIdentDecoder::block() {
    const double amplitude=2*std::hypot(m_i,m_q)/m_historyCount;
    const double rms=std::sqrt(m_energy/m_samples);
    const bool key=amplitude>0.004 && amplitude>rms*0.055;
    if(key) {
        if(!m_keyed) { if(m_offBlocks>=80) { m_identifier.clear(); m_code.clear(); } m_onBlocks=0; m_keyed=true; }
        ++m_onBlocks; m_offBlocks=0; m_characterEmitted=false;
    } else {
        if(m_keyed) { finishMark(); m_keyed=false; m_offBlocks=0; }
        if(m_offBlocks<3000) ++m_offBlocks;
        if(m_offBlocks>=25 && !m_characterEmitted) { finishCharacter(); m_characterEmitted=true; }
        // An identifier is only a recent observation. Do not keep displaying
        // it indefinitely after the keyed tone disappears from a live signal.
        if(m_offBlocks==3000) { m_identifier.clear(); m_code.clear(); }
    }
    m_energy=0; m_samples=0;
}
void VorIdentDecoder::finishMark() {
    if(m_onBlocks<6 || m_onBlocks>90 || m_code.size()>=5) { m_code.clear(); return; }
    m_code+=m_onBlocks<24 ? '.' : '-';
}
void VorIdentDecoder::finishCharacter() {
    if(m_code.empty()) return;
    for(std::size_t n=0;n<26;++n) if(m_code==patterns[n]) {
        if(m_identifier.size()>=6) m_identifier.clear();
        m_identifier+=char('A'+n); m_code.clear(); return;
    }
    for(std::size_t n=0;n<10;++n) if(m_code==digits[n]) {
        if(m_identifier.size()>=6) m_identifier.clear();
        m_identifier+=char('0'+n); break;
    }
    m_code.clear();
}
}
