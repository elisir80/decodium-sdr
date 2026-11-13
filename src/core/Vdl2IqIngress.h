// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Vdl2Session.h"
#include "RawIqTap.h"
#include <array>
#include <atomic>
namespace dsdr::core {
// Adapter for the engine's interleaved raw IQ. Single producer only; the
// session must outlive it. Construct/reconfigure outside the producer thread.
// The configured source metadata prevents decoding under the wrong tuning.
class Vdl2IqIngress final : public RawIqTap {
public:
    enum class Status { Waiting, Active, NoInput, WrongRate, WrongCenter };
    Vdl2IqIngress(Vdl2Session& session,double sourceRate,std::int64_t centerHz) noexcept;
    void process(const float* interleaved,std::size_t frames,double sourceRate,
                 std::int64_t centerHz) noexcept override;
    void discontinuity() noexcept override;
    void invalidateResults() noexcept override { m_status.store(Status::Waiting,std::memory_order_release); m_session.invalidateResults(); }
    Status status() const noexcept { return m_status.load(std::memory_order_acquire); }
private:
    Vdl2Session& m_session;
    double m_rate;
    std::int64_t m_center;
    bool m_valid=false;
    std::atomic<Status> m_status{Status::Waiting};
    std::array<dsp::Complex,4096> m_iq{};
};
}
