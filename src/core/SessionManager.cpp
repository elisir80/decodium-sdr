// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/SessionManager.h"

#include "audio/AudioRouter.h"
#include "core/DspEngine.h"
#include "core/SpectrumFeed.h"
#include "hal/BackendRegistry.h"
#include "hal/IRadioBackend.h"

#include <QLoggingCategory>
#include <QCoreApplication>
#include <QDir>
#include <QHostAddress>
#include <QStandardPaths>
#include <QThread>
#include <QTcpSocket>

#include <algorithm>
#include <cmath>

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
    case DemodMode::Dsb:  return {-2300, 2300};
    case DemodMode::Fm:   return {-90000, 90000};
    case DemodMode::Nfm:  return {-6000, 6000};
    case DemodMode::DigU:
    case DemodMode::DigL: return {200, 3000};
    case DemodMode::Iq:   return {-6000, 6000};
    default:              return {300, 2700};
    }
}

QString rigctlModeName(DemodMode mode)
{
    switch (mode) {
    case DemodMode::Fm:   return QStringLiteral("WFM");
    case DemodMode::Nfm:  return QStringLiteral("NFM");
    case DemodMode::Sam:  return QStringLiteral("SAM");
    case DemodMode::Am:   return QStringLiteral("AM");
    case DemodMode::Dsb:  return QStringLiteral("DSB");
    case DemodMode::Cw:
    case DemodMode::Cwr:  return QStringLiteral("CW");
    case DemodMode::Usb:  return QStringLiteral("USB");
    case DemodMode::Lsb:  return QStringLiteral("LSB");
    case DemodMode::DigU: return QStringLiteral("DIGU");
    case DemodMode::DigL: return QStringLiteral("DIGL");
    case DemodMode::Iq:   return QStringLiteral("IQ");
    }
    return QStringLiteral("USB");
}

std::vector<qint64> parseRdsAfFrequencies(const QString &values)
{
    std::vector<qint64> result;
    for (const QString &value : values.split(QStringLiteral(","), Qt::SkipEmptyParts)) {
        const QString number = value.trimmed().section(QLatin1Char(' '), 0, 0);
        bool ok = false;
        const double frequencyMHz = number.toDouble(&ok);
        if (!ok || frequencyMHz < 50.0 || frequencyMHz > 2000.0)
            continue;
        const qint64 frequencyHz = qRound64(frequencyMHz * 1'000'000.0);
        if (std::find(result.begin(), result.end(), frequencyHz) == result.end())
            result.push_back(frequencyHz);
    }
    return result;
}

bool demodModeFromRigctl(const QString &value, DemodMode &mode)
{
    const QString upper = value.trimmed().toUpper();
    if (upper == QStringLiteral("WFM") || upper == QStringLiteral("FM"))
        mode = DemodMode::Fm;
    else if (upper == QStringLiteral("NFM"))
        mode = DemodMode::Nfm;
    else if (upper == QStringLiteral("SAM"))
        mode = DemodMode::Sam;
    else if (upper == QStringLiteral("AM"))
        mode = DemodMode::Am;
    else if (upper == QStringLiteral("DSB"))
        mode = DemodMode::Dsb;
    else if (upper == QStringLiteral("CW"))
        mode = DemodMode::Cw;
    else if (upper == QStringLiteral("LSB"))
        mode = DemodMode::Lsb;
    else if (upper == QStringLiteral("DIGL"))
        mode = DemodMode::DigL;
    else if (upper == QStringLiteral("DIGU"))
        mode = DemodMode::DigU;
    else if (upper == QStringLiteral("IQ"))
        mode = DemodMode::Iq;
    else if (upper == QStringLiteral("USB"))
        mode = DemodMode::Usb;
    else
        return false;
    return true;
}

} // namespace

SessionManager::SessionManager(QObject *parent)
    : QObject(parent)
    , m_audio(new audio::AudioRouter(this))
    , m_scanTimer(this)
    , m_rigctlServer(this)
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
            [this](ChannelId id, float signalDb, float noiseFloorDb,
                   float snrDb, float audioLevelDb, float agcGainDb) {
                m_channels.updateMeters(id, signalDb, noiseFloorDb, snrDb,
                                        audioLevelDb, agcGainDb);
                if (!m_scanning || m_scanRow < 0)
                    return;
                const ChannelEntry *entry = m_channels.at(m_scanRow);
                if (!entry || entry->id != id || signalDb < m_scanThresholdDb
                    || entry->frequencyHz != m_scanFrequency
                    || m_scanLastHit == m_scanFrequency)
                    return;

                QVariantMap result;
                result.insert(QStringLiteral("frequencyHz"), m_scanFrequency);
                result.insert(QStringLiteral("signalDb"), signalDb);
                m_scanResults.append(result);
                m_scanLastHit = m_scanFrequency;
                emit scanResultsChanged();
                qCInfo(dsdrCore) << "scanner: segnale a" << m_scanFrequency
                                 << "Hz" << signalDb << "dBFS";
            });

    connect(&m_scanTimer, &QTimer::timeout, this, &SessionManager::advanceScan);

    m_rdsAfProbeTimer.setSingleShot(true);
    connect(&m_rdsAfProbeTimer, &QTimer::timeout, this, [this] {
        finishRdsAfProbe(false);
    });

    connect(&m_rigctlServer, &QTcpServer::newConnection, this, [this] {
        while (m_rigctlServer.hasPendingConnections()) {
            QTcpSocket *socket = m_rigctlServer.nextPendingConnection();
            if (!socket)
                continue;
            connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
                while (socket->canReadLine())
                    handleRigctlLine(socket, socket->readLine());
            });
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            qCInfo(dsdrCore) << "rigctl: client connesso da" << socket->peerAddress();
        }
    });

    connect(m_engine, &DspEngine::rdsUpdated, this,
            [this](ChannelId id, bool synced, const QString &pi,
                   int countryCode, int programCoverage, int referenceNumber,
                   const QString &callsign,
                   const QString &programType, const QString &alternateFrequencies,
                   const QString &programService, const QString &radioText) {
                m_channels.updateRds(id, synced, pi, countryCode, programCoverage,
                                     referenceNumber, callsign, programType,
                                     alternateFrequencies, programService, radioText);
                handleAutomaticRdsAf(id, synced, pi);
            });

    connect(m_engine, &DspEngine::overrunDetected, this, [this](quint64 lost) {
        setStatus(tr("Campioni persi: %1 — il DSP non sta al passo.").arg(lost));
    });

    connect(&m_recorder, &IqRecorder::failed, this, [this](const QString &message) {
        setStatus(message);
        emit errorReported(message, false);
    });
    connect(&m_audioRecorder, &IqRecorder::failed, this, [this](const QString &message) {
        setStatus(message);
        emit errorReported(message, false);
    });

    const QStringList ids = hal::BackendRegistry::instance().backendIds();
    if (!ids.isEmpty())
        selectBackend(ids.first());
    startRigctl();
}

