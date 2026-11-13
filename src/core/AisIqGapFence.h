// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "core/AisReceiver.h"
#include <atomic>
#include <cstdint>
namespace dsdr::core {
static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
              "AIS gap notifications require lock-free 64-bit atomics");
// Conservative loss fence for one SpscRing<float> containing interleaved IQ.
// Positions are absolute *float elements*, not IQ frames. The source owner
// must snapshot totalWritten() after a loss, while that same ring is alive,
// and prevent old-source notifications from entering a new ring epoch.
class AisIqGapFence final {
public:
    struct Ticket { std::uint64_t event=0, generation=0; bool valid=false; };
    explicit AisIqGapFence(AisReceiver &receiver) noexcept : m_receiver(receiver) {}
    // May run on the source notification thread. The snapshot must cover the
    // reported loss; invalidates queued AIS immediately. Monotonic within epoch.
    void notifyLoss(std::uint64_t writtenFloats) noexcept {
        m_inFlight.fetch_add(1); m_receiver.discontinuity();
        if(writtenFloats & 1u) m_invalid.store(true);
        auto previous=m_fence.load();
        while(previous<writtenFloats && !m_fence.compare_exchange_weak(previous,writtenFloats)) {}
        m_event.fetch_add(1); m_inFlight.fetch_sub(1);
    }
    // Call before acquiring the source block. An event crossing the read causes
    // acceptance to fail; submitForGeneration provides the final generation check.
    Ticket snapshot() const noexcept {
        Ticket t; if(m_inFlight.load() || m_invalid.load()) return t;
        t.event=m_event.load(); t.generation=m_receiver.generation();
        t.valid=!m_inFlight.load() && !m_invalid.load() && t.event==m_event.load(); return t;
    }
    // first/last are totalRead() before/after the read, in float elements.
    // A block crossing the fence is dropped whole, not spliced.
    bool accepts(const Ticket &t,std::uint64_t first,std::uint64_t last) const noexcept {
        if(!t.valid || (first&1u) || (last&1u) || last<=first || m_inFlight.load() || m_invalid.load()) return false;
        const auto fence=m_fence.load();
        return first>=fence && t.event==m_event.load() && t.generation==m_receiver.generation()
               && !m_inFlight.load() && !m_invalid.load();
    }
    // Source notifications, producer, consumer and ring reset must all be
    // quiescent. Call after old notifications are detached, before new epoch.
    void resetQuiescent() noexcept {
        m_receiver.discontinuity(); m_fence.store(0); m_invalid.store(false); m_event.fetch_add(1);
    }
private:
    AisReceiver &m_receiver;
    std::atomic<std::uint64_t> m_fence{0},m_event{0};
    std::atomic<unsigned> m_inFlight{0};
    std::atomic<bool> m_invalid{false};
};
}
