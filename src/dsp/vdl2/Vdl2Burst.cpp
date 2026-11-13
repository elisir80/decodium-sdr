// SPDX-License-Identifier: GPL-3.0-or-later
// Based on OnAir by Kristijonas Adomaitis - https://on-air.dev
#include "Vdl2Burst.h"
#include "Vdl2Frame.h"
#include "OnAirVdl2Abi.h"
#include <algorithm>
#include <cstddef>

namespace dsdr::vdl2 {
namespace {
constexpr std::size_t headerBits=25, maxBits=262144;
std::optional<BurstHeader> inspectDecoded(const std::vector<std::uint8_t>& decoded) {
    std::uint32_t header=0, length=0;
    for(std::size_t i=0;i<headerBits;++i) header=(header<<1)|decoded[i];
    BurstHeader result;
    OnairVdl2Layout layout{};
    if(!onair_vdl2_header_decode(header,&length,&result.corrected) ||
       !onair_vdl2_layout(length,&layout) || layout.data_oct<0 || layout.fec_oct<0) return {};
    const auto body=std::size_t(layout.data_oct)+std::size_t(layout.fec_oct);
    if(body>(maxBits-headerBits)/8 || length>maxBits) return {};
    result.dataBits=length;
    result.totalBits=headerBits+body*8;
    return result;
}
}
std::optional<BurstHeader> inspectBurstHeader(const std::vector<std::uint8_t>& bits) {
    if(!compatible() || bits.size()<headerBits ||
       std::any_of(bits.begin(),bits.begin()+headerBits,[](auto b){return b>1;})) return {};
    std::vector<std::uint8_t> decoded(bits.begin(),bits.begin()+headerBits);
    onair_vdl2_scramble(decoded.data(),decoded.size());
    return inspectDecoded(decoded);
}
std::optional<Burst> decodeBurst(const std::vector<std::uint8_t>& bits) {
    if (!compatible() || bits.size()<headerBits || bits.size()>maxBits ||
        std::any_of(bits.begin(),bits.end(),[](auto b){return b>1;})) return {};
    auto decoded=bits;
    onair_vdl2_scramble(decoded.data(),decoded.size());
    const auto header=inspectDecoded(decoded);
    if(!header || decoded.size()<header->totalBits) return {};
    Burst result;
    result.headerCorrected=header->corrected;
    OnairVdl2Layout layout{};
    if(!onair_vdl2_layout(header->dataBits,&layout)) return {};
    const auto body=(header->totalBits-headerBits)/8;
    std::vector<std::uint8_t> data(header->dataBits);
    std::int32_t fixed=0;
    const auto written=onair_vdl2_burst_data(decoded.data()+headerBits,body*8,&layout,header->dataBits,
                                              data.data(),data.size(),&fixed);
    if(written<0 || std::size_t(written)!=data.size()) return {};
    result.correctedOctets=fixed;
    std::vector<std::uint8_t> octets(data.size()/8+1);
    std::vector<std::uint32_t> sizes(data.size()/16+1);
    std::size_t count=0, used=0;
    if(!onair_vdl2_hdlc_frames(data.data(),data.size(),octets.data(),octets.size(),
                                sizes.data(),sizes.size(),&count,&used) ||
       count>sizes.size() || used>octets.size()) return {};
    std::size_t offset=0;
    for(std::size_t i=0;i<count;++i) {
        const auto n=std::size_t(sizes[i]);
        if(n>used-offset) return {};
        result.frames.emplace_back(octets.begin()+offset,octets.begin()+offset+n);
        offset+=n;
    }
    if(offset!=used) return {};
    return result;
}
}
