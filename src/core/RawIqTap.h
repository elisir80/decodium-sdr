// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>
namespace dsdr::core {
// DSP-thread callback for raw interleaved IQ. The callback must not block,
// allocate or retain the samples. Attach only while the DSP is quiescent and
// detach only after its producer/backend lifetime fence has completed.
class RawIqTap {
public:
    virtual ~RawIqTap() = default;
    virtual void process(const float*,std::size_t,double,std::int64_t) noexcept=0;
    virtual void discontinuity() noexcept=0;
    // May be called concurrently with DSP processing; fences already queued
    // results without mutating producer-owned filter/queue state.
    virtual void invalidateResults() noexcept=0;
};
}
