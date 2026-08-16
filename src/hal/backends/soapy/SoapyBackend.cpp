// SPDX-License-Identifier: GPL-3.0-or-later
#include "hal/backends/soapy/SoapyBackend.h"
#include "hal/HalLog.h"
#include "hal/backends/rtlsdr/RtlSdrCapabilities.h"
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
constexpr qint64 kMinimumTuningFrequencyHz = 100'000;
constexpr qint64 kMaximumTuningFrequencyHz = 1'766'000'000;

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

bool SoapyBackend::isRtlSdr() const
{
    const QString driver = m_profile.isValid() ? m_profile.driver
                                                : m_device.extra.value(QStringLiteral("driver")).toString();
    return driver.compare(QStringLiteral("rtlsdr"), Qt::CaseInsensitive) == 0;
}

QString SoapyBackend::deviceIdentity() const
{
    return QStringList{m_profile.driver,
                       m_profile.hardware,
                       m_profile.label,
                       m_device.model,
                       m_device.displayName,
                       m_device.extra.value(QStringLiteral("product")).toString(),
                       m_device.extra.value(QStringLiteral("manufacturer")).toString()}
        .join(QLatin1Char(' '));
}

bool SoapyBackend::autoIfUsesLsb() const
{
    return m_activeDemod == DemodMode::Lsb || m_activeDemod == DemodMode::DigL;
}

qint64 SoapyBackend::ifReferenceFrequency(qint64 fallbackFrequencyHz) const
{
    auto reference = m_channels.cend();
    for (auto it = m_channels.cbegin(); it != m_channels.cend(); ++it) {
        if (reference == m_channels.cend() || it.key() < reference.key())
            reference = it;
    }
    return reference == m_channels.cend() ? fallbackFrequencyHz : reference->frequencyHz;
}

rtlsdr::TuningPlan SoapyBackend::tuningPlanFor(qint64 dialFrequencyHz) const
{
    // SoapySDR serve anche Airspy, HackRF, Lime, USRP… L'IF e il Q ADC sono
    // specifici del RTL2832: per ogni altro device la sintonia resta quella
    // nativa, senza una traduzione IQ che nessuno gli ha chiesto.
    if (!isRtlSdr()) {
        rtlsdr::TuningPlan direct;
        direct.dialFrequencyHz = dialFrequencyHz;
        direct.selectedInputFrequencyHz = dialFrequencyHz;
        direct.hardwareCenterFrequencyHz = dialFrequencyHz;
        direct.logicalCenterFrequencyHz = dialFrequencyHz;
        return direct;
    }

    rtlsdr::TuningRequest request;
    request.dialFrequencyHz = dialFrequencyHz;
    request.sampleRate = m_sampleRate;
    request.ifEnabled = m_ifEnabled;
    request.ifFrequencyHz = m_ifFrequencyHz;
    request.usbShiftHz = m_ifUsbShiftHz;
    request.lsbShiftHz = m_ifLsbShiftHz;
    request.sideband = m_ifSideband == 2
        || (m_ifSideband == 0 && autoIfUsesLsb()) ? rtlsdr::IfSideband::Lsb
                                                    : rtlsdr::IfSideband::Usb;
    request.spectrumInverted = m_ifSpectrumInverted;
    request.logicalSelectedOffsetHz = m_ifEnabled
        ? ifReferenceFrequency(dialFrequencyHz) - dialFrequencyHz : 0;
    return rtlsdr::makeTuningPlan(request, kMinimumTuningFrequencyHz,
                                  kMaximumTuningFrequencyHz);
}

