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

} // namespace dsdr::hal
