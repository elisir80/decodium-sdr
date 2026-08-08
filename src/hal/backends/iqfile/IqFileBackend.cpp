// SPDX-License-Identifier: GPL-3.0-or-later
#include "hal/backends/iqfile/IqFileBackend.h"
#include "hal/HalLog.h"
#include "hal/backends/iqfile/IqFileWorker.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>

#include <algorithm>

namespace dsdr::hal::iqfile {

namespace {

/// Ring da ~1 secondo a 192 kHz, come gli altri backend raw-IQ.
constexpr std::size_t kIqRingFloats = 1 << 20;

constexpr int kMaxRxChannels = 4;

/// Cartella in cui `IqRecorder` salva. La costante è replicata invece di
/// includere `core/IqRecorder.h`: la HAL non può dipendere dal core, e questa
/// è l'unica cosa che le due parti devono sapere l'una dell'altra.
QString defaultRecordingDirectory()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
    return (base.isEmpty() ? QDir::homePath() : base) + QStringLiteral("/DECODIUM SDR");
}

/// Cartelle e file indicati a mano. Accetta più voci separate da `;`, così una
/// sola variabile basta per una cartella di archivio e un file singolo.
QStringList extraPaths()
{
    const QByteArray raw = qgetenv("DSDR_IQFILE_PATH");
    if (raw.isEmpty())
        return {};

    QStringList result;
    for (const QString &part : QString::fromLocal8Bit(raw)
                                   .split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
        const QString trimmed = part.trimmed();
        if (!trimmed.isEmpty())
            result.append(trimmed);
    }
    return result;
}

void collectFrom(const QString &path, QStringList &files)
{
    const QFileInfo info(path);
    if (info.isFile()) {
        files.append(info.absoluteFilePath());
        return;
    }
    if (!info.isDir())
        return;

    QDir dir(path);
    const auto entries = dir.entryInfoList({QStringLiteral("*.wav"), QStringLiteral("*.WAV")},
                                           QDir::Files, QDir::Name);
    for (const QFileInfo &entry : entries)
        files.append(entry.absoluteFilePath());
}

} // namespace

IqFileBackend::IqFileBackend(QObject *parent)
    : IRadioBackend(parent)
    , m_iqRing(std::make_unique<SampleRing>(kIqRingFloats))
{
    qRegisterMetaType<RecordingInfo>("dsdr::hal::iqfile::RecordingInfo");
}

IqFileBackend::~IqFileBackend()
{
    close();
}

QString IqFileBackend::displayName() const
{
    return m_device.isValid() ? m_device.displayName
                              : QStringLiteral("Registrazione IQ");
}

DeviceDescriptor IqFileBackend::describe(const RecordingInfo &info)
{
    DeviceDescriptor device;
    device.backendId = QStringLiteral("iqfile");
    // Il percorso è la chiave stabile: lo stesso file riaperto domani deve
    // ritrovare le proprie impostazioni.
    device.deviceId = info.filePath;
    device.displayName = info.displayName;
    device.model = info.backendId.isEmpty()
        ? QObject::tr("Registrazione IQ")
        : QObject::tr("Registrazione — %1").arg(info.backendId);
    device.transport = QStringLiteral("file");
    device.address = info.filePath;
    device.extra.insert(QStringLiteral("path"), info.filePath);
    device.extra.insert(QStringLiteral("sampleRate"), info.sampleRate);
    device.extra.insert(QStringLiteral("centerHz"), info.centerFrequencyHz);
    device.extra.insert(QStringLiteral("durationMs"), info.durationMs);
    device.extra.insert(QStringLiteral("hasSidecar"), info.hasSidecar);
    if (!info.deviceName.isEmpty())
        device.extra.insert(QStringLiteral("recordedWith"), info.deviceName);
    return device;
}

