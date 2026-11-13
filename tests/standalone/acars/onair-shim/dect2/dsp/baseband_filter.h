// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <vector>
namespace dect2::bbf {
inline std::vector<float> overrideTaps(const char*,std::vector<float> taps,double,double) { return taps; }
}
