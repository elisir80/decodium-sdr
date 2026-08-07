// SPDX-License-Identifier: GPL-3.0-or-later
#include "hal/backends/nettcp/NetTcpBackend.h"
#include "hal/HalLog.h"
#include "hal/backends/nettcp/EndpointProbe.h"
#include "hal/backends/nettcp/RtlTcpClient.h"

#include <QThread>

#include <algorithm>

namespace dsdr::hal::nettcp {

namespace {

/// ~1,3 s di IQ a 2,048 MS/s (due float per coppia): assorbe una pausa del
/// consumatore senza mascherare un DSP cronicamente in ritardo.
constexpr std::size_t kIqRingFloats = 1 << 23;

/// I canali sono entità del DSP client: il limite è di buon senso sul carico,
/// non un vincolo del protocollo.
constexpr int kMaxRxChannels = 4;

QStringList &addedEndpoints()
{
    static QStringList endpoints;
    return endpoints;
}

} // namespace

NetTcpBackend::NetTcpBackend(QObject *parent)
    : IRadioBackend(parent)
    , m_iqRing(std::make_unique<SampleRing>(kIqRingFloats))
{
}

NetTcpBackend::~NetTcpBackend()
{
    close();
}

QString NetTcpBackend::displayName() const
{
    return m_device.isValid() ? m_device.displayName
                              : QStringLiteral("Sorgente IQ di rete (rtl_tcp)");
}

QStringList NetTcpBackend::configuredEndpoints()
{
    QStringList endpoints;

    const QByteArray fromEnv = qgetenv("DSDR_NETTCP_HOSTS");
    if (!fromEnv.isEmpty()) {
        const QStringList parts = QString::fromUtf8(fromEnv).split(QLatin1Char(','),
                                                                   Qt::SkipEmptyParts);
        for (const QString &part : parts)
            endpoints.append(part.trimmed());
    }

    endpoints.append(addedEndpoints());

    if (endpoints.isEmpty())
        endpoints.append(QStringLiteral("127.0.0.1:1234"));

    endpoints.removeDuplicates();
    return endpoints;
}

void NetTcpBackend::addEndpoint(const QString &hostPort)
{
    if (hostPort.trimmed().isEmpty())
        return;
    QStringList &list = addedEndpoints();
    if (!list.contains(hostPort.trimmed()))
        list.append(hostPort.trimmed());
}

void NetTcpBackend::clearAddedEndpoints()
{
    addedEndpoints().clear();
}

BackendCapabilities NetTcpBackend::capabilities() const
{
    BackendCapabilities caps;
    caps.maxRxChannels = kMaxRxChannels;
    // I canali derivano tutti dallo stesso flusso IQ: sono coerenti per
    // costruzione, esattamente come nel backend demo.
    caps.coherentRx = true;
    caps.maxPanadapters = 4;
    caps.tx = TxSupport::None;     // una chiavetta RTL non trasmette

    caps.demod = DspLocation::Client;
    caps.spectrum = DspLocation::Client;
    caps.agc = DspLocation::Client;

    int rateCount = 0;
    const double *rates = supportedSampleRates(rateCount);
    for (int i = 0; i < rateCount; ++i)
        caps.sampleRates.append(rates[i]);
    caps.defaultSampleRate = 2'048'000.0;

    // Prima dell'handshake il tuner è ignoto: si dichiara la copertura
    // generica, e la si stringe quando il server dice che cos'è.
    const TunerCoverage coverage = coverageFor(static_cast<TunerType>(m_tunerType));
    caps.minFrequencyHz = coverage.minHz;
    caps.maxFrequencyHz = coverage.maxHz;

    caps.hasHardwareFilters = false;
    caps.hasPreamp = m_gainStepCount > 0;
    caps.hasAttenuator = false;
    caps.adcBits = 8;

    caps.remoteCapable = true;
    caps.multiClient = false;      // rtl_tcp serve un client alla volta
    caps.supportsRecording = true;

    return caps;
}

void NetTcpBackend::setState(BackendState state)
{
    if (m_state == state)
        return;
    m_state = state;
    emit stateChanged(state);
}

void NetTcpBackend::reportError(BackendError::Code code, const QString &message, bool fatal)
{
    BackendError error;
    error.code = code;
    error.message = message;
    error.detail = QStringLiteral("backend=nettcp device=%1").arg(m_device.deviceId);
    error.fatal = fatal;

    qCWarning(dsdrHal) << "nettcp:" << message;
    emit errorOccurred(error);
    if (fatal)
        setState(BackendState::Error);
}

void NetTcpBackend::startDiscovery()
{
    if (m_pendingProbes > 0)
        return;

    setState(BackendState::Discovering);

    const QStringList endpoints = configuredEndpoints();
    m_pendingProbes = 0;

    for (const QString &endpoint : endpoints) {
        const int colon = endpoint.lastIndexOf(QLatin1Char(':'));
        const QString host = colon > 0 ? endpoint.left(colon) : endpoint;
        const quint16 port = colon > 0
            ? static_cast<quint16>(endpoint.mid(colon + 1).toUShort())
            : kDefaultPort;

        auto *probe = new EndpointProbe(host, port == 0 ? kDefaultPort : port, this);
        m_probes.append(probe);
        ++m_pendingProbes;

        connect(probe, &EndpointProbe::probed, this,
                [this](EndpointProbe *finished, bool found, quint32 tuner, quint32 gains) {
                    if (found) {
                        const TunerCoverage coverage = coverageFor(static_cast<TunerType>(tuner));

                        DeviceDescriptor device;
                        device.backendId = backendId();
                        device.deviceId = QStringLiteral("%1:%2")
                                              .arg(finished->host())
                                              .arg(finished->port());
                        device.displayName = QStringLiteral("rtl_tcp %1 — %2")
                                                 .arg(device.deviceId)
                                                 .arg(QString::fromLatin1(coverage.name));
                        device.model = QString::fromLatin1(coverage.name);
                        device.transport = QStringLiteral("tcp");
                        device.address = device.deviceId;
                        device.extra.insert(QStringLiteral("host"), finished->host());
                        device.extra.insert(QStringLiteral("port"), finished->port());
                        device.extra.insert(QStringLiteral("tunerType"), tuner);
                        device.extra.insert(QStringLiteral("gainSteps"), gains);

                        emit deviceFound(device);
                    }

                    m_probes.removeAll(finished);
                    finished->deleteLater();

                    if (--m_pendingProbes <= 0) {
                        m_pendingProbes = 0;
                        if (!m_open)
                            setState(BackendState::Idle);
                        emit discoveryFinished();
                    }
                });

        probe->start();
    }

    if (m_pendingProbes == 0) {
        setState(BackendState::Idle);
        emit discoveryFinished();
    }
}

void NetTcpBackend::stopDiscovery()
{
    for (EndpointProbe *probe : std::as_const(m_probes))
        probe->deleteLater();
    m_probes.clear();
    m_pendingProbes = 0;
    if (!m_open && m_state == BackendState::Discovering)
        setState(BackendState::Idle);
}

void NetTcpBackend::open(const DeviceDescriptor &device)
{
    if (m_open)
        close();

    if (!device.isValid() || device.backendId != backendId()) {
        reportError(BackendError::NotFound,
                    tr("Il device %1 non appartiene al backend di rete.").arg(device.key()),
                    true);
        return;
    }

    m_device = device;
    m_tunerType = device.extra.value(QStringLiteral("tunerType")).toUInt();
    m_gainStepCount = device.extra.value(QStringLiteral("gainSteps")).toUInt();

    const QString host = device.extra.value(QStringLiteral("host")).toString();
    const quint16 port = static_cast<quint16>(
        device.extra.value(QStringLiteral("port"), kDefaultPort).toUInt());
    if (host.isEmpty()) {
        reportError(BackendError::NotFound, tr("Endpoint non specificato."), true);
        return;
    }

    // La frequenza iniziale deve stare nella copertura del tuner: aprire un
    // R820T a 100 kHz darebbe solo rumore e sembrerebbe un guasto.
    const BackendCapabilities caps = capabilities();
    if (!caps.coversFrequency(m_centerHz))
        m_centerHz = std::clamp<qint64>(m_centerHz, caps.minFrequencyHz, caps.maxFrequencyHz);

    setState(BackendState::Connecting);
    m_open = true;
    m_iqRing->clear();
    m_sequence = 0;

    auto *client = new RtlTcpClient(m_iqRing.get());
    m_thread = new QThread(this);
    m_thread->setObjectName(QStringLiteral("dsdr-nettcp-ingest"));
    client->moveToThread(m_thread);
    connect(m_thread, &QThread::finished, client, &QObject::deleteLater);

    connect(client, &RtlTcpClient::connected, this, &NetTcpBackend::onClientConnected);

    connect(client, &RtlTcpClient::failed, this, [this](const QString &message, bool fatal) {
        reportError(BackendError::TransportError, message, fatal);
    });

    connect(client, &RtlTcpClient::disconnected, this, [this] {
        if (m_open)
            reportError(BackendError::TransportError,
                        tr("Il server ha chiuso la connessione."), true);
    });

    // DirectConnection: il descrittore nasce nel thread di ingest e il signal
    // è riemesso da lì; i consumatori si collegano queued e leggono dal ring.
    connect(client, &RtlTcpClient::samplesProduced, this,
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

    m_client = client;
    m_thread->start();

    QMetaObject::invokeMethod(client, "setSampleRate", Qt::QueuedConnection,
                              Q_ARG(double, m_sampleRate));
    QMetaObject::invokeMethod(client, "setFrequency", Qt::QueuedConnection,
                              Q_ARG(qint64, m_centerHz));
    QMetaObject::invokeMethod(client, "setGain", Qt::QueuedConnection,
                              Q_ARG(int, m_gainTenthsDb));
    QMetaObject::invokeMethod(client, "connectToServer", Qt::QueuedConnection,
                              Q_ARG(QString, host), Q_ARG(quint16, port));
}

void NetTcpBackend::onClientConnected(quint32 tunerType, quint32 gainStepCount)
{
    m_tunerType = tunerType;
    m_gainStepCount = gainStepCount;

    setState(BackendState::Streaming);
    emit capabilitiesChanged();     // la copertura ora è quella del tuner vero
    emit centerFrequencyChanged(m_centerHz);
    emit sampleRateChanged(m_sampleRate);
}

void NetTcpBackend::close()
{
    stopDiscovery();

    if (!m_open && !m_thread)
        return;

    m_open = false;

    if (m_thread) {
        if (m_client && m_thread->isRunning())
            QMetaObject::invokeMethod(m_client, "disconnectFromServer",
                                      Qt::BlockingQueuedConnection);
        m_thread->quit();
        m_thread->wait();
        delete m_thread;
        m_thread = nullptr;
        m_client = nullptr;
    }

    m_channels.clear();
    m_panadapters.clear();
    m_device = DeviceDescriptor();
    setState(BackendState::Idle);
}

void NetTcpBackend::setCenterFrequency(qint64 hz)
{
    const BackendCapabilities caps = capabilities();
    if (!caps.coversFrequency(hz)) {
        reportError(BackendError::Unsupported,
                    tr("Il tuner non copre %1 Hz.").arg(hz));
        return;
    }
    if (m_centerHz == hz)
        return;

    m_centerHz = hz;
    if (m_client)
        QMetaObject::invokeMethod(m_client, "setFrequency", Qt::QueuedConnection,
                                  Q_ARG(qint64, hz));
    emit centerFrequencyChanged(hz);
}

void NetTcpBackend::setSampleRate(double rate)
{
    if (!capabilities().sampleRates.contains(rate)) {
        reportError(BackendError::Unsupported,
                    tr("Frequenza di campionamento %1 non supportata da rtl_tcp.").arg(rate));
        return;
    }
    if (qFuzzyCompare(m_sampleRate, rate))
        return;

    m_sampleRate = rate;
    if (m_client)
        QMetaObject::invokeMethod(m_client, "setSampleRate", Qt::QueuedConnection,
                                  Q_ARG(double, rate));
    emit sampleRateChanged(rate);
}

ChannelId NetTcpBackend::createRxChannel(const RxChannelConfig &config)
{
    if (!m_open) {
        reportError(BackendError::TransportError, tr("Device non aperto."));
        return kInvalidChannel;
    }
    if (m_channels.size() >= kMaxRxChannels) {
        reportError(BackendError::ResourceExhausted,
                    tr("Massimo %1 canali RX su questa sorgente.").arg(kMaxRxChannels));
        return kInvalidChannel;
    }

    const ChannelId id = m_nextChannelId++;
    m_channels.insert(id, config);
    return id;
}

void NetTcpBackend::destroyRxChannel(ChannelId channel)
{
    m_channels.remove(channel);
}

QList<ChannelId> NetTcpBackend::channels() const
{
    QList<ChannelId> ids = m_channels.keys();
    std::sort(ids.begin(), ids.end());
    return ids;
}

void NetTcpBackend::setFrequency(ChannelId channel, qint64 hz)
{
    auto it = m_channels.find(channel);
    if (it == m_channels.end()) {
        reportError(BackendError::NotFound, tr("Canale %1 inesistente.").arg(channel));
        return;
    }
    // Il canale vive dentro la banda campionata: la sintonia è del DSP client,
    // qui si registra soltanto.
    it->frequencyHz = hz;
}

void NetTcpBackend::setDemod(ChannelId channel, DemodMode mode)
{
    auto it = m_channels.find(channel);
    if (it != m_channels.end())
        it->mode = mode;
}

void NetTcpBackend::setFilter(ChannelId channel, int lowHz, int highHz)
{
    auto it = m_channels.find(channel);
    if (it != m_channels.end()) {
        it->filterLowHz = lowHz;
        it->filterHighHz = highHz;
    }
}

PanId NetTcpBackend::createPanadapter(const PanConfig &config)
{
    if (m_panadapters.size() >= capabilities().maxPanadapters) {
        reportError(BackendError::ResourceExhausted, tr("Troppi panadattatori aperti."));
        return kInvalidPan;
    }
    const PanId id = m_nextPanId++;
    m_panadapters.insert(id, config);
    return id;
}

void NetTcpBackend::destroyPanadapter(PanId pan)
{
    m_panadapters.remove(pan);
}

void NetTcpBackend::setPtt(bool transmit)
{
    if (transmit) {
        // Receive-only: la richiesta si rifiuta, non si ignora in silenzio.
        reportError(BackendError::Unsupported,
                    tr("Questa sorgente è solo in ricezione."));
    }
}

void NetTcpBackend::setTxFrequency(qint64 hz)
{
    Q_UNUSED(hz)
}

SampleRing *NetTcpBackend::iqStream(ChannelId channel) const
{
    Q_UNUSED(channel)
    return m_iqRing.get();
}

SampleRing *NetTcpBackend::audioStream(ChannelId channel) const
{
    Q_UNUSED(channel)
    return nullptr;
}

SampleRing *NetTcpBackend::spectrumStream(PanId pan) const
{
    Q_UNUSED(pan)
    return nullptr;
}

QVariant NetTcpBackend::nativeCommand(const QString &command, const QVariantMap &args)
{
    // Comandi specifici di rtl_tcp: stanno qui e non nell'interfaccia
    // generale perché nessun'altra radio ha un "bias tee" o un "ppm" (§4.1).
    // "net.addEndpoint" è la convenzione condivisa dai backend che dichiarano
    // remoteCapable: permette al core di offrire "aggiungi un indirizzo" senza
    // sapere quale backend sia attivo (CONSTITUTION §7).
    if (command == QLatin1String("net.addEndpoint")
        || command == QLatin1String("nettcp.addHost")) {
        const QString endpoint = args.value(QStringLiteral("endpoint"),
                                            args.value(QStringLiteral("hostPort"))).toString();
        addEndpoint(endpoint);
        return QVariant(configuredEndpoints());
    }
    if (command == QLatin1String("net.endpoints")
        || command == QLatin1String("nettcp.endpoints")) {
        return QVariant(configuredEndpoints());
    }

    if (command == QLatin1String("nettcp.setGain")) {
        m_gainTenthsDb = args.value(QStringLiteral("tenthsDb"), -1).toInt();
        if (m_client)
            QMetaObject::invokeMethod(m_client, "setGain", Qt::QueuedConnection,
                                      Q_ARG(int, m_gainTenthsDb));
        return QVariant(m_gainTenthsDb);
    }
    if (command == QLatin1String("nettcp.setPpm")) {
        m_ppm = args.value(QStringLiteral("ppm"), 0).toInt();
        if (m_client)
            QMetaObject::invokeMethod(m_client, "setFrequencyCorrection", Qt::QueuedConnection,
                                      Q_ARG(int, m_ppm));
        return QVariant(m_ppm);
    }
    if (command == QLatin1String("nettcp.setBiasTee")) {
        const bool enabled = args.value(QStringLiteral("enabled"), false).toBool();
        if (m_client)
            QMetaObject::invokeMethod(m_client, "setBiasTee", Qt::QueuedConnection,
                                      Q_ARG(bool, enabled));
        return QVariant(enabled);
    }

    return IRadioBackend::nativeCommand(command, args);
}

} // namespace dsdr::hal::nettcp
