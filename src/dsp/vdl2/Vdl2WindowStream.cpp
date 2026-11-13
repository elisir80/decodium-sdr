// SPDX-License-Identifier: GPL-3.0-or-later
#include "Vdl2WindowStream.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>
namespace dsdr::vdl2 {
WindowStream::WindowStream(double rate,double floor,Callback callback)
    :m_rate(rate),m_floor(floor),m_callback(std::move(callback)) {
    if(!std::isfinite(rate) || rate<42000 || rate>4000000 ||
       !std::isfinite(floor) || floor<0 || !m_callback) throw std::invalid_argument("invalid VDL2 stream configuration");
    m_samples.reserve(windowSize);
}
void WindowStream::reset() noexcept { m_samples.clear(); m_base=0; m_lastStart=0; m_haveLast=false; }
void WindowStream::feed(const std::complex<float>* iq,std::size_t count) {
    if(!iq && count) throw std::invalid_argument("null VDL2 IQ samples");
    while(count) {
        const auto take=std::min(count,windowSize-m_samples.size());
        m_samples.insert(m_samples.end(),iq,iq+take); iq+=take; count-=take;
        if(m_samples.size()!=windowSize) continue;
        // Partition around each valid burst to find others in the same window.
        // Search other regions even if the strongest candidate is invalid.
        // This experimental receiver is not a multi-signal detector.
        struct Range { std::size_t first,last; };
        std::vector<Range> pending{{0,m_samples.size()}};
        std::vector<std::pair<std::size_t,OfflineBurst>> found;
        std::size_t attempts=0;
        while(!pending.empty() && found.size()<32 && attempts++<64) {
            const auto range=pending.back(); pending.pop_back();
            const auto candidate=findPreamble(m_samples.data()+range.first,range.last-range.first,m_rate,m_floor);
            if(!candidate) continue;
            auto decoded=decodeCandidate(m_samples.data()+range.first,range.last-range.first,m_rate,*candidate);
            // A rounded preamble peak can land next to the best symbol phase,
            // especially near the minimum sample rate. Keep the fallback bounded
            // and require normal header, FEC and frame integrity checks.
            for(int shift : {-1,1}) {
                if(decoded) break;
                if(shift<0 && candidate->sample==0) continue;
                auto adjusted=*candidate;
                if(shift<0) --adjusted.sample; else ++adjusted.sample;
                decoded=decodeCandidate(m_samples.data()+range.first,range.last-range.first,m_rate,adjusted);
            }
            const auto start=range.first+candidate->sample;
            const auto guard=std::size_t(std::ceil(16*m_rate/10500.0));
            const auto end=decoded ? range.first+decoded->endSample : std::min(range.last,start+guard);
            const auto before=start>range.first+guard ? start-guard : range.first;
            if(before>range.first) pending.push_back({range.first,before});
            if(end<range.last) pending.push_back({end,range.last});
            if(decoded) found.emplace_back(start,*decoded);
        }
        std::sort(found.begin(),found.end(),[](const auto& a,const auto& b){return a.first<b.first;});
        for(const auto& entry:found) {
            const auto start=m_base+entry.first;
            // Sampling uncertainty across overlapping windows can shift the
            // estimated preamble by several samples.
            if(!m_haveLast || start>m_lastStart+std::uint64_t(std::ceil(m_rate/10500.0))) {
                m_lastStart=start; m_haveLast=true; m_callback(entry.second);
            }
        }
        m_samples.erase(m_samples.begin(),m_samples.begin()+hopSize);
        m_base+=hopSize;
    }
}
}
