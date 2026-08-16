// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — profilo del backend RTL-SDR nativo.
#pragma once

#include "hal/BackendCapabilities.h"

#include <QList>
#include <QMetaType>
#include <QString>

namespace dsdr::hal::rtlsdr {

struct RtlSdrDeviceProfile
{
    QString product;
    QString manufacturer;
    QString serial;
    QString tuner;
    int index = -1;

    QList<double> sampleRates;
    double preferredSampleRate = 2'400'000.0;
    QList<int> gainTenthsDb;

    qint64 minFrequencyHz = 24'000'000;
    qint64 maxFrequencyHz = 1'766'000'000;
    bool directSampling = false;

    bool isValid() const { return index >= 0 && !product.isEmpty(); }
};

/// Traduce il profilo hardware nelle capability comuni della HAL.
BackendCapabilities capabilitiesFrom(const RtlSdrDeviceProfile &profile);

/// Guadagno iniziale per l'AUTO RTL-SDR, scelto sul passo hardware più vicino
/// al profilo 22 dB usato da SDR++. L'AGC digitale RTL2832 resta disponibile
/// in AUTO; la guardia anti-saturazione può comunque imporre un passo manuale
/// più basso quando rileva clipping.
int safeAutoGainTenthsDb(const QList<int> &gainSteps);

inline constexpr int kMaxLogicalRxChannels = 4;

} // namespace dsdr::hal::rtlsdr

Q_DECLARE_METATYPE(dsdr::hal::rtlsdr::RtlSdrDeviceProfile)
