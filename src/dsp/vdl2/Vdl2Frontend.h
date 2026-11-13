// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <complex>
#include <cstddef>
#include <vector>
namespace dsdr::vdl2 {
// Single-channel mixer, FIR and integer decimator. The callback runs on the
// calling thread; nullptr/zero denotes a discontinuity. Configure and reset
// require a quiescent producer. Output is 84-168 ksample/s.
class Frontend final {
public:
    using Sample=std::complex<float>;
    using Callback=void (*)(void*,const Sample*,std::size_t) noexcept;
    Frontend(Callback callback,void* context=nullptr) noexcept;
    bool configure(unsigned rate,double offset);
    void reset() noexcept;
    void process(const Sample* samples,std::size_t count) noexcept;
    double outputRate() const noexcept { return m_factor?double(m_rate)/m_factor:0; }
private:
    Callback m_callback; void* m_context;
    unsigned m_rate=0,m_factor=0,m_phase=0;
    std::vector<float> m_taps; std::vector<Sample> m_history;
    std::size_t m_cursor=0;
    std::complex<double> m_osc{1,0},m_step{1,0};
    std::array<Sample,1024> m_output{};
};
}
