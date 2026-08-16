// SPDX-License-Identifier: GPL-3.0-or-later
// Scelta del canale che definisce il riferimento di una IF fissa.
#pragma once

#include "hal/Frames.h"

#include <QHash>

namespace dsdr::hal::rtlsdr {

// RX 1 (l'id piu' basso) e' il VFO della radio. Gli altri canali sono
// demodulatori virtuali nella stessa fetta IQ: il loro modo non deve cambiare
// il lato dell'IF acquisita dall'hardware.
inline DemodMode ifReferenceDemod(const QHash<ChannelId, RxChannelConfig> &channels,
                                  DemodMode fallback)
{
    auto reference = channels.cend();
    for (auto it = channels.cbegin(); it != channels.cend(); ++it) {
        if (reference == channels.cend() || it.key() < reference.key())
            reference = it;
    }
    return reference == channels.cend() ? fallback : reference->mode;
}

inline bool ifReferenceUsesLsb(const QHash<ChannelId, RxChannelConfig> &channels,
                               DemodMode fallback)
{
    const DemodMode mode = ifReferenceDemod(channels, fallback);
    return mode == DemodMode::Lsb || mode == DemodMode::DigL;
}

} // namespace dsdr::hal::rtlsdr
