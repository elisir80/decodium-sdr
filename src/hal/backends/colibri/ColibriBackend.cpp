// SPDX-License-Identifier: GPL-3.0-or-later
#include "hal/backends/colibri/ColibriBackend.h"
#include "hal/HalLog.h"

#include <QTimer>

#include <algorithm>
#include <cmath>

namespace dsdr::hal::colibri {

namespace {

/// ~1,4 s di IQ a 3,072 MS/s (due float per coppia). La libreria consegna a
/// raffiche dal proprio thread: un ring generoso evita che una pausa del DSP
/// diventi un buco udibile.
constexpr std::size_t kIqRingFloats = 1 << 23;

/// Il ricevitore è uno solo, ma i canali che l'utente apre sono logici: vivono
/// dentro la banda campionata e li demodula il DSP client.
constexpr int kMaxLogicalRxChannels = 4;

/// Copertura del ricevitore. Sopra i 55 MHz si entra nelle zone di Nyquist
/// successive dell'ADC a 122,88 MHz: ricezione possibile ma con filtro esterno
/// e senza garanzie, quindi non la si dichiara.
constexpr qint64 kMinFrequencyHz = 100'000;
constexpr qint64 kMaxFrequencyHz = 55'000'000;

} // namespace

ColibriBackend::ColibriBackend(QObject *parent)
    : IRadioBackend(parent)
    , m_iqRing(std::make_unique<SampleRing>(kIqRingFloats))
{
}

ColibriBackend::~ColibriBackend()
{
    close();
}

QString ColibriBackend::displayName() const
{
    return m_device.isValid() ? m_device.displayName : QStringLiteral("ColibriNANO");
}

BackendCapabilities ColibriBackend::capabilities() const
{
    BackendCapabilities caps;

    caps.maxRxChannels = kMaxLogicalRxChannels;
    // Un solo ricevitore fisico: non c'è nulla con cui essere coerenti.
    caps.coherentRx = false;
    caps.maxPanadapters = 1;
    caps.tx = TxSupport::None;      // non esiste trasmettitore

    caps.demod = DspLocation::Client;
    caps.spectrum = DspLocation::Client;
    caps.agc = DspLocation::Client;

    for (const int rate : kSampleRatesHz)
        caps.sampleRates.append(static_cast<double>(rate));
    caps.defaultSampleRate = 768000.0;

    caps.minFrequencyHz = kMinFrequencyHz;
    caps.maxFrequencyHz = kMaxFrequencyHz;

    caps.hasHardwareFilters = true;   // passa-basso commutato dal device
    caps.hasPreamp = true;            // −31,5…+6 dB, preamplificatore e attenuatore insieme
    caps.hasAttenuator = true;
    caps.adcBits = 14;

    caps.remoteCapable = false;
    caps.multiClient = false;         // la libreria apre il device in esclusiva
    caps.supportsRecording = true;

    caps.nativePanels.append(QStringLiteral("ColibriDevicePanel"));

    return caps;
}

void ColibriBackend::setState(BackendState state)
{
    if (m_state == state)
        return;
    m_state = state;
    emit stateChanged(state);
}

void ColibriBackend::reportError(BackendError::Code code, const QString &message, bool fatal)
{
    BackendError error;
    error.code = code;
    error.message = message;
    error.detail = QStringLiteral("backend=colibri device=%1").arg(m_device.deviceId);
    error.fatal = fatal;

    qCWarning(dsdrHal) << "colibri:" << message;
    emit errorOccurred(error);
    if (fatal)
        setState(BackendState::Error);
}

// ─────────────────────────────────────────────────────────────────────────────
// Discovery
// ─────────────────────────────────────────────────────────────────────────────

void ColibriBackend::startDiscovery()
{
    setState(BackendState::Discovering);

    QTimer::singleShot(0, this, [this] {
        QString error;
        if (!ColibriLibrary::instance().ensureLoaded(QString(), &error)) {
            // Non è un errore fatale: significa solo che su questa macchina
            // non c'è la libreria, quindi non ci sono device da offrire.
            //
            // Lo si dice una volta sola. La discovery si rilancia a ogni
            // apertura del pannello delle sorgenti, e ripetere lo stesso
            // messaggio a ogni giro riempie il log di righe identiche fino a
            // nascondere quelle che contano davvero. Il confronto è sul testo:
            // se un giorno la libreria comparisse, o fallisse per un motivo
            // diverso, il messaggio cambia e torna a farsi vedere.
            if (error != m_lastLoadError) {
                m_lastLoadError = error;
                qCInfo(dsdrHal) << "colibri:" << error;
            }
            if (!m_open)
                setState(BackendState::Idle);
            emit discoveryFinished();
            return;
        }

        // Sondare il bus FTDI mentre uno stream è attivo non è documentato
        // come sicuro: non lo si scopre sul ricevitore di qualcuno.
        if (ColibriLibrary::instance().deviceInUse()) {
            emit discoveryFinished();
            return;
        }

        const std::uint32_t count = ColibriLibrary::instance().deviceCount();
        for (std::uint32_t index = 0; index < count; ++index) {
            DeviceDescriptor device;
            device.backendId = backendId();
            // La libreria non espone un seriale: l'identità è l'indice.
            device.deviceId = QStringLiteral("colibrinano-%1").arg(index);
            device.displayName = count > 1
                ? QStringLiteral("ColibriNANO #%1").arg(index + 1)
                : QStringLiteral("ColibriNANO");
            device.model = QStringLiteral("ColibriNANO");
            device.transport = QStringLiteral("usb");
            device.extra.insert(QStringLiteral("index"), index);
            emit deviceFound(device);
        }

        if (!m_open)
            setState(BackendState::Idle);
        emit discoveryFinished();
    });
}

void ColibriBackend::stopDiscovery()
{
    // La discovery è a colpo singolo: non c'è nulla da fermare, si torna solo
    // a riposo se era in corso.
    if (!m_open && m_state == BackendState::Discovering)
        setState(BackendState::Idle);
}

// ─────────────────────────────────────────────────────────────────────────────
// Apertura e streaming
// ─────────────────────────────────────────────────────────────────────────────

void ColibriBackend::open(const DeviceDescriptor &device)
{
    if (m_open)
        close();

    if (!device.isValid() || device.backendId != backendId()) {
        reportError(BackendError::NotFound,
                    tr("Il device %1 non appartiene al backend ColibriNANO.").arg(device.key()),
                    true);
        return;
    }

    QString error;
    if (!ColibriLibrary::instance().ensureLoaded(QString(), &error)) {
        reportError(BackendError::NotFound, error, true);
        return;
    }

    const auto index = static_cast<std::uint32_t>(
        device.extra.value(QStringLiteral("index"), 0).toUInt());

    setState(BackendState::Connecting);

    if (!ColibriLibrary::instance().open(&m_handle, index) || !m_handle) {
        reportError(BackendError::TransportError,
                    tr("Apertura del ColibriNANO fallita. È già in uso da un altro programma?"),
                    true);
        setState(BackendState::Error);
        return;
    }

    m_device = device;
    m_open = true;
    ColibriLibrary::instance().setDeviceInUse(true);

    // La frequenza deve stare nella copertura: aprire a 100 MHz darebbe solo
    // rumore e sembrerebbe un guasto.
    m_centerHz = std::clamp(m_centerHz, kMinFrequencyHz, kMaxFrequencyHz);

    ColibriLibrary::instance().setFrequency(m_handle, static_cast<std::uint32_t>(m_centerHz));
    ColibriLibrary::instance().setPreamp(m_handle, m_preampDb);

    if (!startStream()) {
        close();
        return;
    }

    setState(BackendState::Streaming);
    emit capabilitiesChanged();
    emit centerFrequencyChanged(m_centerHz);
    emit sampleRateChanged(m_sampleRate);

    qCInfo(dsdrHal) << "colibri: aperto indice" << index << "a" << m_sampleRate << "S/s";
}

bool ColibriBackend::startStream()
{
    const int index = sampleRateIndex(static_cast<int>(m_sampleRate));
    if (index < 0) {
        reportError(BackendError::Unsupported,
                    tr("Frequenza di campionamento %1 non offerta dal device.").arg(m_sampleRate));
        return false;
    }

    m_iqRing->clear();
    m_sequence.store(0, std::memory_order_relaxed);

    if (!ColibriLibrary::instance().start(m_handle, index, &ColibriBackend::rxTrampoline, this)) {
        reportError(BackendError::TransportError, tr("Avvio del flusso IQ fallito."), true);
        return false;
    }

    m_streaming = true;
    return true;
}

void ColibriBackend::stopStream()
{
    if (!m_streaming || !m_handle)
        return;

    // Dopo `stop()` la libreria non invoca più la callback: da qui in poi il
    // ring non ha più un produttore.
    ColibriLibrary::instance().stop(m_handle);
    m_streaming = false;
}

void ColibriBackend::close()
{
    stopDiscovery();

    if (!m_open)
        return;

    stopStream();

    if (m_handle) {
        ColibriLibrary::instance().close(m_handle);
        m_handle = nullptr;
    }
    ColibriLibrary::instance().setDeviceInUse(false);

    m_open = false;
    m_channels.clear();
    m_panadapters.clear();
    m_device = DeviceDescriptor();
    setState(BackendState::Idle);
}

// ─────────────────────────────────────────────────────────────────────────────
// Percorso dei campioni
// ─────────────────────────────────────────────────────────────────────────────

bool DSDR_COLIBRI_CALL ColibriBackend::rxTrampoline(ColibriComplex *iq,
                                                    std::uint32_t length,
                                                    bool adcOverload,
                                                    void *user)
{
    if (auto *self = static_cast<ColibriBackend *>(user))
        self->onSamples(iq, length, adcOverload);
    return true;   // false direbbe alla libreria di smettere
}

void ColibriBackend::onSamples(ColibriComplex *iq, std::uint32_t length, bool adcOverload)
{
    if (!iq || length == 0)
        return;

    m_adcOverload.store(adcOverload, std::memory_order_relaxed);
    if (adcOverload)
        m_overloadBlocks.fetch_add(1, std::memory_order_relaxed);

    const std::size_t floats = static_cast<std::size_t>(length) * 2;
    std::size_t written = 0;

    if (m_conjugate.load(std::memory_order_relaxed)) {
        // Calibrazione delle bande laterali: coniugare scambia USB e LSB.
        // Si scrive campione per campione perché il segno cambia solo la
        // parte immaginaria; il costo è trascurabile rispetto al DSP.
        for (std::uint32_t i = 0; i < length; ++i) {
            const float pair[2] = {iq[i].re, -iq[i].im};
            if (m_iqRing->write(pair, 2) != 2)
                break;
            written += 2;
        }
    } else {
        // `ColibriComplex` è {float re; float im}: già interleaved come il
        // nostro ring, quindi un solo memcpy senza toccare i campioni.
        written = m_iqRing->write(reinterpret_cast<const float *>(iq), floats);
    }

    const std::size_t frames = written / 2;
    const std::size_t dropped = length - frames;

    IqFrame frame;
    frame.channel = kInvalidChannel;
    frame.sequence = m_sequence.fetch_add(1, std::memory_order_relaxed) + 1;
    frame.centerFrequencyHz = m_centerHz;
    frame.sampleRate = m_sampleRate;
    frame.frameCount = static_cast<quint32>(frames);
    frame.droppedFrames = static_cast<quint32>(dropped);
    frame.timestampNs = 0;

    // Emesso dal thread della libreria: i consumatori si collegano queued e
    // leggono i campioni dal ring (§4.1).
    emit iqFrameReady(frame);
}

// ─────────────────────────────────────────────────────────────────────────────
// Controlli
// ─────────────────────────────────────────────────────────────────────────────

void ColibriBackend::setCenterFrequency(qint64 hz)
{
    if (!capabilities().coversFrequency(hz)) {
        reportError(BackendError::Unsupported,
                    tr("Il ColibriNANO riceve da %1 a %2 MHz.")
                        .arg(kMinFrequencyHz / 1e6, 0, 'f', 1)
                        .arg(kMaxFrequencyHz / 1e6, 0, 'f', 0));
        return;
    }
    if (m_centerHz == hz)
        return;

    m_centerHz = hz;
    if (m_handle)
        ColibriLibrary::instance().setFrequency(m_handle, static_cast<std::uint32_t>(hz));
    emit centerFrequencyChanged(hz);
}

void ColibriBackend::setSampleRate(double rate)
{
    if (sampleRateIndex(static_cast<int>(rate)) < 0) {
        reportError(BackendError::Unsupported,
                    tr("Frequenza di campionamento %1 non offerta dal device.").arg(rate));
        return;
    }
    if (qFuzzyCompare(m_sampleRate, rate))
        return;

    // Il rate si sceglie all'avvio del flusso: cambiarlo significa fermare e
    // riavviare, non scrivere un registro.
    const bool wasStreaming = m_streaming;
    if (wasStreaming)
        stopStream();

    m_sampleRate = rate;

    if (wasStreaming && !startStream()) {
        // Il riavvio è fallito: la sessione non è più utilizzabile.
        close();
        return;
    }

    emit sampleRateChanged(rate);
}

ChannelId ColibriBackend::createRxChannel(const RxChannelConfig &config)
{
    if (!m_open) {
        reportError(BackendError::TransportError, tr("Device non aperto."));
        return kInvalidChannel;
    }
    if (m_channels.size() >= kMaxLogicalRxChannels) {
        reportError(BackendError::ResourceExhausted,
                    tr("Massimo %1 canali RX su questo device.").arg(kMaxLogicalRxChannels));
        return kInvalidChannel;
    }

    const ChannelId id = m_nextChannelId++;
    m_channels.insert(id, config);
    return id;
}

void ColibriBackend::destroyRxChannel(ChannelId channel)
{
    m_channels.remove(channel);
}

QList<ChannelId> ColibriBackend::channels() const
{
    QList<ChannelId> ids = m_channels.keys();
    std::sort(ids.begin(), ids.end());
    return ids;
}

void ColibriBackend::setFrequency(ChannelId channel, qint64 hz)
{
    auto it = m_channels.find(channel);
    if (it == m_channels.end()) {
        reportError(BackendError::NotFound, tr("Canale %1 inesistente.").arg(channel));
        return;
    }
    // Il canale vive dentro la banda campionata: la sintonia è del DSP client.
    it->frequencyHz = hz;
}

void ColibriBackend::setDemod(ChannelId channel, DemodMode mode)
{
    auto it = m_channels.find(channel);
    if (it != m_channels.end())
        it->mode = mode;
}

void ColibriBackend::setFilter(ChannelId channel, int lowHz, int highHz)
{
    auto it = m_channels.find(channel);
    if (it != m_channels.end()) {
        it->filterLowHz = lowHz;
        it->filterHighHz = highHz;
    }
}

PanId ColibriBackend::createPanadapter(const PanConfig &config)
{
    // Un ricevitore, un panadattatore: la banda mostrata È la frequenza di
    // campionamento.
    if (!m_panadapters.isEmpty()) {
        reportError(BackendError::ResourceExhausted,
                    tr("Il ColibriNANO offre un solo panadattatore."));
        return kInvalidPan;
    }
    const PanId id = m_nextPanId++;
    m_panadapters.insert(id, config);
    return id;
}

void ColibriBackend::destroyPanadapter(PanId pan)
{
    m_panadapters.remove(pan);
}

void ColibriBackend::setPtt(bool transmit)
{
    if (transmit) {
        reportError(BackendError::Unsupported,
                    tr("Il ColibriNANO è un ricevitore: non ha trasmettitore."));
    }
}

void ColibriBackend::setTxFrequency(qint64 hz)
{
    Q_UNUSED(hz)
}

SampleRing *ColibriBackend::iqStream(ChannelId channel) const
{
    Q_UNUSED(channel)
    return m_iqRing.get();
}

SampleRing *ColibriBackend::audioStream(ChannelId channel) const
{
    Q_UNUSED(channel)
    return nullptr;
}

SampleRing *ColibriBackend::spectrumStream(PanId pan) const
{
    Q_UNUSED(pan)
    return nullptr;
}

QVariant ColibriBackend::nativeCommand(const QString &command, const QVariantMap &args)
{
    // Preamplificatore e attenuatore sono la stessa manopola: un solo valore
    // fra −31,5 e +6 dB.
    if (command == QLatin1String("colibri.setPreamp")) {
        const float db = std::clamp(static_cast<float>(args.value(QStringLiteral("db"), 0.0).toDouble()),
                                    kMinPreampDb, kMaxPreampDb);
        m_preampDb = db;
        if (m_handle)
            ColibriLibrary::instance().setPreamp(m_handle, db);
        return QVariant(db);
    }
    if (command == QLatin1String("colibri.preampRange")) {
        return QVariantMap{{QStringLiteral("min"), kMinPreampDb},
                           {QStringLiteral("max"), kMaxPreampDb},
                           {QStringLiteral("value"), m_preampDb}};
    }

    // Calibrazione delle bande laterali: se USB e LSB risultano scambiate,
    // la convenzione di segno del flusso è l'opposta.
    if (command == QLatin1String("colibri.setConjugate")) {
        const bool value = args.value(QStringLiteral("enabled"), false).toBool();
        m_conjugate.store(value, std::memory_order_relaxed);
        return QVariant(value);
    }
    if (command == QLatin1String("colibri.conjugate"))
        return QVariant(m_conjugate.load(std::memory_order_relaxed));

    if (command == QLatin1String("colibri.health")) {
        return QVariantMap{
            {QStringLiteral("adcOverload"), m_adcOverload.load(std::memory_order_relaxed)},
            {QStringLiteral("overloadBlocks"),
             static_cast<qulonglong>(m_overloadBlocks.load(std::memory_order_relaxed))},
            {QStringLiteral("library"), ColibriLibrary::instance().libraryPath()},
        };
    }

    return IRadioBackend::nativeCommand(command, args);
}

} // namespace dsdr::hal::colibri
