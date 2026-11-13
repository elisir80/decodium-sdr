// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <complex>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>
namespace dsdr::vdl2 {
// Input is already carrier-corrected and sampled at symbol centres. The first
// sample is the final training symbol; subsequent samples carry D8PSK data.
// Burst detection, timing and carrier recovery are not provided here.
std::optional<std::vector<std::uint8_t>> decodeSymbols(
    const std::complex<float>* samples, std::size_t count);
}
