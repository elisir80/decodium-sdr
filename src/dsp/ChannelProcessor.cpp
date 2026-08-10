// SPDX-License-Identifier: GPL-3.0-or-later
#include "dsp/ChannelProcessor.h"
#include "dsp/FirDesign.h"

#include <QLoggingCategory>

#include <algorithm>
#include <cmath>

Q_LOGGING_CATEGORY(dsdrChannelDsp, "dsdr.dsp.channel")

namespace dsdr::dsp {

namespace {

/// Quanto deve risalire il segnale sopra la soglia perché lo squelch riapra.
/// Senza, un segnale che respira attorno alla soglia — il caso normale in HF —
/// farebbe sbattere l'audio più volte al secondo.
constexpr float kSquelchHysteresisDb = 3.0f;

/// Velocità di apertura e chiusura, per campione a 48 kHz. L'apertura è
/// rapida — non si vuole perdere la prima sillaba — e la chiusura più lenta,
/// così una pausa nel parlato non taglia la frase in due.
constexpr float kSquelchAttack = 0.02f;
constexpr float kSquelchRelease = 0.002f;
bool squelchAllowed(DemodMode mode) noexcept
{
    return mode != DemodMode::Cw && mode != DemodMode::Cwr
        && mode != DemodMode::Iq;
}

} // namespace

bool ChannelSettings::operator==(const ChannelSettings &o) const noexcept
{
    return offsetHz == o.offsetHz && mode == o.mode && filterLowHz == o.filterLowHz
        && filterHighHz == o.filterHighHz && agcMode == o.agcMode
        && agcThresholdDb == o.agcThresholdDb && agcMaxGainDb == o.agcMaxGainDb
        && cwPitchHz == o.cwPitchHz && passbandShiftHz == o.passbandShiftHz
        && nrEnabled == o.nrEnabled && nrStrength == o.nrStrength
        && anfEnabled == o.anfEnabled
        && notches == o.notches
        && agcAttackMs == o.agcAttackMs && agcDecayMs == o.agcDecayMs
        && amCarrierAgc == o.amCarrierAgc
        && cwPitchHz == o.cwPitchHz && volume == o.volume && muted == o.muted
        && audioHighPassEnabled == o.audioHighPassEnabled
        && audioHighPassHz == o.audioHighPassHz
        && fmStereo == o.fmStereo && fmAudioLowPass == o.fmAudioLowPass
        && fmDeemphasisUs == o.fmDeemphasisUs
        && fmRds == o.fmRds && rdsAutomaticAf == o.rdsAutomaticAf
        && rdsRegion == o.rdsRegion
        && squelchEnabled == o.squelchEnabled
        && squelchThresholdDb == o.squelchThresholdDb
        && ctcssEnabled == o.ctcssEnabled
        && ctcssDecodeOnly == o.ctcssDecodeOnly
        && ctcssToneHz == o.ctcssToneHz
        && fmIfNoiseReductionEnabled == o.fmIfNoiseReductionEnabled
        && fmIfNoiseReductionPreset == o.fmIfNoiseReductionPreset;

}

ChannelProcessor::ChannelProcessor()
{
    m_filterTaps.reserve(kMaxFirTaps);
}

bool ChannelProcessor::configure(double deviceSampleRate, double audioSampleRate)
{
    if (deviceSampleRate <= 0.0 || audioSampleRate <= 0.0)
        return false;

    m_deviceRate = deviceSampleRate;
    m_audioRate = audioSampleRate;

    m_configured = false;
    return configureForMode();
}

bool ChannelProcessor::configureForMode()
{
    if (m_deviceRate <= 0.0 || m_audioRate <= 0.0)
        return false;

    m_wideFm = m_settings.mode == DemodMode::Fm;

    // Broadcast FM needs the complete ~200 kHz RF channel before the
    // discriminator. Keep at least 240 kHz of complex baseband so the
    // 90 kHz filter edges leave room for the 75 kHz deviation and the 15 kHz
    // audio band. The following resampler returns the discriminator output
    // to the fixed 48 kHz AudioRouter rate.
    const int decimation = m_wideFm
        ? std::max(1, static_cast<int>(std::floor(m_deviceRate / 240000.0)))
        : std::max(1, static_cast<int>(std::lround(m_deviceRate / m_audioRate)));
    const double passband = m_wideFm ? 90000.0 : 8000.0;

    // Banda utile conservata dalla decimazione: copre la FM larga, che è il
    // canale più largo che il demodulatore possa chiedere.
    if (!m_chain.configure(m_deviceRate, decimation, passband))
        return false;

    m_channelRate = m_chain.outputRate();

    // La riconfigurazione avviene anche quando l'operatore passa da NFM a
    // Wide-FM. In quel caso l'offset del canale deve restare agganciato:
    // azzerarlo porterebbe una stazione a +200 kHz fuori dal filtro e
    // lascerebbe passare soltanto il rumore.
    m_nco.configure(m_deviceRate, tuningOffsetHz());
    m_demod.configure(m_channelRate);
    m_demod.setMode(m_settings.mode);
    const double fmDeviation = m_wideFm
        ? 75000.0
        : (m_settings.mode == DemodMode::Nfm ? 2500.0 : 5000.0);
    m_demod.setFmDeviation(fmDeviation);
    m_demod.setAmCarrierAgc(m_settings.amCarrierAgc);
    if (m_wideFm && !m_broadcastFmStereo.configure(m_channelRate))
        return false;
    m_broadcastFmStereo.setLowPass(m_settings.fmAudioLowPass);
    if (m_wideFm && !m_rds.configure(m_channelRate))
        return false;
    if (!m_ctcss.configure(m_channelRate, m_settings.ctcssToneHz))
        return false;
    if (!m_fmIfNoiseReducer.configure(m_channelRate))
        return false;
    m_fmIfNoiseReducer.setPreset(m_settings.fmIfNoiseReductionPreset);
    m_agc.configure(m_channelRate);
    m_agc.setMode(m_settings.agcMode);
    m_agc.setThresholdDb(m_settings.agcThresholdDb);
    m_agc.setMaxGainDb(m_settings.agcMaxGainDb);
    m_agc.setAttackMs(m_settings.agcAttackMs);
    m_agc.setDecayMs(m_settings.agcDecayMs);

    // Filtri di disturbo sull'audio. I due predittori adattivi hanno memorie
    // diverse di proposito — il notch automatico deve agganciare una riga in
    // fretta, la riduzione di rumore deve restare stabile sulla voce, e con lo
    // stesso ritardo di decorrelazione farebbero lo stesso mestiere due volte.
    for (auto &notch : m_notches)
        notch.configure(m_channelRate);
    m_anf.configure(64, 8);

    // Il NR spettrale lavora sull'audio demodulato: finestra da 512 per la
    // voce. Costa una FFT ogni mezza finestra — meno dell'uno per cento di un
    // core — e aggiunge una finestra di ritardo, dentro il budget di RNF-03.
    m_nr.configure(m_channelRate, 512);


    const std::size_t block = kMaxBlockFrames;
    m_mixed.assign(block, Complex(0.0f, 0.0f));
    m_decimated.assign(m_chain.maxOutput(block) + 8, Complex(0.0f, 0.0f));
    m_filtered.assign(m_chain.maxOutput(block) + 8, Complex(0.0f, 0.0f));
    m_demodulated.assign(m_chain.maxOutput(block) + 8, 0.0f);

    m_resampleAudio = std::abs(m_channelRate - m_audioRate) > 1e-6;
    const bool nfm = m_settings.mode == DemodMode::Nfm;
    if (m_resampleAudio || nfm) {
        const double audioCutoff = m_wideFm
            ? 15000.0
            : (nfm ? 3000.0
                   : std::min({18000.0, m_channelRate * 0.45, m_audioRate * 0.45}));
        const double transition = nfm ? 1000.0 : 3000.0;
        int taps = estimateTaps(transition, m_channelRate, 80.0);
        taps = std::min<int>(taps, 511);
        m_audioFilterTaps = designLowpass(audioCutoff, m_channelRate, taps,
                                          kaiserBeta(80.0));
        m_audioFilterDelay.assign(m_audioFilterTaps.size() * 2, 0.0f);
        m_resampleBuffer.clear();
        m_resampleBuffer.reserve(m_demodulated.size() + 16);
        m_stereoAudioFilterDelay.assign(m_audioFilterTaps.size() * 4, 0.0f);
        m_stereoResampleBuffer.clear();
        m_stereoResampleBuffer.reserve((m_demodulated.size() + 16) * 2);
        m_resampleStep = m_channelRate / m_audioRate;
    } else {
        m_audioFilterTaps.clear();
        m_audioFilterDelay.clear();
        m_resampleBuffer.clear();
        m_stereoAudioFilterDelay.clear();
        m_stereoResampleBuffer.clear();
        m_resampleStep = 1.0;
    }

    const double outputRate = m_resampleAudio ? m_audioRate : m_channelRate;
    if (!m_audioHighPassLeft.configure(outputRate, m_settings.audioHighPassHz)
        || !m_audioHighPassRight.configure(outputRate, m_settings.audioHighPassHz))
        return false;

    m_stereoAudio.assign(m_demodulated.size() * 2, 0.0f);

    m_configured = true;
    redesignFilter();
    qCInfo(dsdrChannelDsp) << "catena canale:" << demodModeName(m_settings.mode)
                           << "device rate" << m_deviceRate
                           << "channel rate" << m_channelRate
                           << "decimazione" << m_chain.totalDecimation()
                           << "offset" << m_settings.offsetHz
                           << "NCO" << m_nco.frequency()
                           << "filtro" << m_settings.filterLowHz << m_settings.filterHighHz
                           << "wide FM" << m_wideFm
                           << "resample audio" << m_resampleAudio
                           << "audio rate" << m_audioRate;
    reset();
    return true;
}

double ChannelProcessor::tuningOffsetHz() const
{
    // Per la CW la portante non va portata a DC ma al tono di battimento
    // scelto dall'operatore: è il BFO, realizzato spostando il DDC.
    switch (m_settings.mode) {
    case DemodMode::Cw:
        return m_settings.offsetHz - m_settings.cwPitchHz;
    case DemodMode::Cwr:
        return m_settings.offsetHz + m_settings.cwPitchHz;
    default:
        return m_settings.offsetHz;
    }
}

void ChannelProcessor::computeFilterEdges(double &loHz, double &hiHz) const
{
    const double nyquist = m_channelRate * 0.5;
    const double lo = static_cast<double>(m_settings.filterLowHz);
    const double hi = static_cast<double>(m_settings.filterHighHz);

    switch (m_settings.mode) {
    case DemodMode::Usb:
    case DemodMode::DigU:
        loHz = lo;
        hiHz = hi;
        break;
    case DemodMode::Lsb:
    case DemodMode::DigL:
        loHz = -hi;
        hiHz = -lo;
        break;
    case DemodMode::Cw: {
        const double half = std::max(50.0, (hi - lo) * 0.5);
        loHz = m_settings.cwPitchHz - half;
        hiHz = m_settings.cwPitchHz + half;
        break;
    }
    case DemodMode::Cwr: {
        const double half = std::max(50.0, (hi - lo) * 0.5);
        loHz = -m_settings.cwPitchHz - half;
        hiHz = -m_settings.cwPitchHz + half;
        break;
    }
    case DemodMode::Am:
    case DemodMode::Sam:
    case DemodMode::Dsb:
    case DemodMode::Fm:
    case DemodMode::Nfm:
        // Modalità simmetriche: la portante sta a DC. Entrambi i cursori
        // rappresentano un bordo; usiamo il più stretto per evitare che
        // modificare un solo lato lasci inavvertitamente passare una banda
        // più larga di quella richiesta.
        {
            const double halfWidth = std::min(std::abs(lo), std::abs(hi));
            loHz = -halfWidth;
            hiHz = halfWidth;
        }
        break;
    case DemodMode::Iq:
        loHz = -nyquist * 0.9;
        hiHz = nyquist * 0.9;
        break;
    }

    // Lo spostamento si applica dopo il calcolo dei bordi e prima del taglio
    // a Nyquist: sposta la finestra, non la allarga, ed è la stessa cosa per
    // ogni modo — anche in CW, dove il pitch ha già spostato il DDC.
    loHz += m_settings.passbandShiftHz;
    hiHz += m_settings.passbandShiftHz;

    loHz = std::clamp(loHz, -nyquist * 0.95, nyquist * 0.95);
    hiHz = std::clamp(hiHz, -nyquist * 0.95, nyquist * 0.95);
    if (hiHz - loHz < 50.0)
        hiHz = loHz + 50.0;
}

double ChannelProcessor::notchAudioHz(double offsetHz) const
{
    switch (m_settings.mode) {
    case DemodMode::Lsb:
    case DemodMode::DigL:
        // La banda laterale inferiore ribalta lo spettro: ciò che in RF sta
        // sopra la portante, in audio scende.
        return std::abs(-offsetHz);
    case DemodMode::Cw:
        return std::abs(offsetHz + m_settings.cwPitchHz);
    case DemodMode::Cwr:
        return std::abs(-offsetHz + m_settings.cwPitchHz);
    default:
        return std::abs(offsetHz);
    }
}

void ChannelProcessor::redesignFilter()
{
    if (!m_configured)
        return;

    double lo = 0.0;
    double hi = 0.0;
    computeFilterEdges(lo, hi);

    const double width = hi - lo;
    const double transition = std::clamp(width * 0.15, 60.0, 800.0);
    int taps = estimateTaps(transition, m_channelRate, 70.0);
    taps = std::min<int>(taps, 511);

    designBandpassInto(m_filterTaps, lo, hi, m_channelRate, taps, kaiserBeta(70.0));
    m_filter.setTaps(m_filterTaps);
}

void ChannelProcessor::applySettings(const ChannelSettings &settings)
{
    const bool filterChanged = settings.mode != m_settings.mode
        || settings.filterLowHz != m_settings.filterLowHz
        || settings.filterHighHz != m_settings.filterHighHz
        || settings.passbandShiftHz != m_settings.passbandShiftHz
        || settings.cwPitchHz != m_settings.cwPitchHz;
    const bool amCarrierAgcChanged = settings.amCarrierAgc != m_settings.amCarrierAgc;
    const bool tuningChanged = settings.offsetHz != m_settings.offsetHz
        || settings.cwPitchHz != m_settings.cwPitchHz || settings.mode != m_settings.mode;
    const bool wideFmChanged = (settings.mode == DemodMode::Fm) != m_wideFm;
    const bool nfmAudioPathChanged = (settings.mode == DemodMode::Nfm)
        != (m_settings.mode == DemodMode::Nfm);
    const bool audioProfileChanged = settings.fmStereo != m_settings.fmStereo
        || settings.fmAudioLowPass != m_settings.fmAudioLowPass
        || settings.fmDeemphasisUs != m_settings.fmDeemphasisUs
        || settings.fmRds != m_settings.fmRds;
    const bool audioHighPassChanged = settings.audioHighPassEnabled
        != m_settings.audioHighPassEnabled
        || settings.audioHighPassHz != m_settings.audioHighPassHz;
    const bool rdsRegionChanged = settings.rdsRegion != m_settings.rdsRegion;
    const bool squelchChanged = settings.squelchEnabled != m_settings.squelchEnabled
        || settings.squelchThresholdDb != m_settings.squelchThresholdDb;
    const bool ctcssChanged = settings.ctcssEnabled != m_settings.ctcssEnabled
        || settings.ctcssDecodeOnly != m_settings.ctcssDecodeOnly
        || settings.ctcssToneHz != m_settings.ctcssToneHz;
    const bool fmIfNoiseReductionChanged = settings.fmIfNoiseReductionEnabled
        != m_settings.fmIfNoiseReductionEnabled
        || settings.fmIfNoiseReductionPreset != m_settings.fmIfNoiseReductionPreset;

    // Chi era acceso prima, per accorgersi delle accensioni.
    const struct {
        bool nr, anf;
    } wasEnabled{m_settings.nrEnabled, m_settings.anfEnabled};

    m_settings = settings;

    if (m_configured && (wideFmChanged || nfmAudioPathChanged)) {
        // Broadcast FM changes the required channel rate; NFM changes the
        // audio low-pass path even when its decimation factor is unchanged.
        configureForMode();
        return;
    }

    if (tuningChanged)
        m_nco.setFrequency(tuningOffsetHz());
    if (tuningChanged && m_wideFm) {
        // Un cambio di frequenza non può riutilizzare il PI/PS della stazione
        // precedente: l'AF automatico deve validare dati RDS nuovi.
        m_rds.reset();
        m_broadcastFmStereo.reset();
    }
    if (filterChanged) {
        m_demod.setMode(m_settings.mode);
        m_demod.setFmDeviation(m_settings.mode == DemodMode::Nfm ? 2500.0
                                                                  : (m_wideFm ? 75000.0 : 5000.0));
        redesignFilter();
    }
    if (amCarrierAgcChanged)
        m_demod.setAmCarrierAgc(m_settings.amCarrierAgc);

    if (audioProfileChanged) {
        m_broadcastFmStereo.setLowPass(m_settings.fmAudioLowPass);
        m_broadcastFmStereo.reset();
        std::fill(m_audioFilterDelay.begin(), m_audioFilterDelay.end(), 0.0f);
        m_audioFilterPosition = m_audioFilterTaps.size();
        m_rds.reset();
        m_deemphasisLeft = 0.0f;
        m_deemphasisRight = 0.0f;
    }

    if (rdsRegionChanged) {
        m_rds.setRegion(m_settings.rdsRegion);
        m_rds.reset();
    }

    if (squelchChanged) {
        m_squelchOpen = !squelchAllowed(m_settings.mode)
            || (!m_settings.squelchEnabled
                && !(m_settings.ctcssEnabled && !m_settings.ctcssDecodeOnly));
    }

    if (ctcssChanged) {
        m_ctcss.setTone(m_settings.ctcssToneHz);
        m_squelchOpen = !m_settings.ctcssEnabled || m_settings.ctcssDecodeOnly;
    }

    if (fmIfNoiseReductionChanged) {
        m_fmIfNoiseReducer.setPreset(m_settings.fmIfNoiseReductionPreset);
        m_fmIfNoiseReducer.reset();
    }

    if (audioHighPassChanged && m_configured) {
        const double outputRate = m_resampleAudio ? m_audioRate : m_channelRate;
        m_audioHighPassLeft.configure(outputRate, m_settings.audioHighPassHz);
        m_audioHighPassRight.configure(outputRate, m_settings.audioHighPassHz);
    }

    m_agc.setMode(m_settings.agcMode);
    m_agc.setThresholdDb(m_settings.agcThresholdDb);
    m_agc.setMaxGainDb(m_settings.agcMaxGainDb);
    m_agc.setAttackMs(m_settings.agcAttackMs);
    m_agc.setDecayMs(m_settings.agcDecayMs);

    // Ogni notch riceve la sua frequenza audio, che dipende dal modo: lo
    // scostamento dalla portante diventa un tono diverso in USB, in LSB e in
    // CW, dove il BFO ha già spostato tutto del pitch.
    for (int i = 0; i < ChannelSettings::kMaxNotches; ++i) {
        const auto &spec = m_settings.notches[static_cast<std::size_t>(i)];
        if (!spec.enabled)
            continue;
        m_notches[static_cast<std::size_t>(i)].setNotch(notchAudioHz(spec.offsetHz),
                                                        spec.widthHz);
    }
    m_nr.setStrength(m_settings.nrStrength);

    // Un filtro adattivo che riparte da spento porta con sé i coefficienti di
    // prima: erano la risposta a un segnale che non c'è più, e per qualche
    // decimo di secondo colorano l'audio. Si riazzera all'accensione.
    if (!wasEnabled.nr && m_settings.nrEnabled)
        m_nr.reset();
    if (!wasEnabled.anf && m_settings.anfEnabled)
        m_anf.reset();

}

void ChannelProcessor::reset() noexcept
{
    m_nco.reset();
    m_chain.reset();
    m_filter.reset();
    m_demod.reset();
    m_agc.reset();
    for (auto &notch : m_notches)
        notch.reset();
    m_anf.reset();
    m_nr.reset();
    m_audioHighPassLeft.reset();
    m_audioHighPassRight.reset();
    std::fill(m_audioFilterDelay.begin(), m_audioFilterDelay.end(), 0.0f);
    m_audioFilterPosition = m_audioFilterTaps.size();
    std::fill(m_stereoAudioFilterDelay.begin(), m_stereoAudioFilterDelay.end(), 0.0f);
    m_stereoAudioFilterPosition = m_audioFilterTaps.size();
    m_resampleBuffer.clear();
    m_stereoResampleBuffer.clear();
    m_resamplePosition = 0.0;
    m_stereoResamplePosition = 0.0;
    m_broadcastFmStereo.reset();
    m_ctcss.reset();
    m_fmIfNoiseReducer.reset();
    m_deemphasisLeft = 0.0f;
    m_deemphasisRight = 0.0f;
    m_squelchOpen = !m_settings.squelchEnabled
        && !(m_settings.ctcssEnabled && !m_settings.ctcssDecodeOnly);
    m_signalLevelDb = -160.0f;
    m_noiseFloorDb = -160.0f;
    m_snrDb = 0.0f;
    m_audioPower = 0.0f;
    m_audioLevelDb = -160.0f;
    m_noiseFloorInitialized = false;
    m_lastBasebandFrames = 0;
}

void ChannelProcessor::updateAudioMeter(float sample) noexcept
{
    const float power = sample * sample;
    // Il meter audio è volutamente più lento del percorso audio: serve a
    // mostrare il livello RMS ascoltato, non a inseguire ogni picco.
    m_audioPower += (power - m_audioPower) * 0.01f;
    m_audioLevelDb = powerToDb(m_audioPower);
}

float ChannelProcessor::processAudioLowpass(float sample) noexcept
{
    if ((m_wideFm || m_settings.mode == DemodMode::Nfm)
        && !m_settings.fmAudioLowPass)
        return sample;
    const std::size_t taps = m_audioFilterTaps.size();
    if (taps == 0)
        return sample;

    if (m_audioFilterPosition == 0)
        m_audioFilterPosition = taps;
    --m_audioFilterPosition;
    m_audioFilterDelay[m_audioFilterPosition] = sample;
    m_audioFilterDelay[m_audioFilterPosition + taps] = sample;

    float result = 0.0f;
    for (std::size_t i = 0; i < taps; ++i)
        result += m_audioFilterTaps[i] * m_audioFilterDelay[m_audioFilterPosition + i];
    return result;
}

std::size_t ChannelProcessor::resampleAudio(const float *input,
                                            std::size_t count,
                                            float *output) noexcept
{
    for (std::size_t i = 0; i < count; ++i)
        m_resampleBuffer.push_back(processAudioLowpass(input[i]));

    std::size_t produced = 0;
    while (m_resamplePosition + 1.0 < static_cast<double>(m_resampleBuffer.size())) {
        const std::size_t index = static_cast<std::size_t>(m_resamplePosition);
        const float fraction = static_cast<float>(m_resamplePosition - index);
        const float a = m_resampleBuffer[index];
        const float b = m_resampleBuffer[index + 1];
        output[produced++] = a + (b - a) * fraction;
        m_resamplePosition += m_resampleStep;
    }

    // Keep the interpolation endpoint and discard samples that can no longer
    // be referenced by the next block. The vector is pre-reserved in
    // configureForMode(), so this does not allocate on the hot path.
    const std::size_t keepFrom = std::min(
        static_cast<std::size_t>(m_resamplePosition),
        m_resampleBuffer.size() > 1 ? m_resampleBuffer.size() - 1 : 0);
    if (keepFrom > 0) {
        std::move(m_resampleBuffer.begin() + static_cast<std::ptrdiff_t>(keepFrom),
                  m_resampleBuffer.end(), m_resampleBuffer.begin());
        m_resampleBuffer.resize(m_resampleBuffer.size() - keepFrom);
        m_resamplePosition -= static_cast<double>(keepFrom);
    }
    return produced;
}

float ChannelProcessor::processDeemphasis(float sample, float &state) noexcept
{
    if (m_settings.fmDeemphasisUs <= 0.0
        || (m_settings.mode != DemodMode::Fm && m_settings.mode != DemodMode::Nfm))
        return sample;

    const double tauSeconds = m_settings.fmDeemphasisUs * 1e-6;
    const float alpha = static_cast<float>(1.0
        - std::exp(-1.0 / (m_channelRate * tauSeconds)));
    state += alpha * (sample - state);
    return state;
}

std::size_t ChannelProcessor::resampleAudioStereo(const float *inputInterleaved,
                                                  std::size_t count,
                                                  float *outputInterleaved) noexcept
{
    const std::size_t taps = m_audioFilterTaps.size();
    for (std::size_t i = 0; i < count; ++i) {
        if (m_wideFm && !m_settings.fmAudioLowPass) {
            m_stereoResampleBuffer.push_back(inputInterleaved[i * 2]);
            m_stereoResampleBuffer.push_back(inputInterleaved[i * 2 + 1]);
            continue;
        }
        if (taps == 0) {
            m_stereoResampleBuffer.push_back(inputInterleaved[i * 2]);
            m_stereoResampleBuffer.push_back(inputInterleaved[i * 2 + 1]);
            continue;
        }

        if (m_stereoAudioFilterPosition == 0)
            m_stereoAudioFilterPosition = taps;
        --m_stereoAudioFilterPosition;

        const std::size_t rightBase = m_stereoAudioFilterPosition + taps * 2;
        m_stereoAudioFilterDelay[m_stereoAudioFilterPosition]
            = inputInterleaved[i * 2];
        m_stereoAudioFilterDelay[m_stereoAudioFilterPosition + taps]
            = inputInterleaved[i * 2];
        m_stereoAudioFilterDelay[rightBase]
            = inputInterleaved[i * 2 + 1];
        m_stereoAudioFilterDelay[rightBase + taps]
            = inputInterleaved[i * 2 + 1];

        float left = 0.0f;
        float right = 0.0f;
        for (std::size_t k = 0; k < taps; ++k) {
            left += m_audioFilterTaps[k]
                * m_stereoAudioFilterDelay[m_stereoAudioFilterPosition + k];
            right += m_audioFilterTaps[k]
                * m_stereoAudioFilterDelay[rightBase + k];
        }
        m_stereoResampleBuffer.push_back(left);
        m_stereoResampleBuffer.push_back(right);
    }

    std::size_t produced = 0;
    while (m_stereoResamplePosition + 1.0
           < static_cast<double>(m_stereoResampleBuffer.size() / 2)) {
        const std::size_t index = static_cast<std::size_t>(m_stereoResamplePosition);
        const float fraction = static_cast<float>(m_stereoResamplePosition - index);
        const std::size_t base = index * 2;
        outputInterleaved[produced * 2] = m_stereoResampleBuffer[base]
            + (m_stereoResampleBuffer[base + 2]
               - m_stereoResampleBuffer[base]) * fraction;
        outputInterleaved[produced * 2 + 1] = m_stereoResampleBuffer[base + 1]
            + (m_stereoResampleBuffer[base + 3]
               - m_stereoResampleBuffer[base + 1]) * fraction;
        ++produced;
        m_stereoResamplePosition += m_resampleStep;
    }

    const std::size_t availableFrames = m_stereoResampleBuffer.size() / 2;
    const std::size_t keepFrom = std::min(
        static_cast<std::size_t>(m_stereoResamplePosition),
        availableFrames > 1 ? availableFrames - 1 : 0);
    if (keepFrom > 0) {
        std::move(m_stereoResampleBuffer.begin()
                      + static_cast<std::ptrdiff_t>(keepFrom * 2),
                  m_stereoResampleBuffer.end(), m_stereoResampleBuffer.begin());
        m_stereoResampleBuffer.resize(m_stereoResampleBuffer.size() - keepFrom * 2);
        m_stereoResamplePosition -= static_cast<double>(keepFrom);
    }
    return produced;
}

std::size_t ChannelProcessor::processInternal(const Complex *iq, std::size_t n,
                                              float *monoOut,
                                              float *stereoOut) noexcept
{
    if (!m_configured || n == 0)
        return 0;

    std::size_t produced = 0;
    std::size_t offset = 0;

    while (offset < n) {
        const std::size_t chunk = std::min(kMaxBlockFrames, n - offset);

        m_nco.mixDown(iq + offset, m_mixed.data(), chunk);
        const std::size_t decimated = m_chain.process(m_mixed.data(), chunk, m_decimated.data());
        if (decimated == 0) {
            offset += chunk;
            continue;
        }

        m_filter.process(m_decimated.data(), m_filtered.data(), decimated);
        m_lastBasebandFrames = decimated;

        if (m_settings.fmIfNoiseReductionEnabled
            && (m_settings.mode == DemodMode::Fm || m_settings.mode == DemodMode::Nfm))
            m_fmIfNoiseReducer.process(m_filtered.data(), decimated);

        // S-meter: potenza media del canale filtrato, prima dell'AGC.
        float power = 0.0f;
        for (std::size_t i = 0; i < decimated; ++i)
            power += magnitudeSquared(m_filtered[i]);
        power /= static_cast<float>(decimated);
        const float instantDb = powerToDb(power);
        m_signalLevelDb += (instantDb - m_signalLevelDb) * 0.2f;

        // Il fondo rumore scende rapidamente quando la banda si libera, ma
        // sale lentamente davanti a un segnale continuo: in questo modo il
        // S-meter non viene confuso con l'SNR e una portante non si trasforma
        // artificialmente in "rumore di fondo".
        if (!m_noiseFloorInitialized) {
            m_noiseFloorDb = instantDb;
            m_noiseFloorInitialized = true;
        } else {
            const float alpha = instantDb < m_noiseFloorDb ? 0.12f : 0.003f;
            m_noiseFloorDb += (instantDb - m_noiseFloorDb) * alpha;
        }
        m_snrDb = std::clamp(m_signalLevelDb - m_noiseFloorDb, 0.0f, 99.0f);

        const bool ctcssMode = m_settings.ctcssEnabled && m_settings.mode == DemodMode::Nfm;
        const bool ctcssMute = ctcssMode && !m_settings.ctcssDecodeOnly;
        const bool squelchMode = (m_settings.squelchEnabled || ctcssMute)
            && squelchAllowed(m_settings.mode);
        const bool ctcssOpen = !ctcssMute || m_ctcss.detected();
        if (!squelchMode) {
            m_squelchOpen = true;
        } else if (!ctcssOpen) {
            m_squelchOpen = false;
        } else if (m_squelchOpen) {
            if (m_signalLevelDb < m_settings.squelchThresholdDb)
                m_squelchOpen = false;
        } else if (m_signalLevelDb >= m_settings.squelchThresholdDb + 3.0) {
            // Isteresi di 3 dB: impedisce il tremolio della voce al confine.
            m_squelchOpen = true;
        }

        const bool squelched = !m_squelchOpen;
        const bool decodeStereo = m_wideFm && m_settings.fmStereo;

        // IQ non è un modo di demodulazione audio: quando l'operatore lo
        // seleziona, L/R diventano rispettivamente I/Q così il monitor
        // conserva entrambe le componenti invece di perdere Q.
        if (stereoOut && m_settings.mode == DemodMode::Iq) {
            for (std::size_t i = 0; i < decimated; ++i) {
                m_stereoAudio[i * 2] = m_filtered[i].real();
                m_stereoAudio[i * 2 + 1] = m_filtered[i].imag();
            }
            const std::size_t audioProduced = m_resampleAudio
                ? resampleAudioStereo(m_stereoAudio.data(), decimated,
                                       stereoOut + produced * 2)
                : decimated;
            if (!m_resampleAudio)
                std::copy_n(m_stereoAudio.data(), decimated * 2,
                            stereoOut + produced * 2);
            const float gain = m_settings.muted ? 0.0f : m_settings.volume;
            for (std::size_t i = 0; i < audioProduced * 2; ++i)
                stereoOut[produced * 2 + i] = std::clamp(
                    stereoOut[produced * 2 + i] * gain, -1.0f, 1.0f);
            produced += audioProduced;
            offset += chunk;
            continue;
        }

        if (decodeStereo) {
            m_demod.process(m_filtered.data(), decimated, m_demodulated.data());
            m_agc.process(m_demodulated.data(), decimated);
            if (m_settings.fmRds)
                m_rds.process(m_demodulated.data(), decimated);
            m_broadcastFmStereo.process(m_demodulated.data(), decimated,
                                         m_stereoAudio.data());
            for (std::size_t i = 0; i < decimated; ++i) {
                m_stereoAudio[i * 2] = processDeemphasis(m_stereoAudio[i * 2],
                                                          m_deemphasisLeft);
                m_stereoAudio[i * 2 + 1] = processDeemphasis(m_stereoAudio[i * 2 + 1],
                                                              m_deemphasisRight);
                if (squelched) {
                    m_stereoAudio[i * 2] = 0.0f;
                    m_stereoAudio[i * 2 + 1] = 0.0f;
                }
            }

            if (stereoOut) {
                std::size_t audioProduced = decimated;
                if (m_resampleAudio) {
                    audioProduced = resampleAudioStereo(
                        m_stereoAudio.data(), decimated, stereoOut + produced * 2);
                } else {
                    std::copy_n(m_stereoAudio.data(), decimated * 2,
                                stereoOut + produced * 2);
                }
                const float gain = m_settings.muted ? 0.0f : m_settings.volume;
                for (std::size_t i = 0; i < audioProduced; ++i) {
                    stereoOut[(produced + i) * 2] = std::clamp(
                        stereoOut[(produced + i) * 2] * gain, -1.0f, 1.0f);
                    stereoOut[(produced + i) * 2 + 1] = std::clamp(
                        stereoOut[(produced + i) * 2 + 1] * gain, -1.0f, 1.0f);
                }
                produced += audioProduced;
            } else {
                for (std::size_t i = 0; i < decimated; ++i)
                    m_demodulated[i] = 0.5f * (m_stereoAudio[i * 2]
                                               + m_stereoAudio[i * 2 + 1]);
                const std::size_t audioProduced = m_resampleAudio
                    ? resampleAudio(m_demodulated.data(), decimated, monoOut + produced)
                    : decimated;
                const float gain = m_settings.muted ? 0.0f : m_settings.volume;
                if (!m_resampleAudio) {
                    for (std::size_t i = 0; i < decimated; ++i)
                        monoOut[produced + i] = m_demodulated[i];
                }
                for (std::size_t i = 0; i < audioProduced; ++i)
                    monoOut[produced + i] = std::clamp(
                        monoOut[produced + i] * gain, -1.0f, 1.0f);
                produced += audioProduced;
            }
        } else if (m_resampleAudio) {
            m_demod.process(m_filtered.data(), decimated, m_demodulated.data());
            if (m_wideFm && m_settings.fmRds)
                m_rds.process(m_demodulated.data(), decimated);
            if (m_settings.mode == DemodMode::Nfm && m_settings.ctcssEnabled)
                m_ctcss.process(m_demodulated.data(), decimated);

            // ── Filtri di disturbo sull'audio, in quest'ordine ───────────
            //
            // Il notch manuale per primo: una riga forte fa impazzire
            // l'adattamento di quelli che vengono dopo. Poi il notch
            // automatico, e solo alla fine la riduzione di rumore, che lavora
            // meglio quando i toni parassiti non ci sono più.
            //
            // Tutti e tre prima dell'AGC: dopo, il guadagno automatico avrebbe
            // già alzato il rumore che si sta cercando di togliere. Restano
            // fuori dai rami FM stereo e monitor IQ, dove non hanno senso.
            applyAudioFilters(m_demodulated.data(), decimated);
            m_agc.process(m_demodulated.data(), decimated);
            for (std::size_t i = 0; i < decimated; ++i) {
                m_demodulated[i] = processDeemphasis(m_demodulated[i], m_deemphasisLeft);
                if (squelched)
                    m_demodulated[i] = 0.0f;
            }

            if (stereoOut) {
                const std::size_t audioProduced = resampleAudio(
                    m_demodulated.data(), decimated, m_stereoAudio.data());
                const float gain = m_settings.muted ? 0.0f : m_settings.volume;
                for (std::size_t i = 0; i < audioProduced; ++i) {
                    const float value = std::clamp(m_stereoAudio[i] * gain,
                                                   -1.0f, 1.0f);
                    stereoOut[(produced + i) * 2] = value;
                    stereoOut[(produced + i) * 2 + 1] = value;
                }
                produced += audioProduced;
            } else {
                const std::size_t audioProduced = resampleAudio(
                    m_demodulated.data(), decimated, monoOut + produced);
                const float gain = m_settings.muted ? 0.0f : m_settings.volume;
                for (std::size_t i = 0; i < audioProduced; ++i)
                    monoOut[produced + i] = std::clamp(
                        monoOut[produced + i] * gain, -1.0f, 1.0f);
                produced += audioProduced;
            }
        } else {
            float *audio = stereoOut ? m_demodulated.data() : monoOut + produced;
            m_demod.process(m_filtered.data(), decimated, audio);
            if (m_wideFm && m_settings.fmRds)
                m_rds.process(audio, decimated);
            if (m_settings.mode == DemodMode::Nfm && m_settings.ctcssEnabled)
                m_ctcss.process(audio, decimated);

            // ── Filtri di disturbo sull'audio, in quest'ordine ───────────
            //
            // Il notch manuale per primo: una riga forte fa impazzire
            // l'adattamento di quelli che vengono dopo. Poi il notch
            // automatico, e solo alla fine la riduzione di rumore, che lavora
            // meglio quando i toni parassiti non ci sono più.
            //
            // Tutti e tre prima dell'AGC: dopo, il guadagno automatico avrebbe
            // già alzato il rumore che si sta cercando di togliere. Restano
            // fuori dai rami FM stereo e monitor IQ, dove non hanno senso.
            applyAudioFilters(audio, decimated);
            m_agc.process(audio, decimated);

            const float gain = m_settings.muted ? 0.0f : m_settings.volume;
            for (std::size_t i = 0; i < decimated; ++i) {
                audio[i] = processDeemphasis(processAudioLowpass(audio[i]),
                                              m_deemphasisLeft);
                if (squelched)
                    audio[i] = 0.0f;
                audio[i] = std::clamp(audio[i] * gain, -1.0f, 1.0f);
                if (stereoOut) {
                    stereoOut[(produced + i) * 2] = audio[i];
                    stereoOut[(produced + i) * 2 + 1] = audio[i];
                }
            }

            produced += decimated;
        }
        offset += chunk;
    }

    return produced;
}

void ChannelProcessor::applyAudioFilters(float *audio, std::size_t count) noexcept
{
    for (int i = 0; i < ChannelSettings::kMaxNotches; ++i) {
        if (m_settings.notches[static_cast<std::size_t>(i)].enabled)
            m_notches[static_cast<std::size_t>(i)].process(audio, count);
    }
    if (autoNotchActive())
        m_anf.process(audio, count, LmsFilter::Output::Error);
    if (m_settings.nrEnabled)
        m_nr.process(audio, count);
}

std::size_t ChannelProcessor::process(const Complex *iq, std::size_t n, float *out) noexcept
{
    const std::size_t produced = processInternal(iq, n, out, nullptr);
    const bool highPass = m_settings.audioHighPassEnabled
        && m_settings.mode != DemodMode::Cw && m_settings.mode != DemodMode::Cwr
        && m_settings.mode != DemodMode::Iq;
    for (std::size_t i = 0; i < produced; ++i) {
        if (highPass)
            out[i] = std::clamp(m_audioHighPassLeft.process(out[i]), -1.0f, 1.0f);
        updateAudioMeter(out[i]);
    }
    return produced;
}

std::size_t ChannelProcessor::processStereo(const Complex *iq, std::size_t n,
                                            float *outInterleaved) noexcept
{
    const std::size_t produced = processInternal(iq, n, nullptr, outInterleaved);
    const bool highPass = m_settings.audioHighPassEnabled
        && m_settings.mode != DemodMode::Cw && m_settings.mode != DemodMode::Cwr
        && m_settings.mode != DemodMode::Iq;
    for (std::size_t i = 0; i < produced; ++i) {
        float left = outInterleaved[i * 2];
        float right = outInterleaved[i * 2 + 1];
        if (highPass) {
            left = std::clamp(m_audioHighPassLeft.process(left), -1.0f, 1.0f);
            right = std::clamp(m_audioHighPassRight.process(right), -1.0f, 1.0f);
            outInterleaved[i * 2] = left;
            outInterleaved[i * 2 + 1] = right;
        }
        updateAudioMeter(std::sqrt(0.5f * (left * left + right * right)));
    }
    return produced;
}

} // namespace dsdr::dsp
