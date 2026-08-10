// SPDX-License-Identifier: GPL-3.0-or-later
#include "dsp/FftwPlanning.h"

namespace dsdr::dsp {

std::mutex &fftwPlanningMutex()
{
    static std::mutex mutex;
    return mutex;
}

} // namespace dsdr::dsp
