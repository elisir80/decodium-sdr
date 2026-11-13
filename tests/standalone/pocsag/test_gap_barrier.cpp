// SPDX-License-Identifier: GPL-3.0-or-later
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "core/PocsagGapBarrier.h"
#include <cassert>
#include <cstdint>
int main() {
    dsdr::core::PocsagGapBarrier b;
    auto d=b.classify(0,100); assert(!d.discard && !d.invalidate);
    // Unknown position inside the 150 frames written when loss is reported.
    b.onLoss(150);
    d=b.classify(100,75); assert(d.discard && d.invalidate);
    d=b.classify(175,25); assert(!d.discard && !d.invalidate);
    assert(b.discardedFrames()==75);
    // Repeated or out-of-order notifications cannot move the boundary back.
    b.onLoss(300); b.onLoss(250);
    d=b.classify(200,80); assert(d.discard && d.invalidate);
    d=b.classify(280,40); assert(d.discard && !d.invalidate);
    d=b.classify(320,20); assert(!d.discard && !d.invalidate);
    assert(b.discardedFrames()==195);
    d=b.classify(UINT64_MAX-3,8); assert(d.discard);
    d=b.classify(320,0); assert(d.discard);
}
