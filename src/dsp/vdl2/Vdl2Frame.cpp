// SPDX-License-Identifier: GPL-3.0-or-later
#include "Vdl2Frame.h"
#include "OnAirVdl2Abi.h"
#include <memory>
namespace dsdr::vdl2 {
bool compatible() noexcept {
    std::size_t sizes[3]{}; onair_vdl2_struct_sizes(sizes);
    return onair_vdl2_version()==2 && sizes[0]==sizeof(OnairVdl2Parsed) &&
           sizes[1]==sizeof(OnairVdl2Layout) && sizes[2]==sizeof(OnairVdl2Addr);
}
std::optional<Frame> parse(const std::uint8_t* bytes,std::size_t size) {
    // Two FCS bytes plus the minimum AVLC header; bound allocations and ABI input.
    if(!bytes || size<11 || size>4096 || !compatible() || !onair_vdl2_fcs_ok(bytes,size)) return {};
    auto parsed=std::make_unique<OnairVdl2Parsed>();
    if(!onair_vdl2_parse_frame(bytes,size-2,parsed.get())) return {};
    if(parsed->frame_name.len>sizeof(parsed->frame_name.b) ||
       parsed->proto.len>sizeof(parsed->proto.b) || parsed->summary.len>sizeof(parsed->summary.b)) return {};
    Frame out;
    out.source=parsed->src; out.destination=parsed->dst;
    out.sourceType=parsed->src_type; out.destinationType=parsed->dst_type;
    out.fromAir=parsed->from_air!=0; out.fromGround=parsed->from_ground!=0;
    out.kind.assign(reinterpret_cast<const char*>(parsed->frame_name.b),parsed->frame_name.len);
    out.protocol.assign(reinterpret_cast<const char*>(parsed->proto.b),parsed->proto.len);
    out.summary.assign(reinterpret_cast<const char*>(parsed->summary.b),parsed->summary.len);
    return out;
}
}
