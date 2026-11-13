// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <complex>
#include <cstddef>
#include <optional>
namespace dsdr::vdl2 {
struct Acquisition {
    std::size_t sample=0; // approximate first training symbol
    double carrierHz=0;
    double correlation=0;
};
// Searches a finite, already channel-filtered IQ block. The caller supplies an
// energy floor; this component does not provide filtering or streaming state.
std::optional<Acquisition> findPreamble(const std::complex<float>* samples,
    std::size_t count,double sampleRate,double minimumMeanPower);
}
