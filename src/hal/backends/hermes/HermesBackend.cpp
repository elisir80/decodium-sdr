// SPDX-License-Identifier: GPL-3.0-or-later
#include "hal/backends/hermes/HermesBackend.h"
#include "hal/backends/hermes/HermesWorker.h"
#include "hal/HalLog.h"
#include "hal/RadioScout.h"

#include <QThread>

#include <algorithm>

namespace dsdr::hal::hermes {

namespace {

/// Un secondo e mezzo di coda a 192 kS/s. La radio consegna a raffiche di
/// pacchetti da 126 coppie, e una pausa del DSP non deve diventare un buco
/// udibile.
constexpr std::size_t kIqRingFloats = 1 << 20;

/// I canali che l'operatore apre sono logici: vivono dentro la banda
/// campionata e li demodula il DSP client. Il ricevitore della radio, in
/// questo backend, è uno.
constexpr int kMaxLogicalRxChannels = 4;

/// Copertura dell'Hermes-Lite 2: campionamento diretto a 76,8 MHz, quindi
/// prima zona di Nyquist fino a 38,4. Sopra si riceve nelle zone successive,
/// ma senza filtro d'ingresso e senza garanzie — e qui non lo si dichiara.
constexpr qint64 kMinFrequencyHz = 10'000;
constexpr qint64 kMaxFrequencyHz = 38'400'000;

/// Il guadagno d'ingresso dell'Hermes-Lite 2, che è tutto ciò che ha: niente
/// preamplificatore e attenuatore separati, una sola scala.
constexpr double kMinGainDb = -12.0;
constexpr double kMaxGainDb = 48.0;

} // namespace

HermesBackend::HermesBackend(QObject *parent)
    : IRadioBackend(parent)
    , m_iqRing(std::make_unique<SampleRing>(kIqRingFloats))
{
}

HermesBackend::~HermesBackend()
{
    close();
}

QString HermesBackend::displayName() const
{
    return m_model.isEmpty() ? QStringLiteral("OpenHPSDR") : m_model;
}

BackendCapabilities HermesBackend::capabilities() const
{
    BackendCapabilities caps;

    caps.maxRxChannels = kMaxLogicalRxChannels;
    caps.coherentRx = true;   // i canali nascono dallo stesso flusso IQ
    caps.maxPanadapters = 4;

    // Il protocollo trasmette, e nei pacchetti che mandiamo il posto per i
    // campioni c'è già. Ma finché non è provato su una radio vera resta
    // `None`: meglio niente PTT che un PTT che manda in aria qualcosa che
    // nessuno ha misurato.
    caps.tx = TxSupport::None;

    caps.demod = DspLocation::Client;
    caps.spectrum = DspLocation::Client;
    caps.agc = DspLocation::Client;
    caps.modulation = DspLocation::Client;

    // Le quattro velocità del protocollo, non una in più: una che il
    // protocollo non sa esprimere verrebbe ignorata dalla radio, che
    // continuerebbe alla precedente mentre il DSP calcola tutto sull'altra.
    caps.sampleRates = {48000.0, 96000.0, 192000.0, 384000.0};
    caps.defaultSampleRate = 192000.0;
    caps.minFrequencyHz = kMinFrequencyHz;
    caps.maxFrequencyHz = kMaxFrequencyHz;

    caps.hasHardwareFilters = false;
    caps.hasPreamp = true;      // è una manopola sola, da −12 a +48 dB
    caps.hasAttenuator = true;
    caps.adcBits = 12;          // l'ADC dell'Hermes-Lite 2
    caps.maxGainReductionDb = kMaxGainDb - kMinGainDb;

    caps.remoteCapable = true;  // vive in rete: è raggiungibile da un'altra stanza
    caps.multiClient = false;   // il protocollo 1 parla con un solo programma
    caps.supportsRecording = true;
    caps.nativePanels = {QStringLiteral("HermesDevicePanel")};
    return caps;
}

void HermesBackend::setState(BackendState state)
{
    if (m_state == state)
        return;
    m_state = state;
    emit stateChanged(state);
}

void HermesBackend::reportError(BackendError::Code code, const QString &message, bool fatal)
{
    qCWarning(dsdrHal) << "hermes:" << message;
    emit errorOccurred(BackendError{code, message, QString(), fatal});
    if (fatal)
        setState(BackendState::Error);
}

void HermesBackend::startDiscovery()
{
    if (m_discovering)
        return;
    m_discovering = true;
    setState(BackendState::Discovering);

    // La ricerca è la stessa che serve a dire «c'è una radio in rete»: si
    // riusa invece di riscriverla, e chi trova risponde al protocollo che
    // questo backend parla.
    if (!m_scout) {
        m_scout = new RadioScout(this);
        connect(m_scout, &RadioScout::radioFound, this, [this](const ScoutedRadio &found) {
            if (found.family != QLatin1String("OpenHPSDR"))
                return;

            DeviceDescriptor device;
            device.backendId = backendId();
            // Il MAC come chiave: l'indirizzo IP lo cambia il DHCP, e con lui
            // si perderebbero le impostazioni per-radio.
            device.deviceId = found.identity;
            device.displayName = found.model;
            device.model = found.model;
            device.serial = found.identity;
            device.transport = QStringLiteral("udp");
            device.address = found.address;
            device.extra.insert(QStringLiteral("detail"), found.detail);
            emit deviceFound(device);
        });
        connect(m_scout, &RadioScout::finished, this, [this] {
            m_discovering = false;
            setState(m_open ? BackendState::Streaming : BackendState::Idle);
            emit discoveryFinished();
        });
    }
    m_scout->start(3);
}

void HermesBackend::stopDiscovery()
{
    if (m_scout)
        m_scout->stop();
    m_discovering = false;
}

void HermesBackend::open(const DeviceDescriptor &device)
{
    close();

    m_address = QHostAddress(device.address);
    if (m_address.isNull()) {
        reportError(BackendError::NotFound,
                    tr("Indirizzo non valido: %1").arg(device.address), true);
        return;
    }

    m_device = device;
    m_model = device.model;
    setState(BackendState::Connecting);

    m_iqRing->clear();
    m_sequence = 0;
    m_lostPackets = 0;

    auto *worker = new HermesWorker(m_iqRing.get());
    worker->configure(m_address, m_sampleRate, m_centerHz);

    m_thread = new QThread(this);
    m_thread->setObjectName(QStringLiteral("dsdr-hermes"));
    worker->moveToThread(m_thread);
    connect(m_thread, &QThread::finished, worker, &QObject::deleteLater);

    // DirectConnection: il descrittore si costruisce nel thread di rete, e i
    // campioni sono già nel ring quando il segnale parte (§4.1).
    connect(worker, &HermesWorker::framesProduced, this,
            [this](quint32 frames, quint32 dropped, bool overload) {
                m_adcOverload.store(overload, std::memory_order_relaxed);

                IqFrame frame;
                frame.channel = kInvalidChannel;
                frame.sequence = ++m_sequence;
                frame.centerFrequencyHz = m_centerHz;
                frame.sampleRate = m_sampleRate;
                frame.frameCount = frames;
                frame.droppedFrames = dropped;
                frame.timestampNs = 0;
                emit iqFrameReady(frame);
            },
            Qt::DirectConnection);

    connect(worker, &HermesWorker::packetsLost, this,
            [this](quint64 total) { m_lostPackets = total; });
    connect(worker, &HermesWorker::failed, this, [this](const QString &reason) {
        reportError(BackendError::TransportError, reason, true);
    });

    m_worker = worker;
    m_thread->start();
    QMetaObject::invokeMethod(worker, "start", Qt::QueuedConnection);

    m_open = true;
    setState(BackendState::Streaming);
    qCInfo(dsdrHal) << "hermes: aperta" << m_model << "su" << device.address;
}

void HermesBackend::close()
{
    stopDiscovery();

    if (m_thread) {
        // L'arresto va detto alla radio prima di staccare il thread, o
        // continuerebbe a mandare campioni verso una porta chiusa.
        if (m_worker)
            QMetaObject::invokeMethod(m_worker, "stop", Qt::BlockingQueuedConnection);
        m_thread->quit();
        m_thread->wait();
        delete m_thread;
        m_thread = nullptr;
        m_worker = nullptr;
    }

    if (!m_open)
        return;

    m_open = false;
    m_channels.clear();
    m_panadapters.clear();
    m_device = DeviceDescriptor();
    setState(BackendState::Idle);
}

void HermesBackend::setCenterFrequency(qint64 hz)
{
    if (!capabilities().coversFrequency(hz)) {
        reportError(BackendError::Unsupported,
                    tr("L'Hermes-Lite 2 riceve da %1 kHz a %2 MHz.")
                        .arg(kMinFrequencyHz / 1000)
                        .arg(kMaxFrequencyHz / 1e6, 0, 'f', 1));
        return;
    }
    if (m_centerHz == hz)
        return;

    m_centerHz = hz;
    if (m_worker) {
        QMetaObject::invokeMethod(m_worker, "setCenterFrequency", Qt::QueuedConnection,
                                  Q_ARG(qint64, hz));
    }
    emit centerFrequencyChanged(hz);
}

void HermesBackend::setSampleRate(double rate)
{
    const QList<double> rates = capabilities().sampleRates;
    if (!rates.contains(rate)) {
        reportError(BackendError::Unsupported,
                    tr("Velocità %1 non prevista dal protocollo.").arg(rate));
        return;
    }
    if (qFuzzyCompare(m_sampleRate, rate))
        return;

    m_sampleRate = rate;
    // Non serve fermare e riavviare: la velocità viaggia in un registro, e la
    // radio cambia al pacchetto successivo. Il ring però va svuotato, o i
    // campioni della velocità vecchia uscirebbero come se fossero della nuova.
    m_iqRing->clear();
    if (m_worker) {
        QMetaObject::invokeMethod(m_worker, "setSampleRate", Qt::QueuedConnection,
                                  Q_ARG(double, rate));
    }
    emit sampleRateChanged(rate);
}

ChannelId HermesBackend::createRxChannel(const RxChannelConfig &config)
{
    if (m_channels.size() >= kMaxLogicalRxChannels) {
        reportError(BackendError::ResourceExhausted,
                    tr("Massimo %1 canali.").arg(kMaxLogicalRxChannels));
        return kInvalidChannel;
    }
    const ChannelId id = m_nextChannelId++;
    m_channels.insert(id, config);
    return id;
}

void HermesBackend::destroyRxChannel(ChannelId channel)
{
    m_channels.remove(channel);
}

QList<ChannelId> HermesBackend::channels() const
{
    return m_channels.keys();
}

void HermesBackend::setFrequency(ChannelId channel, qint64 hz)
{
    // I canali sono del DSP client: la loro frequenza è un offset dentro la
    // banda, e la radio non ne sa nulla.
    Q_UNUSED(channel)
    Q_UNUSED(hz)
}

void HermesBackend::setDemod(ChannelId channel, DemodMode mode)
{
    Q_UNUSED(channel)
    Q_UNUSED(mode)
}

void HermesBackend::setFilter(ChannelId channel, int lowHz, int highHz)
{
    Q_UNUSED(channel)
    Q_UNUSED(lowHz)
    Q_UNUSED(highHz)
}

PanId HermesBackend::createPanadapter(const PanConfig &config)
{
    const PanId id = m_nextPanId++;
    m_panadapters.insert(id, config);
    return id;
}

void HermesBackend::destroyPanadapter(PanId pan)
{
    m_panadapters.remove(pan);
}

void HermesBackend::setPtt(bool transmit)
{
    // `tx` è `None`: la UI non costruisce il PTT, e se qualcuno arriva qui
    // dall'interfaccia rigctl la risposta è non fare nulla — non mandare in
    // aria una portante che nessuno ha misurato.
    Q_UNUSED(transmit)
}

void HermesBackend::setTxFrequency(qint64 hz)
{
    Q_UNUSED(hz)
}

void HermesBackend::pushGain()
{
    const double effective = std::clamp(m_operatorGainDb - m_gainReductionDb,
                                        kMinGainDb, kMaxGainDb);
    if (m_worker) {
        QMetaObject::invokeMethod(m_worker, "setGainDb", Qt::QueuedConnection,
                                  Q_ARG(double, effective));
    }
}

double HermesBackend::setGainReduction(double db)
{
    // Si scende dal livello scelto dall'operatore, che resta il tetto: la
    // guardia toglie guadagno, non decide quale sia quello giusto.
    const double room = m_operatorGainDb - kMinGainDb;
    m_gainReductionDb = std::clamp(db, 0.0, room);
    pushGain();
    return m_gainReductionDb;
}

SampleRing *HermesBackend::iqStream(ChannelId channel) const
{
    Q_UNUSED(channel)
    return m_iqRing.get();
}

SampleRing *HermesBackend::audioStream(ChannelId channel) const
{
    Q_UNUSED(channel)
    return nullptr;   // nessun audio demodulato a bordo
}

SampleRing *HermesBackend::spectrumStream(PanId pan) const
{
    Q_UNUSED(pan)
    return nullptr;   // lo spettro lo calcola il DSP Engine
}

QVariant HermesBackend::nativeCommand(const QString &command, const QVariantMap &args)
{
    if (command == QLatin1String("hermes.setGain")) {
        m_operatorGainDb = std::clamp(args.value(QStringLiteral("db")).toDouble(),
                                      kMinGainDb, kMaxGainDb);
        // Rimettere le mani sulla manopola azzera la riduzione in corso: il
        // nuovo valore è quello che l'operatore vuole, non una base da cui
        // ricominciare a sottrarre.
        m_gainReductionDb = 0.0;
        pushGain();
        return m_operatorGainDb;
    }

    if (command == QLatin1String("hermes.gainRange")) {
        return QVariantMap{{QStringLiteral("min"), kMinGainDb},
                           {QStringLiteral("max"), kMaxGainDb},
                           {QStringLiteral("value"), m_operatorGainDb}};
    }

    if (command == QLatin1String("hermes.health")) {
        return QVariantMap{
            {QStringLiteral("adcOverload"), m_adcOverload.load(std::memory_order_relaxed)},
            {QStringLiteral("lostPackets"), QVariant::fromValue(m_lostPackets)},
            {QStringLiteral("address"), m_device.address},
            {QStringLiteral("detail"), m_device.extra.value(QStringLiteral("detail"))},
        };
    }

    return IRadioBackend::nativeCommand(command, args);
}

} // namespace dsdr::hal::hermes
