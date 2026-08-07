// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — profilo di un device SoapySDR.
//
// SoapySDR è il moltiplicatore di universalità: un solo backend copre RTL-SDR,
// Airspy, HackRF, LimeSDR, PlutoSDR, USRP e chiunque altro pubblichi un driver.
// Il prezzo è che le capacità non si conoscono a priori — vanno chieste al
// driver device per device.
//
// Il profilo è ciò che si legge dal device; `capabilitiesFrom()` lo traduce nel
// linguaggio della HAL. Tenere separate le due cose permette di verificare la
// traduzione senza avere l'hardware attaccato, che è l'unico modo di provarla
// in CI.
#pragma once

#include "hal/BackendCapabilities.h"

#include <QList>
#include <QMetaType>
#include <QString>
#include <QStringList>

namespace dsdr::hal::soapy {

struct SoapyDeviceProfile
{
    QString driver;        ///< "rtlsdr", "airspy", "hackrf", …
    QString label;         ///< etichetta leggibile fornita dal driver
    QString serial;
    QString hardware;      ///< modello dichiarato dal device
    QString deviceArgs;    ///< kwargs con cui riaprire esattamente questo device

    int rxChannels = 0;
    int txChannels = 0;
    bool fullDuplex = false;

    QList<double> sampleRates;
    double preferredSampleRate = 0.0;

    qint64 minFrequencyHz = 0;
    qint64 maxFrequencyHz = 0;

    bool hasAgc = false;
    double minGainDb = 0.0;
    double maxGainDb = 0.0;

    QStringList antennas;
    QString currentAntenna;

    bool isValid() const { return rxChannels > 0; }
};

/// Traduce il profilo nelle capability della HAL.
///
/// Le regole sono poche ma importanti: si dichiara TX solo se il device ha
/// davvero canali in trasmissione (una chiavetta RTL non trasmette, un HackRF
/// sì), e si dichiara `coherentRx` solo con più canali — che per SoapySDR
/// significa più canali dello stesso device, quindi campionati insieme.
BackendCapabilities capabilitiesFrom(const SoapyDeviceProfile &profile);

/// Numero massimo di canali RX logici offerti al DSP client.
/// Non è il numero di canali hardware: è quanti canali demodulati l'utente può
/// aprire dentro la banda campionata.
inline constexpr int kMaxLogicalRxChannels = 4;

} // namespace dsdr::hal::soapy

Q_DECLARE_METATYPE(dsdr::hal::soapy::SoapyDeviceProfile)
