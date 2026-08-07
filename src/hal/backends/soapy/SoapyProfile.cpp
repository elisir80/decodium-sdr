// SPDX-License-Identifier: GPL-3.0-or-later
#include "hal/backends/soapy/SoapyProfile.h"

#include <algorithm>

namespace dsdr::hal::soapy {

BackendCapabilities capabilitiesFrom(const SoapyDeviceProfile &profile)
{
    BackendCapabilities caps;

    // I canali che l'utente apre sono logici: vivono dentro la banda
    // campionata e li demodula il DSP client. Un device con più canali
    // hardware non li moltiplica — li rende coerenti.
    caps.maxRxChannels = kMaxLogicalRxChannels;
    caps.coherentRx = profile.rxChannels > 1;
    caps.maxPanadapters = 4;

    // TX solo se il device lo ha davvero: dichiararlo per un RTL-SDR farebbe
    // comparire un PTT che non può funzionare.
    if (profile.txChannels > 0)
        caps.tx = profile.fullDuplex ? TxSupport::FullDuplex : TxSupport::Ptt;
    else
        caps.tx = TxSupport::None;

    caps.demod = DspLocation::Client;
    caps.spectrum = DspLocation::Client;
    caps.agc = DspLocation::Client;

    caps.sampleRates = profile.sampleRates;
    std::sort(caps.sampleRates.begin(), caps.sampleRates.end());

    caps.defaultSampleRate = profile.preferredSampleRate;
    if (caps.defaultSampleRate <= 0.0 && !caps.sampleRates.isEmpty()) {
        // In mancanza di indicazioni si sceglie il rate più alto che non
        // superi 2,4 MS/s: oltre, molti device perdono campioni su USB e
        // l'utente lo interpreta come un difetto del programma.
        caps.defaultSampleRate = caps.sampleRates.first();
        for (double rate : std::as_const(caps.sampleRates)) {
            if (rate <= 2'400'000.0)
                caps.defaultSampleRate = rate;
        }
    }

    caps.minFrequencyHz = profile.minFrequencyHz;
    caps.maxFrequencyHz = profile.maxFrequencyHz;

    caps.hasHardwareFilters = false;
    caps.hasPreamp = profile.maxGainDb > profile.minGainDb;
    caps.hasAttenuator = profile.minGainDb < 0.0;
    caps.adcBits = 0;    // SoapySDR non lo espone in modo uniforme

    // Alcuni driver (soapyremote, rtl_tcp via soapy) parlano con hardware
    // che sta altrove, ma dal punto di vista dell'API restano device locali.
    caps.remoteCapable = profile.driver.contains(QLatin1String("remote"));
    caps.multiClient = false;
    caps.supportsRecording = true;

    if (!profile.antennas.isEmpty())
        caps.nativePanels.append(QStringLiteral("SoapyDevicePanel"));

    return caps;
}

} // namespace dsdr::hal::soapy
