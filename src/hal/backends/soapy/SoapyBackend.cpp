// SPDX-License-Identifier: GPL-3.0-or-later
#include "hal/backends/soapy/SoapyBackend.h"
#include "hal/HalLog.h"
#include "hal/backends/soapy/SoapyWorker.h"

#include <SoapySDR/Device.hpp>

#include <QThread>
#include <QTimer>

#include <algorithm>

namespace dsdr::hal::soapy {

namespace {

/// ~1,7 s di IQ a 2,4 MS/s. I device USB consegnano a raffiche: un ring
/// generoso evita che una pausa dello scheduler diventi un buco udibile.
constexpr std::size_t kIqRingFloats = 1 << 23;

/// Compone gli argomenti con cui riaprire esattamente questo device.
QString argsFromKwargs(const SoapySDR::Kwargs &kwargs)
{
    QStringList parts;
    for (const auto &[key, value] : kwargs) {
        parts.append(QStringLiteral("%1=%2")
                         .arg(QString::fromStdString(key), QString::fromStdString(value)));
    }
    return parts.join(QLatin1Char(','));
}

QString kwargValue(const SoapySDR::Kwargs &kwargs, const char *key)
{
    const auto it = kwargs.find(key);
    return it == kwargs.end() ? QString() : QString::fromStdString(it->second);
}

} // namespace

SoapyBackend::SoapyBackend(QObject *parent)
    : IRadioBackend(parent)
    , m_iqRing(std::make_unique<SampleRing>(kIqRingFloats))
{
    qRegisterMetaType<dsdr::hal::soapy::SoapyDeviceProfile>(
        "dsdr::hal::soapy::SoapyDeviceProfile");
}

SoapyBackend::~SoapyBackend()
{
    close();
}

QString SoapyBackend::displayName() const
{
    return m_device.isValid() ? m_device.displayName : QStringLiteral("SoapySDR");
}

BackendCapabilities SoapyBackend::capabilities() const
{
    if (m_profile.isValid())
        return capabilitiesFrom(m_profile);

    // Prima di aprire un device non si sa nulla di preciso: si dichiara il
    // minimo indispensabile, senza promettere bande o rate che potrebbero non
    // esistere.
    BackendCapabilities caps;
    caps.maxRxChannels = kMaxLogicalRxChannels;
    caps.maxPanadapters = 4;
    caps.tx = TxSupport::None;
    caps.demod = DspLocation::Client;
    caps.spectrum = DspLocation::Client;
    caps.agc = DspLocation::Client;
    caps.sampleRates = {2'048'000.0};
    caps.defaultSampleRate = 2'048'000.0;
    caps.supportsRecording = true;
    return caps;
}

void SoapyBackend::setState(BackendState state)
{
    if (m_state == state)
        return;
    m_state = state;
    emit stateChanged(state);
}

void SoapyBackend::reportError(BackendError::Code code, const QString &message, bool fatal)
{
    BackendError error;
    error.code = code;
    error.message = message;
    error.detail = QStringLiteral("backend=soapy device=%1").arg(m_device.deviceId);
    error.fatal = fatal;

    qCWarning(dsdrHal) << "soapy:" << message;
    emit errorOccurred(error);
    if (fatal)
        setState(BackendState::Error);
}

void SoapyBackend::startDiscovery()
{
    setState(BackendState::Discovering);

    // `enumerate()` interroga i driver e può richiedere qualche centinaio di
    // millisecondi; resta però sincrona. La si rimanda al ciclo di eventi
    // successivo per non consegnare device durante la chiamata, che è ciò che
    // la conformance suite verifica per tutti i backend.
    QTimer::singleShot(0, this, [this] {
        std::vector<SoapySDR::Kwargs> results;
        try {
            results = SoapySDR::Device::enumerate();
        } catch (const std::exception &e) {
            reportError(BackendError::InternalError,
                        tr("Enumerazione dei device fallita: %1").arg(QString::fromUtf8(e.what())));
        }

        for (const SoapySDR::Kwargs &kwargs : results) {
            DeviceDescriptor device;
            device.backendId = backendId();

            const QString driver = kwargValue(kwargs, "driver");
            const QString serial = kwargValue(kwargs, "serial");
            const QString label = kwargValue(kwargs, "label");

            // Il seriale è l'unico identificatore stabile fra un riavvio e
            // l'altro; senza, si ripiega sull'indice, che cambia se l'utente
            // sposta la chiavetta di porta.
            device.deviceId = serial.isEmpty()
                ? QStringLiteral("%1:%2").arg(driver).arg(kwargValue(kwargs, "index"))
                : QStringLiteral("%1:%2").arg(driver, serial);

            device.displayName = label.isEmpty()
                ? QStringLiteral("SoapySDR %1").arg(driver)
                : label;
            device.model = kwargValue(kwargs, "hardware");
            device.serial = serial;
            device.transport = QStringLiteral("soapy");
            device.extra.insert(QStringLiteral("args"), argsFromKwargs(kwargs));
            device.extra.insert(QStringLiteral("driver"), driver);

            emit deviceFound(device);
        }

        if (!m_open)
            setState(BackendState::Idle);
        emit discoveryFinished();
    });
}

void SoapyBackend::stopDiscovery()
{
    if (!m_open && m_state == BackendState::Discovering)
        setState(BackendState::Idle);
}

void SoapyBackend::open(const DeviceDescriptor &device)
{
    if (m_open)
        close();

    if (!device.isValid() || device.backendId != backendId()) {
        reportError(BackendError::NotFound,
                    tr("Il device %1 non appartiene al backend SoapySDR.").arg(device.key()),
                    true);
        return;
    }

    const QString args = device.extra.value(QStringLiteral("args")).toString();
    if (args.isEmpty()) {
        reportError(BackendError::NotFound, tr("Argomenti del device mancanti."), true);
        return;
    }

    m_device = device;
    setState(BackendState::Connecting);
    m_open = true;
    m_iqRing->clear();
    m_sequence = 0;

    auto *worker = new SoapyWorker(m_iqRing.get());
    m_thread = new QThread(this);
    m_thread->setObjectName(QStringLiteral("dsdr-soapy-ingest"));
    worker->moveToThread(m_thread);
    connect(m_thread, &QThread::finished, worker, &QObject::deleteLater);

    connect(worker, &SoapyWorker::opened, this, &SoapyBackend::onDeviceOpened);

    connect(worker, &SoapyWorker::failed, this, [this](const QString &message, bool fatal) {
        reportError(fatal ? BackendError::TransportError : BackendError::Unsupported,
                    message, fatal);
    });

    connect(worker, &SoapyWorker::samplesProduced, this,
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

    QMetaObject::invokeMethod(worker, "openAndRun", Qt::QueuedConnection,
                              Q_ARG(QString, args),
                              Q_ARG(qint64, m_centerHz),
                              Q_ARG(double, m_sampleRate));
}

void SoapyBackend::onDeviceOpened(const SoapyDeviceProfile &profile)
{
    m_profile = profile;

    // Ora si sa davvero che device è: la frequenza va riportata dentro la sua
    // copertura, altrimenti si ascolterebbe rumore credendo a un guasto.
    const BackendCapabilities caps = capabilities();
    if (!caps.coversFrequency(m_centerHz) && caps.maxFrequencyHz > caps.minFrequencyHz) {
        m_centerHz = std::clamp(m_centerHz, caps.minFrequencyHz, caps.maxFrequencyHz);
        if (m_worker)
            m_worker->requestFrequency(m_centerHz);
    }

    if (!caps.sampleRates.contains(m_sampleRate) && caps.defaultSampleRate > 0.0) {
        m_sampleRate = caps.defaultSampleRate;
        if (m_worker)
            m_worker->requestSampleRate(m_sampleRate);
    }

    setState(BackendState::Streaming);
    emit capabilitiesChanged();
    emit centerFrequencyChanged(m_centerHz);
    emit sampleRateChanged(m_sampleRate);

    qCInfo(dsdrHal) << "soapy: aperto" << profile.driver << profile.hardware
                    << "RX" << profile.rxChannels << "TX" << profile.txChannels;
}

void SoapyBackend::close()
{
    if (!m_open && !m_thread)
        return;

    m_open = false;

    if (m_thread) {
        if (m_worker)
            m_worker->requestStop();   // atomica: il ciclo esce da solo
        m_thread->quit();
        if (!m_thread->wait(3000)) {
            qCWarning(dsdrHal) << "soapy: il thread di ingest non si è fermato in tempo";
            m_thread->terminate();
            m_thread->wait(1000);
        }
        delete m_thread;
        m_thread = nullptr;
        m_worker = nullptr;
    }

    m_channels.clear();
    m_panadapters.clear();
    m_profile = SoapyDeviceProfile();
    m_device = DeviceDescriptor();
    m_ptt = false;
    setState(BackendState::Idle);
}

void SoapyBackend::setCenterFrequency(qint64 hz)
{
    const BackendCapabilities caps = capabilities();
    if (!caps.coversFrequency(hz)) {
        reportError(BackendError::Unsupported, tr("Il device non copre %1 Hz.").arg(hz));
        return;
    }
    if (m_centerHz == hz)
        return;

    m_centerHz = hz;
    if (m_worker)
        m_worker->requestFrequency(hz);
    emit centerFrequencyChanged(hz);
}

void SoapyBackend::setSampleRate(double rate)
{
    if (!capabilities().sampleRates.contains(rate)) {
        reportError(BackendError::Unsupported,
                    tr("Frequenza di campionamento %1 non supportata dal device.").arg(rate));
        return;
    }
    if (qFuzzyCompare(m_sampleRate, rate))
        return;

    m_sampleRate = rate;
    if (m_worker)
        m_worker->requestSampleRate(rate);
    emit sampleRateChanged(rate);
}

ChannelId SoapyBackend::createRxChannel(const RxChannelConfig &config)
{
    if (!m_open) {
        reportError(BackendError::TransportError, tr("Device non aperto."));
        return kInvalidChannel;
    }
    if (m_channels.size() >= capabilities().maxRxChannels) {
        reportError(BackendError::ResourceExhausted,
                    tr("Massimo %1 canali RX su questo device.")
                        .arg(capabilities().maxRxChannels));
        return kInvalidChannel;
    }

    const ChannelId id = m_nextChannelId++;
    m_channels.insert(id, config);
    return id;
}

void SoapyBackend::destroyRxChannel(ChannelId channel)
{
    m_channels.remove(channel);
}

QList<ChannelId> SoapyBackend::channels() const
{
    QList<ChannelId> ids = m_channels.keys();
    std::sort(ids.begin(), ids.end());
    return ids;
}

void SoapyBackend::setFrequency(ChannelId channel, qint64 hz)
{
    auto it = m_channels.find(channel);
    if (it == m_channels.end()) {
        reportError(BackendError::NotFound, tr("Canale %1 inesistente.").arg(channel));
        return;
    }
    it->frequencyHz = hz;
}

void SoapyBackend::setDemod(ChannelId channel, DemodMode mode)
{
    auto it = m_channels.find(channel);
    if (it != m_channels.end())
        it->mode = mode;
}

void SoapyBackend::setFilter(ChannelId channel, int lowHz, int highHz)
{
    auto it = m_channels.find(channel);
    if (it != m_channels.end()) {
        it->filterLowHz = lowHz;
        it->filterHighHz = highHz;
    }
}

PanId SoapyBackend::createPanadapter(const PanConfig &config)
{
    if (m_panadapters.size() >= capabilities().maxPanadapters) {
        reportError(BackendError::ResourceExhausted, tr("Troppi panadattatori aperti."));
        return kInvalidPan;
    }
    const PanId id = m_nextPanId++;
    m_panadapters.insert(id, config);
    return id;
}

void SoapyBackend::destroyPanadapter(PanId pan)
{
    m_panadapters.remove(pan);
}

void SoapyBackend::setPtt(bool transmit)
{
    if (transmit && !capabilities().canTransmit()) {
        reportError(BackendError::Unsupported, tr("Questo device è solo in ricezione."));
        return;
    }
    if (m_ptt == transmit)
        return;

    // La catena di trasmissione arriva in Fase 2: qui si registra solo lo
    // stato, senza fingere che una portante stia uscendo davvero.
    m_ptt = transmit;
    emit pttChanged(transmit);
}

void SoapyBackend::setTxFrequency(qint64 hz)
{
    Q_UNUSED(hz)
}

SampleRing *SoapyBackend::iqStream(ChannelId channel) const
{
    Q_UNUSED(channel)
    return m_iqRing.get();
}

SampleRing *SoapyBackend::audioStream(ChannelId channel) const
{
    Q_UNUSED(channel)
    return nullptr;
}

SampleRing *SoapyBackend::spectrumStream(PanId pan) const
{
    Q_UNUSED(pan)
    return nullptr;
}

QVariant SoapyBackend::nativeCommand(const QString &command, const QVariantMap &args)
{
    if (command == QLatin1String("soapy.setGain")) {
        const double db = args.value(QStringLiteral("db"), -1.0).toDouble();
        if (m_worker)
            m_worker->requestGain(db);
        return QVariant(db);
    }
    if (command == QLatin1String("soapy.gainRange")) {
        return QVariantMap{{QStringLiteral("min"), m_profile.minGainDb},
                           {QStringLiteral("max"), m_profile.maxGainDb},
                           {QStringLiteral("hasAgc"), m_profile.hasAgc}};
    }
    if (command == QLatin1String("soapy.antennas"))
        return QVariant(m_profile.antennas);
    if (command == QLatin1String("soapy.setAntenna")) {
        const QString name = args.value(QStringLiteral("antenna")).toString();
        const int index = m_profile.antennas.indexOf(name);
        if (index < 0) {
            reportError(BackendError::Unsupported,
                        tr("Il device non ha un'antenna «%1».").arg(name));
            return QVariant();
        }
        if (m_worker)
            m_worker->requestAntenna(index);
        return QVariant(name);
    }
    if (command == QLatin1String("soapy.driver"))
        return QVariant(m_profile.driver);

    return IRadioBackend::nativeCommand(command, args);
}

} // namespace dsdr::hal::soapy
