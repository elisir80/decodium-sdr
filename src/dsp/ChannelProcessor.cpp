// SPDX-License-Identifier: GPL-3.0-or-later
#include "dsp/ChannelProcessor.h"
#include "dsp/FirDesign.h"

#include <algorithm>
#include <cmath>

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

} // namespace

bool ChannelSettings::operator==(const ChannelSettings &o) const noexcept
{
    return offsetHz == o.offsetHz && mode == o.mode && filterLowHz == o.filterLowHz
        && filterHighHz == o.filterHighHz && agcMode == o.agcMode
        && agcThresholdDb == o.agcThresholdDb && agcMaxGainDb == o.agcMaxGainDb
        && cwPitchHz == o.cwPitchHz && passbandShiftHz == o.passbandShiftHz
        && volume == o.volume && muted == o.muted
        && squelchEnabled == o.squelchEnabled
        && squelchThresholdDb == o.squelchThresholdDb
        && nrEnabled == o.nrEnabled && nrStrength == o.nrStrength
        && anfEnabled == o.anfEnabled
        && notchEnabled == o.notchEnabled
        && notchFrequencyHz == o.notchFrequencyHz
        && notchWidthHz == o.notchWidthHz;
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

    int decimation = static_cast<int>(std::lround(deviceSampleRate / audioSampleRate));
    decimation = std::max(1, decimation);

    // Banda utile conservata dalla decimazione: copre la FM larga, che è il
    // canale più largo che il demodulatore possa chiedere.
    if (!m_chain.configure(deviceSampleRate, decimation, 8000.0))
        return false;

    m_channelRate = m_chain.outputRate();

    m_nco.configure(deviceSampleRate, 0.0);
    m_demod.configure(m_channelRate);
    m_agc.configure(m_channelRate);

    // Filtri di disturbo sull'audio. I due predittori adattivi hanno memorie
    // diverse di proposito — il notch automatico deve agganciare una riga in
    // fretta, la riduzione di rumore deve restare stabile sulla voce, e con lo
    // stesso ritardo di decorrelazione farebbero lo stesso mestiere due volte.
    m_notch.configure(m_channelRate);
    m_anf.configure(64, 8);
    m_nr.configure(64, 16);

    // Risalita del fondo: circa sei dB al minuto. Abbastanza lenta perché una
    // trasmissione lunga non la trascini con sé, abbastanza svelta da seguire
    // il rumore che monta nell'arco di una serata.
    const double blocksPerSecond = m_channelRate / static_cast<double>(kMaxBlockFrames);
    m_floorRiseRate = static_cast<float>(0.1 / std::max(1.0, blocksPerSecond));

    const std::size_t block = kMaxBlockFrames;
    m_mixed.assign(block, Complex(0.0f, 0.0f));
    m_decimated.assign(m_chain.maxOutput(block) + 8, Complex(0.0f, 0.0f));
    m_filtered.assign(m_chain.maxOutput(block) + 8, Complex(0.0f, 0.0f));

    m_configured = true;
    redesignFilter();
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
    case DemodMode::Fm:
    case DemodMode::Nfm:
        // Modalità simmetriche: la portante sta a DC, serve tutta la banda.
        loHz = -std::abs(hi);
        hiHz = std::abs(hi);
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
    const bool tuningChanged = settings.offsetHz != m_settings.offsetHz
        || settings.cwPitchHz != m_settings.cwPitchHz || settings.mode != m_settings.mode;

    // Chi era acceso prima, per accorgersi delle accensioni.
    const struct {
        bool nr, anf, notch;
    } wasEnabled{m_settings.nrEnabled, m_settings.anfEnabled, m_settings.notchEnabled};

    m_settings = settings;

    if (tuningChanged)
        m_nco.setFrequency(tuningOffsetHz());
    if (filterChanged) {
        m_demod.setMode(m_settings.mode);
        m_demod.setFmDeviation(m_settings.mode == DemodMode::Nfm ? 2500.0 : 5000.0);
        redesignFilter();
    }

    m_agc.setMode(m_settings.agcMode);
    m_agc.setThresholdDb(m_settings.agcThresholdDb);
    m_agc.setMaxGainDb(m_settings.agcMaxGainDb);

    m_notch.setNotch(m_settings.notchFrequencyHz, m_settings.notchWidthHz);
    m_nr.setRate(static_cast<float>(m_settings.nrStrength));

    // Un filtro adattivo che riparte da spento porta con sé i coefficienti di
    // prima: erano la risposta a un segnale che non c'è più, e per qualche
    // decimo di secondo colorano l'audio. Si riazzera all'accensione.
    if (!wasEnabled.nr && m_settings.nrEnabled)
        m_nr.reset();
    if (!wasEnabled.anf && m_settings.anfEnabled)
        m_anf.reset();
    if (!wasEnabled.notch && m_settings.notchEnabled)
        m_notch.reset();
}

