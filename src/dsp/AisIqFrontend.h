// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <complex>
#include <cstddef>
#include <vector>
namespace dsdr::dsp {
// One-channel frequency translation, FIR channel filter and decimator for AIS.
// Output is complex IQ centred on the selected channel, at a rate between
// 48 and 96 ksample/s (input rate divided by an integer).
// The callback executes synchronously on the producer's thread; it must not
// block or throw. A callback with nullptr and count zero indicates an invalid
// input sample; the consumer must mark a discontinuity before accepting more.
// Configure/reset require a quiescent producer. No allocations in process().
// One instance per AIS channel.
class AisIqFrontend final {
public:
    using Sample = std::complex<float>;
    using Callback = void (*)(void *, const Sample *, std::size_t) noexcept;
    explicit AisIqFrontend(Callback callback, void *context = nullptr) noexcept;
    // Input rate 48 ksample/s to 2.4 Msample/s. offsetHz is relative to
    // the source IQ centre and must leave 15 kHz to the Nyquist edge.
    bool configure(unsigned inputRate, double offsetHz);
    void reset() noexcept;
    void process(const Sample *samples, std::size_t count) noexcept;
    bool configured() const noexcept { return m_rate != 0; }
    double outputRate() const noexcept { return m_factor ? double(m_rate)/m_factor : 0; }
private:
    Callback m_callback;
    void *m_context;
    unsigned m_rate = 0, m_factor = 0, m_phase = 0;
    std::vector<float> m_taps;
    std::vector<Sample> m_history;
    std::size_t m_cursor = 0;
    std::complex<double> m_osc{1,0}, m_step{1,0};
    std::array<Sample,1024> m_output{};
};
}
