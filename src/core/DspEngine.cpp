// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/DspEngine.h"

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

/// Quanti campioni IQ si elaborano per giro. Coincide con il blocco massimo
/// dei ChannelProcessor: nessuna suddivisione ulteriore, nessuna allocazione.
constexpr std::size_t kProcessBlock = dsp::kMaxBlockFrames;

/// Intervallo minimo fra due emissioni di meter. L'occhio non distingue oltre
/// ~15 aggiornamenti al secondo, mentre ogni signal costa un attraversamento
/// di thread e, a valle, un dataChanged che rilancia le animazioni del delegate.
constexpr qint64 kMeterIntervalNs = 66'000'000; // ~15 Hz

} // namespace

DspEngine::DspEngine(QObject *parent)
    : QObject(parent)
    , m_audioRing(std::make_unique<dsp::SpscRing<float>>(kAudioRingFloats))
    , m_spectrum(new SpectrumFeed(this))
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
    m_sourceRate.store(sampleRate, std::memory_order_release);
    m_centerHz.store(centerFrequencyHz, std::memory_order_release);
    m_source.store(ring, std::memory_order_release);
    m_needsReconfigure.store(true, std::memory_order_release);
}

void DspEngine::clearSource()
{
    m_source.store(nullptr, std::memory_order_release);
    m_audioRing->clear();
}

void DspEngine::setCenterFrequency(qint64 hz)
{
    m_centerHz.store(hz, std::memory_order_release);
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

void DspEngine::reconfigure()
{
    m_activeRate = m_sourceRate.load(std::memory_order_acquire);
    if (m_activeRate <= 0.0)
        return;

    m_analyzer.configure(m_fftSize, m_activeRate);
    m_analyzer.setAveraging(0.5f);
    m_analyzer.setOverlap(0.5f);

    m_spectrum->configure(m_fftSize, m_activeRate, m_centerHz.load(std::memory_order_acquire));
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

    m_audioRing->clear();
    m_needsReconfigure.store(false, std::memory_order_release);
    m_lastStatsNs = m_uptime.nsecsElapsed();
    m_statsIqFrames = 0;
    m_statsAudioFrames = 0;
    m_statsBlocks = 0;

    qCInfo(dsdrDsp) << "DSP riconfigurato:" << m_activeRate << "Hz, FFT" << m_fftSize
                    << "canali:" << m_channels.size();
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

    while (true) {
        const std::size_t availableFrames = source->available() / 2;
        if (availableFrames == 0)
            break;

        const std::size_t frames = std::min(availableFrames, kProcessBlock);
        const std::size_t got = source->read(m_interleaved.data(), frames * 2);
        const std::size_t count = got / 2;
        if (count == 0)
            break;

        m_statsIqFrames += count;
        ++m_statsBlocks;

        // Tap di registrazione prima di qualunque elaborazione: su disco
        // finisce ciò che la radio ha consegnato, non ciò che il DSP ne ha
        // fatto. `feed()` non blocca e non alloca.
        if (IqRecorder *recorder = m_recorder.load(std::memory_order_acquire))
            recorder->feed(m_interleaved.data(), got);

        for (std::size_t i = 0; i < count; ++i)
            m_iq[i] = Complex(m_interleaved[i * 2], m_interleaved[i * 2 + 1]);

        // ── Ramo spettro: tap in parallelo alla demodulazione (§5.1) ─────
        if (m_analyzer.push(m_iq.data(), count))
            m_spectrum->publish(m_analyzer.magnitudesDb().data());

        // ── Ramo audio ──────────────────────────────────────────────────
        const std::size_t audioCapacity = static_cast<std::size_t>(std::ceil(
            static_cast<double>(count) * kInternalAudioRate / m_activeRate));
        if (audioCapacity == 0)
            continue;

        std::fill_n(m_mix.begin(), audioCapacity * kAudioChannels, 0.0f);
        std::size_t audioFrames = 0;
        bool hasAudio = false;

        for (auto &[channelId, channel] : m_channels) {
            if (!channel.processor)
                continue;

            const std::size_t produced =
                channel.processor->processStereo(m_iq.data(), count, channel.audio.data());
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
        if (IqRecorder *recorder = m_audioRecorder.load(std::memory_order_acquire))
            recorder->feed(m_mix.data(), audioSamples);
        if (m_audioRing->space() < audioSamples)
            m_audioRing->discard(audioSamples - m_audioRing->space());
        m_audioRing->write(m_mix.data(), audioSamples);

        const qint64 now = m_uptime.nsecsElapsed();
        if (now - m_lastStatsNs >= 1'000'000'000) {
            const double seconds = static_cast<double>(now - m_lastStatsNs) / 1e9;
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
                if (channel.processor && channel.settings.noiseBlankerEnabled)
                    qCDebug(dsdrDsp) << "    noise blanker impulsi"
                                     << channel.processor->noiseBlankedSamples();
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
}

} // namespace dsdr::core
