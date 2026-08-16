// SPDX-License-Identifier: GPL-3.0-or-later
#include "hal/backends/rtlsdr/RtlSdrProfile.h"
#include "hal/backends/rtlsdr/RtlSdrCapabilities.h"

#include <algorithm>

namespace dsdr::hal::rtlsdr {

BackendCapabilities capabilitiesFrom(const RtlSdrDeviceProfile &profile)
{
    BackendCapabilities caps;
    caps.maxRxChannels = kMaxLogicalRxChannels;
    caps.coherentRx = true; // i canali logici condividono lo stesso flusso IQ
    caps.maxPanadapters = 4;
    caps.tx = TxSupport::None;
    caps.demod = DspLocation::Client;
    caps.spectrum = DspLocation::Client;
    caps.agc = DspLocation::Client;

    caps.sampleRates = profile.sampleRates;
    std::sort(caps.sampleRates.begin(), caps.sampleRates.end());
    caps.sampleRates.erase(std::unique(caps.sampleRates.begin(), caps.sampleRates.end()),
                           caps.sampleRates.end());

    caps.defaultSampleRate = profile.preferredSampleRate;
    if (caps.defaultSampleRate <= 0.0 || !caps.sampleRates.contains(caps.defaultSampleRate)) {
        caps.defaultSampleRate = caps.sampleRates.isEmpty() ? 2'400'000.0
                                                              : caps.sampleRates.first();
        for (double rate : std::as_const(caps.sampleRates)) {
            if (rate <= 2'400'000.0)
                caps.defaultSampleRate = rate;
        }
    }

    // Il Blog V4 riceve in HF tramite il proprio upconverter, pur restando
    // in modalità tuner; una chiavetta convenzionale raggiunge invece l'HF
    // solo con il Q ADC. Non pubblicare questi limiti corretti significherebbe
    // accettare una sintonia che il backend dovrà poi rifiutare.
    if (isRtlSdrBlogV4Identity(profile.product)) {
        caps.minFrequencyHz = kDirectSamplingMinimumFrequencyHz;
        caps.maxFrequencyHz = profile.maxFrequencyHz;
    } else if (profile.directSampling) {
        caps.minFrequencyHz = kDirectSamplingMinimumFrequencyHz;
        caps.maxFrequencyHz = kDirectSamplingMaximumFrequencyHz;
    } else {
        caps.minFrequencyHz = profile.minFrequencyHz;
        caps.maxFrequencyHz = profile.maxFrequencyHz;
    }
    caps.hasHardwareFilters = false;
    caps.hasPreamp = !profile.directSampling && !profile.gainTenthsDb.isEmpty();
    caps.hasAttenuator = false;
    caps.adcBits = 8;
    if (!profile.directSampling && profile.gainTenthsDb.size() >= 2) {
        QList<int> gains = profile.gainTenthsDb;
        std::sort(gains.begin(), gains.end());
        caps.maxGainReductionDb =
            static_cast<double>(gains.last() - gains.first()) / 10.0;
    }
    caps.remoteCapable = false;
    caps.multiClient = false;
    caps.supportsRecording = true;
    caps.nativePanels.append(QStringLiteral("RtlSdrDevicePanel"));
    return caps;
}

int safeAutoGainTenthsDb(const QList<int> &gainSteps)
{
    // SDR++ stores 22 dB for this RTL-SDR.  The exact value depends on the
    // tuner gain table, therefore choose the nearest real hardware step.
    constexpr int kPreferredTenthsDb = 220;
    if (gainSteps.isEmpty())
        return kPreferredTenthsDb;

    return *std::min_element(gainSteps.cbegin(), gainSteps.cend(),
                             [](int left, int right) {
                                 return std::abs(left - kPreferredTenthsDb)
                                     < std::abs(right - kPreferredTenthsDb);
                             });
}

} // namespace dsdr::hal::rtlsdr