QList<DeviceDescriptor> IqFileBackend::availableRecordings()
{
    QStringList files;
    for (const QString &path : extraPaths())
        collectFrom(path, files);
    collectFrom(defaultRecordingDirectory(), files);
    files.removeDuplicates();

    QList<DeviceDescriptor> devices;
    for (const QString &file : files) {
        RecordingInfo info;
        QString error;
        if (!IqFileReader::probe(file, info, &error)) {
            // Un file illeggibile non è un errore del backend: la cartella
            // delle registrazioni può contenere qualunque WAV.
            qCDebug(dsdrHal) << "iqfile: ignoro" << file << ":" << error;
            continue;
        }
        devices.append(describe(info));
    }
    return devices;
}

BackendCapabilities IqFileBackend::capabilities() const
{
    BackendCapabilities caps;
    caps.maxRxChannels = kMaxRxChannels;
    caps.coherentRx = true;   // i canali nascono tutti dallo stesso IQ
    caps.maxPanadapters = 4;
    caps.tx = TxSupport::None;   // non si trasmette da un file

    caps.demod = DspLocation::Client;
    caps.spectrum = DspLocation::Client;
    caps.agc = DspLocation::Client;

    // Il rate non si sceglie: è quello con cui la registrazione fu fatta.
    // Prima dell'apertura dichiariamo i valori consueti, perché il contratto
    // vuole che un backend raw-IQ non lasci mai la lista vuota.
    if (m_sampleRate > 0.0) {
        caps.sampleRates = {m_sampleRate};
        caps.defaultSampleRate = m_sampleRate;
    } else {
        caps.sampleRates = {192000.0, 384000.0, 768000.0, 1536000.0, 2048000.0};
        caps.defaultSampleRate = 192000.0;
    }

    // Nemmeno la frequenza si sceglie, ma dichiarare una copertura stretta
    // attorno alla registrazione farebbe rifiutare alla UI sintonie legittime
    // dentro la banda registrata. Si dichiara tutto lo spettro plausibile e si
    // rifiuta altrove, in `setCenterFrequency`.
    caps.minFrequencyHz = 0;
    caps.maxFrequencyHz = 0;
    caps.hasHardwareFilters = false;
    caps.hasPreamp = false;
    caps.hasAttenuator = false;
    caps.adcBits = 0;

    caps.remoteCapable = false;
    caps.multiClient = false;
    caps.supportsRecording = false;   // ri-registrare una registrazione non serve a nulla

    caps.nativePanels = {QStringLiteral("IqFileDevicePanel")};

    return caps;
}

void IqFileBackend::setState(BackendState state)
{
    if (m_state == state)
        return;
    m_state = state;
    emit stateChanged(state);
}

void IqFileBackend::reportError(BackendError::Code code, const QString &message, bool fatal)
{
    BackendError error;
    error.code = code;
    error.message = message;
    error.detail = QStringLiteral("backend=iqfile file=%1").arg(m_device.deviceId);
    error.fatal = fatal;

    qCWarning(dsdrHal) << "iqfile:" << message;
    emit errorOccurred(error);
    if (fatal)
        setState(BackendState::Error);
}

void IqFileBackend::startDiscovery()
{
    if (m_discovering)
        return;
    m_discovering = true;
    setState(BackendState::Discovering);

    // Asincrona come le altre: leggere le intestazioni di una cartella piena
    // di registrazioni non deve bloccare il thread della UI.
    QTimer::singleShot(0, this, [this] {
        if (!m_discovering)
            return;
        for (const DeviceDescriptor &device : availableRecordings())
            emit deviceFound(device);
        m_discovering = false;
        if (!m_open)
            setState(BackendState::Idle);
        emit discoveryFinished();
    });
}

void IqFileBackend::stopDiscovery()
{
    m_discovering = false;
    if (!m_open && m_state == BackendState::Discovering)
        setState(BackendState::Idle);
}

