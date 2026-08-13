// SPDX-License-Identifier: GPL-3.0-or-later
#include "hal/backends/rtlsdr/RtlSdrBackend.h"

#include "dsp/SpscRing.h"
#include "hal/HalLog.h"
#include "hal/backends/rtlsdr/RtlSdrWorker.h"

#include <rtl-sdr.h>

#include <QTimer>
#include <QThread>

#include <algorithm>
#include <array>

namespace dsdr::hal::rtlsdr {

namespace {

constexpr std::size_t kIqRingFloats = 1 << 23;
QString usbString(const char *value)
{
    return value ? QString::fromUtf8(value) : QString();
}

} // namespace

RtlSdrBackend::RtlSdrBackend(QObject *parent)
    : IRadioBackend(parent)
    , m_iqRing(std::make_unique<SampleRing>(kIqRingFloats))
{
    qRegisterMetaType<RtlSdrDeviceProfile>("dsdr::hal::rtlsdr::RtlSdrDeviceProfile");
}

RtlSdrBackend::~RtlSdrBackend()
{
    close();
}

QString RtlSdrBackend::displayName() const
{
    return m_device.isValid() ? m_device.displayName
                              : QStringLiteral("RTL-SDR (nativo)");
}

BackendCapabilities RtlSdrBackend::capabilities() const
{
    if (m_profile.isValid())
        return capabilitiesFrom(m_profile);

    RtlSdrDeviceProfile profile;
    profile.index = 0;
    profile.product = QStringLiteral("RTL-SDR");
    profile.sampleRates = {250'000.0, 1'024'000.0, 1'536'000.0, 1'792'000.0,
                           1'920'000.0, 2'048'000.0, 2'160'000.0, 2'400'000.0};
    profile.preferredSampleRate = 2'048'000.0;
    profile.gainTenthsDb = {0, 99, 198, 280, 370, 496};
    return capabilitiesFrom(profile);
}

void RtlSdrBackend::setState(BackendState state)
{
    if (m_state == state)
        return;
    m_state = state;
    emit stateChanged(state);
}

void RtlSdrBackend::reportError(BackendError::Code code, const QString &message, bool fatal)
{
    BackendError error;
    error.code = code;
    error.message = message;
    error.detail = QStringLiteral("backend=rtlsdr device=%1").arg(m_device.deviceId);
    error.fatal = fatal;
    qCWarning(dsdrHal) << "rtlsdr:" << message;
    emit errorOccurred(error);
    if (fatal)
        setState(BackendState::Error);
}

void RtlSdrBackend::startDiscovery()
{
    setState(BackendState::Discovering);

    QTimer::singleShot(0, this, [this] {
        const uint32_t count = rtlsdr_get_device_count();
        qCInfo(dsdrHal) << "rtlsdr: discovery" << count << "device";

        for (uint32_t index = 0; index < count; ++index) {
            std::array<char, 256> manufacturer{};
            std::array<char, 256> product{};
            std::array<char, 256> serial{};
            if (rtlsdr_get_device_usb_strings(index, manufacturer.data(), product.data(),
                                               serial.data()) != 0) {
                qCWarning(dsdrHal) << "rtlsdr: stringhe USB non leggibili per" << index;
                continue;
            }

            const QString deviceName = usbString(rtlsdr_get_device_name(index));
            const QString productName = usbString(product.data());
            const QString serialName = usbString(serial.data());
            DeviceDescriptor device;
            device.backendId = backendId();
            device.deviceId = serialName.isEmpty()
                ? QStringLiteral("index:%1").arg(index)
                : QStringLiteral("serial:%1").arg(serialName);
            device.displayName = productName.isEmpty() ? deviceName : productName;
            if (!deviceName.isEmpty() && !device.displayName.contains(deviceName,
                                                                       Qt::CaseInsensitive)) {
                device.displayName += QStringLiteral(" — ") + deviceName;
            }
            if (device.displayName.isEmpty())
                device.displayName = QStringLiteral("RTL-SDR #%1").arg(index);
            device.model = productName.isEmpty() ? deviceName : productName;
            device.serial = serialName;
            device.transport = QStringLiteral("usb");
            device.extra.insert(QStringLiteral("index"), static_cast<int>(index));
            device.extra.insert(QStringLiteral("manufacturer"), usbString(manufacturer.data()));
            device.extra.insert(QStringLiteral("product"), productName);
            device.extra.insert(QStringLiteral("serial"), serialName);
            device.extra.insert(QStringLiteral("driver"), QStringLiteral("librtlsdr"));
            emit deviceFound(device);
        }

        if (!m_open)
            setState(BackendState::Idle);
        emit discoveryFinished();
    });
}

void RtlSdrBackend::stopDiscovery()
{
    if (!m_open && m_state == BackendState::Discovering)
        setState(BackendState::Idle);
}

void RtlSdrBackend::open(const DeviceDescriptor &device)
{
    if (m_open)
        close();

    if (!device.isValid() || device.backendId != backendId()) {
        reportError(BackendError::NotFound,
                    tr("Il device %1 non appartiene al backend RTL-SDR nativo.")
                        .arg(device.key()), true);
        return;
    }

    const int index = device.extra.value(QStringLiteral("index"), -1).toInt();
    if (index < 0) {
        reportError(BackendError::NotFound, tr("Indice del device RTL-SDR mancante."), true);
        return;
    }

    m_device = device;
    m_open = true;
    m_iqRing->clear();
    m_sequence = 0;
    setState(BackendState::Connecting);

    auto *worker = new RtlSdrWorker(m_iqRing.get());
    m_thread = new QThread(this);
    m_thread->setObjectName(QStringLiteral("dsdr-rtlsdr-ingest"));
    worker->moveToThread(m_thread);
    connect(m_thread, &QThread::finished, worker, &QObject::deleteLater);
    connect(worker, &RtlSdrWorker::finished, m_thread, &QThread::quit,
            Qt::DirectConnection);
    connect(worker, &RtlSdrWorker::opened, this, &RtlSdrBackend::onDeviceOpened);
    connect(worker, &RtlSdrWorker::failed, this,
            [this](const QString &message, bool fatal) {
                reportError(fatal ? BackendError::TransportError : BackendError::Unsupported,
                            message, fatal);
            });
    connect(worker, &RtlSdrWorker::samplesProduced, this,
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
    worker->requestGain(m_gainDb);
    worker->requestPpm(m_ppm);
    worker->requestBiasTee(m_biasTee);
    worker->requestDirectSampling(m_directSampling);
    if (m_offsetTuning)
        worker->requestOffsetTuning(true);
    m_thread->start();
    QMetaObject::invokeMethod(worker, "openAndRun", Qt::QueuedConnection,
                              Q_ARG(int, index),
                              Q_ARG(QString, device.serial),
                              Q_ARG(qint64, m_centerHz),
                              Q_ARG(double, m_sampleRate));
}

void RtlSdrBackend::onDeviceOpened(const RtlSdrDeviceProfile &profile)
{
    m_profile = profile;
    m_autoGainDb = safeAutoGainTenthsDb(profile.gainTenthsDb) / 10.0;
    m_gainReductionDb = 0.0;
    const BackendCapabilities caps = capabilities();
    if (!caps.coversFrequency(m_centerHz) && caps.maxFrequencyHz > caps.minFrequencyHz) {
        m_centerHz = std::clamp(m_centerHz, caps.minFrequencyHz, caps.maxFrequencyHz);
        if (m_worker)
            m_worker->requestFrequency(m_centerHz);
    }
    if (!caps.sampleRates.contains(m_sampleRate)) {
        m_sampleRate = caps.defaultSampleRate;
        if (m_worker)
            m_worker->requestSampleRate(m_sampleRate);
    }
    setState(BackendState::Streaming);
    emit capabilitiesChanged();
    emit centerFrequencyChanged(m_centerHz);
    emit sampleRateChanged(m_sampleRate);
    qCInfo(dsdrHal) << "rtlsdr: aperto" << profile.product << profile.tuner
                    << "gain step" << profile.gainTenthsDb.size();
}

void RtlSdrBackend::close()
{
    if (!m_open && !m_thread)
        return;

    m_open = false;
    if (m_thread) {
        if (m_worker)
            m_worker->requestStop();
        m_thread->requestInterruption();
        m_thread->quit();
        if (!m_thread->wait(10000)) {
            qCWarning(dsdrHal) << "rtlsdr: il thread non si è fermato in tempo";
            m_thread->terminate();
            m_thread->wait(5000);
        }
        delete m_thread;
        m_thread = nullptr;
        m_worker = nullptr;
    }

    m_channels.clear();
    m_panadapters.clear();
    m_profile = RtlSdrDeviceProfile();
    m_device = DeviceDescriptor();
    setState(BackendState::Idle);
}

void RtlSdrBackend::setCenterFrequency(qint64 hz)
{
    // `open()` returns before the worker has read the tuner profile. Allow a
    // startup tune to be queued in that interval; hardware-range validation
    // remains active once the profile is ready.
    if (m_profile.isValid() && !capabilities().coversFrequency(hz)) {
        reportError(BackendError::Unsupported, tr("Il device non copre %1 Hz.").arg(hz));
        return;
    }
    if (m_centerHz == hz)
        return;
    m_centerHz = hz;
    if (m_worker)
        m_worker->requestFrequency(hz);
    qCInfo(dsdrHal) << "rtlsdr: centro richiesto" << hz
                    << (m_profile.isValid() ? "profilo pronto" : "profilo in caricamento");
    emit centerFrequencyChanged(hz);
}

void RtlSdrBackend::setSampleRate(double rate)
{
    if (!capabilities().sampleRates.contains(rate)) {
        reportError(BackendError::Unsupported,
                    tr("Frequenza di campionamento %1 non supportata.").arg(rate));
        return;
    }
    if (qFuzzyCompare(m_sampleRate, rate))
        return;
    m_sampleRate = rate;
    if (m_worker)
        m_worker->requestSampleRate(rate);
    emit sampleRateChanged(rate);
}

ChannelId RtlSdrBackend::createRxChannel(const RxChannelConfig &config)
{
    if (!m_open) {
        reportError(BackendError::TransportError, tr("Device non aperto."));
        return kInvalidChannel;
    }
    if (m_channels.size() >= capabilities().maxRxChannels) {
        reportError(BackendError::ResourceExhausted,
                    tr("Massimo %1 canali RX su questo device.").arg(kMaxLogicalRxChannels));
        return kInvalidChannel;
    }
    const ChannelId id = m_nextChannelId++;
    m_channels.insert(id, config);
    return id;
}

void RtlSdrBackend::destroyRxChannel(ChannelId channel) { m_channels.remove(channel); }

QList<ChannelId> RtlSdrBackend::channels() const
{
    QList<ChannelId> ids = m_channels.keys();
    std::sort(ids.begin(), ids.end());
    return ids;
}

void RtlSdrBackend::setFrequency(ChannelId channel, qint64 hz)
{
    auto it = m_channels.find(channel);
    if (it == m_channels.end()) {
        reportError(BackendError::NotFound, tr("Canale %1 inesistente.").arg(channel));
        return;
    }
    it->frequencyHz = hz;
}

void RtlSdrBackend::setDemod(ChannelId channel, DemodMode mode)
{
    auto it = m_channels.find(channel);
    if (it != m_channels.end())
        it->mode = mode;
}

void RtlSdrBackend::setFilter(ChannelId channel, int lowHz, int highHz)
{
    auto it = m_channels.find(channel);
    if (it != m_channels.end()) {
        it->filterLowHz = lowHz;
        it->filterHighHz = highHz;
    }
}

PanId RtlSdrBackend::createPanadapter(const PanConfig &config)
{
    if (m_panadapters.size() >= capabilities().maxPanadapters) {
        reportError(BackendError::ResourceExhausted, tr("Troppi panadattatori aperti."));
        return kInvalidPan;
    }
    const PanId id = m_nextPanId++;
    m_panadapters.insert(id, config);
    return id;
}

void RtlSdrBackend::destroyPanadapter(PanId pan) { m_panadapters.remove(pan); }

void RtlSdrBackend::setPtt(bool transmit)
{
    if (transmit)
        reportError(BackendError::Unsupported, tr("RTL-SDR è un ricevitore di sola ricezione."));
}

void RtlSdrBackend::setTxFrequency(qint64 hz) { Q_UNUSED(hz) }

SampleRing *RtlSdrBackend::iqStream(ChannelId channel) const
{
    Q_UNUSED(channel)
    return m_iqRing.get();
}

SampleRing *RtlSdrBackend::audioStream(ChannelId channel) const
{
    Q_UNUSED(channel)
    return nullptr;
}

SampleRing *RtlSdrBackend::spectrumStream(PanId pan) const
{
    Q_UNUSED(pan)
    return nullptr;
}

double RtlSdrBackend::setGainReduction(double db)
{
    const double operatorGain = m_gainDb >= 0.0 ? m_gainDb : m_autoGainDb;
    const double minimumGain = m_profile.gainTenthsDb.isEmpty()
        ? 0.0 : *std::min_element(m_profile.gainTenthsDb.cbegin(),
                                  m_profile.gainTenthsDb.cend()) / 10.0;
    const double room = std::max(0.0, operatorGain - minimumGain);
    m_gainReductionDb = std::clamp(db, 0.0, room);

    if (m_worker) {
        const double effectiveGain = std::max(minimumGain, operatorGain - m_gainReductionDb);
        m_worker->requestGain(effectiveGain);
        qCInfo(dsdrHal) << "rtlsdr: correzione anti-overflow"
                        << m_gainReductionDb << "dB, gain" << effectiveGain << "dB";
    }
    return m_gainReductionDb;
}

QVariant RtlSdrBackend::nativeCommand(const QString &command, const QVariantMap &args)
{
    if (command == QLatin1String("rtlsdr.setGain")) {
        m_gainDb = args.value(QStringLiteral("db"), -1.0).toDouble();
        m_gainReductionDb = 0.0;
        if (m_worker)
            m_worker->requestGain(m_gainDb);
        return m_gainDb;
    }
    if (command == QLatin1String("rtlsdr.gainRange")) {
        const double min = m_profile.gainTenthsDb.isEmpty()
            ? 0.0 : m_profile.gainTenthsDb.first() / 10.0;
        const double max = m_profile.gainTenthsDb.isEmpty()
            ? 49.6 : m_profile.gainTenthsDb.last() / 10.0;
        return QVariantMap{{QStringLiteral("min"), min},
                           {QStringLiteral("max"), max},
                           {QStringLiteral("hasAgc"), true}};
    }
    if (command == QLatin1String("rtlsdr.setPpm")) {
        m_ppm = args.value(QStringLiteral("ppm"), 0).toInt();
        if (m_worker)
            m_worker->requestPpm(m_ppm);
        return m_ppm;
    }
    if (command == QLatin1String("rtlsdr.ppm"))
        return m_ppm;
    if (command == QLatin1String("rtlsdr.setBiasTee")) {
        m_biasTee = args.value(QStringLiteral("enabled"), false).toBool();
        if (m_worker)
            m_worker->requestBiasTee(m_biasTee);
        return m_biasTee;
    }
    if (command == QLatin1String("rtlsdr.biasTee"))
        return m_biasTee;
    if (command == QLatin1String("rtlsdr.setDirectSampling")) {
        const int mode = std::clamp(args.value(QStringLiteral("mode"), 0).toInt(), 0, 2);
        m_directSampling = mode;
        if (m_worker)
            m_worker->requestDirectSampling(mode);
        emit capabilitiesChanged();
        return m_directSampling;
    }
    if (command == QLatin1String("rtlsdr.directSampling"))
        return m_directSampling;
    if (command == QLatin1String("rtlsdr.setOffsetTuning")) {
        m_offsetTuning = args.value(QStringLiteral("enabled"), false).toBool();
        if (m_worker)
            m_worker->requestOffsetTuning(m_offsetTuning);
        return m_offsetTuning;
    }
    if (command == QLatin1String("rtlsdr.tuner"))
        return m_profile.tuner;
    if (command == QLatin1String("rtlsdr.driver"))
        return QStringLiteral("librtlsdr");
    return IRadioBackend::nativeCommand(command, args);
}

} // namespace dsdr::hal::rtlsdr
