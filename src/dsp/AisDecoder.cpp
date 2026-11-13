// SPDX-License-Identifier: GPL-3.0-or-later
#include "dsp/AisDecoder.h"
#include <algorithm>
#include <cmath>

namespace dsdr::dsp {
namespace {
constexpr double pi = 3.14159265358979323846;
constexpr std::size_t maxFrameBits = 1152;
std::uint32_t bits(const std::vector<std::uint8_t> &p, unsigned start, unsigned count) {
    std::uint32_t n=0;
    for (unsigned i=0;i<count;++i) n=(n<<1)|((p[(start+i)/8]>>(7-(start+i)%8))&1);
    return n;
}
std::int32_t signedBits(const std::vector<std::uint8_t> &p, unsigned start, unsigned count) {
    const auto u=bits(p,start,count);
    return (u & (1u<<(count-1))) ? static_cast<std::int32_t>(std::int64_t(u)-(std::int64_t(1)<<count)) : static_cast<std::int32_t>(u);
}
}
AisDecoder::AisDecoder(Callback cb, void *ctx):m_callback(cb),m_context(ctx) {}
bool AisDecoder::configure(double rate, double offset) {
    if (!std::isfinite(rate)||!std::isfinite(offset)||rate<48000||rate>384000||std::abs(offset)>rate/2-15000) {
        m_rate=0; m_ring.clear(); reset(); return false;
    }
    m_rate=rate; m_period=rate/9600.; m_offsetStep=2*pi*offset/rate;
    m_ring.assign(std::max<std::size_t>(1,std::size_t(std::lround(m_period*0.8))),0);
    reset(); return true;
}
void AisDecoder::reset() {
    std::fill(m_ring.begin(),m_ring.end(),0); m_sum=0; m_index=m_filled=0;
    m_previous={}; m_havePrevious=false; m_sample=m_lastDelivery=0; m_lastPayload.clear();
    for (std::size_t i=0;i<m_branches.size();++i) {
        m_branches[i]=Branch{}; m_branches[i].next=m_ring.size()+m_period*i/m_branches.size();
    }
}
std::uint16_t AisDecoder::fcs(const std::uint8_t *data,std::size_t size) noexcept {
    std::uint16_t crc=0xffff;
    for(std::size_t i=0;i<size;++i) { crc^=data[i]; for(int b=0;b<8;++b) crc=std::uint16_t((crc>>1)^((crc&1)?0x8408:0)); }
    return std::uint16_t(crc^0xffff);
}
void AisDecoder::process(const std::complex<float> *iq,std::size_t count) {
    if (!m_rate || !iq) return;
    for(std::size_t i=0;i<count;++i) {
        const auto z=iq[i];
        if (!std::isfinite(z.real())||!std::isfinite(z.imag())||std::norm(z)<1e-12f) {
            // A carrier break invalidates all partially received frames.
            if(m_havePrevious) reset();
            continue;
        }
        if(!m_havePrevious) { m_previous=z; m_havePrevious=true; continue; }
        const auto diff=std::conj(m_previous)*z; m_previous=z;
        const double discriminator=std::remainder(std::atan2(diff.imag(),diff.real())-m_offsetStep,2*pi);
        m_sum+=discriminator-m_ring[m_index]; m_ring[m_index]=discriminator;
        m_index=(m_index+1)%m_ring.size(); if(m_filled<m_ring.size()) ++m_filled;
        ++m_sample; if(m_filled<m_ring.size()) continue;
        const bool symbol=m_sum>0;
        for(auto &branch:m_branches) {
            if(double(m_sample)<branch.next) continue;
            branch.next+=m_period;
            if(branch.havePrevious) bit(branch,symbol==branch.previous);
            branch.previous=symbol; branch.havePrevious=true;
        }
    }
}
void AisDecoder::bit(Branch &b,bool value) {
    b.shift=std::uint8_t((b.shift>>1)|(value?0x80:0)); if(b.seen<8) ++b.seen;
    if(b.inFrame) b.bits.push_back(value);
    if(b.seen==8 && b.shift==0x7e) {
        if(b.inFrame && b.bits.size()>=8) { b.bits.resize(b.bits.size()-8); finish(b.bits); }
        b.bits.clear(); b.inFrame=true;
    } else if(b.bits.size()>maxFrameBits) { b.bits.clear(); b.inFrame=false; }
}
void AisDecoder::finish(const std::vector<std::uint8_t> &raw) {
    std::vector<std::uint8_t> bytes; bytes.reserve(raw.size()/8);
    unsigned ones=0, index=0; std::uint8_t byte=0;
    for(auto b:raw) {
        if(b) { if(++ones>5) return; }
        else { if(ones==5) { ones=0; continue; } ones=0; }
        byte|=std::uint8_t(b<<index);
        if(++index==8) { bytes.push_back(byte); byte=0; index=0; }
    }
    if(index || ones==5 || bytes.size()<7 || bytes.size()>130) return;
    const auto crc=fcs(bytes.data(),bytes.size()-2);
    if(bytes[bytes.size()-2]!=(crc&255)||bytes.back()!=(crc>>8)) return;
    bytes.resize(bytes.size()-2);
    Message m; m.payload=bytes; m.type=bits(bytes,0,6); m.mmsi=bits(bytes,8,30);
    if(m.type==0 || m.type>27) return;
    // Class A position reports use a different navigation-block offset from
    // class B. Require the full standard message, not just the position bits.
    const bool classA = (m.type<=3 || m.type==9) && bytes.size()>=21;
    const bool classB = (m.type==18 && bytes.size()>=21) || (m.type==19 && bytes.size()>=39);
    const bool baseStation = (m.type==4 || m.type==11) && bytes.size()>=21;
    // Message 21 may include a name extension, but its fixed part is 272 bits.
    const bool aidToNavigation = m.type==21 && bytes.size()>=34 && bytes.size()<=45;
    if(classA || classB || baseStation || aidToNavigation) {
        const unsigned lonOffset=classA ? 61 : (baseStation ? 79 : (aidToNavigation ? 164 : 57));
        const unsigned latOffset=classA ? 89 : (baseStation ? 107 : (aidToNavigation ? 192 : 85));
        const auto lon=signedBits(bytes,lonOffset,28), lat=signedBits(bytes,latOffset,27);
        if(std::abs(double(lon))<=180*600000. && std::abs(double(lat))<=90*600000.) {
            m.hasPosition=true; m.longitude=lon/600000.; m.latitude=lat/600000.;
        }
    }
    // Long-range reports use tenths of a minute instead of 1/10000 minute.
    if((m.type==27 && bytes.size()>=12) || (m.type==17 && bytes.size()>=10 && bytes.size()<=102)) {
        const auto lon=signedBits(bytes,m.type==17 ? 40 : 44,18), lat=signedBits(bytes,m.type==17 ? 58 : 62,17);
        if(std::abs(double(lon))<=180*600. && std::abs(double(lat))<=90*600.) {
            m.hasPosition=true; m.longitude=lon/600.; m.latitude=lat/600.;
        }
    }
    if(bytes==m_lastPayload && m_sample-m_lastDelivery < m_period*1.5) return;
    m_lastPayload=bytes; m_lastDelivery=m_sample;
    if(m_callback) m_callback(m_context,m);
}
}
