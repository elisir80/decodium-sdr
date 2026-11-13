// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Vdl2Acquisition.h"
#include "Vdl2Burst.h"
namespace dsdr::vdl2 {
// Experimental finite-window receiver for channel-filtered IQ. It uses a
// fixed symbol clock, no timing PLL, and is not a streaming app receiver.
struct OfflineBurst { Acquisition acquisition; Burst burst; std::size_t endSample=0; std::uint64_t generation=0; };
std::optional<OfflineBurst> decodeOffline(const std::complex<float>* iq,
    std::size_t count,double sampleRate,double minimumMeanPower);
// Decode an already detected candidate; acquisition refers to this IQ block.
std::optional<OfflineBurst> decodeCandidate(const std::complex<float>* iq,
    std::size_t count,double sampleRate,const Acquisition& candidate);
}
