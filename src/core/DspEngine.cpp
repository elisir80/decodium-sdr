// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/DspEngine.h"
#include "dsp/FirDesign.h"

#include <QLoggingCategory>
#include <QFileInfo>
#include <QLibrary>

#include <algorithm>
#include <cmath>

Q_LOGGING_CATEGORY(dsdrDsp, "dsdr.dsp")

namespace dsdr::core {

using dsp::Complex;

namespace {

/// ~1.3 s di audio stereo a 48 kHz: assorbe una pausa lunga della scheda audio senza
/// far crescere la latenza percepita, che resta governata dal buffer del sink.
constexpr std::size_t kAudioRingFloats = 1 << 17;
constexpr std::size_t kAudioChannels = 2;

/// Punti della trasformata usata per analizzare l'audio.
///
/// Duemilaquarantotto su quarantottomila danno poco più di venti hertz di
/// risoluzione: su una passata di tre kilohertz è quello che serve — si
/// distingue una nota dall'altra — e costa una frazione di quanto costa la
/// trasformata della banda, che è due volte più lunga su un flusso che va
/// quattro volte più veloce.
constexpr int kAudioFftSize = 2048;

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
    , m_audioSpectrum(new SpectrumFeed(this))
{
    m_interleaved.resize(kProcessBlock * 2);
    m_iq.resize(kProcessBlock);
    m_mix.resize(kProcessBlock * kAudioChannels);
    m_moduleIq.resize(kProcessBlock * 2);
    m_uptime.start();
}

struct DspEngine::LoadedIqModule
{
    std::unique_ptr<QLibrary> library;
    dsdr_iq_module_v1 *module = nullptr;
    QString path;
};

DspEngine::~DspEngine()
{
    unloadIqModules();
}

void DspEngine::setSource(dsp::SpscRing<float> *ring, double sampleRate, qint64 centerFrequencyHz)
{
    m_sourceIsAudio.store(false, std::memory_order_release);
    attachSource(ring, sampleRate, centerFrequencyHz);
}

void DspEngine::attachSource(dsp::SpscRing<float> *ring, double sampleRate,
                             qint64 centerFrequencyHz)
{
    m_sourceRate.store(sampleRate, std::memory_order_release);
    m_centerHz.store(centerFrequencyHz, std::memory_order_release);
    m_source.store(ring, std::memory_order_release);
    m_needsReconfigure.store(true, std::memory_order_release);
}

void DspEngine::setAudioSource(dsp::SpscRing<float> *ring, double sampleRate,
                               qint64 centerFrequencyHz)
{
    m_sourceIsAudio.store(true, std::memory_order_release);
    attachSource(ring, sampleRate, centerFrequencyHz);
}

void DspEngine::setAudioSideband(int sideband)
{
    const int previous = m_sideband.exchange(sideband, std::memory_order_acq_rel);
    if (previous == sideband)
        return;
    // Cambiare lato ribalta lo spettro: la storia raccolta finora descrive
    // l'altra metà della banda, e riascoltarla mostrerebbe i segnali dalla
    // parte sbagliata del VFO.
    m_historyDirty.store(true, std::memory_order_release);
}

void DspEngine::clearSource()
{
    m_source.store(nullptr, std::memory_order_release);
    m_sourceIsAudio.store(false, std::memory_order_release);
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

void DspEngine::setOverloadMode(int mode)
{
    m_overloadMode.store(std::clamp(mode, 0, 2), std::memory_order_release);
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

void DspEngine::setAudioRecorder(IqRecorder *recorder)
{
    m_audioRecorder.store(recorder, std::memory_order_release);
}

bool DspEngine::loadIqModule(const QString &path)
{
    const QString absolutePath = QFileInfo(path).absoluteFilePath();
    if (path.isEmpty() || !QFileInfo::exists(absolutePath)) {
        qCWarning(dsdrDsp) << "modulo IQ non trovato:" << path;
        return false;
    }

    auto library = std::make_unique<QLibrary>(absolutePath);
    if (!library->load()) {
        qCWarning(dsdrDsp) << "caricamento modulo IQ fallito:" << absolutePath
                           << library->errorString();
        return false;
    }

    const auto creator = reinterpret_cast<dsdr_create_iq_module_v1_fn>(
        library->resolve("dsdr_create_iq_module_v1"));
    if (!creator) {
        qCWarning(dsdrDsp) << "modulo IQ senza dsdr_create_iq_module_v1:" << absolutePath;
        library->unload();
        return false;
    }

    dsdr_iq_module_v1 *module = creator();
    if (!module || module->abi_version != DSDR_IQ_MODULE_ABI_VERSION
        || !module->process_iq || !module->name || !*module->name) {
        qCWarning(dsdrDsp) << "ABI modulo IQ non valido:" << absolutePath;
        if (module && module->destroy)
            module->destroy(module->user);
        library->unload();
        return false;
    }

    auto loaded = std::make_unique<LoadedIqModule>();
    loaded->library = std::move(library);
    loaded->module = module;
    loaded->path = absolutePath;
    qCInfo(dsdrDsp) << "modulo IQ caricato:" << module->name << absolutePath;
    m_iqModules.push_back(std::move(loaded));
    return true;
}

void DspEngine::unloadIqModules()
{
    for (auto it = m_iqModules.rbegin(); it != m_iqModules.rend(); ++it) {
        if ((*it)->module && (*it)->module->destroy)
            (*it)->module->destroy((*it)->module->user);
        if ((*it)->library)
            (*it)->library->unload();
    }
    m_iqModules.clear();
}

QStringList DspEngine::iqModuleNames() const
{
    QStringList result;
    for (const auto &loaded : m_iqModules) {
        if (loaded && loaded->module && loaded->module->name)
            result.append(QString::fromUtf8(loaded->module->name));
    }
    return result;
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

void DspEngine::makeAnalytic(std::size_t count)
{
    const Sideband sideband =
        static_cast<Sideband>(m_sideband.load(std::memory_order_acquire));

    if (sideband == Sideband::Double) {
        // AM e FM occupano davvero entrambi i lati della portante: qui lo
        // spettro speculare non è un artefatto, è ciò che c'è in aria. Il
        // segnale resta reale, e non serve alcun filtro.
        for (std::size_t i = 0; i < count; ++i) {
            m_interleaved[i * 2] = m_mono[i];
            m_interleaved[i * 2 + 1] = 0.0f;
        }
        return;
    }

    for (std::size_t i = 0; i < count; ++i)
        m_analyticScratch[i] = Complex(m_mono[i], 0.0f);

    m_analytic.process(m_analyticScratch.data(), m_analyticScratch.data(), count);

    // Il fattore due recupera la metà dell'energia che stava nelle frequenze
    // negative: senza, l'audio di una radio arriverebbe 6 dB sotto quello di
    // un SDR a parità di segnale, e l'operatore alzerebbe il volume cercando
    // il guasto altrove.
    const bool invert = sideband == Sideband::Lower;
    for (std::size_t i = 0; i < count; ++i) {
        const Complex z = m_analyticScratch[i] * 2.0f;
        m_interleaved[i * 2] = z.real();
        // In LSB l'audio scende quando la radiofrequenza sale: il coniugato
        // ribalta lo spettro e rimette ogni segnale dove sta davvero.
        m_interleaved[i * 2 + 1] = invert ? -z.imag() : z.imag();
    }
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

    // L'analizzatore dell'audio non dipende dal device: la frequenza
    // dell'audio interno è fissa, e la sua geometria non cambia con la banda.
    // Si configura qui lo stesso perché è il punto in cui si allocano i
    // buffer, e allocare nel percorso caldo è vietato (CONSTITUTION §5).
    if (m_audioAnalyzer.configure(kAudioFftSize, kInternalAudioRate)) {
        m_audioScratch.assign(static_cast<std::size_t>(kProcessBlock), dsp::Complex{});
        // Metà bin e metà banda: la trasformata di un segnale reale è
        // simmetrica, e la metà negativa non aggiunge niente. Il centro sta a
        // un quarto della frequenza di campionamento, così l'asse va da zero
        // alla Nyquist come chiunque si aspetta da un analizzatore audio.
        m_audioSpectrum->configure(kAudioFftSize / 2, kInternalAudioRate / 2.0,
                                   static_cast<qint64>(kInternalAudioRate / 4.0));
    }
    const std::size_t expectedAudioFrames = static_cast<std::size_t>(std::ceil(
        static_cast<double>(kProcessBlock) * kInternalAudioRate / m_activeRate)) + 8;
    m_mix.assign(expectedAudioFrames * kAudioChannels,
                 0.0f);

    for (auto &[id, channel] : m_channels) {
        Q_UNUSED(id)
        channel.processor->configure(m_activeRate, kInternalAudioRate);
        channel.processor->applySettings(channel.settings);
        channel.audio.assign(channel.processor->maxAudioFrames(kProcessBlock)
                                 * kAudioChannels,
                             0.0f);
    }

    // La memoria di scorrimento si alloca qui, dove si conosce il ritmo di
    // campionamento e dove allocare è ancora lecito (CONSTITUTION §5).
    const std::size_t historyFrames = historyFramesFor(m_activeRate);
    m_history.configure(historyFrames);
    m_blanker.configure(m_activeRate);
    m_overload.configure(m_activeRate);

    if (m_sourceIsAudio.load(std::memory_order_acquire)) {
        // Un solo filtro fa due mestieri: rende analitico il segnale — tenendo
        // le sole frequenze positive — e lo limita alla banda che una radio
        // consegna davvero. Progettarne due sarebbe il doppio del lavoro nel
        // punto più caldo per lo stesso risultato.
        //
        // La transizione a 250 Hz è ciò che decide il costo: sotto i 200 il
        // numero di tap supererebbe kMaxFirTaps, sopra i 400 comincerebbe a
        // mangiare le voci più basse.
        constexpr double kLowEdgeHz = 200.0;
        constexpr double kHighEdgeHz = 4000.0;
        int taps = dsp::estimateTaps(250.0, m_activeRate, 60.0);
        taps = std::min(taps, static_cast<int>(dsp::kMaxFirTaps) - 1);
        if ((taps & 1) == 0)
            ++taps;
        m_analytic.setTaps(dsp::designBandpass(kLowEdgeHz, kHighEdgeHz, m_activeRate,
                                               taps, dsp::kaiserBeta(60.0)));
        m_mono.assign(kProcessBlock, 0.0f);
        m_analyticScratch.assign(kProcessBlock, Complex(0.0f, 0.0f));
    }
    m_lastOverloadReported = false;
    m_historyFrames.store(0, std::memory_order_release);
    m_replayDelayFrames.store(0, std::memory_order_release);
    m_historyDirty.store(false, std::memory_order_release);

    m_audioRing->clear();
    m_needsReconfigure.store(false, std::memory_order_release);
    m_lastStatsNs = m_uptime.nsecsElapsed();
    m_statsIqFrames = 0;
    m_statsAudioFrames = 0;
    m_statsBlocks = 0;

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
        channel.audio.assign(channel.processor->maxAudioFrames(kProcessBlock)
                                 * kAudioChannels,
                             0.0f);
    }

    m_channels.emplace(id, std::move(channel));
}

void DspEngine::updateChannel(ChannelId id, const dsp::ChannelSettings &settings)
{
    auto it = m_channels.find(id);
    if (it == m_channels.end())
        return;

    const bool tuningChanged = settings.offsetHz != it->second.settings.offsetHz
        || settings.mode != it->second.settings.mode
        || settings.fmRds != it->second.settings.fmRds
        || settings.rdsRegion != it->second.settings.rdsRegion;
    it->second.settings = settings;
    if (tuningChanged) {
        it->second.lastRdsSynced = false;
        it->second.lastRdsPi.clear();
        it->second.lastRdsCountryCode = -1;
        it->second.lastRdsProgramCoverage = -1;
        it->second.lastRdsReferenceNumber = -1;
        it->second.lastRdsCallsign.clear();
        it->second.lastRdsProgramType.clear();
        it->second.lastRdsAlternateFrequencies.clear();
        it->second.lastRdsProgramService.clear();
        it->second.lastRdsRadioText.clear();
    }
    if (m_activeRate > 0.0) {
        it->second.processor->applySettings(settings);
        it->second.audio.assign(it->second.processor->maxAudioFrames(kProcessBlock)
                                   * kAudioChannels,
                               0.0f);
    }
}

void DspEngine::removeChannel(ChannelId id)
{
    m_channels.erase(id);
}

void DspEngine::onAudioFrameReady(const hal::AudioFrame &frame)
{
    Q_UNUSED(frame)
    ++m_statsAudioFrames;
    processAvailable();
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

void DspEngine::analyzeAudio(std::size_t frames)
{
    if (frames == 0 || m_audioScratch.size() < frames)
        return;

    // Mono: la somma dei due canali diviso due. Analizzare un canale solo
    // perderebbe metà del segnale su una sorgente che li usa in modo diverso,
    // e analizzarli separati raddoppierebbe il costo per mostrare due volte la
    // stessa cosa — l'audio di un ricevitore è mono in tutto tranne che nel
    // formato.
    for (std::size_t i = 0; i < frames; ++i) {
        const float mono = 0.5f * (m_mix[i * kAudioChannels] + m_mix[i * kAudioChannels + 1]);
        m_audioScratch[i] = dsp::Complex{mono, 0.0f};
    }

    if (!m_audioAnalyzer.push(m_audioScratch.data(), frames))
        return;

    const std::vector<float> &mags = m_audioAnalyzer.magnitudesDb();
    if (static_cast<int>(mags.size()) < kAudioFftSize)
        return;

    // Solo la metà positiva. Dopo il fftshift le frequenze crescenti partono
    // da metà tabella: da lì in avanti c'è da zero alla Nyquist.
    const float *positive = mags.data() + kAudioFftSize / 2;
    m_audioSpectrum->publish(positive);

    // ── Il tono dominante ────────────────────────────────────────────────
    //
    // Una volta ogni tanto e non a ogni trasformata: è un numero che si legge,
    // e un numero che cambia sessanta volte al secondo non si legge.
    const qint64 now = m_uptime.nsecsElapsed();
    if (now - m_lastToneNs < 200'000'000)
        return;
    m_lastToneNs = now;

    const int bins = kAudioFftSize / 2;
    // Sotto i cinquanta hertz c'è il residuo di continua e il rumore di
    // alimentazione, e sono sempre i più forti di tutti: cercare il tono
    // partendo da zero vuol dire trovare sempre e solo quelli.
    const double binWidth = kInternalAudioRate / static_cast<double>(kAudioFftSize);
    const int first = static_cast<int>(50.0 / binWidth) + 1;

    int peak = -1;
    float peakDb = -200.0f;
    for (int bin = first; bin < bins; ++bin) {
        if (positive[bin] > peakDb) {
            peakDb = positive[bin];
            peak = bin;
        }
    }

    // Deve emergere dal fondo, altrimenti non è un tono: è il bin che ha vinto
    // per caso fra mille bin di rumore. Dodici decibel sopra la mediana sono
    // la soglia oltre la quale una nota si sente davvero.
    float median = -120.0f;
    if (peak >= 0) {
        std::vector<float> sorted(positive + first, positive + bins);
        const std::size_t middle = sorted.size() / 2;
        std::nth_element(sorted.begin(), sorted.begin() + middle, sorted.end());
        median = sorted[middle];
    }

    if (peak < 0 || peakDb - median < 12.0f) {
        emit audioToneMeasured(0.0, static_cast<double>(peakDb));
        return;
    }

    // Interpolazione parabolica sui tre bin attorno al massimo: senza, la
    // lettura salta di venti hertz alla volta, e su una CW venti hertz sono la
    // differenza fra essere in nota e non esserlo.
    double offset = 0.0;
    if (peak > 0 && peak + 1 < bins) {
        const double left = positive[peak - 1];
        const double centre = positive[peak];
        const double right = positive[peak + 1];
        const double denom = left - 2.0 * centre + right;
        if (std::abs(denom) > 1e-6)
            offset = 0.5 * (left - right) / denom;
    }

    emit audioToneMeasured((peak + offset) * binWidth, static_cast<double>(peakDb));
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

    // Un backend server-DSP consegna audio reale invece di banda base
    // complessa. La differenza vive tutta in queste poche righe: da
    // `makeAnalytic` in poi il motore non sa più da dove sia arrivato il
    // flusso, e ogni stadio della SPEC-003 continua a valere.
    const bool audioSource = m_sourceIsAudio.load(std::memory_order_acquire);

    while (true) {
        const std::size_t availableFrames =
            audioSource ? source->available() : source->available() / 2;
        if (availableFrames == 0)
            break;

        const std::size_t frames = std::min(availableFrames, kProcessBlock);
        std::size_t count = 0;
        if (audioSource) {
            count = source->read(m_mono.data(), frames);
            if (count == 0)
                break;
            makeAnalytic(count);
        } else {
            const std::size_t got = source->read(m_interleaved.data(), frames * 2);
            count = got / 2;
            if (count == 0)
                break;
        }

        m_statsIqFrames += count;
        ++m_statsBlocks;

        // Tap di registrazione prima di qualunque elaborazione: su disco
        // finisce ciò che la radio ha consegnato, non ciò che il DSP ne ha
        // fatto. `feed()` non blocca e non alloca.
        // Su disco finisce il segnale analitico e non l'audio grezzo: è una
        // registrazione IQ della passata, riapribile con il backend `iqfile`
        // come qualunque altra.
        if (IqRecorder *recorder = m_recorder.load(std::memory_order_acquire))
            recorder->feed(m_interleaved.data(), count * 2);

        // ── Guardia contro la saturazione (SPEC-003 §3) ─────────────────
        //
        // Sui campioni appena arrivati, prima di tutto il resto: la
        // saturazione avviene nel convertitore, e osservarla dopo il blanker
        // — che gli impulsi li toglie — vorrebbe dire non vederla più.
        // Guarda sempre il presente, anche mentre si sta riascoltando il
        // passato: la radio continua a ricevere, e se è in saturazione adesso
        // è adesso che va detto.
        //
        // `Complex` è std::complex<float>, il cui contenuto è garantito
        // equivalente a due float in sequenza: l'array interleaved si può
        // leggere così com'è, senza copiarlo.
        m_overload.setMode(static_cast<dsp::OverloadGuard::Mode>(
            m_overloadMode.load(std::memory_order_acquire)));
        m_overload.feed(reinterpret_cast<const Complex *>(m_interleaved.data()), count);

        const double gainRequest = m_overload.takeRequestDb();
        const bool overloadedNow = m_overload.overloaded();
        m_overloaded.store(overloadedNow, std::memory_order_release);
        m_peakDbfs.store(m_overload.peakDbfs(), std::memory_order_release);

        // Si parla solo quando cambia qualcosa: la guardia chiude una finestra
        // dieci volte al secondo, e un segnale per finestra sarebbe rumore.
        if (overloadedNow != m_lastOverloadReported || gainRequest != 0.0) {
            m_lastOverloadReported = overloadedNow;
            emit overloadStateChanged(overloadedNow, m_overload.peakDbfs(), gainRequest);
        }

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
        // Con una sorgente audio il blanker non si applica (SPEC-004 §6 [B]):
        // gli impulsi arrivano già allargati dai filtri della radio, e a
        // quel punto toglierli vuol dire bucare il segnale insieme al
        // disturbo. Meglio non fare nulla che fare finta.
        if (!audioSource && m_nbEnabled.load(std::memory_order_acquire)) {
            m_blanker.setThreshold(m_nbThreshold.load(std::memory_order_acquire));
            m_blanker.process(m_iq.data(), toProcess);
            m_nbActivity.store(m_blanker.lastSuppressedRatio(), std::memory_order_release);
        }

        // ── Ramo spettro: tap in parallelo alla demodulazione (§5.1) ─────
        if (m_analyzer.push(m_iq.data(), toProcess))
            m_spectrum->publish(m_analyzer.magnitudesDb().data());

        // ── Ramo audio ──────────────────────────────────────────────────
        // `toProcess` e non `count`: durante il riascolto i campioni sono
        // quelli ripescati dalla memoria, e possono essere meno di quelli
        // appena arrivati.
        const std::size_t audioCapacity = static_cast<std::size_t>(std::ceil(
            static_cast<double>(toProcess) * kInternalAudioRate / m_activeRate));
        if (audioCapacity == 0)
            continue;

        std::fill_n(m_mix.begin(), audioCapacity * kAudioChannels, 0.0f);
        std::size_t audioFrames = 0;
        bool hasAudio = false;

        for (auto &[channelId, channel] : m_channels) {
            if (!channel.processor)
                continue;

            const std::size_t produced =
                channel.processor->processStereo(m_iq.data(), toProcess,
                                                 channel.audio.data());
            const std::size_t usable = std::min(produced, audioCapacity);
            for (std::size_t i = 0; i < usable * kAudioChannels; ++i)
                m_mix[i] += channel.audio[i];
            audioFrames = std::max(audioFrames, usable);
            hasAudio = hasAudio || usable > 0;

            if (!m_iqModules.empty() && channel.processor->lastBasebandFrames() > 0) {
                const std::size_t moduleFrames = std::min(
                    channel.processor->lastBasebandFrames(), kProcessBlock);
                const Complex *baseband = channel.processor->lastBaseband();
                for (std::size_t i = 0; i < moduleFrames; ++i) {
                    m_moduleIq[i * 2] = baseband[i].real();
                    m_moduleIq[i * 2 + 1] = baseband[i].imag();
                }
                for (const auto &loaded : m_iqModules) {
                    if (loaded && loaded->module && loaded->module->process_iq) {
                        loaded->module->process_iq(
                            loaded->module->user, channelId, m_moduleIq.data(),
                            moduleFrames, channel.processor->channelRate(),
                            m_centerHz.load(std::memory_order_acquire),
                            channel.settings.offsetHz);
                    }
                }
            }

            const qint64 now = m_uptime.nsecsElapsed();
            if (now - channel.lastMeterNs >= kMeterIntervalNs) {
                channel.lastMeterNs = now;
                emit metersUpdated(channelId,
                                   channel.processor->signalLevelDb(),
                                   channel.processor->noiseFloorDb(),
                                   channel.processor->snrDb(),
                                   channel.processor->audioLevelDb(),
                                   channel.processor->agcGainDb());
            }

            if (channel.settings.mode == DemodMode::Fm && channel.settings.fmRds
                && now - channel.lastRdsNs >= 250'000'000) {
                channel.lastRdsNs = now;
                const bool synced = channel.processor->rdsSynced();
                const QString pi = synced
                    ? QStringLiteral("%1").arg(channel.processor->rdsPiCode(), 4, 16,
                                                QChar('0')).toUpper()
                    : QString();
                const int countryCode = synced
                    ? static_cast<int>(channel.processor->rdsCountryCode()) : -1;
                const int programCoverage = synced
                    ? static_cast<int>(channel.processor->rdsProgramCoverage()) : -1;
                const int referenceNumber = synced
                    ? static_cast<int>(channel.processor->rdsProgramReferenceNumber()) : -1;
                const QString callsign = synced
                    ? QString::fromStdString(channel.processor->rdsCallsign()) : QString();
                const QString pty = synced
                    ? QString::fromStdString(channel.processor->rdsProgramType())
                    : QString();
                const QString af = synced
                    ? QString::fromStdString(channel.processor->rdsAlternateFrequencies())
                    : QString();
                const QString ps = QString::fromStdString(
                    channel.processor->rdsProgramService());
                const QString text = QString::fromStdString(
                    channel.processor->rdsRadioText());
                if (synced != channel.lastRdsSynced || pi != channel.lastRdsPi
                    || countryCode != channel.lastRdsCountryCode
                    || programCoverage != channel.lastRdsProgramCoverage
                    || referenceNumber != channel.lastRdsReferenceNumber
                    || callsign != channel.lastRdsCallsign
                    || pty != channel.lastRdsProgramType
                    || af != channel.lastRdsAlternateFrequencies
                    || ps != channel.lastRdsProgramService
                    || text != channel.lastRdsRadioText) {
                    channel.lastRdsSynced = synced;
                    channel.lastRdsPi = pi;
                    channel.lastRdsCountryCode = countryCode;
                    channel.lastRdsProgramCoverage = programCoverage;
                    channel.lastRdsReferenceNumber = referenceNumber;
                    channel.lastRdsCallsign = callsign;
                    channel.lastRdsProgramType = pty;
                    channel.lastRdsAlternateFrequencies = af;
                    channel.lastRdsProgramService = ps;
                    channel.lastRdsRadioText = text;
                    emit rdsUpdated(channelId, synced, pi, countryCode, programCoverage,
                                    referenceNumber, callsign, pty, af, ps, text);
                }
            }
        }

        // Se non c'è ancora un canale pronto, manteniamo l'uscita silenziosa
        // con la capacità temporale attesa. Con almeno un canale, invece,
        // scriviamo solo i campioni realmente prodotti: il ricampionatore
        // conserva la frazione residua fra un blocco e l'altro.
        if (!hasAudio)
            audioFrames = audioCapacity;

        m_statsAudioFrames += audioFrames;

        if (hasAudio) {
            for (std::size_t i = 0; i < audioFrames * kAudioChannels; ++i)
                m_mix[i] = std::clamp(m_mix[i], -1.0f, 1.0f);
        }

        // Se il consumatore audio è in ritardo scartiamo il campione più
        // vecchio: meglio un micro-salto che una latenza che cresce senza fine.
        const std::size_t audioSamples = audioFrames * kAudioChannels;
        // ── Ramo analisi dell'audio ─────────────────────────────────────
        //
        // Il tap sta qui, sul mix finale: è esattamente quello che esce dagli
        // altoparlanti — filtri del canale, AGC e riduzione di rumore
        // compresi. Prenderlo prima significherebbe mostrare un audio che
        // nessuno sta ascoltando.
        analyzeAudio(audioFrames);

        if (IqRecorder *recorder = m_audioRecorder.load(std::memory_order_acquire))
            recorder->feed(m_mix.data(), audioSamples);
        if (m_audioRing->space() < audioSamples)
            m_audioRing->discard(audioSamples - m_audioRing->space());
        m_audioRing->write(m_mix.data(), audioSamples);

        const qint64 now = m_uptime.nsecsElapsed();
        if (now - m_lastStatsNs >= 1'000'000'000) {
            const double seconds = static_cast<double>(now - m_lastStatsNs) / 1e9;
            // La stessa misura che finisce nel log va anche alla UI: dice se
            // il flusso regge, ed è l'unica cosa che su una sorgente di rete
            // distingue «la banda è vuota» da «i campioni non arrivano».
            emit streamRateMeasured(m_statsIqFrames / seconds, m_activeRate);

            qCDebug(dsdrDsp) << "flusso DSP:"
                             << (m_statsIqFrames / seconds) << "IQ/s"
                             << (m_statsAudioFrames / seconds) << "audio/s"
                             << "blocchi" << m_statsBlocks
                             << "ring audio" << (m_audioRing->available() / kAudioChannels)
                             << "stereo frames";
            for (const auto &[id, channel] : m_channels) {
                if (channel.processor)
                    qCDebug(dsdrDsp) << "  canale" << id
                                     << "RF" << channel.processor->signalLevelDb() << "dBFS"
                                     << "noise floor" << channel.processor->noiseFloorDb() << "dBFS"
                                     << "SNR" << channel.processor->snrDb() << "dB"
                                     << "audio" << channel.processor->audioLevelDb() << "dBFS"
                                     << "AGC gain" << channel.processor->agcGainDb() << "dB";
                if (channel.processor && channel.settings.ctcssEnabled)
                    qCDebug(dsdrDsp) << "    CTCSS"
                                     << channel.processor->ctcssLevelDb() << "dB"
                                     << (channel.processor->ctcssDetected() ? "detected" : "not detected");
                if (channel.processor && channel.settings.fmIfNoiseReductionEnabled)
                    qCDebug(dsdrDsp) << "    FM IF noise reduction preset"
                                     << channel.settings.fmIfNoiseReductionPreset;
                if (channel.processor && channel.settings.mode == DemodMode::Fm
                    && channel.settings.fmRds)
                    qCDebug(dsdrDsp) << "    RDS"
                                     << (channel.processor->rdsSynced() ? "sync" : "no sync")
                                     << "PI" << channel.processor->rdsPiCode()
                                     << "country" << channel.processor->rdsCountryCode()
                                     << "coverage" << channel.processor->rdsProgramCoverage()
                                     << "ref" << channel.processor->rdsProgramReferenceNumber()
                                     << "callsign" << QString::fromStdString(
                                            channel.processor->rdsCallsign())
                                     << "PTY" << QString::fromStdString(
                                            channel.processor->rdsProgramType())
                                     << "AF" << QString::fromStdString(
                                            channel.processor->rdsAlternateFrequencies())
                                     << "PS" << QString::fromStdString(
                                            channel.processor->rdsProgramService())
                                     << "RadioText" << QString::fromStdString(
                                            channel.processor->rdsRadioText());
            }
            m_lastStatsNs = now;
            m_statsIqFrames = 0;
            m_statsAudioFrames = 0;
            m_statsBlocks = 0;
        }
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

        // Anche il picco, con lo stesso contagocce: dentro il ciclo si parla
        // solo quando la saturazione comincia o finisce, ma su una banda
        // tranquilla — dove non cambia mai niente — la misura non arriverebbe
        // mai alla UI, e il numero mostrato resterebbe quello di fabbrica.
        emit overloadStateChanged(m_overload.overloaded(), m_overload.peakDbfs(), 0.0);
    }
}

} // namespace dsdr::core
