// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "core/VorAsyncReceiver.h"
#include "dsp/DecimatorChain.h"
#include <array>
#include <complex>
#include <cstddef>
#include <cstdint>
namespace dsdr::core {
// Engine-thread only. The owner must quiesce the producer before detaching or
// changing configuration and must keep the receiver alive until then.
class VorEngineTap final {
public:
    static constexpr unsigned MinInputRate=96000, MaxInputRate=2048000;
    explicit VorEngineTap(VorAsyncReceiver& receiver) : m_receiver(receiver) {}
    // Raw complex input, integer rates from 96 kHz to 2.048 MHz. Tune is
    // relative to source centre. Retains approximately +/- 12 kHz at output.
    bool configure(unsigned inputRate, double tuneHz);
    void reset() noexcept;
    void process(const dsp::Complex* samples, std::size_t count) noexcept;
    void processForGeneration(const dsp::Complex* samples, std::size_t count, std::uint64_t generation) noexcept;
    std::uint64_t submitted() const noexcept { return m_submitted; }
private:
    void deliver(dsp::Complex sample, std::uint64_t generation) noexcept;
    static constexpr std::size_t maxTaps=961, chunk=512;
    VorAsyncReceiver& m_receiver;
    std::array<float,maxTaps> m_coeff{};
    std::array<std::complex<float>,maxTaps> m_ring{};
    std::array<std::complex<float>,chunk> m_output{};
    dsp::DecimatorChain m_highRateChain;
    std::array<dsp::Complex,dsp::kMaxBlockFrames> m_mixed{}, m_decimated{};
    bool m_highRate=false;
    unsigned m_rate=0,m_decimation=0,m_phase=0;
    std::size_t m_pos=0,m_out=0;
    std::size_t m_taps=0;
    std::uint64_t m_resamplePhase=0;
    std::complex<float> m_previous{};
    bool m_havePrevious=false;
    double m_osc=0,m_step=0;
    std::uint64_t m_submitted=0;
    std::uint64_t m_generation=0;
};
}
