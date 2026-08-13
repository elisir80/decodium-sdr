// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — esposizione di Greyline a QML.
//
// Stesso motivo di `SessionSingleton`: il motore vive in dsdr_core, che non
// dipende da QtQml, e la registrazione sta qui, nel livello applicativo.
//
// Non è un singleton, e non per distrazione: due pannelli possono voler
// guardare due istanti diversi — «adesso» in uno e «fra tre ore» nell'altro —
// e un'istanza sola li costringerebbe a contendersi lo stesso orologio.
#pragma once

#include "core/Greyline.h"

#include <QQmlEngine>

namespace dsdr::app {

struct GreylineForeign
{
    Q_GADGET
    QML_FOREIGN(dsdr::core::Greyline)
    QML_NAMED_ELEMENT(Greyline)
};

} // namespace dsdr::app
