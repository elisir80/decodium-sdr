// SPDX-License-Identifier: GPL-3.0-or-later
#include "Vdl2Offline.h"
#include "Vdl2Symbols.h"
#include "Vdl2Frame.h"
#include <algorithm>
#include <cmath>
namespace dsdr::vdl2 {
std::optional<OfflineBurst> decodeOffline(const std::complex<float>* iq,std::size_t count,double rate,double floor) {
    const auto found=findPreamble(iq,count,rate,floor);
    if(!found) return {};
    return decodeCandidate(iq,count,rate,*found);
}
std::optional<OfflineBurst> decodeCandidate(const std::complex<float>* iq,std::size_t count,double rate,const Acquisition& candidate) {
    constexpr double pi=3.14159265358979323846, baud=10500;
    if(!iq || !std::isfinite(rate) || rate<42000 || rate>4000000 || candidate.sample>=count ||
       !std::isfinite(candidate.carrierHz)) return {};
    const auto* found=&candidate;
    const double T=rate/baud;
    auto sample=[&](std::size_t symbols)->std::optional<std::vector<std::complex<float>>> {
        std::vector<std::complex<float>> out;
        if(symbols>89999) return {};
        out.reserve(symbols+1);
        for(std::size_t i=0;i<=symbols;++i) {
            const double position=double(found->sample)+(15.0+double(i))*T;
            if(!std::isfinite(position) || position<0 || position+1>=double(count)) return {};
            const auto index=std::size_t(position);
            const float frac=float(position-index);
            const auto z=iq[index]*(1-frac)+iq[index+1]*frac;
            const double angle=-2*pi*found->carrierHz*(position-double(found->sample))/rate;
            out.push_back(z*std::polar(1.0f,float(angle)));
        }
        return out;
    };
    auto headerSamples=sample(9);
    if(!headerSamples) return {};
    const auto headerBits=decodeSymbols(headerSamples->data(),headerSamples->size());
    if(!headerBits) return {};
    const auto header=inspectBurstHeader(*headerBits);
    if(!header) return {};
    auto allSamples=sample((header->totalBits+2)/3);
    if(!allSamples) return {};
    const auto bits=decodeSymbols(allSamples->data(),allSamples->size());
    if(!bits) return {};
    auto burst=decodeBurst(*bits);
    if(!burst) return {};
    // AVLC may contain multiple frames in one burst. A damaged frame must not
    // suppress other frames with a valid FCS.
    burst->frames.erase(std::remove_if(burst->frames.begin(),burst->frames.end(),
        [](const auto& frame){return !parse(frame.data(),frame.size());}),burst->frames.end());
    if(burst->frames.empty()) return {};
    const auto end=std::size_t(std::ceil(double(found->sample)+(15.0+double((header->totalBits+2)/3))*T))+1;
    return OfflineBurst{*found,std::move(*burst),end};
}
}