void IqFileBackend::open(const DeviceDescriptor &device)
{
    if (m_open)
        close();

    const QString path = device.extra.value(QStringLiteral("path")).toString().isEmpty()
        ? device.deviceId
        : device.extra.value(QStringLiteral("path")).toString();

    if (path.isEmpty()) {
        reportError(BackendError::NotFound, tr("Nessun file indicato."), true);
        return;
    }
    if (device.isValid() && device.backendId != backendId()) {
        reportError(BackendError::NotFound,
                    tr("Il device %1 non appartiene al backend iqfile.").arg(device.key()),
                    true);
        return;
    }

    RecordingInfo info;
    QString error;
    if (!IqFileReader::probe(path, info, &error)) {
        reportError(BackendError::Unsupported,
                    tr("Impossibile leggere %1: %2").arg(QFileInfo(path).fileName(), error),
                    true);
        return;
    }

    m_recording = info;
    m_device = describe(info);
    m_centerHz = info.centerFrequencyHz;
    m_sampleRate = info.sampleRate;
    m_positionMs = 0;
    m_paused = false;

    setState(BackendState::Connecting);
    m_open = true;
    startWorker(path);

    setState(BackendState::Streaming);
    emit centerFrequencyChanged(m_centerHz);
    emit sampleRateChanged(m_sampleRate);
    emit capabilitiesChanged();

    if (!info.hasSidecar) {
        // Non è un errore, ma va detto: senza sidecar la frequenza mostrata è
        // una deduzione dal nome del file, e potrebbe essere sbagliata.
        qCInfo(dsdrHal) << "iqfile: sidecar assente per" << path
                        << "— frequenza dedotta dal nome:" << m_centerHz;
    }
}

void IqFileBackend::startWorker(const QString &path)
{
    stopWorker();
    if (!m_open)
        return;

    m_iqRing->clear();
    m_sequence = 0;

    auto *worker = new IqFileWorker(m_iqRing.get());

    m_thread = new QThread(this);
    m_thread->setObjectName(QStringLiteral("dsdr-iqfile-ingest"));
    worker->moveToThread(m_thread);
    connect(m_thread, &QThread::finished, worker, &QObject::deleteLater);

    // DirectConnection: il descrittore nasce nel thread di ingest, i campioni
    // stanno già nel ring (§4.1).
    connect(worker, &IqFileWorker::framesProduced, this,
            [this](quint32 frames, quint32 dropped, quint64 timestampNs) {
                if (frames == 0 && dropped == 0)
                    return;
                IqFrame frame;
                frame.channel = kInvalidChannel;
                frame.sequence = ++m_sequence;
                frame.centerFrequencyHz = m_centerHz;
                frame.sampleRate = m_sampleRate;
                frame.frameCount = frames;
                frame.droppedFrames = dropped;
                frame.timestampNs = timestampNs;
                emit iqFrameReady(frame);
            },
            Qt::DirectConnection);

    connect(worker, &IqFileWorker::positionChanged, this,
            [this](qint64 positionMs, qint64) { m_positionMs = positionMs; },
            Qt::QueuedConnection);

    connect(worker, &IqFileWorker::failed, this,
            [this](const QString &message) {
                reportError(BackendError::TransportError, message, true);
            },
            Qt::QueuedConnection);

    connect(worker, &IqFileWorker::finished, this,
            [this] {
                // Fine della riproduzione senza loop: la sorgente tace ma il
                // device resta aperto, così si può riavvolgere e ripartire.
                m_paused = true;
                setState(BackendState::Ready);
            },
            Qt::QueuedConnection);

    m_worker = worker;
    m_thread->start();

    QMetaObject::invokeMethod(worker, "setLoop", Qt::QueuedConnection, Q_ARG(bool, m_loop));
    QMetaObject::invokeMethod(worker, "setSpeed", Qt::QueuedConnection, Q_ARG(double, m_speed));
    QMetaObject::invokeMethod(worker, "openFile", Qt::QueuedConnection, Q_ARG(QString, path));
    QMetaObject::invokeMethod(worker, "start", Qt::QueuedConnection);
}

