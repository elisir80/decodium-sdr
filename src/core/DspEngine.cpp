// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/DspEngine.h"

#include <QLoggingCategory>

#include <algorithm>
#include <cmath>

Q_LOGGING_CATEGORY(dsdrDsp, "dsdr.dsp")

namespace dsdr::core {

using dsp::Complex;

namespace {

/// ~1.3 s di audio a 48 kHz: assorbe una pausa lunga della scheda audio senza
/// far crescere la latenza percepita, che resta governata dal buffer del sink.
constexpr std::size_t kAudioRingFloats = 1 << 16;

/// Quanti campioni IQ si elaborano per giro. Coincide con il blocco massimo
/// dei ChannelProcessor: nessuna suddivisione ulteriore, nessuna allocazione.
constexpr std::size_t kProcessBlock = dsp::kMaxBlockFrames;

/// Intervallo minimo fra due emissioni di meter. L'occhio non distingue oltre
/// ~15 aggiornamenti al secondo, mentre ogni signal costa un attraversamento
/// di thread e, a valle, un dataChanged che rilancia le animazioni del delegate.
constexpr qint64 kMeterIntervalNs = 66'000'000; // ~15 Hz

// ── Quanta banda si tiene in memoria ────────────────────────────────────
//
// Due limiti insieme, e vince il più stretto. Il primo è quanto passato serve
// davvero: due minuti bastano a riprendere un nominativo perso, oltre si
// entra nel mestiere del registratore, che è un'altra funzione e scrive su
// disco. Il secondo è la memoria: a 1,536 MS/s un secondo di IQ costa 12 MB, e
// senza un tetto una banda larga si mangerebbe tutta la RAM di un CM5.
//
// Il risultato è che la profondità dipende dal ritmo di campionamento — 87
// secondi a 192 kS/s, una decina a 1,536 MS/s — e per questo la UI mostra la
// storia disponibile invece di promettere un numero fisso.
constexpr double kHistoryTargetSeconds = 120.0;
constexpr std::size_t kHistoryBudgetBytes = std::size_t(96) << 20;   // 96 MiB

/// Intervallo minimo fra due aggiornamenti di stato della macchina del tempo.
constexpr qint64 kReplayIntervalNs = 100'000'000; // 10 Hz

std::size_t historyFramesFor(double sampleRate)
{
    if (!(sampleRate > 0.0))
        return 0;

    constexpr std::size_t kBytesPerFrame = 2 * sizeof(float);
    const std::size_t byBudget = kHistoryBudgetBytes / kBytesPerFrame;
    const std::size_t byTime =
        static_cast<std::size_t>(kHistoryTargetSeconds * sampleRate);
    return std::min(byBudget, byTime);
}

} // namespace

DspEngine::DspEngine(QObject *parent)
    : QObject(parent)
    , m_audioRing(std::make_unique<dsp::SpscRing<float>>(kAudioRingFloats))
    , m_spectrum(new SpectrumFeed(this))
{
    m_interleaved.resize(kProcessBlock * 2);
    m_iq.resize(kProcessBlock);
    m_mix.resize(kProcessBlock);
    m_uptime.start();
}

DspEngine::~DspEngine() = default;

void DspEngine::setSource(dsp::SpscRing<float> *ring, double sampleRate, qint64 centerFrequencyHz)
{
    m_sourceRate.store(sampleRate, std::memory_order_release);
    m_centerHz.store(centerFrequencyHz, std::memory_order_release);
    m_source.store(ring, std::memory_order_release);
    m_needsReconfigure.store(true, std::memory_order_release);
}

void DspEngine::clearSource()
{
    m_source.store(nullptr, std::memory_order_release);
    m_audioRing->clear();

    // Staccata la radio, la sua storia non serve più a nessuno: chi si
    // riconnette non deve ritrovarsi in ascolto del device precedente.
    m_replayDelayFrames.store(0, std::memory_order_release);
    m_historyFrames.store(0, std::memory_order_release);
    m_historyDirty.store(true, std::memory_order_release);
}

void DspEngine::setCenterFrequency(qint64 hz)
{
    if (m_centerHz.exchange(hz, std::memory_order_acq_rel) == hz)
        return;

    // Spostare il centro cambia quale porzione di spettro è quella registrata:
    // ciò che sta in memoria non è più la storia di questa banda, e riascoltarlo
    // mostrerebbe frequenze che non sono mai state lì. Si riparte dal presente.
    m_historyDirty.store(true, std::memory_order_release);
}

void DspEngine::setReplayDelaySeconds(double seconds)
{
    const double rate = m_sourceRate.load(std::memory_order_acquire);
    if (!(rate > 0.0) || !(seconds > 0.0)) {
        m_replayDelayFrames.store(0, std::memory_order_release);
        return;
    }

    // Il taglio alla storia disponibile si fa già qui, non solo nel thread
    // DSP: chi chiede un'ora indietro deve leggere subito il ritardo vero.
    // Aspettare il blocco successivo significherebbe mostrare per un istante
    // un numero inventato, e su un pannello un istante basta a essere letto.
    const std::size_t wanted = static_cast<std::size_t>(seconds * rate);
    const std::size_t available = m_historyFrames.load(std::memory_order_acquire);
    m_replayDelayFrames.store(std::min(wanted, available), std::memory_order_release);
}

double DspEngine::replayDelaySeconds() const
{
    const double rate = m_sourceRate.load(std::memory_order_acquire);
    if (!(rate > 0.0))
        return 0.0;
    return static_cast<double>(m_replayDelayFrames.load(std::memory_order_acquire)) / rate;
}

double DspEngine::historySeconds() const
{
    const double rate = m_sourceRate.load(std::memory_order_acquire);
    if (!(rate > 0.0))
        return 0.0;
    return static_cast<double>(m_historyFrames.load(std::memory_order_acquire)) / rate;
}

void DspEngine::setNoiseBlanker(bool enabled, double threshold)
{
    m_nbThreshold.store(threshold, std::memory_order_release);
    m_nbEnabled.store(enabled, std::memory_order_release);
    if (!enabled)
        m_nbActivity.store(0.0f, std::memory_order_release);
}

double DspEngine::noiseBlankerThreshold() const
{
    return m_nbThreshold.load(std::memory_order_acquire);
}

double DspEngine::historyCapacitySeconds() const
{
    const double rate = m_sourceRate.load(std::memory_order_acquire);
    if (!(rate > 0.0))
        return 0.0;
    return static_cast<double>(historyFramesFor(rate)) / rate;
}

void DspEngine::setRecorder(IqRecorder *recorder)
{
    m_recorder.store(recorder, std::memory_order_release);
}

void DspEngine::setFftSize(int size)
{
    if (size < 256 || (size & (size - 1)) != 0) {
        qCWarning(dsdrDsp) << "dimensione FFT non valida, ignorata:" << size;
        return;
    }
    m_fftSize = size;
    m_needsReconfigure.store(true, std::memory_order_release);
}

void DspEngine::reconfigure()
{
    m_activeRate = m_sourceRate.load(std::memory_order_acquire);
    if (m_activeRate <= 0.0)
        return;

    m_analyzer.configure(m_fftSize, m_activeRate);
    m_analyzer.setAveraging(0.5f);
    m_analyzer.setOverlap(0.5f);

    m_spectrum->configure(m_fftSize, m_activeRate, m_centerHz.load(std::memory_order_acquire));

    for (auto &[id, channel] : m_channels) {
        Q_UNUSED(id)
        channel.processor->configure(m_activeRate, kInternalAudioRate);
        channel.processor->applySettings(channel.settings);
        channel.audio.assign(channel.processor->maxAudioFrames(kProcessBlock), 0.0f);
    }

    // La memoria di scorrimento si alloca qui, dove si conosce il ritmo di
    // campionamento e dove allocare è ancora lecito (CONSTITUTION §5).
    const std::size_t historyFrames = historyFramesFor(m_activeRate);
    m_history.configure(historyFrames);
    m_blanker.configure(m_activeRate);
    m_historyFrames.store(0, std::memory_order_release);
    m_replayDelayFrames.store(0, std::memory_order_release);
    m_historyDirty.store(false, std::memory_order_release);

    m_audioRing->clear();
    m_needsReconfigure.store(false, std::memory_order_release);

    qCInfo(dsdrDsp) << "DSP riconfigurato:" << m_activeRate << "Hz, FFT" << m_fftSize
                    << "canali:" << m_channels.size()
                    << "storia:" << (historyFrames / std::max(1.0, m_activeRate)) << "s";
}

void DspEngine::addChannel(ChannelId id, const dsp::ChannelSettings &settings)
{
    if (id == kInvalidChannel || m_channels.find(id) != m_channels.end())
        return;

    Channel channel;
    channel.processor = std::make_unique<dsp::ChannelProcessor>();
    channel.settings = settings;

    if (m_activeRate > 0.0) {
        channel.processor->configure(m_activeRate, kInternalAudioRate);
        channel.processor->applySettings(settings);
        channel.audio.assign(channel.processor->maxAudioFrames(kProcessBlock), 0.0f);
    }

    m_channels.emplace(id, std::move(channel));
}

void DspEngine::updateChannel(ChannelId id, const dsp::ChannelSettings &settings)
{
    auto it = m_channels.find(id);
    if (it == m_channels.end())
        return;

    it->second.settings = settings;
    if (m_activeRate > 0.0)
        it->second.processor->applySettings(settings);
}

void DspEngine::removeChannel(ChannelId id)
{
    m_channels.erase(id);
}

void DspEngine::onIqFrameReady(const hal::IqFrame &frame)
{
    if (frame.droppedFrames > 0) {
        m_totalDropped += frame.droppedFrames;
        // In overrun sostenuto il segnale arriverebbe a ogni frame: la UI ne
        // ricaverebbe solo un flusso di re-layout della barra di stato.
        const qint64 now = m_uptime.nsecsElapsed();
        if (now - m_lastOverrunReportNs >= 500'000'000) {
            m_lastOverrunReportNs = now;
            emit overrunDetected(m_totalDropped);
        }
    }
    processAvailable();
}

void DspEngine::processAvailable()
{
    dsp::SpscRing<float> *source = m_source.load(std::memory_order_acquire);
    if (!source)
        return;

    if (m_needsReconfigure.load(std::memory_order_acquire))
        reconfigure();
    if (m_activeRate <= 0.0)
        return;

    // La banda sotto osservazione è cambiata mentre eravamo altrove: la storia
    // raccolta finora non descrive più questo pezzo di spettro.
    if (m_historyDirty.exchange(false, std::memory_order_acq_rel)) {
        m_history.clear();
        m_historyFrames.store(0, std::memory_order_release);
        m_replayDelayFrames.store(0, std::memory_order_release);
    }

    const int decimation =
        std::max(1, static_cast<int>(std::lround(m_activeRate / kInternalAudioRate)));

    while (true) {
        const std::size_t availableFrames = source->available() / 2;
        if (availableFrames == 0)
            break;

        const std::size_t frames = std::min(availableFrames, kProcessBlock);
        const std::size_t got = source->read(m_interleaved.data(), frames * 2);
        const std::size_t count = got / 2;
        if (count == 0)
            break;

        // Tap di registrazione prima di qualunque elaborazione: su disco
        // finisce ciò che la radio ha consegnato, non ciò che il DSP ne ha
        // fatto. `feed()` non blocca e non alloca.
        if (IqRecorder *recorder = m_recorder.load(std::memory_order_acquire))
            recorder->feed(m_interleaved.data(), got);

        // ── Macchina del tempo ──────────────────────────────────────────
        //
        // La storia si scrive sempre, anche quando la si sta già riascoltando:
        // altrimenti il presente andrebbe perso proprio mentre si guarda il
        // passato, e tornare in diretta lascerebbe un buco.
        m_history.write(m_interleaved.data(), count);
        m_historyFrames.store(m_history.availableFrames(), std::memory_order_release);

        std::size_t toProcess = count;
        const std::size_t requestedDelay = m_replayDelayFrames.load(std::memory_order_acquire);
        if (requestedDelay > 0) {
            // Si rilegge lo stesso buffer qualche secondo più indietro. Il
            // ritardo effettivo può essere minore di quello chiesto — la
            // storia comincia quando comincia — e viene riscritto perché la
            // UI mostri il tempo vero e non quello sperato.
            const std::size_t granted = m_history.clampDelay(requestedDelay, count);
            if (granted != requestedDelay)
                m_replayDelayFrames.store(granted, std::memory_order_release);
            if (granted > 0)
                toProcess = m_history.readDelayed(granted, m_interleaved.data(), count);
        }

        if (toProcess == 0)
            continue;

        for (std::size_t i = 0; i < toProcess; ++i)
            m_iq[i] = Complex(m_interleaved[i * 2], m_interleaved[i * 2 + 1]);

        // ── Noise blanker, a banda piena (SPEC-003 §4) ──────────────────
        //
        // Qui e non più in basso: dopo la decimazione e il filtro di canale un
        // impulso è già diventato una coda di millisecondi, e toglierlo
        // significa bucare il segnale insieme al disturbo.
        //
        // Dopo il registratore e dopo la memoria di scorrimento, di proposito:
        // su disco e in memoria finisce ciò che la radio ha consegnato, così
        // riascoltando si può ancora cambiare idea sul blanker.
        if (m_nbEnabled.load(std::memory_order_acquire)) {
            m_blanker.setThreshold(m_nbThreshold.load(std::memory_order_acquire));
            m_blanker.process(m_iq.data(), toProcess);
            m_nbActivity.store(m_blanker.lastSuppressedRatio(), std::memory_order_release);
        }

        // ── Ramo spettro: tap in parallelo alla demodulazione (§5.1) ─────
        if (m_analyzer.push(m_iq.data(), toProcess))
            m_spectrum->publish(m_analyzer.magnitudesDb().data());

        // ── Ramo audio ──────────────────────────────────────────────────
        const std::size_t audioFrames = toProcess / static_cast<std::size_t>(decimation);
        if (audioFrames == 0)
            continue;

        std::fill_n(m_mix.begin(), audioFrames, 0.0f);
        bool hasAudio = false;

        for (auto &[channelId, channel] : m_channels) {
            if (!channel.processor)
                continue;

            const std::size_t produced =
                channel.processor->process(m_iq.data(), toProcess, channel.audio.data());
            const std::size_t usable = std::min(produced, audioFrames);
            for (std::size_t i = 0; i < usable; ++i)
                m_mix[i] += channel.audio[i];
            hasAudio = hasAudio || usable > 0;

            const qint64 now = m_uptime.nsecsElapsed();
            if (now - channel.lastMeterNs >= kMeterIntervalNs) {
                channel.lastMeterNs = now;
                emit metersUpdated(channelId,
                                   channel.processor->signalLevelDb(),
                                   channel.processor->agcGainDb());
            }
        }

        if (hasAudio) {
            for (std::size_t i = 0; i < audioFrames; ++i)
                m_mix[i] = std::clamp(m_mix[i], -1.0f, 1.0f);
        }

        // Se il consumatore audio è in ritardo scartiamo il campione più
        // vecchio: meglio un micro-salto che una latenza che cresce senza fine.
        if (m_audioRing->space() < audioFrames)
            m_audioRing->discard(audioFrames - m_audioRing->space());
        m_audioRing->write(m_mix.data(), audioFrames);
    }

    // Il centro può essere cambiato mentre eravamo dentro il ciclo: la
    // geometria dello spettro va allineata prima del prossimo frame.
    const qint64 center = m_centerHz.load(std::memory_order_acquire);
    if (center != m_spectrum->centerFrequency())
        m_spectrum->configure(m_fftSize, m_activeRate, center);

    // Stato della macchina del tempo, fuori dal ciclo e col contagocce: la
    // storia si allunga a ogni blocco, ma una barra che si muove dieci volte
    // al secondo è già più fluida dell'occhio.
    const qint64 now = m_uptime.nsecsElapsed();
    if (now - m_lastReplayReportNs >= kReplayIntervalNs) {
        m_lastReplayReportNs = now;
        emit replayStateChanged(replayDelaySeconds(), historySeconds());
    }
}

} // namespace dsdr::core
