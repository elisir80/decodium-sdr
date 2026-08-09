// SPDX-License-Identifier: GPL-3.0-or-later
#include "hal/IRadioBackend.h"
#include "hal/HalLog.h"

Q_LOGGING_CATEGORY(dsdrHal, "dsdr.hal")

namespace dsdr::hal {

QVariant IRadioBackend::nativeCommand(const QString &command, const QVariantMap &args)
{
    Q_UNUSED(args)
    qCWarning(dsdrHal) << backendId() << "non implementa il comando nativo" << command;
    return QVariant();
}

double IRadioBackend::setGainReduction(double db)
{
    // Silenzio, e non un avvertimento: chi non sa togliere guadagno lo dichiara
    // già con `maxGainReductionDb` a zero, e il core non glielo chiede. Se
    // arriva lo stesso, restituire zero è la risposta onesta — nulla applicato.
    Q_UNUSED(db)
    return 0.0;
}

double IRadioBackend::gainReduction() const
{
    return 0.0;
}

} // namespace dsdr::hal