void IqFileBackend::stopWorker()
{
    if (!m_thread)
        return;

    if (m_worker && m_thread->isRunning())
        QMetaObject::invokeMethod(m_worker, "stop", Qt::BlockingQueuedConnection);
    m_thread->quit();
    m_thread->wait();
    delete m_thread;
    m_thread = nullptr;
    m_worker = nullptr;
}

void IqFileBackend::close()
{
    if (!m_open && !m_thread)
        return;

    m_open = false;
    stopWorker();

    m_channels.clear();
    m_panadapters.clear();
    m_device = DeviceDescriptor();
    m_recording = RecordingInfo();
    m_positionMs = 0;
    m_paused = false;
    setState(BackendState::Idle);
}

void IqFileBackend::setCenterFrequency(qint64 hz)
{
    // La frequenza di una registrazione è quella che era: cambiarla
    // significherebbe mentire su cosa contengono i campioni. Si accetta solo
    // il valore reale, così la UI che rilegge il proprio stato non trova un
    // rifiuto inatteso.
    if (hz == m_centerHz)
        return;

    reportError(BackendError::Unsupported,
                tr("La frequenza di una registrazione non è modificabile: "
                   "i campioni furono acquisiti a %1 Hz.")
                    .arg(m_centerHz));
    emit centerFrequencyChanged(m_centerHz);
}

void IqFileBackend::setSampleRate(double rate)
{
    if (qFuzzyCompare(rate, m_sampleRate))
        return;

    reportError(BackendError::Unsupported,
                tr("La frequenza di campionamento di una registrazione è fissa: %1.")
                    .arg(m_sampleRate));
    emit sampleRateChanged(m_sampleRate);
}

ChannelId IqFileBackend::createRxChannel(const RxChannelConfig &config)
{
    if (!m_open) {
        reportError(BackendError::TransportError, tr("Nessuna registrazione aperta."));
        return kInvalidChannel;
    }
    if (m_channels.size() >= kMaxRxChannels) {
        reportError(BackendError::ResourceExhausted,
                    tr("Al massimo %1 canali RX su una registrazione.").arg(kMaxRxChannels));
        return kInvalidChannel;
    }

    const ChannelId id = m_nextChannelId++;
    m_channels.insert(id, config);
    return id;
}

void IqFileBackend::destroyRxChannel(ChannelId channel)
{
    m_channels.remove(channel);
}

QList<ChannelId> IqFileBackend::channels() const
{
    QList<ChannelId> ids = m_channels.keys();
    std::sort(ids.begin(), ids.end());
    return ids;
}

void IqFileBackend::setFrequency(ChannelId channel, qint64 hz)
{
    auto it = m_channels.find(channel);
    if (it == m_channels.end()) {
        reportError(BackendError::NotFound, tr("Canale %1 inesistente.").arg(channel));
        return;
    }
    it->frequencyHz = hz;
}

void IqFileBackend::setDemod(ChannelId channel, DemodMode mode)
{
    auto it = m_channels.find(channel);
    if (it != m_channels.end())
        it->mode = mode;
}

void IqFileBackend::setFilter(ChannelId channel, int lowHz, int highHz)
{
    auto it = m_channels.find(channel);
    if (it != m_channels.end()) {
        it->filterLowHz = lowHz;
        it->filterHighHz = highHz;
    }
}

PanId IqFileBackend::createPanadapter(const PanConfig &config)
{
    if (m_panadapters.size() >= capabilities().maxPanadapters) {
        reportError(BackendError::ResourceExhausted, tr("Troppi panadattatori aperti."));
        return kInvalidPan;
    }
    const PanId id = m_nextPanId++;
    m_panadapters.insert(id, config);
    return id;
}

void IqFileBackend::destroyPanadapter(PanId pan)
{
    m_panadapters.remove(pan);
}

void IqFileBackend::setPtt(bool transmit)
{
    if (!transmit)
        return;
    reportError(BackendError::Unsupported, tr("Da una registrazione non si trasmette."));
}

void IqFileBackend::setTxFrequency(qint64 hz)
{
    Q_UNUSED(hz)
}

