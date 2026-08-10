// SPDX-License-Identifier: GPL-3.0-or-later
#include "hal/backends/demo/DemoBackend.h"
#include "hal/HalLog.h"
#include "hal/backends/demo/DemoWorker.h"

#include <QThread>
#include <QTimer>

#include <algorithm>
#include <cmath>

namespace dsdr::hal::demo {

namespace {

/// Il ring tiene ~1 secondo di IQ a 192 kHz (2 float per frame): abbastanza
/// per assorbire una pausa del DSP senza perdere campioni, non tanto da
/// nascondere un consumatore cronicamente lento.
constexpr std::size_t kIqRingFloats = 1 << 20;

constexpr int kMaxRxChannels = 4;

} // namespace

DemoBackend::DemoBackend(QObject *parent)
    : IRadioBackend(parent)
    , m_iqRing(std::make_unique<SampleRing>(kIqRingFloats))
    , m_txRing(std::make_unique<SampleRing>(kIqRingFloats))
{
}

DemoBackend::~DemoBackend()
{
    close();
}

QString DemoBackend::displayName() const
{
    return m_device.isValid() ? m_device.displayName
                              : QStringLiteral("Demo (segnali sintetici)");
}

QList<DeviceDescriptor> DemoBackend::syntheticDevices()
{
    DeviceDescriptor hf;
    hf.backendId = QStringLiteral("demo");
    hf.deviceId = QStringLiteral("synthetic-hf");
    hf.displayName = QStringLiteral("DECODIUM Demo — HF 40 m");
    hf.model = QStringLiteral("Synthetic HF");
    hf.serial = QStringLiteral("DEMO-HF-0001");
    hf.transport = QStringLiteral("synthetic");
    hf.extra.insert(QStringLiteral("band"), QStringLiteral("hf"));
    hf.extra.insert(QStringLiteral("centerHz"), 7'100'000);

    DeviceDescriptor vhf;
    vhf.backendId = QStringLiteral("demo");
    vhf.deviceId = QStringLiteral("synthetic-vhf");
    vhf.displayName = QStringLiteral("DECODIUM Demo — VHF 2 m");
    vhf.model = QStringLiteral("Synthetic VHF");
    vhf.serial = QStringLiteral("DEMO-VHF-0001");
    vhf.transport = QStringLiteral("synthetic");
    vhf.extra.insert(QStringLiteral("band"), QStringLiteral("vhf"));
    vhf.extra.insert(QStringLiteral("centerHz"), 145'000'000);

    return {hf, vhf};
}

BackendCapabilities DemoBackend::capabilities() const
{
    BackendCapabilities caps;
    caps.maxRxChannels = kMaxRxChannels;
    caps.coherentRx = true; // coerenza simulata: i canali nascono dallo stesso IQ
    caps.maxPanadapters = 4;
    caps.tx = TxSupport::Ptt;

    caps.demod = DspLocation::Client;
    caps.spectrum = DspLocation::Client;
    caps.agc = DspLocation::Client;

    caps.sampleRates = {192000.0, 384000.0, 768000.0, 960000.0, 1536000.0};
    caps.defaultSampleRate = 192000.0;
    // Il demo sa togliere guadagno come lo saprebbe una radio: serve a provare
    // davvero la guardia contro la saturazione, in CI e senza hardware.
    caps.maxGainReductionDb = 30.0;
    caps.minFrequencyHz = 100'000;
    caps.maxFrequencyHz = 2'000'000'000;
    caps.hasHardwareFilters = false;
    caps.hasPreamp = true;
    caps.hasAttenuator = true;
    caps.adcBits = 16;

    caps.remoteCapable = false;
    caps.multiClient = false;
    caps.supportsRecording = true;

    return caps;
}

void DemoBackend::setState(BackendState state)
{
    if (m_state == state)
        return;
    m_state = state;
    emit stateChanged(state);
}

void DemoBackend::reportError(BackendError::Code code, const QString &message, bool fatal)
{
    BackendError error;
    error.code = code;
    error.message = message;
    error.detail = QStringLiteral("backend=demo device=%1").arg(m_device.deviceId);
    error.fatal = fatal;

    qCWarning(dsdrHal) << "demo:" << message;
    emit errorOccurred(error);
    if (fatal)
        setState(BackendState::Error);
}

void DemoBackend::startDiscovery()
{
    if (m_discovering)
        return;
    m_discovering = true;
    setState(BackendState::Discovering);

    // Asincrona come le discovery vere, così il core non sviluppa per errore
    // l'assunzione che i device compaiano già durante la chiamata.
    QTimer::singleShot(120, this, [this] {
        if (!m_discovering)
            return;
        for (const DeviceDescriptor &device : syntheticDevices())
            emit deviceFound(device);
        m_discovering = false;
        if (!m_open)
            setState(BackendState::Idle);
        emit discoveryFinished();
    });
}

void DemoBackend::stopDiscovery()
{
    m_discovering = false;
    if (!m_open && m_state == BackendState::Discovering)
        setState(BackendState::Idle);
}

std::vector<StationSpec> DemoBackend::bandPlanFor(const DeviceDescriptor &device) const
{
    return device.extra.value(QStringLiteral("band")).toString() == QLatin1String("vhf")
        ? SyntheticBand::vhfBandPlan()
        : SyntheticBand::hfBandPlan();
}

void DemoBackend::open(const DeviceDescriptor &device)
{
    if (m_open)
        close();

    DeviceDescriptor target = device;
    if (!target.isValid()) {
        const QList<DeviceDescriptor> devices = syntheticDevices();
        target = devices.first();
    }
    if (target.backendId != backendId()) {
        reportError(BackendError::NotFound,
                    tr("Il device %1 non appartiene al backend demo.").arg(target.key()),
                    true);
        return;
    }

    m_device = target;
    setState(BackendState::Connecting);

    const QVariant center = target.extra.value(QStringLiteral("centerHz"));
    if (center.isValid())
        m_centerHz = center.toLongLong();
    m_txFrequencyHz = m_centerHz;

    m_open = true;
    restartWorker();

    setState(BackendState::Streaming);
    emit centerFrequencyChanged(m_centerHz);
    emit sampleRateChanged(m_sampleRate);
    emit capabilitiesChanged();
}

void DemoBackend::restartWorker()
{
    if (m_thread) {
        if (m_worker && m_thread->isRunning())
            QMetaObject::invokeMethod(m_worker, "stop", Qt::BlockingQueuedConnection);
        m_thread->quit();
        m_thread->wait();
        delete m_thread;
        m_thread = nullptr;
        m_worker = nullptr;
    }

    if (!m_open)
        return;

    m_iqRing->clear();
    // Anche il ring di trasmissione: quel che era in coda apparteneva alla
    // configurazione precedente e uscirebbe adesso, alla frequenza sbagliata.
    m_txRing->clear();
    m_sequence = 0;

    auto *worker = new DemoWorker(m_iqRing.get(), m_txRing.get());
    worker->configure(m_sampleRate, m_centerHz, bandPlanFor(m_device));

    m_thread = new QThread(this);
    m_thread->setObjectName(QStringLiteral("dsdr-demo-ingest"));
    worker->moveToThread(m_thread);
    connect(m_thread, &QThread::finished, worker, &QObject::deleteLater);

    // DirectConnection: il descrittore viene costruito nel thread di ingest e
    // il signal riemesso da lì. I consumatori si collegano con una queued
    // connection e leggono i campioni dal ring (§4.1).
    connect(worker, &DemoWorker::framesProduced, this,
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

    m_worker = worker;
    m_thread->start();
    QMetaObject::invokeMethod(worker, "start", Qt::QueuedConnection);
}

void DemoBackend::close()
{
    if (!m_open && !m_thread)
        return;

    m_open = false;

    if (m_thread) {
        if (m_worker && m_thread->isRunning())
            QMetaObject::invokeMethod(m_worker, "stop", Qt::BlockingQueuedConnection);
        m_thread->quit();
        m_thread->wait();
        delete m_thread;
        m_thread = nullptr;
        m_worker = nullptr;
    }

    m_channels.clear();
    m_panadapters.clear();
    m_ptt = false;
    m_device = DeviceDescriptor();
    setState(BackendState::Idle);
}

void DemoBackend::setCenterFrequency(qint64 hz)
{
    const BackendCapabilities caps = capabilities();
    if (!caps.coversFrequency(hz)) {
        reportError(BackendError::Unsupported,
                    tr("Frequenza %1 Hz fuori dalla copertura del device.").arg(hz));
        return;
    }
    if (m_centerHz == hz)
        return;

    m_centerHz = hz;
    if (m_worker)
        QMetaObject::invokeMethod(m_worker, "setCenterFrequency", Qt::QueuedConnection,
                                  Q_ARG(qint64, hz));
    emit centerFrequencyChanged(hz);
}

void DemoBackend::setSampleRate(double rate)
{
    const BackendCapabilities caps = capabilities();
    if (!caps.sampleRates.contains(rate)) {
        reportError(BackendError::Unsupported,
                    tr("Frequenza di campionamento %1 non supportata.").arg(rate));
        return;
    }
    if (qFuzzyCompare(m_sampleRate, rate))
        return;

    m_sampleRate = rate;
    restartWorker();
    emit sampleRateChanged(rate);
}

ChannelId DemoBackend::createRxChannel(const RxChannelConfig &config)
{
    if (!m_open) {
        reportError(BackendError::TransportError, tr("Device non aperto."));
        return kInvalidChannel;
    }
    if (m_channels.size() >= kMaxRxChannels) {
        reportError(BackendError::ResourceExhausted,
                    tr("Il device demo supporta al massimo %1 canali RX.").arg(kMaxRxChannels));
        return kInvalidChannel;
    }

    const ChannelId id = m_nextChannelId++;
    m_channels.insert(id, config);
    return id;
}

void DemoBackend::destroyRxChannel(ChannelId channel)
{
    m_channels.remove(channel);
}

QList<ChannelId> DemoBackend::channels() const
{
    QList<ChannelId> ids = m_channels.keys();
    std::sort(ids.begin(), ids.end());
    return ids;
}

void DemoBackend::setFrequency(ChannelId channel, qint64 hz)
{
    auto it = m_channels.find(channel);
    if (it == m_channels.end()) {
        reportError(BackendError::NotFound, tr("Canale %1 inesistente.").arg(channel));
        return;
    }
    it->frequencyHz = hz;
}

void DemoBackend::setDemod(ChannelId channel, DemodMode mode)
{
    // Backend raw-IQ: la demodulazione è del client. Registriamo comunque la
    // scelta perché il descrittore del canale resti veritiero.
    auto it = m_channels.find(channel);
    if (it != m_channels.end())
        it->mode = mode;
}

void DemoBackend::setFilter(ChannelId channel, int lowHz, int highHz)
{
    auto it = m_channels.find(channel);
    if (it != m_channels.end()) {
        it->filterLowHz = lowHz;
        it->filterHighHz = highHz;
    }
}

PanId DemoBackend::createPanadapter(const PanConfig &config)
{
    if (m_panadapters.size() >= capabilities().maxPanadapters) {
        reportError(BackendError::ResourceExhausted, tr("Troppi panadattatori aperti."));
        return kInvalidPan;
    }
    const PanId id = m_nextPanId++;
    m_panadapters.insert(id, config);
    return id;
}

void DemoBackend::destroyPanadapter(PanId pan)
{
    m_panadapters.remove(pan);
}

double DemoBackend::setGainReduction(double db)
{
    // Passi da tre dB, come su un attenuatore vero: chi chiede sette dB ne
    // ottiene sei, e deve poterlo sapere dal valore restituito invece di
    // credere di averne sette.
    const double wanted = std::clamp(db, 0.0, capabilities().maxGainReductionDb);
    m_gainReductionDb = std::floor(wanted / 3.0) * 3.0;

    if (m_worker) {
        QMetaObject::invokeMethod(m_worker, "setGainReductionDb", Qt::QueuedConnection,
                                  Q_ARG(double, m_gainReductionDb));
    }
    return m_gainReductionDb;
}

void DemoBackend::setPtt(bool transmit)
{
    if (m_ptt == transmit)
        return;
    m_ptt = transmit;
    if (m_worker)
        QMetaObject::invokeMethod(m_worker, "setTransmitting", Qt::QueuedConnection,
                                  Q_ARG(bool, transmit));
    emit pttChanged(transmit);
}

void DemoBackend::setTxFrequency(qint64 hz)
{
    m_txFrequencyHz = hz;
}

SampleRing *DemoBackend::txStream()
{
    return m_txRing.get();
}

SampleRing *DemoBackend::iqStream(ChannelId channel) const
{
    // Raw-IQ: esiste un solo flusso, quello del device. I canali sono entità
    // del DSP client, non del backend.
    Q_UNUSED(channel)
    return m_iqRing.get();
}

SampleRing *DemoBackend::audioStream(ChannelId channel) const
{
    Q_UNUSED(channel)
    return nullptr; // nessun audio demodulato a bordo
}

SampleRing *DemoBackend::spectrumStream(PanId pan) const
{
    Q_UNUSED(pan)
    return nullptr; // lo spettro lo calcola il DSP Engine
}

QVariant DemoBackend::nativeCommand(const QString &command, const QVariantMap &args)
{
    if (command == QLatin1String("demo.setNoiseFloorDb")) {
        // Comando dimostrativo: mostra la forma della valvola di sfogo senza
        // che il core ne sappia nulla (§4.1).
        return QVariant(args.value(QStringLiteral("db"), -95.0));
    }
    if (command == QLatin1String("demo.stationCount"))
        return QVariant(static_cast<int>(bandPlanFor(m_device).size()));

    return IRadioBackend::nativeCommand(command, args);
}

} // namespace dsdr::hal::demo
