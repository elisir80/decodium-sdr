// SPDX-License-Identifier: GPL-3.0-or-later
#include "hal/backends/rtlsdr/RtlSdrBackend.h"

#include "dsp/SpscRing.h"
#include "hal/HalLog.h"
#include "hal/backends/rtlsdr/RtlSdrCapabilities.h"
#include "hal/backends/rtlsdr/RtlSdrIfReference.h"
#include "hal/backends/rtlsdr/RtlSdrWorker.h"

#include <rtl-sdr.h>

#include <QTimer>
#include <QThread>

#include <algorithm>
#include <array>

namespace dsdr::hal::rtlsdr {

namespace {

constexpr std::size_t kIqRingFloats = 1 << 23;
constexpr qint64 kMinimumTuningFrequencyHz = 100'000;
constexpr qint64 kMaximumTuningFrequencyHz = 1'766'000'000;
QString usbString(const char *value)
{
    return value ? QString::fromUtf8(value) : QString();
}

} // namespace

QString RtlSdrBackend::deviceIdentity() const
{
    return QStringList{m_profile.product,
                       m_device.model,
                       m_device.displayName,
                       m_device.extra.value(QStringLiteral("product")).toString()}
        .join(QLatin1Char(' '));
}

bool RtlSdrBackend::autoIfUsesLsb() const
{
    return ifReferenceUsesLsb(m_channels, m_activeDemod);
}

qint64 RtlSdrBackend::ifReferenceFrequency(qint64 fallbackFrequencyHz) const
{
    // Un'IF fissa descrive una radio, non un'intera banda RF. Il primo RX e'
    // quindi il suo VFO logico; gli eventuali RX aggiuntivi restano ricevitori
    // virtuali all'interno della stessa finestra IQ.
    auto reference = m_channels.cend();
    for (auto it = m_channels.cbegin(); it != m_channels.cend(); ++it) {
        if (reference == m_channels.cend() || it.key() < reference.key())
            reference = it;
    }
    return reference == m_channels.cend() ? fallbackFrequencyHz : reference->frequencyHz;
}

TuningPlan RtlSdrBackend::tuningPlanFor(qint64 dialFrequencyHz) const
{
    TuningRequest request;
    request.dialFrequencyHz = dialFrequencyHz;
    request.sampleRate = m_sampleRate;
    request.ifEnabled = m_ifEnabled;
    request.ifFrequencyHz = m_ifFrequencyHz;
    request.usbShiftHz = m_ifUsbShiftHz;
    request.lsbShiftHz = m_ifLsbShiftHz;
    request.sideband = m_ifSideband == 2
        || (m_ifSideband == 0 && autoIfUsesLsb()) ? IfSideband::Lsb : IfSideband::Usb;
    request.spectrumInverted = m_ifSpectrumInverted;
    request.logicalSelectedOffsetHz = m_ifEnabled
        ? ifReferenceFrequency(dialFrequencyHz) - dialFrequencyHz : 0;
    return makeTuningPlan(request, kMinimumTuningFrequencyHz, kMaximumTuningFrequencyHz);
}

bool RtlSdrBackend::hardwarePlanIsSupported(const TuningPlan &plan) const
{
    if (!m_profile.isValid())
        return true;

    qint64 minimum = m_profile.minFrequencyHz;
    qint64 maximum = m_profile.maxFrequencyHz;
    if (isRtlSdrBlogV4Identity(deviceIdentity())) {
        // Il V4 commuta autonomamente il proprio upconverter sotto la banda
        // del tuner: librtlsdr lo gestisce, anche se alcune versioni di
        // `get_tuner_type()` continuano a dichiarare 24 MHz come minimo.
        minimum = kDirectSamplingMinimumFrequencyHz;
    } else if (m_directSampling != 0) {
        minimum = kDirectSamplingMinimumFrequencyHz;
        maximum = kDirectSamplingMaximumFrequencyHz;
    }

    return plan.selectedInputFrequencyHz >= minimum
        && plan.selectedInputFrequencyHz <= maximum
        && plan.hardwareCenterFrequencyHz >= minimum
        && plan.hardwareCenterFrequencyHz <= maximum;
}

bool RtlSdrBackend::applyTuningPlan(qint64 dialFrequencyHz, bool notifyCenter)
{
    const TuningPlan plan = tuningPlanFor(dialFrequencyHz);
    if (m_directSampling != 0) {
        const DirectSamplingBlockReason reason = directSamplingBlockReason(
            deviceIdentity(), plan.selectedInputFrequencyHz);
        if (reason != DirectSamplingBlockReason::None) {
            reportError(BackendError::Unsupported,
                        reason == DirectSamplingBlockReason::BlogV4UsesUpconverter
                            ? tr("RTL-SDR Blog V4 usa l'upconverter HF: il direct sampling Q ADC non è disponibile.")
                            : tr("Il direct sampling RTL-SDR è disponibile solo tra 500 kHz e 24 MHz."));
            return false;
        }
    }
    if (!hardwarePlanIsSupported(plan)) {
        reportError(BackendError::Unsupported,
                    tr("Il percorso RTL-SDR selezionato non copre %1 Hz.")
                        .arg(plan.selectedInputFrequencyHz));
        return false;
    }

    const bool hardwareFrequencyChanged = m_hardwareCenterHz != plan.hardwareCenterFrequencyHz;
    const bool basebandTransformChanged = !qFuzzyCompare(m_appliedBasebandTranslationHz + 1.0,
                                                          plan.basebandTranslationHz + 1.0)
        || m_appliedSpectrumInverted != plan.spectrumInverted;
    const bool centerChanged = m_centerHz != plan.logicalCenterFrequencyHz;
    m_centerHz = plan.logicalCenterFrequencyHz;
    m_hardwareCenterHz = plan.hardwareCenterFrequencyHz;
    m_appliedBasebandTranslationHz = plan.basebandTranslationHz;
    m_appliedSpectrumInverted = plan.spectrumInverted;
    if (m_worker && basebandTransformChanged) {
        // La traslazione e' solo DSP e viene applicata nel callback: non
        // fermare USB per un movimento fine del VFO, altrimenti l'audio si
        // interrompe durante il trascinamento.
        m_worker->requestBasebandTransform(plan.basebandTranslationHz,
                                           plan.spectrumInverted);
    }
    if (m_worker && hardwareFrequencyChanged) {
        // I campioni nel ring appartengono al piano precedente: conservarli
        // farebbe disegnare per un istante IF e RF sulla stessa scala.
        m_iqRing->clear();
        m_worker->requestFrequency(plan.hardwareCenterFrequencyHz);
    }
    qCInfo(dsdrHal) << "rtlsdr: piano di sintonia"
                    << "dial" << plan.dialFrequencyHz
                    << "input" << plan.selectedInputFrequencyHz
                    << "hardware" << plan.hardwareCenterFrequencyHz
                    << "shift" << plan.basebandTranslationHz
                    << "RX offset" << plan.logicalSelectedOffsetHz
                    << "if" << plan.ifEnabled
                    << "inverted" << plan.spectrumInverted
                    << "IF policy" << m_ifSideband
                    << "auto LSB" << autoIfUsesLsb();
    if (notifyCenter && centerChanged)
        emit centerFrequencyChanged(m_centerHz);
    return true;
}

QVariantMap RtlSdrBackend::directSamplingInfo() const
{
    const TuningPlan plan = tuningPlanFor(m_centerHz);
    const DirectSamplingBlockReason reason = directSamplingBlockReason(
        deviceIdentity(), plan.selectedInputFrequencyHz);
    const bool v4 = isRtlSdrBlogV4Identity(deviceIdentity());
    const bool rangeOk = isDirectSamplingFrequency(plan.selectedInputFrequencyHz);
    QString message;
    if (v4) {
        message = tr("RTL-SDR Blog V4 usa il tuner con upconverter HF automatico: Q ADC non disponibile.");
    } else if (!rangeOk) {
        message = tr("Direct sampling Q ADC: sintonizzare prima fra 500 kHz e 24 MHz.");
    } else {
        message = tr("Q ADC: ricezione HF diretta fra 500 kHz e 24 MHz.");
    }
    return {{QStringLiteral("supported"), !v4},
            {QStringLiteral("canEnableNow"), reason == DirectSamplingBlockReason::None},
            {QStringLiteral("active"), m_directSampling != 0},
            {QStringLiteral("minimumHz"), kDirectSamplingMinimumFrequencyHz},
            {QStringLiteral("maximumHz"), kDirectSamplingMaximumFrequencyHz},
            {QStringLiteral("message"), message}};
}

QVariantMap RtlSdrBackend::ifSettings() const
{
    return {{QStringLiteral("enabled"), m_ifEnabled},
            {QStringLiteral("frequencyHz"), m_ifFrequencyHz},
            {QStringLiteral("sideband"), m_ifSideband == 2 ? QStringLiteral("lsb")
                                                             : m_ifSideband == 1 ? QStringLiteral("usb")
                                                                                 : QStringLiteral("auto")},
            {QStringLiteral("usbShiftHz"), m_ifUsbShiftHz},
            {QStringLiteral("lsbShiftHz"), m_ifLsbShiftHz},
            {QStringLiteral("spectrumInverted"), m_ifSpectrumInverted},
            {QStringLiteral("hardwareCenterHz"), m_hardwareCenterHz}};
}

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
    if (m_profile.isValid()) {
        BackendCapabilities caps = capabilitiesFrom(m_profile);
        // RtlSdrWorker riceve da librtlsdr il nome generico del tuner. Il nome
        // USB conservato nel descriptor e' quello che identifica davvero una
        // Blog V4, il cui upconverter copre HF anche in modalita' Tuner.
        if (isRtlSdrBlogV4Identity(deviceIdentity()))
            caps.minFrequencyHz = kDirectSamplingMinimumFrequencyHz;
        return caps;
    }

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

void RtlSdrBackend::setIqInputBlocked(bool blocked)
{
    if (m_iqInputBlocked == blocked)
        return;
    m_iqInputBlocked = blocked;
    if (blocked)
        m_iqRing->clear();
    if (m_worker)
        m_worker->requestStreamPause(blocked);
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
    worker->requestStreamPause(m_iqInputBlocked);
    const TuningPlan initialPlan = tuningPlanFor(m_centerHz);
    m_hardwareCenterHz = initialPlan.hardwareCenterFrequencyHz;
    m_appliedBasebandTranslationHz = initialPlan.basebandTranslationHz;
    m_appliedSpectrumInverted = initialPlan.spectrumInverted;
    worker->requestGain(m_gainDb);
    // Un device appena aperto parte sempre senza correzione. Evitare la
    // scrittura esplicita di 0: alcuni V4 la rifiutano pur ricevendo
    // perfettamente e trasformano un valore predefinito in un falso warning.
    if (m_ppm != 0)
        worker->requestPpm(m_ppm);
    worker->requestBiasTee(m_biasTee);
    // La modalita' normale del tuner e' gia' attiva dopo rtlsdr_open().
    // Chiedere inutilmente "direct sampling 0" fa stampare diagnostica
    // non strutturata da librtlsdr a ogni avvio.
    if (m_directSampling != 0)
        worker->requestDirectSampling(m_directSampling);
    worker->requestBasebandTransform(initialPlan.basebandTranslationHz,
                                     initialPlan.spectrumInverted);
    if (m_offsetTuning)
        worker->requestOffsetTuning(true);
    m_thread->start();
    QMetaObject::invokeMethod(worker, "openAndRun", Qt::QueuedConnection,
                              Q_ARG(int, index),
                              Q_ARG(QString, device.serial),
                              Q_ARG(qint64, initialPlan.hardwareCenterFrequencyHz),
                              Q_ARG(double, m_sampleRate));
}

void RtlSdrBackend::onDeviceOpened(const RtlSdrDeviceProfile &profile)
{
    m_profile = profile;
    m_profile.directSampling = m_directSampling != 0;
    m_autoGainDb = safeAutoGainTenthsDb(profile.gainTenthsDb) / 10.0;
    m_gainReductionDb = 0.0;
    const BackendCapabilities caps = capabilities();
    if (!applyTuningPlan(m_centerHz, false)) {
        // Il profilo appena letto può rivelare che la frequenza iniziale non
        // è nella via scelta. Si torna a una frequenza dichiarata, invece di
        // lasciare un centro grafico che l'hardware non sta ricevendo.
        m_centerHz = std::clamp(m_centerHz, caps.minFrequencyHz, caps.maxFrequencyHz);
        applyTuningPlan(m_centerHz, false);
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
    if (m_centerHz == hz)
        return;
    // Prima del profilo il worker non sa ancora che chiavetta sia: si accoda
    // comunque la sintonia e la si normalizza appena la lettura è pronta.
    if (!m_profile.isValid()) {
        m_centerHz = hz;
        const TuningPlan plan = tuningPlanFor(hz);
        m_hardwareCenterHz = plan.hardwareCenterFrequencyHz;
        m_appliedBasebandTranslationHz = plan.basebandTranslationHz;
        m_appliedSpectrumInverted = plan.spectrumInverted;
        if (m_worker) {
            m_worker->requestBasebandTransform(plan.basebandTranslationHz,
                                               plan.spectrumInverted);
            m_worker->requestFrequency(plan.hardwareCenterFrequencyHz);
        }
        emit centerFrequencyChanged(hz);
        return;
    }
    applyTuningPlan(hz);
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
    const double previousRate = m_sampleRate;
    m_sampleRate = rate;
    if (!applyTuningPlan(m_centerHz, false)) {
        m_sampleRate = previousRate;
        return;
    }
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
    m_activeDemod = config.mode;
    if (m_ifEnabled && m_channels.size() == 1)
        applyTuningPlan(m_centerHz, false);
    return id;
}

void RtlSdrBackend::destroyRxChannel(ChannelId channel)
{
    if (m_channels.remove(channel) > 0 && m_ifEnabled)
        applyTuningPlan(m_centerHz, false);
}

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
    if (m_ifEnabled)
        applyTuningPlan(m_centerHz, false);
}

void RtlSdrBackend::setDemod(ChannelId channel, DemodMode mode)
{
    auto it = m_channels.find(channel);
    if (it != m_channels.end()) {
        const bool autoSidebandWasLsb = autoIfUsesLsb();
        it->mode = mode;
        m_activeDemod = mode;
        const bool sidebandChanged = m_ifEnabled && m_ifSideband == 0
            && autoSidebandWasLsb != autoIfUsesLsb();
        if (sidebandChanged)
            applyTuningPlan(m_centerHz, false);
    }
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
    if (m_directSampling != 0) {
        m_gainReductionDb = 0.0;
        return 0.0;
    }
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
    if (command == QLatin1String("rtlsdr.directSamplingInfo"))
        return directSamplingInfo();
    if (command == QLatin1String("rtlsdr.ifSettings"))
        return ifSettings();
    if (command == QLatin1String("rtlsdr.setGain")) {
        m_gainDb = args.value(QStringLiteral("db"), -1.0).toDouble();
        m_gainReductionDb = 0.0;
        if (m_worker && m_directSampling == 0)
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
                           {QStringLiteral("hasAgc"), m_directSampling == 0}};
    }
    if (command == QLatin1String("rtlsdr.setPpm")) {
        m_ppm = args.value(QStringLiteral("ppm"), 0).toInt();
        if (m_worker)
            m_worker->requestPpm(m_ppm);
        return m_ppm;
    }
    if (command == QLatin1String("rtlsdr.ppm"))
        return m_ppm;
    if (command == QLatin1String("rtlsdr.ppmSupported"))
        return true;
    if (command == QLatin1String("rtlsdr.setBiasTee")) {
        m_biasTee = args.value(QStringLiteral("enabled"), false).toBool();
        if (m_worker)
            m_worker->requestBiasTee(m_biasTee);
        return m_biasTee;
    }
    if (command == QLatin1String("rtlsdr.biasTee"))
        return m_biasTee;
    if (command == QLatin1String("rtlsdr.setDirectSampling")) {
        const int requested = std::clamp(args.value(QStringLiteral("mode"), 0).toInt(), 0, 2);
        // La scelta utile e verificata è il Q ADC. Conserviamo l'accettazione
        // del vecchio valore I per compatibilità del comando, ma non esponiamo
        // un percorso che non corrisponde al cablaggio HF convenzionale.
        const int mode = requested == 0 ? 0 : 2;
        if (mode != 0) {
            const TuningPlan plan = tuningPlanFor(m_centerHz);
            const DirectSamplingBlockReason reason = directSamplingBlockReason(
                deviceIdentity(), plan.selectedInputFrequencyHz);
            if (reason != DirectSamplingBlockReason::None) {
                reportError(BackendError::Unsupported,
                            reason == DirectSamplingBlockReason::BlogV4UsesUpconverter
                                ? tr("RTL-SDR Blog V4 usa l'upconverter HF: direct sampling non selezionabile.")
                                : tr("Per il direct sampling sintonizzare prima fra 500 kHz e 24 MHz."));
                return m_directSampling;
            }
        }
        const int previous = m_directSampling;
        m_directSampling = mode;
        m_profile.directSampling = mode != 0;
        if (!applyTuningPlan(m_centerHz, false)) {
            m_directSampling = previous;
            m_profile.directSampling = previous != 0;
            return m_directSampling;
        }
        if (m_worker)
            m_worker->requestDirectSampling(mode);
        if (mode != 0 && m_offsetTuning) {
            m_offsetTuning = false;
            if (m_worker)
                m_worker->requestOffsetTuning(false);
        }
        emit capabilitiesChanged();
        return m_directSampling;
    }
    if (command == QLatin1String("rtlsdr.directSampling"))
        return m_directSampling;
    if (command == QLatin1String("rtlsdr.setOffsetTuning")) {
        const bool requested = args.value(QStringLiteral("enabled"), false).toBool();
        if (requested && m_directSampling != 0) {
            reportError(BackendError::Unsupported,
                        tr("L'offset tuning del tuner non esiste nel direct sampling Q ADC."));
            return false;
        }
        m_offsetTuning = requested;
        if (m_worker)
            m_worker->requestOffsetTuning(m_offsetTuning);
        return m_offsetTuning;
    }
    if (command == QLatin1String("rtlsdr.setIfSettings")) {
        const bool oldEnabled = m_ifEnabled;
        const qint64 oldFrequency = m_ifFrequencyHz;
        const qint64 oldUsbShift = m_ifUsbShiftHz;
        const qint64 oldLsbShift = m_ifLsbShiftHz;
        const int oldSideband = m_ifSideband;
        const bool oldInverted = m_ifSpectrumInverted;

        m_ifEnabled = args.value(QStringLiteral("enabled"), false).toBool();
        m_ifFrequencyHz = std::clamp(args.value(QStringLiteral("frequencyHz"),
                                                 m_ifFrequencyHz).toLongLong(),
                                       kMinimumTuningFrequencyHz, kMaximumTuningFrequencyHz);
        m_ifUsbShiftHz = std::clamp(args.value(QStringLiteral("usbShiftHz"),
                                                m_ifUsbShiftHz).toLongLong(),
                                      qint64(-500'000), qint64(500'000));
        m_ifLsbShiftHz = std::clamp(args.value(QStringLiteral("lsbShiftHz"),
                                                m_ifLsbShiftHz).toLongLong(),
                                      qint64(-500'000), qint64(500'000));
        const QString sideband = args.value(QStringLiteral("sideband"),
                                             QStringLiteral("auto")).toString().trimmed().toLower();
        m_ifSideband = sideband == QLatin1String("lsb") ? 2
                     : sideband == QLatin1String("usb") ? 1 : 0;
        m_ifSpectrumInverted = args.value(QStringLiteral("spectrumInverted"), false).toBool();
        const TuningPlan candidate = tuningPlanFor(m_centerHz);
        const bool stagedForDirectSampling = m_ifEnabled && m_directSampling == 0
            && !isRtlSdrBlogV4Identity(deviceIdentity())
            && isDirectSamplingFrequency(candidate.selectedInputFrequencyHz)
            && !hardwarePlanIsSupported(candidate);
        if (stagedForDirectSampling) {
            // Un VFO può stare in VHF mentre l'IF della radio è in HF. Il
            // profilo IF va quindi memorizzato prima di poter selezionare il
            // Q ADC: rifiutarlo qui renderebbe impossibile quella sequenza.
            qCInfo(dsdrHal) << "rtlsdr: IF pronta, attendo direct sampling"
                            << candidate.selectedInputFrequencyHz;
        } else if (!applyTuningPlan(m_centerHz, false)) {
            m_ifEnabled = oldEnabled;
            m_ifFrequencyHz = oldFrequency;
            m_ifUsbShiftHz = oldUsbShift;
            m_ifLsbShiftHz = oldLsbShift;
            m_ifSideband = oldSideband;
            m_ifSpectrumInverted = oldInverted;
        }
        return ifSettings();
    }
    if (command == QLatin1String("rtlsdr.tuner"))
        return m_profile.tuner;
    if (command == QLatin1String("rtlsdr.driver"))
        return QStringLiteral("librtlsdr");
    return IRadioBackend::nativeCommand(command, args);
}

} // namespace dsdr::hal::rtlsdr
