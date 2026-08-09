// SPDX-License-Identifier: GPL-3.0-or-later
#include "common/Types.h"

namespace dsdr {

QString demodModeName(DemodMode mode)
{
    switch (mode) {
    case DemodMode::Usb:  return QStringLiteral("USB");
    case DemodMode::Lsb:  return QStringLiteral("LSB");
    case DemodMode::Cw:   return QStringLiteral("CW");
    case DemodMode::Cwr:  return QStringLiteral("CW-R");
    case DemodMode::Am:   return QStringLiteral("AM");
    case DemodMode::Sam:  return QStringLiteral("SAM");
    case DemodMode::Fm:   return QStringLiteral("Wide FM");
    case DemodMode::Nfm:  return QStringLiteral("NFM");
    case DemodMode::DigU: return QStringLiteral("DIGU");
    case DemodMode::DigL: return QStringLiteral("DIGL");
    case DemodMode::Iq:   return QStringLiteral("IQ");
    case DemodMode::Dsb:  return QStringLiteral("DSB");
    }
    return QStringLiteral("?");
}

} // namespace dsdr
