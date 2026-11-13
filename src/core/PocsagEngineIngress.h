// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "core/PocsagGapBarrier.h"
#include "core/PocsagReceiver.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace dsdr::core {
// One instance per source/ring epoch. The receiver outlives this object.
// onLoss may run on the ring producer; snapshot/process run on the sole DSP
// consumer. Do not destroy or reconfigure either object until both are idle.
// Positions are whole IQ frames, while the engine ring counts interleaved
// floats: callers must validate even alignment and divide positions by two.
class PocsagEngineIngress final {
public:
    explicit PocsagEngineIngress(PocsagReceiver &receiver) noexcept : m_receiver(receiver) {}
    std::uint64_t snapshot() const noexcept { return m_receiver.generation(); }
    // Call at the loss notification with the observed exclusive write position.
    // The double invalidation covers a consumer racing publication of the
    // boundary: old snapshots cannot become valid after the boundary changes.
    void onLoss(std::uint64_t writtenFrames) noexcept {
        m_receiver.invalidateResults();
        m_barrier.onLoss(writtenFrames);
        m_receiver.invalidateResults();
    }
    // Snapshot before reading the ring. No allocations, locks or waits.
    // The caller must discard malformed / odd-aligned ring reads itself.
    void process(const float *iq, std::size_t frames, std::uint64_t firstFrame,
                 std::uint64_t generation) noexcept {
        const auto decision = m_barrier.classify(firstFrame, frames);
        if (decision.invalidate) m_receiver.discontinuity();
        if (decision.discard || !iq || generation != m_receiver.generation()) return;
        for (std::size_t at = 0; at < frames;) {
            const auto n = std::min(m_block.size(), frames - at);
            for (std::size_t i = 0; i < n; ++i)
                m_block[i] = {iq[2*(at+i)], iq[2*(at+i)+1]};
            if (m_receiver.submitForGeneration(m_block.data(), n, generation)
                    != AsyncIqDecoder::SubmitResult::Accepted) return;
            if (generation != m_receiver.generation()) return;
            at += n;
        }
    }
    std::uint64_t discardedFrames() const noexcept { return m_barrier.discardedFrames(); }
private:
    PocsagReceiver &m_receiver;
    PocsagGapBarrier m_barrier;
    std::array<dsp::Complex, 1024> m_block{};
};
}
