// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — esposizione di BandConditions a QML.
//
// Stesso motivo degli altri: il registro vive in dsdr_core, che non dipende da
// QtQml. Qui non c'è un `QML_ELEMENT` che lo renda istanziabile, e non è una
// dimenticanza — il registro è uno solo e lo possiede la sessione, che lo
// espone come `Session.conditions`. Un secondo registro scriverebbe sullo
// stesso file di quello vero.
#pragma once

#include "core/BandConditions.h"

#include <QQmlEngine>

namespace dsdr::app {

struct ConditionsForeign
{
    Q_GADGET
    QML_FOREIGN(dsdr::core::BandConditions)
    QML_NAMED_ELEMENT(BandConditions)
    QML_UNCREATABLE("Il registro è quello della sessione: Session.conditions.")
};

} // namespace dsdr::app
