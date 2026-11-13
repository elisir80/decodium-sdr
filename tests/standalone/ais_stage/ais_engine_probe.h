// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
struct AisProbeMessage {
    std::uint32_t mmsi;
    unsigned type;
    bool position;
    double latitude, longitude;
};
