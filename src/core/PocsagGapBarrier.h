// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <atomic>
#include <cstdint>

namespace dsdr::core {
// Tracks the exclusive absolute producer position of IQ made ambiguous by a
// loss. Positions must belong to the same source epoch and count whole frames.
// onLoss may run on the producer; classify is single-consumer. Source/epoch
// replacement requires both threads quiescent and a fresh barrier. This is
// not an integration barrier by itself: a loss may race classify/submit, so
// the host must independently invalidate queued IQ and results by generation,
// and must serialize epoch snapshots with actual ring positions.
class PocsagGapBarrier final {
public:
    struct Decision { bool discard; bool invalidate; };
    void onLoss(std::uint64_t writtenFrames) noexcept {
        auto old = m_boundary.load(std::memory_order_relaxed);
        while (old < writtenFrames && !m_boundary.compare_exchange_weak(old, writtenFrames,
                         std::memory_order_release, std::memory_order_relaxed)) {}
        m_epoch.fetch_add(1, std::memory_order_release);
    }
    Decision classify(std::uint64_t firstFrame, std::uint64_t count) noexcept {
        const auto epoch = m_epoch.load(std::memory_order_acquire);
        const auto boundary = m_boundary.load(std::memory_order_acquire);
        const bool changed = epoch != m_seen;
        m_seen = epoch;
        // A crossing block is discarded whole, even its post-boundary suffix.
        // An invalid/overflowing interval is always rejected.
        const bool invalid = !count || firstFrame > UINT64_MAX - count;
        const bool discard = invalid || firstFrame < boundary;
        if (discard && !invalid) m_discarded.fetch_add(count, std::memory_order_relaxed);
        return {discard, changed};
    }
    std::uint64_t discardedFrames() const noexcept { return m_discarded.load(std::memory_order_relaxed); }
private:
    std::atomic<std::uint64_t> m_boundary{0}, m_epoch{0}, m_discarded{0};
    std::uint64_t m_seen = 0;
};
}
