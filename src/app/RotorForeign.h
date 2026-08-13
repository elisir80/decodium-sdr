// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — esposizione di RotorController a QML.
//
// Stesso motivo di `SessionSingleton` e `GreylineForeign`: il controller vive
// in dsdr_core, che non dipende da QtQml.
//
// Non è un singleton, e qui la ragione è più forte che altrove: una stazione
// con due antenne ha due rotori, e ognuno ha il suo `rotctld` su una porta
// diversa. Un'istanza sola li costringerebbe a contendersi lo stesso palo.
#pragma once

#include "core/RotorController.h"

#include <QQmlEngine>

namespace dsdr::app {

struct RotorForeign
{
    Q_GADGET
    QML_FOREIGN(dsdr::core::RotorController)
    QML_NAMED_ELEMENT(RotorController)
};

} // namespace dsdr::app