void ChannelProcessor::reset() noexcept
{
    m_nco.reset();
    m_chain.reset();
    m_filter.reset();
    m_demod.reset();
    m_agc.reset();
    m_notch.reset();
    m_anf.reset();
    m_nr.reset();
    m_signalLevelDb = -160.0f;
    m_noiseFloorDb = -160.0f;
    m_lastBasebandFrames = 0;
}

std::size_t ChannelProcessor::process(const Complex *iq, std::size_t n, float *out) noexcept
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

        // S-meter: potenza media del canale filtrato, prima dell'AGC.
        float power = 0.0f;
        for (std::size_t i = 0; i < decimated; ++i)
            power += magnitudeSquared(m_filtered[i]);
        power /= static_cast<float>(decimated);
        const float instantDb = powerToDb(power);
        m_signalLevelDb += (instantDb - m_signalLevelDb) * 0.2f;

        // ── Fondo di rumore, per minima statistica (SPEC-003 §9) ────────
        //
        // Scende subito e risale piano: il livello più basso che il canale
        // tocca è il fondo, tutto il resto è qualcuno che trasmette. La
        // risalita lenta serve a inseguire il fondo vero quando cambia — il
        // QRN che monta la sera, un motore che si accende nel palazzo — senza
        // farsi tirare su dai segnali che passano.
        if (m_signalLevelDb < m_noiseFloorDb)
            m_noiseFloorDb = m_signalLevelDb;
        else
            m_noiseFloorDb += m_floorRiseRate;

        float *audio = out + produced;
        m_demod.process(m_filtered.data(), decimated, audio);

        // ── Filtri sull'audio, in quest'ordine e non in un altro ────────
        //
        // Il notch manuale per primo: una riga forte fa impazzire
        // l'adattamento di quelli che vengono dopo, e toglierla prima è come
        // spegnere una luce puntata negli occhi.
        //
        // Poi il notch automatico, che toglie le righe rimaste, e solo alla
        // fine la riduzione di rumore — che lavora meglio quando i toni
        // parassiti non ci sono più.
        //
        // Tutti e tre prima dell'AGC: dopo, il guadagno automatico avrebbe già
        // alzato il rumore che si sta cercando di togliere.
        if (m_settings.notchEnabled)
            m_notch.process(audio, decimated);
        if (autoNotchActive())
            m_anf.process(audio, decimated, LmsFilter::Output::Error);
        if (m_settings.nrEnabled)
            m_nr.process(audio, decimated, LmsFilter::Output::Prediction);

        m_agc.process(audio, decimated);

        // ── Squelch ──────────────────────────────────────────────────────
        //
        // Si decide sul livello *prima* dell'AGC: dopo, il guadagno
        // automatico ha già tirato su il rumore fino al livello del parlato,
        // e ogni soglia diventerebbe una monetina lanciata.
        //
        // L'isteresi non è un lusso. Con una soglia secca, un segnale che
        // respira attorno a quel valore — cioè il caso normale in HF — apre e
        // chiude l'audio più volte al secondo, ed è più faticoso da ascoltare
        // del rumore che si voleva togliere.
        if (m_settings.squelchEnabled) {
            const auto threshold = static_cast<float>(m_settings.squelchThresholdDb);
            const float open = m_squelchClosed ? threshold + kSquelchHysteresisDb
                                               : threshold;
            m_squelchClosed = m_signalLevelDb < open;
        } else {
            m_squelchClosed = false;
        }

        // L'apertura non salta da zero a uno: un gradino sul campione
        // successivo si sente come un colpo secco in cuffia.
        const float target = m_squelchClosed ? 0.0f : 1.0f;
        const float step = (target > m_squelchGain) ? kSquelchAttack : kSquelchRelease;

        const float gain = m_settings.muted ? 0.0f : m_settings.volume;
        for (std::size_t i = 0; i < decimated; ++i) {
            m_squelchGain += (target - m_squelchGain) * step;
            audio[i] = std::clamp(audio[i] * gain * m_squelchGain, -1.0f, 1.0f);
        }

        produced += decimated;
        offset += chunk;
    }

    return produced;
}

} // namespace dsdr::dsp