bool SoapyBackend::hardwarePlanIsSupported(const rtlsdr::TuningPlan &plan) const
{
    if (!m_profile.isValid())
        return true;

    qint64 minimum = m_profile.minFrequencyHz;
    qint64 maximum = m_profile.maxFrequencyHz;
    if (rtlsdr::isRtlSdrBlogV4Identity(deviceIdentity())) {
        minimum = rtlsdr::kDirectSamplingMinimumFrequencyHz;
    } else if (m_directSampling != 0) {
        minimum = rtlsdr::kDirectSamplingMinimumFrequencyHz;
        maximum = rtlsdr::kDirectSamplingMaximumFrequencyHz;
    }
    return plan.selectedInputFrequencyHz >= minimum
        && plan.selectedInputFrequencyHz <= maximum
        && plan.hardwareCenterFrequencyHz >= minimum
        && plan.hardwareCenterFrequencyHz <= maximum;
}

bool SoapyBackend::applyTuningPlan(qint64 dialFrequencyHz, bool notifyCenter)
{
    const rtlsdr::TuningPlan plan = tuningPlanFor(dialFrequencyHz);
    if (m_directSampling != 0) {
        const auto reason = rtlsdr::directSamplingBlockReason(deviceIdentity(),
                                                               plan.selectedInputFrequencyHz);
        if (reason != rtlsdr::DirectSamplingBlockReason::None) {
            reportError(BackendError::Unsupported,
                        reason == rtlsdr::DirectSamplingBlockReason::BlogV4UsesUpconverter
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

    const bool physicalChange = m_hardwareCenterHz != plan.hardwareCenterFrequencyHz
        || !qFuzzyCompare(m_appliedBasebandTranslationHz + 1.0,
                          plan.basebandTranslationHz + 1.0)
        || m_appliedSpectrumInverted != plan.spectrumInverted;
    const bool centerChanged = m_centerHz != plan.logicalCenterFrequencyHz;
    m_centerHz = plan.logicalCenterFrequencyHz;
    m_hardwareCenterHz = plan.hardwareCenterFrequencyHz;
    m_appliedBasebandTranslationHz = plan.basebandTranslationHz;
    m_appliedSpectrumInverted = plan.spectrumInverted;
    if (m_worker && physicalChange) {
        m_iqRing->clear();
        m_worker->requestBasebandTransform(plan.basebandTranslationHz,
                                           plan.spectrumInverted);
        m_worker->requestFrequency(plan.hardwareCenterFrequencyHz);
    }
    qCInfo(dsdrHal) << "soapy: piano RTL-SDR"
                    << "dial" << plan.dialFrequencyHz
                    << "input" << plan.selectedInputFrequencyHz
                    << "hardware" << plan.hardwareCenterFrequencyHz
                    << "shift" << plan.basebandTranslationHz
                    << "RX offset" << plan.logicalSelectedOffsetHz
                    << "if" << plan.ifEnabled
                    << "inverted" << plan.spectrumInverted;
    if (notifyCenter && centerChanged)
        emit centerFrequencyChanged(m_centerHz);
    return true;
}

QVariantMap SoapyBackend::directSamplingInfo() const
{
    const rtlsdr::TuningPlan plan = tuningPlanFor(m_centerHz);
    const bool v4 = rtlsdr::isRtlSdrBlogV4Identity(deviceIdentity());
    const bool rangeOk = rtlsdr::isDirectSamplingFrequency(plan.selectedInputFrequencyHz);
    QString message;
    if (!isRtlSdr()) {
        message = tr("Il dispositivo Soapy attivo non è un RTL-SDR.");
    } else if (v4) {
        message = tr("RTL-SDR Blog V4 usa il tuner con upconverter HF automatico: Q ADC non disponibile.");
    } else if (!rangeOk) {
        message = tr("Direct sampling Q ADC: sintonizzare prima fra 500 kHz e 24 MHz.");
    } else {
        message = tr("Q ADC: ricezione HF diretta fra 500 kHz e 24 MHz.");
    }
    return {{QStringLiteral("supported"), isRtlSdr() && !v4},
            {QStringLiteral("canEnableNow"), isRtlSdr() && !v4 && rangeOk},
            {QStringLiteral("active"), m_directSampling != 0},
            {QStringLiteral("minimumHz"), rtlsdr::kDirectSamplingMinimumFrequencyHz},
            {QStringLiteral("maximumHz"), rtlsdr::kDirectSamplingMaximumFrequencyHz},
            {QStringLiteral("message"), message}};
}

QVariantMap SoapyBackend::ifSettings() const
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
    if (m_profile.isValid()) {
        BackendCapabilities caps = capabilitiesFrom(m_profile);
        if (isRtlSdr()) {
            caps.nativePanels.removeAll(QStringLiteral("SoapyDevicePanel"));
            caps.nativePanels.append(QStringLiteral("RtlSdrDevicePanel"));
            if (rtlsdr::isRtlSdrBlogV4Identity(deviceIdentity())) {
                caps.minFrequencyHz = rtlsdr::kDirectSamplingMinimumFrequencyHz;
            } else if (m_directSampling != 0) {
                caps.minFrequencyHz = rtlsdr::kDirectSamplingMinimumFrequencyHz;
                caps.maxFrequencyHz = rtlsdr::kDirectSamplingMaximumFrequencyHz;
                caps.hasPreamp = false;
                caps.maxGainReductionDb = 0.0;
            }
        }
        return caps;
    }

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
            const QString manufacturer = kwargValue(kwargs, "manufacturer");
            const QString product = kwargValue(kwargs, "product");
            const QString tuner = kwargValue(kwargs, "tuner");

            // Il seriale è l'unico identificatore stabile fra un riavvio e
            // l'altro; senza, si ripiega sull'indice, che cambia se l'utente
            // sposta la chiavetta di porta.
            device.deviceId = serial.isEmpty()
                ? QStringLiteral("%1:%2").arg(driver).arg(kwargValue(kwargs, "index"))
                : QStringLiteral("%1:%2").arg(driver, serial);

            device.displayName = label.isEmpty()
                ? QStringLiteral("SoapySDR %1").arg(driver)
                : label;
            // SoapyRTLSDR reports the V4 as `product=Blog V4` but gives the
            // generic RTL2832U label precedence. Keep both: the product is
            // what the operator needs to recognise in a multi-radio list.
            if (!product.isEmpty()
                && !device.displayName.contains(product, Qt::CaseInsensitive)) {
                device.displayName = product + QStringLiteral(" — ") + device.displayName;
            }
            device.model = kwargValue(kwargs, "hardware");
            if (device.model.isEmpty())
                device.model = product;
            device.serial = serial;
            device.transport = QStringLiteral("soapy");
            device.extra.insert(QStringLiteral("args"), argsFromKwargs(kwargs));
            device.extra.insert(QStringLiteral("driver"), driver);
            device.extra.insert(QStringLiteral("manufacturer"), manufacturer);
            device.extra.insert(QStringLiteral("product"), product);
            device.extra.insert(QStringLiteral("tuner"), tuner);

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
    // `openAndRun()` occupa il thread finché lo stream non è stato chiuso,
    // quindi l'event loop non può ricevere un quit queued. Il segnale viene
    // emesso dal worker alla fine della routine e deve fermare direttamente
    // il QThread prima che il chiamante di close() lo attenda.
    connect(worker, &SoapyWorker::finished, m_thread, &QThread::quit,
            Qt::DirectConnection);

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
    const rtlsdr::TuningPlan initialPlan = tuningPlanFor(m_centerHz);
    m_hardwareCenterHz = initialPlan.hardwareCenterFrequencyHz;
    m_appliedBasebandTranslationHz = initialPlan.basebandTranslationHz;
    m_appliedSpectrumInverted = initialPlan.spectrumInverted;
    worker->requestBasebandTransform(initialPlan.basebandTranslationHz,
                                     initialPlan.spectrumInverted);
    m_thread->start();

    QMetaObject::invokeMethod(worker, "openAndRun", Qt::QueuedConnection,
                              Q_ARG(QString, args),
                              Q_ARG(qint64, initialPlan.hardwareCenterFrequencyHz),
                              Q_ARG(double, m_sampleRate));
}

void SoapyBackend::onDeviceOpened(const SoapyDeviceProfile &profile)
{
    m_profile = profile;
    m_autoGainDb = safeAutoGainDb(profile);
    m_gainReductionDb = 0.0;

    // Ora si sa davvero che device è: si valida il piano logico/IF contro la
    // sua copertura, non soltanto il numero che il VFO mostra.
    const BackendCapabilities caps = capabilities();
    if (!applyTuningPlan(m_centerHz, false)) {
        m_centerHz = std::clamp(m_centerHz, caps.minFrequencyHz, caps.maxFrequencyHz);
        applyTuningPlan(m_centerHz, false);
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
        m_thread->requestInterruption();
        m_thread->quit();
        // Un driver USB può impiegare più di un timeout di readStream per
        // completare deactivateStream/closeStream. Attendere abbastanza evita
        // di distruggere un QThread ancora vivo, che Qt considera fatale.
        if (!m_thread->wait(10000)) {
            qCWarning(dsdrHal) << "soapy: il thread di ingest non si è fermato in tempo";
            m_thread->terminate();
            m_thread->wait(5000);
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
    if (m_centerHz == hz)
        return;
    if (!m_profile.isValid()) {
        m_centerHz = hz;
        const rtlsdr::TuningPlan plan = tuningPlanFor(hz);
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

void SoapyBackend::setSampleRate(double rate)
{
    if (!capabilities().sampleRates.contains(rate)) {
        reportError(BackendError::Unsupported,
                    tr("Frequenza di campionamento %1 non supportata dal device.").arg(rate));
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
    m_activeDemod = config.mode;
    if (m_ifEnabled && m_channels.size() == 1)
        applyTuningPlan(m_centerHz, false);
    return id;
}

void SoapyBackend::destroyRxChannel(ChannelId channel)
{
    if (m_channels.remove(channel) > 0 && m_ifEnabled)
        applyTuningPlan(m_centerHz, false);
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
    if (m_ifEnabled)
        applyTuningPlan(m_centerHz, false);
}

void SoapyBackend::setDemod(ChannelId channel, DemodMode mode)
{
    auto it = m_channels.find(channel);
    if (it != m_channels.end()) {
        it->mode = mode;
        if (m_activeDemod != mode) {
            const bool sidebandChanged = m_ifEnabled && m_ifSideband == 0
                && (autoIfUsesLsb() != (mode == DemodMode::Lsb || mode == DemodMode::DigL));
            m_activeDemod = mode;
            if (sidebandChanged)
                applyTuningPlan(m_centerHz, false);
        }
    }
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

double SoapyBackend::setGainReduction(double db)
{
    if (m_directSampling != 0) {
        m_gainReductionDb = 0.0;
        return 0.0;
    }
    const double operatorGain = m_gainDb >= 0.0 ? m_gainDb : m_autoGainDb;
    const double minimumGain = m_profile.minGainDb;
    const double room = std::max(0.0, operatorGain - minimumGain);
    m_gainReductionDb = std::clamp(db, 0.0, room);

    if (m_worker) {
        const double effectiveGain = std::max(minimumGain,
                                              operatorGain - m_gainReductionDb);
        m_worker->requestGain(effectiveGain);
        qCInfo(dsdrHal) << "soapy: correzione anti-overflow"
                        << m_gainReductionDb << "dB, gain" << effectiveGain << "dB";
    }
    return m_gainReductionDb;
}

QVariant SoapyBackend::nativeCommand(const QString &command, const QVariantMap &args)
{
    if (command.startsWith(QLatin1String("rtlsdr.")) && !isRtlSdr()) {
        reportError(BackendError::Unsupported,
                    tr("I controlli RTL-SDR sono disponibili solo con un driver SoapyRTLSDR."));
        return QVariant();
    }
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
        return QVariantMap{{QStringLiteral("min"), m_profile.minGainDb},
                           {QStringLiteral("max"), m_profile.maxGainDb},
                           {QStringLiteral("hasAgc"), m_profile.hasAgc && m_directSampling == 0}};
    }
    if (command == QLatin1String("rtlsdr.ppmSupported"))
        return false;
    if (command == QLatin1String("rtlsdr.ppm"))
        return 0;
    if (command == QLatin1String("rtlsdr.setPpm"))
        return 0;
    if (command == QLatin1String("rtlsdr.setBiasTee")) {
        const bool enabled = args.value(QStringLiteral("enabled"), false).toBool();
        if (m_worker)
            m_worker->requestDeviceSetting(QStringLiteral("biastee"),
                                           enabled ? QStringLiteral("true") : QStringLiteral("false"));
        return enabled;
    }
    if (command == QLatin1String("rtlsdr.setDirectSampling")) {
        const int requested = std::clamp(args.value(QStringLiteral("mode"), 0).toInt(), 0, 2);
        const int mode = requested == 0 ? 0 : 2;
        if (mode != 0) {
            const auto plan = tuningPlanFor(m_centerHz);
            const auto reason = rtlsdr::directSamplingBlockReason(deviceIdentity(),
                                                                    plan.selectedInputFrequencyHz);
            if (reason != rtlsdr::DirectSamplingBlockReason::None) {
                reportError(BackendError::Unsupported,
                            reason == rtlsdr::DirectSamplingBlockReason::BlogV4UsesUpconverter
                                ? tr("RTL-SDR Blog V4 usa l'upconverter HF: direct sampling non selezionabile.")
                                : tr("Per il direct sampling sintonizzare prima fra 500 kHz e 24 MHz."));
                return m_directSampling;
            }
        }
        const int previous = m_directSampling;
        m_directSampling = mode;
        if (!applyTuningPlan(m_centerHz, false)) {
            m_directSampling = previous;
            return m_directSampling;
        }
        if (m_worker)
            m_worker->requestDeviceSetting(QStringLiteral("direct_samp"), QString::number(mode));
        if (mode != 0 && m_offsetTuning) {
            m_offsetTuning = false;
            if (m_worker)
                m_worker->requestDeviceSetting(QStringLiteral("offset_tune"),
                                               QStringLiteral("false"));
        }
        emit capabilitiesChanged();
        return m_directSampling;
    }
    if (command == QLatin1String("rtlsdr.directSampling"))
        return m_directSampling;
    if (command == QLatin1String("rtlsdr.setOffsetTuning")) {
        const bool enabled = args.value(QStringLiteral("enabled"), false).toBool();
        if (enabled && m_directSampling != 0) {
            reportError(BackendError::Unsupported,
                        tr("L'offset tuning del tuner non esiste nel direct sampling Q ADC."));
            return false;
        }
        m_offsetTuning = enabled;
        if (m_worker)
            m_worker->requestDeviceSetting(QStringLiteral("offset_tune"),
                                           enabled ? QStringLiteral("true") : QStringLiteral("false"));
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
        const auto candidate = tuningPlanFor(m_centerHz);
        const bool stagedForDirectSampling = m_ifEnabled && m_directSampling == 0
            && !rtlsdr::isRtlSdrBlogV4Identity(deviceIdentity())
            && rtlsdr::isDirectSamplingFrequency(candidate.selectedInputFrequencyHz)
            && !hardwarePlanIsSupported(candidate);
        if (stagedForDirectSampling) {
            qCInfo(dsdrHal) << "soapy: IF pronta, attendo direct sampling"
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
        return m_profile.hardware;
    if (command == QLatin1String("rtlsdr.driver"))
        return QStringLiteral("SoapyRTLSDR");
    if (command == QLatin1String("soapy.setGain")) {
        const double db = args.value(QStringLiteral("db"), -1.0).toDouble();
        m_gainDb = db;
        m_gainReductionDb = 0.0;
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
