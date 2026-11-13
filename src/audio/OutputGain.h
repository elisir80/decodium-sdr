// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace dsdr::audio {
// Caller supplies the test override, keeping the normal volume curve intact.
inline float outputGain(float volume, bool muted, bool testSilent) noexcept
{
    return (muted || testSilent) ? 0.0f : volume * volume;
}
}
