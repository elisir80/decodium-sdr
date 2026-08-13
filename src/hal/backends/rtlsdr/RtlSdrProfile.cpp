// SPDX-License-Identifier: GPL-3.0-or-later
#include "hal/backends/rtlsdr/RtlSdrProfile.h"

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

    caps.minFrequencyHz = profile.directSampling ? 0 : profile.minFrequencyHz;
    caps.maxFrequencyHz = profile.maxFrequencyHz;
    caps.hasHardwareFilters = false;
    caps.hasPreamp = !profile.gainTenthsDb.isEmpty();
    caps.hasAttenuator = false;
    caps.adcBits = 8;
    if (profile.gainTenthsDb.size() >= 2) {
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
    constexpr int kPreferredTenthsDb = 198; // 19.8 dB: moderate VHF starting point
    if (gainSteps.isEmpty())
        return kPreferredTenthsDb;

    QList<int> sorted = gainSteps;
    std::sort(sorted.begin(), sorted.end());
    int selected = sorted.first();
    for (const int step : sorted) {
        if (step > kPreferredTenthsDb)
            break;
        selected = step;
    }
    return selected;
}

} // namespace dsdr::hal::rtlsdr
