// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "core/AisReceiver.h"
#include "dsp/AisIqFrontend.h"
#include <cstddef>
#include <cstdint>
namespace dsdr::core {
// Producer-side AIS channel selector; owns no thread or receiver. Receiver must
// outlive the tap. Configure/reset/process are serialized on the IQ producer;
// configure allocates filter state and belongs in a reconfiguration phase.
class AisEngineTap final {
public:
    explicit AisEngineTap(AisReceiver &receiver) noexcept;
    // Source rate is in [48 kHz, 2.4 MHz]. Select one channel offset relative
    // to source centre. Each reconfigure invalidates
    // old AIS generations. Receiver source state is published separately.
    bool configure(unsigned sourceRate, double channelOffsetHz);
    double outputRate() const noexcept { return m_frontend.outputRate(); }
    void reset() noexcept;
    // Snapshot generation before the source block is acquired. No allocation,
    // waiting, source-state mutation or GUI calls in this producer path.
    void process(const dsp::Complex *samples, std::size_t count, std::uint64_t generation) noexcept;
private:
    static void output(void *,const dsp::AisIqFrontend::Sample *,std::size_t) noexcept;
    AisReceiver &m_receiver;
    dsp::AisIqFrontend m_frontend;
    std::uint64_t m_generation = 0, m_filterGeneration = 0;
    bool m_accept = false;
};
}
