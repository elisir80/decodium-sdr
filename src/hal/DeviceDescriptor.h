// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — identità di un device scoperto.
#pragma once

#include <QMetaType>
#include <QString>
#include <QVariantMap>

namespace dsdr::hal {

/// Ciò che la discovery sa di un device prima ancora di aprirlo.
///
/// `deviceId` deve essere stabile fra riavvii (seriale, MAC, path USB): è la
/// chiave con cui SettingsStore ritrova le impostazioni per-radio.
struct DeviceDescriptor
{
    QString backendId;    ///< "demo", "soapy", "hpsdr", ...
    QString deviceId;     ///< chiave stabile, unica dentro il backend
    QString displayName;  ///< nome mostrato in lista
    QString model;
    QString serial;
    QString transport;    ///< "usb", "udp", "tcp", "websocket", "synthetic"
    QString address;      ///< IP:porta o path, se pertinente

    /// Parametri opachi che il backend si passa fra discovery e open.
    /// Il core li tratta come dati, mai come comportamento.
    QVariantMap extra;

    bool isValid() const noexcept { return !backendId.isEmpty() && !deviceId.isEmpty(); }

    /// Chiave globale, usata come identificatore nelle impostazioni e in UI.
    QString key() const { return backendId + QLatin1Char(':') + deviceId; }

    bool operator==(const DeviceDescriptor &o) const noexcept
    {
        return backendId == o.backendId && deviceId == o.deviceId;
    }
};

} // namespace dsdr::hal

Q_DECLARE_METATYPE(dsdr::hal::DeviceDescriptor)
