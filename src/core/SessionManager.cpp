// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/SessionManager.h"

#include "audio/AudioRouter.h"
#include "core/DspEngine.h"
#include "core/SpectrumFeed.h"
#include "hal/BackendRegistry.h"
#include "hal/IRadioBackend.h"

#include <QLoggingCategory>
#include <QThread>

#include <algorithm>

Q_LOGGING_CATEGORY(dsdrCore, "dsdr.core")

namespace dsdr::core {

namespace {

/// Larghezza di filtro predefinita per modo: sono i valori che un operatore si
/// aspetta di trovare già impostati aprendo la radio.
struct DefaultFilter
{
    int low;
    int high;
};

DefaultFilter defaultFilterFor(DemodMode mode)
{
    switch (mode) {
    case DemodMode::Cw:
    case DemodMode::Cwr:  return {-250, 250};
    case DemodMode::Am:
    case DemodMode::Sam:  return {-4000, 4000};
    case DemodMode::Fm:   return {-8000, 8000};
    case DemodMode::Nfm:  return {-6000, 6000};
    case DemodMode::DigU:
    case DemodMode::DigL: return {200, 3000};
    case DemodMode::Iq:   return {-6000, 6000};
    default:              return {300, 2700};
    }
}

} // namespace

SessionManager::SessionManager(QObject *parent)
    : QObject(parent)
    , m_audio(new audio::AudioRouter(this))
{
    hal::registerBuiltinBackends();
    qRegisterMetaType<dsp::ChannelSettings>("dsdr::dsp::ChannelSettings");
    qRegisterMetaType<ChannelId>("dsdr::ChannelId");

    // Il DSP vive su un thread proprio per tutta la durata della sessione:
    // crearlo e distruggerlo a ogni connessione moltiplicherebbe le occasioni
    // di sbagliare la sincronizzazione.
    m_engine = new DspEngine;
    m_dspThread = new QThread(this);
    m_dspThread->setObjectName(QStringLiteral("dsdr-dsp"));
    m_engine->moveToThread(m_dspThread);
    connect(m_dspThread, &QThread::finished, m_engine, &QObject::deleteLater);
    // HighPriority e non TimeCritical: un thread DSP che satura una CPU con
    // priorità time-critical affama il thread della UI, e l'applicazione
    // smette di rispondere pur continuando a produrre audio corretto.
    m_dspThread->start(QThread::HighPriority);

    connect(m_engine, &DspEngine::metersUpdated, this,
            [this](ChannelId id, float signalDb, float agcGainDb) {
                m_channels.updateMeters(id, signalDb, agcGainDb);
            });

    connect(m_engine, &DspEngine::overrunDetected, this, [this](quint64 lost) {
        setStatus(tr("Campioni persi: %1 — il DSP non sta al passo.").arg(lost));
    });

    // Il ritardo lo rilegge dal motore invece di ricordarsi quello chiesto: il
    // DSP lo accorcia quando la storia non arriva così indietro, e la UI deve
    // mostrare il tempo in cui si sta davvero ascoltando.
    connect(m_engine, &DspEngine::replayStateChanged, this,
            [this](double delaySeconds, double historySeconds) {
                if (qFuzzyCompare(m_replayDelay, delaySeconds)
                    && qFuzzyCompare(m_replayHistory, historySeconds))
                    return;
                m_replayDelay = delaySeconds;
                m_replayHistory = historySeconds;
                emit replayChanged();
            });

    connect(&m_recorder, &IqRecorder::failed, this, [this](const QString &message) {
        setStatus(message);
        emit errorReported(message, false);
    });

    // Backend di partenza: la radio, se questa compilazione la contempla.
    //
    // L'ordine di registrazione metteva primo il demo, e chi apre
    // l'applicazione con il ricevitore attaccato si trovava sui segnali
    // sintetici — dovendo passare dal pannello delle sorgenti prima di
    // ascoltare qualcosa di vero. Il demo resta la rete di sicurezza: se il
    // ColibriNANO non è compilato in questa build, si ricade sul primo
    // registrato, che è quello che accadeva sempre fino a ieri.
    //
    // Resta un default, non un vincolo: `--backend` e il pannello delle
    // sorgenti hanno l'ultima parola, e la scelta non è cablata sopra la HAL
    // (§4) perché passa per l'identificativo, non per il tipo.
    const QStringList ids = hal::BackendRegistry::instance().backendIds();
    if (ids.contains(QStringLiteral("colibri")))
        selectBackend(QStringLiteral("colibri"));
    else if (!ids.isEmpty())
        selectBackend(ids.first());
}

SessionManager::~SessionManager()
{
    disconnectDevice();
    teardownBackend();

    if (m_dspThread) {
        m_dspThread->quit();
        m_dspThread->wait();
    }
}

QVariantList SessionManager::availableBackends() const
{
    QVariantList list;
    for (const hal::BackendInfo &info : hal::BackendRegistry::instance().backends()) {
        QVariantMap entry;
        entry.insert(QStringLiteral("id"), info.id);
        entry.insert(QStringLiteral("name"), info.displayName);
        entry.insert(QStringLiteral("description"), info.description);
        list.append(entry);
    }
    return list;
}

QString SessionManager::backendName() const
{
    return hal::BackendRegistry::instance().info(m_backendId).displayName;
}

SpectrumFeed *SessionManager::spectrum() const
{
    return m_engine ? m_engine->spectrumFeed() : nullptr;
}

void SessionManager::setSpectrumAveraging(int frames)
{
    frames = std::clamp(frames, 1, SpectrumFeed::kMaxAveraging);
    if (m_spectrumAveraging == frames)
        return;

    m_spectrumAveraging = frames;
    // La scrittura verso il thread del DSP è un atomico, non una chiamata di
    // metodo: non serve accodarla, e accodarla vorrebbe dire farla arrivare
    // all'elaborazione dopo la prossima.
    if (SpectrumFeed *feed = spectrum())
        feed->setAveraging(frames);

    emit spectrumAveragingChanged();
}

void SessionManager::setStatus(const QString &message)
{
    if (m_statusMessage == message)
        return;
    m_statusMessage = message;
    emit statusMessageChanged();
}

void SessionManager::setDiscovering(bool discovering)
{
    if (m_discovering == discovering)
        return;
    m_discovering = discovering;
    emit discoveringChanged();
}

void SessionManager::teardownBackend()
{
    if (!m_backend)
        return;
    m_backend->close();
    m_backend->deleteLater();
    m_backend = nullptr;
}

void SessionManager::selectBackend(const QString &backendId)
{
    if (m_backendId == backendId && m_backend)
        return;

    disconnectDevice();
    teardownBackend();
    m_devices.clear();

    m_backend = hal::BackendRegistry::instance().create(backendId, this);
    if (!m_backend) {
        m_backendId.clear();
        setStatus(tr("Backend «%1» non disponibile in questa build.").arg(backendId));
        emit backendChanged();
        return;
    }

    m_backendId = backendId;
    m_capabilities.setCapabilities(m_backend->capabilities());

    connect(m_backend, &hal::IRadioBackend::deviceFound, this,
            [this](const hal::DeviceDescriptor &device) { m_devices.addDevice(device); });

    connect(m_backend, &hal::IRadioBackend::discoveryFinished, this, [this] {
        setDiscovering(false);
        setStatus(m_devices.rowCount() > 0
                      ? tr("%1 device trovati.").arg(m_devices.rowCount())
                      : tr("Nessun device trovato."));
    });

    connect(m_backend, &hal::IRadioBackend::capabilitiesChanged, this,
            [this] { m_capabilities.setCapabilities(m_backend->capabilities()); });

    connect(m_backend, &hal::IRadioBackend::errorOccurred, this, &SessionManager::onBackendError);

    connect(m_backend, &hal::IRadioBackend::pttChanged, this, [this](bool transmit) {
        if (m_transmitting == transmit)
            return;
        m_transmitting = transmit;
        emit transmittingChanged();
    });

    connect(m_backend, &hal::IRadioBackend::centerFrequencyChanged, this, [this](qint64 hz) {
        if (m_centerFrequency == hz)
            return;
        m_centerFrequency = hz;
        if (m_engine)
            m_engine->setCenterFrequency(hz);
        refreshChannelOffsets();
        emit centerFrequencyChanged();
    });

    // I frame IQ attraversano i thread: connessione queued verso il DSP, che
    // legge poi i campioni dal ring (§4.1).
    connect(m_backend, &hal::IRadioBackend::iqFrameReady,
            m_engine, &DspEngine::onIqFrameReady, Qt::QueuedConnection);

    emit backendChanged();
    setStatus(tr("Backend attivo: %1").arg(backendName()));
}

void SessionManager::onBackendError(const hal::BackendError &error)
{
    qCWarning(dsdrCore) << "backend error:" << error.message << error.detail;
    setStatus(error.message);
    emit errorReported(error.message, error.fatal);

    if (error.fatal)
        disconnectDevice();
}

void SessionManager::startDiscovery()
{
    if (!m_backend) {
        setStatus(tr("Nessun backend selezionato."));
        return;
    }
    m_devices.clear();
    setDiscovering(true);
    setStatus(tr("Ricerca device in corso…"));
    m_backend->startDiscovery();
}

void SessionManager::connectToDevice(int deviceRow)
{
    if (!m_backend)
        return;

    const hal::DeviceDescriptor *device = m_devices.at(deviceRow);
    if (!device) {
        setStatus(tr("Device non valido."));
        return;
    }

    disconnectDevice();

    m_backend->open(*device);
    if (!m_backend->isOpen()) {
        setStatus(tr("Apertura di %1 fallita.").arg(device->displayName));
        return;
    }

    m_capabilities.setCapabilities(m_backend->capabilities());
    m_deviceName = device->displayName;
    m_centerFrequency = m_backend->centerFrequency();
    m_sampleRate = m_backend->sampleRate();

    m_engine->setSource(m_backend->iqStream(), m_sampleRate, m_centerFrequency);
    m_audio->start(m_engine->audioRing());

    m_connected = true;
    emit connectionChanged();
    emit centerFrequencyChanged();
    emit sampleRateChanged();

    // Un canale pronto all'uso: aprire la radio e non sentire nulla sarebbe
    // un pessimo primo secondo di esperienza.
    if (m_channels.rowCount() == 0)
        addChannel(m_centerFrequency);

    setStatus(tr("Connesso a %1").arg(m_deviceName));
}

void SessionManager::disconnectDevice()
{
    if (!m_connected)
        return;

    // Una registrazione aperta va chiusa correttamente: il file resterebbe
    // senza dimensioni valide nell'intestazione.
    stopRecording();

    m_audio->stop();
    if (m_engine)
        m_engine->clearSource();

    if (m_backend) {
        for (int row = m_channels.rowCount() - 1; row >= 0; --row) {
            if (const ChannelEntry *entry = m_channels.at(row))
                m_backend->destroyRxChannel(entry->id);
        }
        m_backend->close();
    }

    if (m_engine) {
        for (int row = 0; row < m_channels.rowCount(); ++row) {
            if (const ChannelEntry *entry = m_channels.at(row))
                QMetaObject::invokeMethod(m_engine, "removeChannel", Qt::QueuedConnection,
                                          Q_ARG(dsdr::ChannelId, entry->id));
        }
    }

    m_channels.clear();
    m_connected = false;
    m_deviceName.clear();
    emit connectionChanged();
    setStatus(tr("Disconnesso."));
}

void SessionManager::setCenterFrequency(qint64 hz)
{
    if (!m_backend || m_centerFrequency == hz)
        return;
    m_backend->setCenterFrequency(hz);
}

void SessionManager::setSampleRate(double rate)
{
    if (!m_backend || qFuzzyCompare(m_sampleRate, rate))
        return;

    m_backend->setSampleRate(rate);
    m_sampleRate = m_backend->sampleRate();

    // Il worker del backend riparte con un ring nuovo: il DSP va riagganciato.
    m_engine->setSource(m_backend->iqStream(), m_sampleRate, m_centerFrequency);
    emit sampleRateChanged();
}

double SessionManager::replayCapacitySeconds() const
{
    return m_engine ? m_engine->historyCapacitySeconds() : 0.0;
}

void SessionManager::rewind(double seconds)
{
    if (!m_connected || !m_engine || !(seconds > 0.0))
        return;

    // Si riparte dal ritardo che il motore concede davvero, non da quello
    // chiesto l'ultima volta: premere «indietro» due volte quando la storia è
    // finita non deve accumulare un debito invisibile che poi fa saltare
    // l'ascolto la prima volta che la memoria si allunga.
    const double target = m_engine->replayDelaySeconds() + seconds;
    m_engine->setReplayDelaySeconds(target);
    m_replayDelay = m_engine->replayDelaySeconds();
    emit replayChanged();

    setStatus(m_replayDelay > 0.05
                  ? tr("In riascolto: %1 s indietro.").arg(m_replayDelay, 0, 'f', 0)
                  : tr("Non c'è ancora abbastanza registrato per tornare indietro."));
}

void SessionManager::setReplayDelay(double seconds)
{
    if (!m_connected || !m_engine)
        return;

    m_engine->setReplayDelaySeconds(std::max(0.0, seconds));
    m_replayDelay = m_engine->replayDelaySeconds();
    emit replayChanged();
}

void SessionManager::returnToLive()
{
    if (!m_engine)
        return;

    m_engine->setReplayDelaySeconds(0.0);
    m_replayDelay = 0.0;
    emit replayChanged();
    setStatus(tr("In diretta."));
}

void SessionManager::tuneTo(qint64 hz)
{
    if (!m_backend || !m_connected)
        return;

    setCenterFrequency(hz);

    // Il canale attivo segue la sintonia. Gli altri restano dove sono: sono
    // ricevitori indipendenti, ed è il motivo per cui esistono.
    const int current = m_channels.currentIndex();
    if (current >= 0)
        setChannelFrequency(current, hz);
    else
        addChannel(hz);
}

int SessionManager::addChannel(qint64 frequencyHz)
{
    if (!m_backend || !m_connected)
        return -1;

    if (m_channels.rowCount() >= m_capabilities.maxRxChannels()) {
        setStatus(tr("Questo device supporta al massimo %1 canali RX.")
                      .arg(m_capabilities.maxRxChannels()));
        return -1;
    }

    hal::RxChannelConfig config;
    config.frequencyHz = frequencyHz;
    config.mode = DemodMode::Usb;
    config.filterLowHz = 300;
    config.filterHighHz = 2700;

    const ChannelId id = m_backend->createRxChannel(config);
    if (id == kInvalidChannel)
        return -1;

    ChannelEntry entry;
    entry.id = id;
    entry.frequencyHz = frequencyHz;
    entry.color = m_channels.nextColor();
    entry.label = tr("RX %1").arg(m_channels.rowCount() + 1);
    entry.settings.offsetHz = static_cast<double>(frequencyHz - m_centerFrequency);
    entry.settings.mode = DemodMode::Usb;
    entry.settings.filterLowHz = 300;
    entry.settings.filterHighHz = 2700;
    entry.settings.agcMode = AgcMode::Medium;
    entry.settings.agcThresholdDb = -100.0;
    entry.settings.volume = 0.7f;

    const int row = m_channels.append(entry);

    QMetaObject::invokeMethod(m_engine, "addChannel", Qt::QueuedConnection,
                              Q_ARG(dsdr::ChannelId, id),
                              Q_ARG(dsdr::dsp::ChannelSettings, entry.settings));
    return row;
}

void SessionManager::removeChannel(int row)
{
    const ChannelEntry *entry = m_channels.at(row);
    if (!entry)
        return;

    const ChannelId id = entry->id;
    if (m_backend)
        m_backend->destroyRxChannel(id);
    QMetaObject::invokeMethod(m_engine, "removeChannel", Qt::QueuedConnection,
                              Q_ARG(dsdr::ChannelId, id));
    m_channels.removeAt(row);
}

void SessionManager::pushChannelToEngine(int row)
{
    const ChannelEntry *entry = m_channels.at(row);
    if (!entry || !m_engine)
        return;

    QMetaObject::invokeMethod(m_engine, "updateChannel", Qt::QueuedConnection,
                              Q_ARG(dsdr::ChannelId, entry->id),
                              Q_ARG(dsdr::dsp::ChannelSettings, entry->settings));
}

void SessionManager::refreshChannelOffsets()
{
    for (int row = 0; row < m_channels.rowCount(); ++row) {
        ChannelEntry *entry = m_channels.mutableAt(row);
        if (!entry)
            continue;
        entry->settings.offsetHz = static_cast<double>(entry->frequencyHz - m_centerFrequency);
        m_channels.entryChanged(row, {ChannelModel::OffsetRole});
        pushChannelToEngine(row);
    }
}

void SessionManager::setChannelFrequency(int row, qint64 hz)
{
    ChannelEntry *entry = m_channels.mutableAt(row);
    if (!entry || entry->frequencyHz == hz)
        return;

    entry->frequencyHz = hz;
    entry->settings.offsetHz = static_cast<double>(hz - m_centerFrequency);

    if (m_backend)
        m_backend->setFrequency(entry->id, hz);

    m_channels.entryChanged(row, {ChannelModel::FrequencyRole, ChannelModel::OffsetRole});
    pushChannelToEngine(row);
}

void SessionManager::nudgeChannel(int row, qint64 deltaHz)
{
    const ChannelEntry *entry = m_channels.at(row);
    if (!entry)
        return;
    setChannelFrequency(row, entry->frequencyHz + deltaHz);
}

void SessionManager::setChannelMode(int row, int mode)
{
    ChannelEntry *entry = m_channels.mutableAt(row);
    if (!entry)
        return;

    const DemodMode demod = static_cast<DemodMode>(mode);
    if (entry->settings.mode == demod)
        return;

    entry->settings.mode = demod;

    // Cambiando modo il filtro precedente quasi mai ha ancora senso: si
    // riparte dal valore tipico del nuovo modo, che l'utente può poi affinare.
    const DefaultFilter filter = defaultFilterFor(demod);
    entry->settings.filterLowHz = filter.low;
    entry->settings.filterHighHz = filter.high;

    if (m_backend) {
        m_backend->setDemod(entry->id, demod);
        m_backend->setFilter(entry->id, filter.low, filter.high);
    }

    m_channels.entryChanged(row, {ChannelModel::ModeRole, ChannelModel::ModeNameRole,
                                  ChannelModel::FilterLowRole, ChannelModel::FilterHighRole});
    pushChannelToEngine(row);
}

void SessionManager::setChannelFilter(int row, int lowHz, int highHz)
{
    ChannelEntry *entry = m_channels.mutableAt(row);
    if (!entry)
        return;

    if (lowHz > highHz)
        std::swap(lowHz, highHz);

    entry->settings.filterLowHz = lowHz;
    entry->settings.filterHighHz = highHz;

    if (m_backend)
        m_backend->setFilter(entry->id, lowHz, highHz);

    m_channels.entryChanged(row, {ChannelModel::FilterLowRole, ChannelModel::FilterHighRole});
    pushChannelToEngine(row);
}

void SessionManager::setChannelAgcMode(int row, int mode)
{
    ChannelEntry *entry = m_channels.mutableAt(row);
    if (!entry)
        return;
    entry->settings.agcMode = static_cast<AgcMode>(mode);
    m_channels.entryChanged(row, {ChannelModel::AgcModeRole});
    pushChannelToEngine(row);
}

void SessionManager::setChannelAgcThreshold(int row, double thresholdDb)
{
    ChannelEntry *entry = m_channels.mutableAt(row);
    if (!entry)
        return;
    entry->settings.agcThresholdDb = thresholdDb;
    m_channels.entryChanged(row, {ChannelModel::AgcThresholdRole});
    pushChannelToEngine(row);
}

void SessionManager::setChannelVolume(int row, double volume)
{
    ChannelEntry *entry = m_channels.mutableAt(row);
    if (!entry)
        return;
    entry->settings.volume = static_cast<float>(std::clamp(volume, 0.0, 1.0));
    m_channels.entryChanged(row, {ChannelModel::VolumeRole});
    pushChannelToEngine(row);
}

void SessionManager::setChannelMuted(int row, bool muted)
{
    ChannelEntry *entry = m_channels.mutableAt(row);
    if (!entry)
        return;
    entry->settings.muted = muted;
    m_channels.entryChanged(row, {ChannelModel::MutedRole});
    pushChannelToEngine(row);
}

void SessionManager::setChannelSquelch(int row, bool enabled, double thresholdDb)
{
    ChannelEntry *entry = m_channels.mutableAt(row);
    if (!entry)
        return;
    entry->settings.squelchEnabled = enabled;
    // Gli estremi sono quelli del cursore: fuori di lì la soglia o non chiude
    // mai o non apre mai, e in entrambi i casi il comando sembra rotto.
    entry->settings.squelchThresholdDb = std::clamp(thresholdDb, -140.0, -20.0);
    m_channels.entryChanged(row, {ChannelModel::SquelchEnabledRole,
                                  ChannelModel::SquelchThresholdRole});
    pushChannelToEngine(row);
}

void SessionManager::setNoiseBlanker(bool enabled, double threshold)
{
    if (!m_engine)
        return;

    // Il campo è quello della specifica: sotto 2 il blanker comincia a
    // scambiare per impulso il segnale stesso, sopra 8 non scatta più su nulla
    // e tanto vale spegnerlo.
    m_nbThreshold = std::clamp(threshold, 2.0, 8.0);
    m_nbEnabled = enabled;
    m_engine->setNoiseBlanker(m_nbEnabled, m_nbThreshold);
    emit noiseBlankerChanged();
}

double SessionManager::noiseBlankerActivity() const
{
    return m_engine ? static_cast<double>(m_engine->noiseBlankerActivity()) : 0.0;
}

void SessionManager::setChannelNoiseReduction(int row, bool enabled, double strength)
{
    ChannelEntry *entry = m_channels.mutableAt(row);
    if (!entry)
        return;
    entry->settings.nrEnabled = enabled;
    // Più in alto insegue meglio ma «respira» sul parlato: è il compromesso
    // che ogni operatore regola a orecchio, e per questo resta esposto.
    entry->settings.nrStrength = std::clamp(strength, 0.005, 0.3);
    m_channels.entryChanged(row, {ChannelModel::NrEnabledRole});
    pushChannelToEngine(row);
}

void SessionManager::setChannelAutoNotch(int row, bool enabled)
{
    ChannelEntry *entry = m_channels.mutableAt(row);
    if (!entry)
        return;
    entry->settings.anfEnabled = enabled;
    m_channels.entryChanged(row, {ChannelModel::AnfEnabledRole});
    pushChannelToEngine(row);
}

void SessionManager::setChannelNotch(int row, bool enabled, double frequencyHz,
                                     double widthHz)
{
    ChannelEntry *entry = m_channels.mutableAt(row);
    if (!entry)
        return;
    entry->settings.notchEnabled = enabled;
    entry->settings.notchFrequencyHz = std::clamp(frequencyHz, 100.0, 5000.0);
    entry->settings.notchWidthHz = std::clamp(widthHz, 20.0, 800.0);
    m_channels.entryChanged(row, {ChannelModel::NotchEnabledRole,
                                  ChannelModel::NotchFrequencyRole,
                                  ChannelModel::NotchWidthRole});
    pushChannelToEngine(row);
}

void SessionManager::setPtt(bool transmit)
{
    if (!m_backend)
        return;
    if (transmit && !m_capabilities.canTransmit()) {
        setStatus(tr("Questo device è solo in ricezione."));
        return;
    }
    m_backend->setPtt(transmit);
}

bool SessionManager::startRecording(const QString &path)
{
    if (!m_connected) {
        setStatus(tr("Nessuna sorgente connessa da registrare."));
        return false;
    }
    if (!m_capabilities.supportsRecording()) {
        setStatus(tr("Questa sorgente non consente la registrazione."));
        return false;
    }

    IqRecordingInfo info;
    info.centerFrequencyHz = m_centerFrequency;
    info.sampleRate = m_sampleRate;
    info.backendId = m_backendId;
    info.deviceName = m_deviceName;
    info.startedAt = QDateTime::currentDateTime();

    if (!m_recorder.start(info, path))
        return false;

    // Il tap si attacca solo a registrazione avviata: prima il writer non
    // sarebbe pronto e il thread DSP riempirebbe il ring nel vuoto.
    if (m_engine)
        m_engine->setRecorder(&m_recorder);

    setStatus(tr("Registrazione in corso: %1").arg(m_recorder.currentFile()));
    return true;
}

void SessionManager::stopRecording()
{
    if (!m_recorder.isRecording())
        return;

    // Prima si stacca il tap, poi si chiude: al contrario il writer potrebbe
    // ricevere campioni a file già chiuso.
    if (m_engine)
        m_engine->setRecorder(nullptr);
    m_recorder.stop();

    setStatus(tr("Registrazione salvata: %1").arg(m_recorder.currentFile()));
}

bool SessionManager::toggleRecording()
{
    if (m_recorder.isRecording()) {
        stopRecording();
        return false;
    }
    return startRecording();
}

bool SessionManager::addRemoteEndpoint(const QString &endpoint)
{
    const QString trimmed = endpoint.trimmed();
    if (trimmed.isEmpty())
        return false;

    if (!m_backend || !m_capabilities.remoteCapable()) {
        setStatus(tr("Questa sorgente non accetta indirizzi di rete."));
        return false;
    }

    const QVariant result = m_backend->nativeCommand(
        QStringLiteral("net.addEndpoint"), {{QStringLiteral("endpoint"), trimmed}});
    if (!result.isValid()) {
        setStatus(tr("Il backend non ha accettato l'indirizzo %1.").arg(trimmed));
        return false;
    }

    setStatus(tr("Indirizzo %1 aggiunto: avvia la ricerca.").arg(trimmed));
    return true;
}

QStringList SessionManager::remoteEndpoints() const
{
    if (!m_backend || !m_capabilities.remoteCapable())
        return {};
    return m_backend->nativeCommand(QStringLiteral("net.endpoints"), {}).toStringList();
}

QStringList SessionManager::modeNames() const
{
    static const QList<DemodMode> order = {
        DemodMode::Usb, DemodMode::Lsb, DemodMode::Cw, DemodMode::Cwr,
        DemodMode::Am, DemodMode::Sam, DemodMode::Fm, DemodMode::Nfm,
        DemodMode::DigU, DemodMode::DigL, DemodMode::Iq,
    };
    QStringList names;
    names.reserve(order.size());
    for (DemodMode mode : order)
        names.append(demodModeName(mode));
    return names;
}

QStringList SessionManager::agcModeNames() const
{
    return {tr("Off"), tr("Fast"), tr("Medium"), tr("Slow"), tr("Long")};
}

QVariant SessionManager::nativeCommand(const QString &command, const QVariantMap &args)
{
    if (!m_backend)
        return QVariant();
    return m_backend->nativeCommand(command, args);
}

} // namespace dsdr::core