SampleRing *IqFileBackend::iqStream(ChannelId channel) const
{
    Q_UNUSED(channel)
    return m_iqRing.get();
}

SampleRing *IqFileBackend::audioStream(ChannelId channel) const
{
    Q_UNUSED(channel)
    return nullptr;
}

SampleRing *IqFileBackend::spectrumStream(PanId pan) const
{
    Q_UNUSED(pan)
    return nullptr;
}

QVariant IqFileBackend::nativeCommand(const QString &command, const QVariantMap &args)
{
    // Il trasporto — pausa, riavvolgimento, velocità — non esiste per nessuna
    // radio vera, quindi non entra nel seam generale: passa di qui, e solo il
    // pannello di questo backend lo usa (§4.1).

    if (command == QLatin1String("iqfile.setPaused")) {
        const bool paused = args.value(QStringLiteral("paused")).toBool();
        if (m_paused != paused) {
            m_paused = paused;
            if (m_worker)
                QMetaObject::invokeMethod(m_worker, "setPaused", Qt::QueuedConnection,
                                          Q_ARG(bool, paused));
            setState(paused ? BackendState::Ready : BackendState::Streaming);
        }
        return QVariant(m_paused);
    }

    if (command == QLatin1String("iqfile.paused"))
        return QVariant(m_paused);

    if (command == QLatin1String("iqfile.setLoop")) {
        m_loop = args.value(QStringLiteral("loop"), true).toBool();
        if (m_worker)
            QMetaObject::invokeMethod(m_worker, "setLoop", Qt::QueuedConnection,
                                      Q_ARG(bool, m_loop));
        return QVariant(m_loop);
    }

    if (command == QLatin1String("iqfile.setSpeed")) {
        m_speed = std::clamp(args.value(QStringLiteral("speed"), 1.0).toDouble(), 0.1, 8.0);
        if (m_worker)
            QMetaObject::invokeMethod(m_worker, "setSpeed", Qt::QueuedConnection,
                                      Q_ARG(double, m_speed));
        return QVariant(m_speed);
    }

    if (command == QLatin1String("iqfile.seek")) {
        const qint64 ms = args.value(QStringLiteral("ms")).toLongLong();
        if (m_worker)
            QMetaObject::invokeMethod(m_worker, "seekMs", Qt::QueuedConnection,
                                      Q_ARG(qint64, ms));
        // Se la riproduzione era finita, riavvolgere la fa ripartire.
        if (m_paused && m_state == BackendState::Ready && ms < m_recording.durationMs) {
            QMetaObject::invokeMethod(m_worker, "start", Qt::QueuedConnection);
            m_paused = false;
            setState(BackendState::Streaming);
        }
        return QVariant(ms);
    }

    if (command == QLatin1String("iqfile.status")) {
        QVariantMap status;
        status.insert(QStringLiteral("path"), m_recording.filePath);
        status.insert(QStringLiteral("positionMs"), m_positionMs);
        status.insert(QStringLiteral("durationMs"), m_recording.durationMs);
        status.insert(QStringLiteral("paused"), m_paused);
        status.insert(QStringLiteral("loop"), m_loop);
        status.insert(QStringLiteral("speed"), m_speed);
        status.insert(QStringLiteral("hasSidecar"), m_recording.hasSidecar);
        status.insert(QStringLiteral("recordedWith"), m_recording.deviceName);
        return status;
    }

    if (command == QLatin1String("iqfile.recordings")) {
        QVariantList list;
        for (const DeviceDescriptor &device : availableRecordings()) {
            QVariantMap entry;
            entry.insert(QStringLiteral("path"), device.extra.value(QStringLiteral("path")));
            entry.insert(QStringLiteral("name"), device.displayName);
            entry.insert(QStringLiteral("durationMs"),
                         device.extra.value(QStringLiteral("durationMs")));
            list.append(entry);
        }
        return list;
    }

    return IRadioBackend::nativeCommand(command, args);
}

} // namespace dsdr::hal::iqfile
