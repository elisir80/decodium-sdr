// SPDX-License-Identifier: GPL-3.0-or-later
// Driver CAT condivisi dai backend che usano una radio tradizionale come
// piano di controllo: audio+CAT e panadapter IF+CAT.
#pragma once

#include "hal/backends/audiorig/CivDriver.h"
#include "hal/backends/audiorig/ICatDriver.h"
#include "hal/backends/audiorig/LocalRigctldDriver.h"
#include "hal/backends/audiorig/NewcatDriver.h"
#include "hal/backends/audiorig/RigctldDriver.h"

#include <memory>

namespace dsdr::hal::audiorig {

inline std::unique_ptr<ICatDriver> makeCatDriver(const QString &driverId)
{
    if (driverId == QLatin1String("civ"))
        return std::make_unique<CivDriver>();
    if (driverId == QLatin1String("rigctld"))
        return std::make_unique<RigctldDriver>();
    if (driverId == QLatin1String("hamlib-local"))
        return std::make_unique<LocalRigctldDriver>();
    return std::make_unique<NewcatDriver>();
}

} // namespace dsdr::hal::audiorig
