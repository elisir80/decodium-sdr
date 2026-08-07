// SPDX-License-Identifier: GPL-3.0-or-later
#include "dsp/DspTypes.h"

#include <cmath>

namespace dsdr::dsp {

float powerToDb(float magSquared) noexcept
{
    constexpr float kFloor = 1e-20f;
    return 10.0f * std::log10(magSquared < kFloor ? kFloor : magSquared);
}

} // namespace dsdr::dsp