SessionManager::~SessionManager()
{
    stopScan();
    stopRigctl();
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
    const hal::BackendCapabilities &caps = m_capabilities.raw();
    qCInfo(dsdrCore) << "backend selezionato:" << backendId
                     << "display:" << m_backend->displayName()
                     << "clientDemod:" << (caps.demod == DspLocation::Client)
                     << "clientAgc:" << (caps.agc == DspLocation::Client)
                     << "sample rates:" << caps.sampleRates;

    connect(m_backend, &hal::IRadioBackend::deviceFound, this,
            [this](const hal::DeviceDescriptor &device) {
                qCDebug(dsdrCore) << "device trovato:" << device.displayName
                                  << "id:" << device.deviceId
                                  << "backend:" << device.backendId;
                m_devices.addDevice(device);
            });

    connect(m_backend, &hal::IRadioBackend::discoveryFinished, this, [this] {
        qCInfo(dsdrCore) << "discovery terminata, device:" << m_devices.rowCount();
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
    qCInfo(dsdrCore) << "avvio discovery backend:" << m_backend->backendId();
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

    qCInfo(dsdrCore) << "apertura device:" << device->displayName
                     << "id:" << device->deviceId;
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
    const bool audioStarted = m_audio->start(m_engine->audioRing());
    qCInfo(dsdrCore) << "device pronto:" << m_deviceName
                     << "center:" << m_centerFrequency
                     << "sample rate:" << m_sampleRate
                     << "audio active:" << audioStarted;

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
    stopScan();

    // Un cambio backend o una chiusura provocata da errore non deve lasciare
    // il PTT logico acceso. Se il backend è ancora in TX, lo riportiamo in RX
    // prima di chiudere il device; poi sincronizziamo sempre la proprietà QML.
    if (m_backend && m_backend->ptt())
        m_backend->setPtt(false);
    if (m_transmitting) {
        m_transmitting = false;
        emit transmittingChanged();
    }
    if (!m_connected)
        return;

    // Una registrazione aperta va chiusa correttamente: il file resterebbe
    // senza dimensioni valide nell'intestazione.
    stopRecording();
    stopAudioRecording();

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

bool SessionManager::startScan(qint64 startHz, qint64 endHz, qint64 stepHz, int dwellMs)
{
    if (!m_connected || m_channels.rowCount() == 0)
        return false;
    if (startHz > endHz)
        std::swap(startHz, endHz);
    stepHz = std::abs(stepHz);
    if (stepHz <= 0 || startHz == endHz)
        return false;

    stopScan();
    m_scanRow = m_channels.currentIndex();
    if (m_scanRow < 0)
        m_scanRow = 0;
    m_scanFrequency = startHz;
    m_scanEnd = endHz;
    m_scanStep = stepHz;
    m_scanLastHit = -1;
    m_scanThresholdDb = -75.0;
    m_scanResults.clear();
    emit scanResultsChanged();

    m_scanning = true;
    emit scanningChanged();
    setChannelFrequency(m_scanRow, m_scanFrequency);
    m_scanTimer.start(std::clamp(dwellMs, 150, 5000));
    setStatus(tr("Scansione in corso: %1–%2 Hz…").arg(startHz).arg(endHz));
    qCInfo(dsdrCore) << "scanner avviato:" << startHz << endHz
                     << "passo" << stepHz << "dwell" << m_scanTimer.interval() << "ms";
    return true;
}

void SessionManager::stopScan()
{
    if (!m_scanning && !m_scanTimer.isActive())
        return;
    m_scanTimer.stop();
    m_scanning = false;
    m_scanRow = -1;
    emit scanningChanged();
    qCInfo(dsdrCore) << "scanner arrestato, risultati:" << m_scanResults.size();
}

void SessionManager::advanceScan()
{
    if (!m_scanning)
        return;

    const qint64 next = m_scanFrequency + m_scanStep;
    if (next > m_scanEnd) {
        m_scanTimer.stop();
        m_scanning = false;
        m_scanRow = -1;
        emit scanningChanged();
        setStatus(tr("Scansione completata: %1 segnali trovati.").arg(m_scanResults.size()));
        qCInfo(dsdrCore) << "scanner completato, risultati:" << m_scanResults.size();
        return;
    }

    m_scanFrequency = next;
    m_scanLastHit = -1;
    setChannelFrequency(m_scanRow, m_scanFrequency);
}

bool SessionManager::startRigctl(int port)
{
    if (port < 1 || port > 65535)
        return false;
    if (m_rigctlServer.isListening() && m_rigctlServer.serverPort() == port)
        return true;
    if (m_rigctlServer.isListening())
        stopRigctl();

    if (!m_rigctlServer.listen(QHostAddress::LocalHost, static_cast<quint16>(port))) {
        qCWarning(dsdrCore) << "rigctl: impossibile aprire la porta" << port
                            << m_rigctlServer.errorString();
        emit rigctlChanged();
        return false;
    }
    qCInfo(dsdrCore) << "rigctl: server locale in ascolto su 127.0.0.1:"
                     << m_rigctlServer.serverPort();
    emit rigctlChanged();
    return true;
}

void SessionManager::stopRigctl()
{
    if (!m_rigctlServer.isListening())
        return;
    const auto sockets = m_rigctlServer.findChildren<QTcpSocket *>();
    for (QTcpSocket *socket : sockets) {
        socket->disconnectFromHost();
        socket->deleteLater();
    }
    m_rigctlServer.close();
    qCInfo(dsdrCore) << "rigctl: server arrestato";
    emit rigctlChanged();
}

void SessionManager::handleRigctlLine(QTcpSocket *socket, const QByteArray &line)
{
    if (!socket)
        return;

    const QByteArray command = line.trimmed();
    const int row = m_channels.currentIndex();
    const ChannelEntry *entry = m_channels.at(row);
    const qint64 frequency = entry ? entry->frequencyHz : m_centerFrequency;

    if (command == "f") {
        socket->write(QByteArray::number(frequency) + '\n');
        return;
    }
    if (command == "m") {
        const DemodMode mode = entry ? entry->settings.mode : DemodMode::Usb;
        const int passband = entry
            ? std::abs(entry->settings.filterHighHz - entry->settings.filterLowHz)
            : 2400;
        socket->write(rigctlModeName(mode).toLatin1() + '\n'
                      + QByteArray::number(passband) + '\n');
        return;
    }
    if (command == "t") {
        socket->write(QByteArray::number(m_backend && m_backend->ptt() ? 1 : 0) + '\n');
        return;
    }
    if (command == "l AF") {
        socket->write(QByteArray::number(static_cast<double>(m_audio->volume()), 'f', 6)
                      + '\n');
        return;
    }
    if (command == "q") {
        socket->write("RPRT 0\n");
        socket->disconnectFromHost();
        return;
    }
    if (command == "\\dump_state") {
        socket->write("0\n" + QByteArray::number(frequency) + "\n"
                      + (entry ? rigctlModeName(entry->settings.mode).toLatin1()
                               : QByteArray("USB"))
                      + "\n");
        return;
    }

    const QList<QByteArray> parts = command.simplified().split(' ');
    if (parts.size() >= 2 && parts.at(0) == "F") {
        bool ok = false;
        const qint64 hz = parts.at(1).toLongLong(&ok);
        if (!ok || hz <= 0) {
            socket->write("RPRT -1\n");
            return;
        }
        if (row >= 0)
            setChannelFrequency(row, hz);
        else
            setCenterFrequency(hz);
        socket->write("RPRT 0\n");
        return;
    }

    if (parts.size() >= 2 && parts.at(0) == "M") {
        DemodMode mode;
        if (!demodModeFromRigctl(QString::fromLatin1(parts.at(1)), mode) || row < 0) {
            socket->write("RPRT -1\n");
            return;
        }
        setChannelMode(row, static_cast<int>(mode));
        if (parts.size() >= 3) {
            bool ok = false;
            const int passband = parts.at(2).toInt(&ok);
            if (ok && passband > 0) {
                const int half = passband / 2;
                setChannelFilter(row, -half, half);
            }
        }
        socket->write("RPRT 0\n");
        return;
    }

    if (parts.size() >= 2 && parts.at(0) == "T") {
        bool ok = false;
        const int transmit = parts.at(1).toInt(&ok);
        if (!ok || (transmit != 0 && transmit != 1)) {
            socket->write("RPRT -1\n");
            return;
        }
        if (transmit != 0 && (!m_backend || !m_capabilities.canTransmit())) {
            qCWarning(dsdrCore) << "rigctl: PTT richiesto ma il backend è solo RX";
            setStatus(tr("Questo device è solo in ricezione."));
            socket->write("RPRT -4\n");
            return;
        }
        setPtt(transmit != 0);
        qCInfo(dsdrCore) << "rigctl: PTT" << (transmit != 0 ? "ON" : "OFF");
        socket->write("RPRT 0\n");
        return;
    }

    if (parts.size() >= 3 && parts.at(0) == "L"
        && parts.at(1).compare("AF", Qt::CaseInsensitive) == 0) {
        bool ok = false;
        const double volume = parts.at(2).toDouble(&ok);
        if (!ok || volume < 0.0 || volume > 1.0) {
            socket->write("RPRT -1\n");
            return;
        }
        m_audio->setVolume(static_cast<float>(volume));
        qCInfo(dsdrCore) << "rigctl: volume AF" << volume;
        socket->write("RPRT 0\n");
        return;
    }

    socket->write("RPRT -4\n");
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
    qCInfo(dsdrCore) << "sample rate richiesto:" << rate << "effettivo:" << m_sampleRate;

    // Il worker del backend riparte con un ring nuovo: il DSP va riagganciato.
    m_engine->setSource(m_backend->iqStream(), m_sampleRate, m_centerFrequency);
    emit sampleRateChanged();
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

    qCInfo(dsdrCore) << "canale aggiunto:" << entry.label
                     << "id:" << id
                     << "frequency:" << frequencyHz
                     << "mode:" << demodModeName(entry.settings.mode)
                     << "filter:" << entry.settings.filterLowHz << entry.settings.filterHighHz;

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

    // Come il tuner di SDR++, quando un VFO viene portato vicino al bordo
    // della banda IQ spostiamo il centro del device per non lasciare il
    // canale fuori dal campionamento. Il margine del 20% evita retune
    // continui mentre si trascina il VFO.
    if (m_backend && m_sampleRate > 0.0) {
        const double distance = std::abs(static_cast<double>(hz - m_centerFrequency));
        if (distance > m_sampleRate * 0.40) {
            qCInfo(dsdrCore) << "auto-retune centro per VFO:" << hz
                             << "distanza:" << distance
                             << "sample rate:" << m_sampleRate;
            m_backend->setCenterFrequency(hz);
        }
    }

    entry->frequencyHz = hz;
    entry->settings.offsetHz = static_cast<double>(hz - m_centerFrequency);

    if (m_backend)
        m_backend->setFrequency(entry->id, hz);

    m_channels.entryChanged(row, {ChannelModel::FrequencyRole, ChannelModel::OffsetRole});
    m_channels.updateRds(entry->id, false, QString(), -1, -1, -1, QString(),
                         QString(), QString(), QString(), QString());
    pushChannelToEngine(row);
}

bool SessionManager::loadIqModule(const QString &path)
{
    if (!m_engine || path.isEmpty())
        return false;

    bool loaded = false;
    if (QThread::currentThread() == m_engine->thread()) {
        loaded = m_engine->loadIqModule(path);
    } else {
        QMetaObject::invokeMethod(m_engine, "loadIqModule",
                                  Qt::BlockingQueuedConnection,
                                  Q_RETURN_ARG(bool, loaded),
                                  Q_ARG(QString, path));
    }
    if (loaded) {
        qCInfo(dsdrCore) << "modulo IQ attivo:" << path;
        QStringList names;
        if (QThread::currentThread() == m_engine->thread()) {
            names = m_engine->iqModuleNames();
        } else {
            QMetaObject::invokeMethod(m_engine, "iqModuleNames",
                                      Qt::BlockingQueuedConnection,
                                      Q_RETURN_ARG(QStringList, names));
        }
        if (names != m_iqModuleNames) {
            m_iqModuleNames = names;
            emit iqModuleNamesChanged();
        }

        const QString absolutePath = QFileInfo(path).absoluteFilePath();
        bool catalogUpdated = false;
        for (QVariant &item : m_iqModuleCatalog) {
            QVariantMap module = item.toMap();
            if (module.value(QStringLiteral("path")).toString() != absolutePath)
                continue;
            module.insert(QStringLiteral("loaded"), true);
            item = module;
            catalogUpdated = true;
            break;
        }
        if (!catalogUpdated) {
            QVariantMap module;
            module.insert(QStringLiteral("name"),
                          names.isEmpty() ? QFileInfo(path).completeBaseName()
                                          : names.back());
            module.insert(QStringLiteral("path"), absolutePath);
            module.insert(QStringLiteral("loaded"), true);
            m_iqModuleCatalog.append(module);
        }
        emit iqModuleCatalogChanged();
    }
    return loaded;
}

void SessionManager::unloadIqModules()
{
    if (!m_engine)
        return;
    if (QThread::currentThread() == m_engine->thread()) {
        m_engine->unloadIqModules();
    } else {
        QMetaObject::invokeMethod(m_engine, "unloadIqModules", Qt::BlockingQueuedConnection);
    }
    if (!m_iqModuleNames.isEmpty()) {
        m_iqModuleNames.clear();
        emit iqModuleNamesChanged();
    }
    if (!m_iqModuleCatalog.isEmpty()) {
        m_iqModuleCatalog.clear();
        emit iqModuleCatalogChanged();
    }
}

void SessionManager::loadIqModulesFromStandardPaths()
{
    QStringList directories;
    const QDir appDir(QCoreApplication::applicationDirPath());
    directories.append(appDir.absoluteFilePath(QStringLiteral("../PlugIns/DecodiumSdr")));
    directories.append(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                       + QStringLiteral("/modules"));

    QStringList filters;
#if defined(Q_OS_MACOS)
    filters << QStringLiteral("*.dylib") << QStringLiteral("*.so");
#elif defined(Q_OS_WIN)
    filters << QStringLiteral("*.dll");
#else
    filters << QStringLiteral("*.so");
#endif

    for (const QString &directoryPath : directories) {
        const QDir directory(directoryPath);
        if (!directory.exists())
            continue;
        const QFileInfoList modules = directory.entryInfoList(
            filters, QDir::Files | QDir::Readable, QDir::Name);
        for (const QFileInfo &module : modules)
            loadIqModule(module.absoluteFilePath());
    }
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

    if (m_rdsAfProbeActive && m_rdsAfProbeRow == row)
        finishRdsAfProbe(false);

    const DemodMode demod = static_cast<DemodMode>(mode);
    if (entry->settings.mode == demod)
        return;

    entry->settings.mode = demod;

    // Cambiando modo il filtro precedente quasi mai ha ancora senso: si
    // riparte dal valore tipico del nuovo modo, che l'utente può poi affinare.
    const DefaultFilter filter = defaultFilterFor(demod);
    entry->settings.filterLowHz = filter.low;
    entry->settings.filterHighHz = filter.high;
    // Proprietà della catena radio: stereo e de-enfasi appartengono alla FM,
    // non devono rimanere accidentalmente attive passando a USB/AM/CW.
    if (demod == DemodMode::Fm) {
        entry->settings.fmStereo = true;
        entry->settings.fmAudioLowPass = true;
        entry->settings.fmDeemphasisUs = 50.0;
        entry->settings.fmRds = true;
        entry->settings.fmIfNoiseReductionEnabled = false;
        entry->settings.ctcssEnabled = false;
        entry->settings.ctcssDecodeOnly = false;
        entry->settings.noiseBlankerEnabled = false;
    } else if (demod == DemodMode::Nfm) {
        entry->settings.fmStereo = false;
        entry->settings.fmAudioLowPass = true;
        entry->settings.fmDeemphasisUs = 75.0;
        entry->settings.fmRds = false;
        entry->settings.fmIfNoiseReductionEnabled = false;
        entry->settings.ctcssEnabled = false;
        entry->settings.ctcssDecodeOnly = false;
        entry->settings.noiseBlankerEnabled = false;
    } else {
        entry->settings.fmStereo = false;
        entry->settings.fmAudioLowPass = false;
        entry->settings.fmDeemphasisUs = 0.0;
        entry->settings.fmRds = false;
        entry->settings.fmIfNoiseReductionEnabled = false;
        entry->settings.ctcssEnabled = false;
        entry->settings.ctcssDecodeOnly = false;
        entry->settings.noiseBlankerEnabled = false;
    }

    qCInfo(dsdrCore) << "canale" << entry->id << "modo:" << demodModeName(demod)
                     << "filtro predefinito:" << filter.low << filter.high;

    if (m_backend) {
        m_backend->setDemod(entry->id, demod);
        m_backend->setFilter(entry->id, filter.low, filter.high);
    }

    m_channels.entryChanged(row, {ChannelModel::ModeRole, ChannelModel::ModeNameRole,
                                  ChannelModel::FilterLowRole, ChannelModel::FilterHighRole,
                                  ChannelModel::FmStereoRole,
                                  ChannelModel::FmDeemphasisRole,
                                  ChannelModel::FmRdsRole,
                                  ChannelModel::RdsRegionRole,
                                  ChannelModel::RdsSyncedRole,
                                  ChannelModel::RdsPiRole,
                                  ChannelModel::RdsCountryCodeRole,
                                  ChannelModel::RdsProgramCoverageRole,
                                  ChannelModel::RdsReferenceNumberRole,
                                  ChannelModel::RdsCallsignRole,
                                  ChannelModel::RdsProgramServiceRole,
                                  ChannelModel::RdsRadioTextRole,
                                  ChannelModel::CtcssEnabledRole,
                                  ChannelModel::CtcssDecodeOnlyRole,
                                  ChannelModel::CtcssToneRole,
                                  ChannelModel::NoiseBlankerEnabledRole,
                                  ChannelModel::NoiseBlankerThresholdRole,
                                  ChannelModel::FmIfNoiseReductionEnabledRole,
                                  ChannelModel::FmIfNoiseReductionPresetRole});
    m_channels.updateRds(entry->id, false, QString(), -1, -1, -1, QString(),
                         QString(), QString(), QString(), QString());
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

    qCDebug(dsdrCore) << "canale" << entry->id << "filtro:" << lowHz << highHz;

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
    qCDebug(dsdrCore) << "canale" << entry->id << "AGC:" << mode;
    m_channels.entryChanged(row, {ChannelModel::AgcModeRole});
    pushChannelToEngine(row);
}

void SessionManager::setChannelAgcThreshold(int row, double thresholdDb)
{
    ChannelEntry *entry = m_channels.mutableAt(row);
    if (!entry)
        return;
    entry->settings.agcThresholdDb = thresholdDb;
    qCDebug(dsdrCore) << "canale" << entry->id << "AGC-T:" << thresholdDb << "dB";
    m_channels.entryChanged(row, {ChannelModel::AgcThresholdRole});
    pushChannelToEngine(row);
}

void SessionManager::setChannelAgcAttack(int row, double milliseconds)
{
    ChannelEntry *entry = m_channels.mutableAt(row);
    if (!entry)
        return;
    entry->settings.agcAttackMs = std::clamp(milliseconds, 0.1, 5000.0);
    qCDebug(dsdrCore) << "canale" << entry->id << "AGC attack:"
                      << entry->settings.agcAttackMs << "ms";
    m_channels.entryChanged(row, {ChannelModel::AgcAttackRole});
    pushChannelToEngine(row);
}

void SessionManager::setChannelAgcDecay(int row, double milliseconds)
{
    ChannelEntry *entry = m_channels.mutableAt(row);
    if (!entry)
        return;
    entry->settings.agcDecayMs = std::clamp(milliseconds, 0.0, 10000.0);
    qCDebug(dsdrCore) << "canale" << entry->id << "AGC decay:"
                      << (entry->settings.agcDecayMs > 0.0
                              ? QString::number(entry->settings.agcDecayMs)
                              : QStringLiteral("auto"))
                      << "ms";
    m_channels.entryChanged(row, {ChannelModel::AgcDecayRole});
    pushChannelToEngine(row);
}

void SessionManager::setChannelAmCarrierAgc(int row, bool enabled)
{
    ChannelEntry *entry = m_channels.mutableAt(row);
    if (!entry)
        return;
    entry->settings.amCarrierAgc = enabled;
    qCDebug(dsdrCore) << "canale" << entry->id << "AM carrier AGC:" << enabled;
    m_channels.entryChanged(row, {ChannelModel::AmCarrierAgcRole});
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

void SessionManager::setChannelAudioHighPassEnabled(int row, bool enabled)
{
    ChannelEntry *entry = m_channels.mutableAt(row);
    if (!entry)
        return;
    entry->settings.audioHighPassEnabled = enabled;
    qCDebug(dsdrCore) << "canale" << entry->id << "audio high-pass:" << enabled;
    m_channels.entryChanged(row, {ChannelModel::AudioHighPassEnabledRole});
    pushChannelToEngine(row);
}

void SessionManager::setChannelAudioHighPassHz(int row, double hertz)
{
    ChannelEntry *entry = m_channels.mutableAt(row);
    if (!entry)
        return;
    entry->settings.audioHighPassHz = std::clamp(hertz, 20.0, 5000.0);
    qCDebug(dsdrCore) << "canale" << entry->id << "audio high-pass cutoff:"
                      << entry->settings.audioHighPassHz << "Hz";
    m_channels.entryChanged(row, {ChannelModel::AudioHighPassHzRole});
    pushChannelToEngine(row);
}

void SessionManager::setChannelFmStereo(int row, bool enabled)
{
    ChannelEntry *entry = m_channels.mutableAt(row);
    if (!entry)
        return;
    entry->settings.fmStereo = enabled;
    qCDebug(dsdrCore) << "canale" << entry->id << "FM stereo:" << enabled;
    m_channels.entryChanged(row, {ChannelModel::FmStereoRole});
    pushChannelToEngine(row);
}

void SessionManager::setChannelFmAudioLowPass(int row, bool enabled)
{
    ChannelEntry *entry = m_channels.mutableAt(row);
    if (!entry)
        return;
    entry->settings.fmAudioLowPass = enabled;
    qCDebug(dsdrCore) << "canale" << entry->id << "FM low-pass:" << enabled;
    m_channels.entryChanged(row, {ChannelModel::FmAudioLowPassRole});
    pushChannelToEngine(row);
}

void SessionManager::setChannelFmDeemphasis(int row, double microseconds)
{
    ChannelEntry *entry = m_channels.mutableAt(row);
    if (!entry)
        return;
    if (microseconds != 0.0 && microseconds != 22.0
        && microseconds != 50.0 && microseconds != 75.0)
        microseconds = 50.0;
    entry->settings.fmDeemphasisUs = microseconds;
    qCDebug(dsdrCore) << "canale" << entry->id << "de-enfasi FM:" << microseconds << "us";
    m_channels.entryChanged(row, {ChannelModel::FmDeemphasisRole});
    pushChannelToEngine(row);
}

void SessionManager::setChannelFmRds(int row, bool enabled)
{
    ChannelEntry *entry = m_channels.mutableAt(row);
    if (!entry)
        return;
    if (!enabled && m_rdsAfProbeActive && m_rdsAfProbeRow == row)
        finishRdsAfProbe(false);
    entry->settings.fmRds = enabled;
    qCDebug(dsdrCore) << "canale" << entry->id << "RDS FM:" << enabled;
    m_channels.entryChanged(row, {ChannelModel::FmRdsRole});
    if (!enabled)
        m_channels.updateRds(entry->id, false, QString(), -1, -1, -1, QString(),
                             QString(), QString(), QString(), QString());
    pushChannelToEngine(row);
}

void SessionManager::setChannelRdsAutomaticAf(int row, bool enabled)
{
    ChannelEntry *entry = m_channels.mutableAt(row);
    if (!entry)
        return;

    if (!enabled && m_rdsAfProbeActive && m_rdsAfProbeRow == row)
        finishRdsAfProbe(false);

    entry->settings.rdsAutomaticAf = enabled;
    if (enabled) {
        m_rdsAfRejectedPi.clear();
        m_rdsAfRejectedList.clear();
        m_rdsAfRejectedFrequency = 0;
    }
    qCInfo(dsdrCore) << "canale" << entry->id << "RDS AF automatico:" << enabled;
    m_channels.entryChanged(row, {ChannelModel::RdsAutomaticAfRole});
    pushChannelToEngine(row);
}

void SessionManager::setChannelRdsRegion(int row, int region)
{
    ChannelEntry *entry = m_channels.mutableAt(row);
    if (!entry)
        return;

    entry->settings.rdsRegion = region == static_cast<int>(RdsRegion::NorthAmerica)
        ? RdsRegion::NorthAmerica : RdsRegion::Europe;
    qCDebug(dsdrCore) << "canale" << entry->id << "RDS region:"
                      << (entry->settings.rdsRegion == RdsRegion::NorthAmerica
                              ? "North America" : "Europe");
    m_channels.entryChanged(row, {ChannelModel::RdsRegionRole,
                                  ChannelModel::RdsSyncedRole,
                                  ChannelModel::RdsProgramTypeRole});
    pushChannelToEngine(row);
}

void SessionManager::followRdsAf(int row)
{
    const ChannelEntry *entry = m_channels.at(row);
    if (!entry || entry->rdsAlternateFrequencies.isEmpty())
        return;

    for (const qint64 frequencyHz : parseRdsAfFrequencies(
             entry->rdsAlternateFrequencies)) {
        if (std::llabs(frequencyHz - entry->frequencyHz) < 100)
            continue;

        qCInfo(dsdrCore) << "RDS AF follow manuale:" << entry->frequencyHz
                         << "->" << frequencyHz;
        setChannelFrequency(row, frequencyHz);
        return;
    }

    qCDebug(dsdrCore) << "RDS AF: nessuna alternativa diversa dalla frequenza corrente";
}

void SessionManager::handleAutomaticRdsAf(ChannelId id, bool synced, const QString &pi)
{
    const int row = m_channels.indexOf(id);
    if (row < 0)
        return;
    const ChannelEntry *entry = m_channels.at(row);
    if (!entry)
        return;

    if (m_rdsAfProbeActive) {
        if (row != m_rdsAfProbeRow || !synced)
            return;

        // Un AF valido deve portare lo stesso programma e un margine reale
        // di segnale; il timer gestisce il caso in cui la nuova frequenza non
        // decodifichi alcun RDS.
        if (pi != m_rdsAfOriginalPi) {
            finishRdsAfProbe(false);
        } else if (entry->signalDb >= m_rdsAfOriginalSignalDb + 3.0) {
            finishRdsAfProbe(true);
        }
        return;
    }

    if (!entry->settings.rdsAutomaticAf || entry->settings.mode != DemodMode::Fm
        || !entry->settings.fmRds || !synced || pi.isEmpty()
        || entry->rdsAlternateFrequencies.isEmpty() || entry->signalDb < -75.0)
        return;

    if (entry->frequencyHz == m_rdsAfRejectedFrequency
        && pi == m_rdsAfRejectedPi
        && entry->rdsAlternateFrequencies == m_rdsAfRejectedList)
        return;

    m_rdsAfCandidates = parseRdsAfFrequencies(entry->rdsAlternateFrequencies);
    if (m_rdsAfCandidates.empty())
        return;

    m_rdsAfProbeActive = true;
    m_rdsAfProbeRow = row;
    m_rdsAfCandidateIndex = 0;
    m_rdsAfOriginalFrequency = entry->frequencyHz;
    m_rdsAfOriginalSignalDb = entry->signalDb;
    m_rdsAfOriginalPi = pi;
    m_rdsAfProbeList = entry->rdsAlternateFrequencies;
    qCInfo(dsdrCore) << "RDS AF automatico: avvio probe per" << m_rdsAfOriginalPi
                     << "da" << m_rdsAfOriginalFrequency;
    probeNextRdsAf();
}

void SessionManager::probeNextRdsAf()
{
    if (!m_rdsAfProbeActive)
        return;

    while (m_rdsAfCandidateIndex < m_rdsAfCandidates.size()) {
        const qint64 candidate = m_rdsAfCandidates[m_rdsAfCandidateIndex++];
        if (std::llabs(candidate - m_rdsAfOriginalFrequency) < 100)
            continue;

        m_rdsAfCandidateFrequency = candidate;
        qCInfo(dsdrCore) << "RDS AF automatico: provo" << candidate
                         << "Hz (PI atteso" << m_rdsAfOriginalPi << ")";
        setChannelFrequency(m_rdsAfProbeRow, candidate);
        m_rdsAfProbeTimer.start(1500);
        return;
    }

    finishRdsAfProbe(false);
}

void SessionManager::finishRdsAfProbe(bool keepCandidate)
{
    if (!m_rdsAfProbeActive)
        return;

    m_rdsAfProbeTimer.stop();
    const int row = m_rdsAfProbeRow;
    const ChannelEntry *entry = m_channels.at(row);

    if (keepCandidate && entry && entry->frequencyHz == m_rdsAfCandidateFrequency) {
        qCInfo(dsdrCore) << "RDS AF automatico: mantengo" << m_rdsAfCandidateFrequency
                         << "Hz, segnale" << entry->signalDb << "dBFS";
        m_rdsAfRejectedPi.clear();
        m_rdsAfRejectedList.clear();
        m_rdsAfRejectedFrequency = 0;
        m_rdsAfProbeActive = false;
        m_rdsAfProbeRow = -1;
        return;
    }

    if (m_rdsAfCandidateIndex < m_rdsAfCandidates.size()) {
        probeNextRdsAf();
        return;
    }

    m_rdsAfRejectedPi = m_rdsAfOriginalPi;
    m_rdsAfRejectedList = m_rdsAfProbeList;
    m_rdsAfRejectedFrequency = m_rdsAfOriginalFrequency;
    qCInfo(dsdrCore) << "RDS AF automatico: nessun candidato migliore, ritorno a"
                     << m_rdsAfOriginalFrequency << "Hz";
    if (entry && entry->frequencyHz != m_rdsAfOriginalFrequency)
        setChannelFrequency(row, m_rdsAfOriginalFrequency);

    m_rdsAfProbeActive = false;
    m_rdsAfProbeRow = -1;
}

void SessionManager::setChannelSquelchEnabled(int row, bool enabled)
{
    ChannelEntry *entry = m_channels.mutableAt(row);
    if (!entry)
        return;
    entry->settings.squelchEnabled = enabled;
    qCDebug(dsdrCore) << "canale" << entry->id << "squelch:" << enabled;
    m_channels.entryChanged(row, {ChannelModel::SquelchEnabledRole});
    pushChannelToEngine(row);
}

void SessionManager::setChannelSquelchThreshold(int row, double thresholdDb)
{
    ChannelEntry *entry = m_channels.mutableAt(row);
    if (!entry)
        return;
    entry->settings.squelchThresholdDb = std::clamp(thresholdDb, -130.0, -20.0);
    qCDebug(dsdrCore) << "canale" << entry->id << "squelch threshold:"
                      << entry->settings.squelchThresholdDb << "dB";
    m_channels.entryChanged(row, {ChannelModel::SquelchThresholdRole});
    pushChannelToEngine(row);
}

void SessionManager::setChannelCtcssEnabled(int row, bool enabled)
{
    ChannelEntry *entry = m_channels.mutableAt(row);
    if (!entry)
        return;
    entry->settings.ctcssEnabled = enabled;
    qCDebug(dsdrCore) << "canale" << entry->id << "CTCSS:" << enabled;
    m_channels.entryChanged(row, {ChannelModel::CtcssEnabledRole});
    pushChannelToEngine(row);
}

void SessionManager::setChannelCtcssDecodeOnly(int row, bool enabled)
{
    ChannelEntry *entry = m_channels.mutableAt(row);
    if (!entry)
        return;
    entry->settings.ctcssDecodeOnly = enabled;
    qCDebug(dsdrCore) << "canale" << entry->id << "CTCSS decode-only:" << enabled;
    m_channels.entryChanged(row, {ChannelModel::CtcssDecodeOnlyRole});
    pushChannelToEngine(row);
}

void SessionManager::setChannelCtcssTone(int row, double toneHz)
{
    ChannelEntry *entry = m_channels.mutableAt(row);
    if (!entry)
        return;
    entry->settings.ctcssToneHz = std::clamp(toneHz, 50.0, 300.0);
    qCDebug(dsdrCore) << "canale" << entry->id << "CTCSS tone:"
                      << entry->settings.ctcssToneHz << "Hz";
    m_channels.entryChanged(row, {ChannelModel::CtcssToneRole});
    pushChannelToEngine(row);
}

void SessionManager::setChannelNoiseBlankerEnabled(int row, bool enabled)
{
    ChannelEntry *entry = m_channels.mutableAt(row);
    if (!entry)
        return;
    entry->settings.noiseBlankerEnabled = enabled;
    qCDebug(dsdrCore) << "canale" << entry->id << "noise blanker:" << enabled;
    m_channels.entryChanged(row, {ChannelModel::NoiseBlankerEnabledRole});
    pushChannelToEngine(row);
}

void SessionManager::setChannelNoiseBlankerThreshold(int row, double thresholdDb)
{
    ChannelEntry *entry = m_channels.mutableAt(row);
    if (!entry)
        return;
    entry->settings.noiseBlankerThresholdDb = std::clamp(thresholdDb, 3.0, 30.0);
    qCDebug(dsdrCore) << "canale" << entry->id << "noise blanker threshold:"
                      << entry->settings.noiseBlankerThresholdDb << "dB";
    m_channels.entryChanged(row, {ChannelModel::NoiseBlankerThresholdRole});
    pushChannelToEngine(row);
}

void SessionManager::setChannelFmIfNoiseReductionEnabled(int row, bool enabled)
{
    ChannelEntry *entry = m_channels.mutableAt(row);
    if (!entry)
        return;
    entry->settings.fmIfNoiseReductionEnabled = enabled;
    qCDebug(dsdrCore) << "canale" << entry->id << "FM IF noise reduction:" << enabled;
    m_channels.entryChanged(row, {ChannelModel::FmIfNoiseReductionEnabledRole});
    pushChannelToEngine(row);
}

void SessionManager::setChannelFmIfNoiseReductionPreset(int row, int preset)
{
    ChannelEntry *entry = m_channels.mutableAt(row);
    if (!entry)
        return;
    entry->settings.fmIfNoiseReductionPreset = std::clamp(preset, 0, 2);
    qCDebug(dsdrCore) << "canale" << entry->id << "FM IF noise preset:"
                      << entry->settings.fmIfNoiseReductionPreset;
    m_channels.entryChanged(row, {ChannelModel::FmIfNoiseReductionPresetRole});
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

bool SessionManager::startAudioRecording(const QString &path)
{
    if (!m_connected || !m_audio->isActive()) {
        setStatus(tr("Nessuna uscita audio attiva da registrare."));
        return false;
    }
    if (m_audioRecorder.isRecording())
        return true;

    if (!m_audioRecorder.startAudio(kInternalAudioRate, m_centerFrequency,
                                    m_audio->deviceName(), path))
        return false;
    m_engine->setAudioRecorder(&m_audioRecorder);
    setStatus(tr("Registrazione audio in corso: %1")
                  .arg(m_audioRecorder.currentFile()));
    return true;
}

void SessionManager::stopAudioRecording()
{
    if (!m_audioRecorder.isRecording())
        return;
    if (m_engine)
        m_engine->setAudioRecorder(nullptr);
    m_audioRecorder.stop();
    setStatus(tr("Registrazione audio salvata: %1")
                  .arg(m_audioRecorder.currentFile()));
}

bool SessionManager::toggleAudioRecording()
{
    if (m_audioRecorder.isRecording()) {
        stopAudioRecording();
        return false;
    }
    return startAudioRecording();
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
        DemodMode::DigU, DemodMode::DigL, DemodMode::Iq, DemodMode::Dsb,
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
