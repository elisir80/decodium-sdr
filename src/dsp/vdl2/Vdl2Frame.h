// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
namespace dsdr::vdl2 {
// Parsed AVLC fields from a complete frame with a valid FCS. ACARS payload
// enrichment and IQ demodulation are not provided by this adapter yet.
struct Frame {
    std::uint32_t source=0,destination=0;
    int sourceType=0,destinationType=0;
    bool fromAir=false,fromGround=false;
    std::string kind,protocol,summary;
};
bool compatible() noexcept;
std::optional<Frame> parse(const std::uint8_t* data,std::size_t size);
}
