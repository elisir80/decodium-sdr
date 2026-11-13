// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "dsp/AdsbDecoder.h"
#include <QString>
#include <optional>

namespace dsdr::core {
// Fields describe one CRC-validated frame, never an aircraft-wide cache.
struct AdsbMessage {
    QString icao;
    int typeCode = -1;
    QString callsign;
    std::optional<int> barometricAltitudeFeet;
    std::optional<double> groundSpeedKnots;
    std::optional<double> groundTrackDegrees;
    std::optional<double> airspeedKnots;
    std::optional<bool> trueAirspeed;
    std::optional<double> magneticHeadingDegrees;
    std::optional<int> verticalRateFeetPerMinute;
    static AdsbMessage decode(const dsp::AdsbDecoder::Frame &frame);
};
}
