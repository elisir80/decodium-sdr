// SPDX-License-Identifier: GPL-3.0-or-later
#include "hal/backends/flex/FlexBackend.h"
#include "hal/backends/flex/FlexClient.h"
#include "hal/backends/flex/FlexProtocol.h"
#include "hal/backends/flex/FlexVita.h"

#include "dsp/FirDesign.h"
#include "hal/HalLog.h"

#include <QNetworkDatagram>
#include <QNetworkInterface>
#include <QTimer>
#include <QUdpSocket>

#include <algorithm>

namespace dsdr::hal::flex {

namespace {

/// Capienza del ring IQ: circa mezzo secondo a 192 kS/s, coppie I/Q.
constexpr std::size_t kIqRingFloats = 192000 * 2 / 2;

/// Le velocità che un flusso DAX IQ può avere. Non sono libere: sono quelle
/// che la radio accetta, e chiederne una fuori elenco fa nascere il flusso a
/// 48 kS/s senza dirlo.
constexpr double kSampleRates[] = {24000.0, 48000.0, 96000.0, 192000.0};

/// Quanto si aspetta un pacchetto prima di dire che non arriva niente.
///
/// Tre secondi. Il flusso, quando parte, parte subito: se dopo tre secondi non
/// è arrivato niente, o la sequenza non è quella giusta per questo firmware o
/// c'è un firewall in mezzo — e sono due cose che si risolvono in modi diversi,
/// per questo il messaggio le nomina entrambe.
constexpr int kSilenceMs = 3000;

/// La larghezza del panadapter che si chiede. La radio la usa per decidere
/// quanti punti mandare; qui non se ne fa niente — lo spettro lo calcoliamo
/// noi dall'IQ — ma il panadapter va creato lo stesso, perché è l'oggetto a
/// cui il flusso si lega.
constexpr int kPanWidth = 800;
constexpr int kPanHeight = 200;

/// La frequenza a cui il motore TX produce audio.
constexpr double kEngineAudioRate = 48000.0;

/// Capienza del ring di trasmissione: un quinto di secondo a 48 kHz. Più
/// lungo vorrebbe dire una voce che arriva in aria in ritardo su quella che si
/// sta dicendo.
constexpr std::size_t kTxRingFrames = 9600;

/// Quanti campioni si tirano fuori dal ring per volta.
constexpr std::size_t kTxChunkFrames = 1024;

/// Ogni quanto si svuota il ring verso la radio. Cinque millisecondi è la
/// durata di un pacchetto a 24 kHz: si manda quello che c'è, quando c'è.
constexpr int kTxPumpMs = 5;

/// Il guinzaglio del PTT, in secondi.
///
/// Non è una comodità: è una radio che trasmette. Se il canale di comando cade
/// mentre si è in aria, o il programma si impianta, nessuno manderebbe più
/// `xmit 0` — e una portante resta in frequenza finché qualcuno non stacca
/// l'alimentazione. Due minuti sono più di qualunque chiamata e meno di
/// qualunque danno.
constexpr int kTransmitWatchdogSeconds = 120;

} // namespace

FlexBackend::FlexBackend(QObject *parent)
    : IRadioBackend(parent)
    , m_client(std::make_unique<FlexClient>())
    , m_iqRing(std::make_unique<SampleRing>(kIqRingFloats))
    , m_txRing(std::make_unique<SampleRing>(kTxRingFrames))
{
    // Tutto quello che serve alla trasmissione nasce qui, una volta sola: a
    // PTT premuto non si alloca più niente.
    const int factor = static_cast<int>(kEngineAudioRate / kTxAudioRate);
    constexpr double kTxCutoffHz = 10'800.0;   // sotto i 12 kHz di Nyquist a 24 kS/s
    constexpr double kTxTransitionHz = 2'000.0;
    m_txDecimator.configure(
        dsp::designLowpass(kTxCutoffHz, kEngineAudioRate,
                           dsp::estimateTaps(kTxTransitionHz, kEngineAudioRate)),
        factor);
    m_txScratch.resize(kTxChunkFrames);
    m_txComplexIn.resize(kTxChunkFrames);
    m_txComplexOut.resize(kTxChunkFrames / static_cast<std::size_t>(factor) + 1);
    m_txPacketBuffer.resize(kTxAudioSamplesPerPacket);
    m_txDatagram.reserve((4 + kTxAudioSamplesPerPacket * 2) * 4);

    connect(m_client.get(), &FlexClient::connected, this, &FlexBackend::onCommandConnected);
    connect(m_client.get(), &FlexClient::failed, this, &FlexBackend::onCommandFailed);
    connect(m_client.get(), &FlexClient::responseReceived, this, &FlexBackend::onResponse);
    connect(m_client.get(), &FlexClient::disconnected, this, [this] {
        if (m_open)
            reportError(BackendError::Code::TransportError,
                        tr("Il FlexRadio ha chiuso il canale di comando."), true);
    });
}

FlexBackend::~FlexBackend()
{
    close();
}

QString FlexBackend::displayName() const
{
    return m_device.displayName.isEmpty() ? QStringLiteral("FlexRadio 6000")
                                          : m_device.displayName;
}

BackendCapabilities FlexBackend::capabilities() const
{
    BackendCapabilities caps;

    // Un Flex demodula a bordo, ma qui prendiamo l'IQ: la demodulazione è
    // nostra, e con essa tutta la catena della SPEC-003.
    caps.maxRxChannels = 4;
    caps.coherentRx = true;
    caps.maxPanadapters = 1;
    // La trasmissione c'è: PTT sul canale di comando e voce in VITA-49 verso
    // la porta dati della radio. Mezzo duplex, come la radio.
    caps.tx = TxSupport::Ptt;

    caps.demod = DspLocation::Client;
    caps.modulation = DspLocation::Device;
    caps.agc = DspLocation::Client;
    caps.spectrum = DspLocation::Client;

    for (const double rate : kSampleRates)
        caps.sampleRates.append(rate);
    caps.defaultSampleRate = 192000.0;

    caps.minFrequencyHz = 30'000;
    caps.maxFrequencyHz = 54'000'000;
    caps.hasHardwareFilters = true;
    caps.hasPreamp = true;
    caps.hasAttenuator = true;
    caps.adcBits = 16;
    caps.multiClient = true;      // SmartSDR gira quasi sempre accanto
    caps.remoteCapable = true;
    caps.supportsRecording = true;
    return caps;
}

void FlexBackend::setState(BackendState state)
{
    if (m_state == state)
        return;
    m_state = state;
    emit stateChanged(state);
}

void FlexBackend::reportError(BackendError::Code code, const QString &message, bool fatal)
{
    qCWarning(dsdrHal) << "flex:" << message;
    emit errorOccurred(BackendError{code, message, QString(), fatal});
    if (fatal)
        setState(BackendState::Error);
}

QString FlexBackend::stepName(Step step)
{
    switch (step) {
    case Step::UdpPort:      return tr("dichiarazione della porta UDP");
    case Step::CreateStream: return tr("creazione del flusso DAX IQ");
    case Step::CreatePan:    return tr("creazione del panadapter");
    case Step::BindStream:   return tr("collegamento del flusso al panadapter");
    case Step::Streaming:    return tr("attesa dei campioni");
    case Step::CreateTxStream: return tr("creazione del flusso audio di trasmissione");
    case Step::Idle:         break;
    }
    return tr("nessun passo");
}

// ── Ricerca ──────────────────────────────────────────────────────────────
//
// Il rilevamento in rete lo fa già il RadioScout del core, che ascolta gli
// annunci del Flex: questo backend non ne ha uno proprio, e non serve che ce
// l'abbia. Chi arriva qui ha già un indirizzo.
void FlexBackend::startDiscovery()
{
    m_discovering = false;
    emit discoveryFinished();
}

void FlexBackend::stopDiscovery()
{
    m_discovering = false;
}

QString FlexBackend::localAddressFor(const QHostAddress &radio) const
{
    // L'indirizzo con cui *questa* macchina raggiunge la radio, non il primo
    // della lista: su una macchina con due reti — una cablata verso la radio,
    // una senza fili verso casa — dire quello sbagliato fa partire un flusso
    // che non arriva mai, e non c'è niente nel comportamento che lo spieghi.
    for (const QNetworkInterface &interface : QNetworkInterface::allInterfaces()) {
        if (!interface.flags().testFlag(QNetworkInterface::IsUp)
            || interface.flags().testFlag(QNetworkInterface::IsLoopBack)) {
            continue;
        }
        for (const QNetworkAddressEntry &entry : interface.addressEntries()) {
            const QHostAddress address = entry.ip();
            if (address.protocol() != QAbstractSocket::IPv4Protocol)
                continue;
            if (radio.isInSubnet(address, entry.prefixLength()))
                return address.toString();
        }
    }
    return QString();
}

void FlexBackend::open(const DeviceDescriptor &device)
{
    close();

    m_device = device;
    m_radioAddress = QHostAddress(device.address);
    if (m_radioAddress.isNull()) {
        reportError(BackendError::Code::NotFound,
                    tr("Indirizzo non valido: %1").arg(device.address), true);
        return;
    }

    setState(BackendState::Connecting);

    // La porta UDP la sceglie il sistema: legarne una fissa vuol dire
    // litigare con SmartSDR, che gira quasi sempre sulla stessa macchina.
    m_udp = std::make_unique<QUdpSocket>();
    if (!m_udp->bind(QHostAddress::AnyIPv4, 0, QAbstractSocket::ShareAddress)) {
        reportError(BackendError::Code::PermissionDenied,
                    tr("Non riesco ad aprire una porta UDP per i campioni: %1")
                        .arg(m_udp->errorString()), true);
        return;
    }
    m_udpPort = m_udp->localPort();
    connect(m_udp.get(), &QUdpSocket::readyRead, this, &FlexBackend::readDatagrams);

    m_iqRing->clear();
    m_packets = 0;
    m_frames = 0;
    m_gaps = 0;
    m_lastPacketCount = -1;
    m_silenceReported = false;
    m_triedAlternateForm = false;
    m_panStreamId.clear();

    qCInfo(dsdrHal) << "flex: porta UDP locale" << m_udpPort;
    m_client->connectTo(device.address);
}

void FlexBackend::onCommandConnected()
{
    qCInfo(dsdrHal) << "flex: canale di comando aperto, handle" << m_client->handle();
    advance(Step::UdpPort);
}

void FlexBackend::onCommandFailed(const QString &reason)
{
    // Se il canale cade mentre si trasmette, il `xmit 0` non arriverebbe mai —
    // e la radio resterebbe in aria finché non ci pensa il guinzaglio, cioè
    // due minuti. Qui si sa già che è caduto: si smette subito, e si smette
    // anche di mandare pacchetti a un indirizzo che non risponde più.
    if (m_ptt) {
        m_ptt = false;
        if (m_txTimer)
            m_txTimer->stop();
        if (m_txWatchdog)
            m_txWatchdog->stop();
        qCWarning(dsdrHal) << "flex: PTT rilasciato, il canale di comando è caduto";
        emit pttChanged(false);
    }

    reportError(BackendError::Code::TransportError,
                tr("Canale di comando non aperto: %1").arg(reason), true);
}

void FlexBackend::advance(Step step)
{
    m_step = step;
    sendStep(step);
}

void FlexBackend::sendStep(Step step)
{
    QString command;

    switch (step) {
    case Step::UdpPort:
        command = commandUdpPort(m_udpPort);
        break;
    case Step::CreateStream: {
        const QString local = localAddressFor(m_radioAddress);
        if (local.isEmpty()) {
            reportError(BackendError::Code::InternalError,
                        tr("Non trovo l'indirizzo di questa macchina sulla rete della "
                           "radio: i campioni non saprebbero dove andare."), true);
            return;
        }
        // Le fonti pubbliche descrivono due forme del comando. Si prova la
        // prima; il firmware che risponde è l'unico documento che conti.
        command = m_triedAlternateForm
            ? QStringLiteral("stream create type=iq daxiq_channel=%1 ip=%2 port=%3")
                  .arg(m_daxChannel).arg(local).arg(m_udpPort)
            : commandCreateIqStream(m_daxChannel, local, m_udpPort);
        break;
    }
    case Step::CreatePan:
        command = commandCreatePanadapter(kPanWidth, kPanHeight);
        break;
    case Step::BindStream:
        command = commandBindIqStream(m_daxChannel, m_panStreamId,
                                      static_cast<int>(m_sampleRate), m_client->handle());
        break;
    case Step::CreateTxStream:
        // Le fonti pubbliche descrivono anche qui due forme, e valgono le
        // stesse regole del flusso di ricezione: si prova la prima e, se la
        // radio la rifiuta, la seconda.
        command = m_triedAlternateForm
            ? QStringLiteral("dax tx 1")
            : QStringLiteral("stream create type=dax_tx");
        break;
    case Step::Streaming:
    case Step::Idle:
        return;
    }

    m_pendingSequence = m_client->send(command);
    qCInfo(dsdrHal) << "flex:" << qPrintable(stepName(step)) << "→" << qPrintable(command);
}

void FlexBackend::onResponse(quint32 sequence, quint32 code, const QString &payload)
{
    if (sequence != m_pendingSequence || m_step == Step::Idle || m_step == Step::Streaming)
        return;

    if (code != 0) {
        // La forma alternativa del comando di creazione: si prova una volta
        // sola. Rifiutate entrambe, il problema non è la forma, ed è meglio
        // dirlo che continuare a provare.
        if (m_step == Step::CreateStream && !m_triedAlternateForm) {
            qCInfo(dsdrHal) << "flex: la prima forma di 'stream create' è stata rifiutata"
                            << Qt::hex << code << "— provo la seconda";
            m_triedAlternateForm = true;
            sendStep(Step::CreateStream);
            return;
        }

        reportError(BackendError::Code::ProtocolError,
                    tr("Il FlexRadio ha rifiutato la %1 con il codice 0x%2. "
                       "È il passo su cui guardare: la sequenza che apre il flusso "
                       "cambia fra le versioni maggiori del firmware.")
                        .arg(stepName(m_step)).arg(code, 0, 16), true);
        return;
    }

    switch (m_step) {
    case Step::UdpPort:
        advance(Step::CreateStream);
        break;

    case Step::CreateStream:
        // La risposta porta l'identificativo del flusso appena creato. Non ci
        // serve per legarlo — quello vuole il panadapter — ma averlo nel log
        // è metà della diagnosi quando qualcosa non torna.
        qCInfo(dsdrHal) << "flex: flusso DAX creato, risposta" << payload;
        advance(Step::CreatePan);
        break;

    case Step::CreatePan:
        // La risposta è l'identificativo del panadapter, che è quello a cui il
        // flusso va legato. Senza, il passo dopo non ha niente da nominare.
        m_panStreamId = payload.trimmed().section(QLatin1Char(','), 0, 0).trimmed();
        if (m_panStreamId.isEmpty()) {
            reportError(BackendError::Code::ProtocolError,
                        tr("Il panadapter è stato creato ma la radio non ne ha detto "
                           "l'identificativo: senza, il flusso non si può legare."), true);
            return;
        }
        qCInfo(dsdrHal) << "flex: panadapter" << m_panStreamId;
        advance(Step::BindStream);
        break;

    case Step::CreateTxStream: {
        // La risposta porta l'identificativo del flusso su cui mandare la
        // voce. Senza, non si trasmette — e non si finge di poterlo fare: si
        // dice, e la ricezione resta buona.
        bool ok = false;
        const QString id = payload.trimmed().section(QLatin1Char(','), 0, 0).trimmed();
        m_txStreamId = id.startsWith(QLatin1String("0x"))
            ? id.mid(2).toUInt(&ok, 16)
            : id.toUInt(&ok, 16);
        if (!ok || m_txStreamId == 0) {
            m_txStreamId = 0;
            reportError(BackendError::Code::ProtocolError,
                        tr("Il flusso audio di trasmissione è stato creato ma la radio "
                           "non ne ha detto l'identificativo: la ricezione funziona, "
                           "la trasmissione no."));
        } else {
            qCInfo(dsdrHal) << "flex: flusso audio di trasmissione" << Qt::hex << m_txStreamId;
        }
        m_step = Step::Streaming;
        break;
    }

    case Step::BindStream:
        m_step = Step::Streaming;
        m_open = true;
        m_sinceBind.start();
        setState(BackendState::Streaming);

        // Da qui in poi si aspettano i pacchetti. Se non arrivano lo si dice:
        // è la differenza fra un backend che tace e uno che si spiega.
        if (!m_silenceTimer) {
            m_silenceTimer = new QTimer(this);
            m_silenceTimer->setSingleShot(true);
            connect(m_silenceTimer, &QTimer::timeout, this, &FlexBackend::checkForSilence);
        }
        m_silenceTimer->start(kSilenceMs);
        qCInfo(dsdrHal) << "flex: sequenza completata, in attesa dei campioni su UDP"
                        << m_udpPort;

        // E poi si apre la strada del ritorno: il flusso su cui mandare la
        // voce. Dopo la ricezione e non prima, perché se qualcosa va storto qui
        // si perde la trasmissione e non l'ascolto.
        advance(Step::CreateTxStream);
        break;

    case Step::Streaming:
    case Step::Idle:
        break;
    }
}

void FlexBackend::checkForSilence()
{
    if (m_packets > 0)
        return;

    m_silenceReported = true;
    // Il messaggio nomina le due cause, perché si risolvono in due modi
    // diversi e chi legge deve sapere da dove cominciare.
    reportError(BackendError::Code::TransportError,
                tr("La sequenza è passata ma non arriva nessun campione sulla porta %1. "
                   "O il firewall di questa macchina blocca l'UDP in ingresso, "
                   "oppure questa versione di firmware vuole una sequenza diversa "
                   "per aprire il flusso DAX IQ. Il diario riporta ogni comando e "
                   "ogni risposta.")
                    .arg(m_udpPort));
}

void FlexBackend::readDatagrams()
{
    while (m_udp && m_udp->hasPendingDatagrams()) {
        const QNetworkDatagram datagram = m_udp->receiveDatagram();
        const QByteArray data = datagram.data();

        const VitaPacket packet = parseVita(data);
        if (!packet.valid)
            continue;

        if (!packet.isFloatPair() && !packet.isPair32()) {
            // Un formato che non sappiamo leggere si salta e si dice, invece
            // di interpretarlo lo stesso: campioni plausibili sono rumore che
            // sembra una banda, ed è l'errore che ha avuto FlexLib.
            qCWarning(dsdrHal) << "flex: pacchetto con formato non riconosciuto, classe"
                               << Qt::hex << packet.packetClass;
            continue;
        }

        // Il contatore a quattro bit dice i buchi. Non si può recuperare
        // niente, ma un flusso che perde pacchetti va saputo: sullo spettro si
        // vede come rumore in più, e si darebbe la colpa all'antenna.
        if (m_lastPacketCount >= 0) {
            const int expected = (m_lastPacketCount + 1) & 0xF;
            if (packet.packetCount != expected)
                ++m_gaps;
        }
        m_lastPacketCount = packet.packetCount;

        const std::size_t floats = static_cast<std::size_t>(packet.payloadBytes) / 4;
        if (floats == 0)
            continue;
        if (m_decoded.size() < floats)
            m_decoded.resize(floats);

        const std::size_t pairs = decodeIq(data, packet, m_decoded.data());
        if (pairs == 0)
            continue;

        const std::size_t samples = pairs * 2;
        if (m_iqRing->space() < samples)
            m_iqRing->discard(samples - m_iqRing->space());
        m_iqRing->write(m_decoded.data(), samples);

        ++m_packets;
        m_frames += pairs;

        IqFrame frame;
        frame.channel = kInvalidChannel;
        frame.sequence = ++m_sequence;
        frame.centerFrequencyHz = m_centerHz;
        frame.sampleRate = m_sampleRate;
        frame.frameCount = static_cast<quint32>(pairs);
        emit iqFrameReady(frame);
    }
}

void FlexBackend::close()
{
    // Prima di ogni altra cosa: se si stava trasmettendo, si smette. Chiudere
    // un backend lasciando la radio in aria è il difetto peggiore che questo
    // file possa avere.
    if (m_ptt)
        setPtt(false);

    if (m_txTimer)
        m_txTimer->stop();
    if (m_txWatchdog)
        m_txWatchdog->stop();
    if (m_silenceTimer)
        m_silenceTimer->stop();

    if (m_client && m_client->isConnected()) {
        // Si chiude il flusso prima del canale: lasciarlo aperto vuol dire
        // lasciare la radio a mandare pacchetti a una porta che non c'è più.
        if (m_step == Step::Streaming) {
            m_client->send(QStringLiteral("stream remove daxiq=%1").arg(m_daxChannel));
            if (m_txStreamId != 0)
                m_client->send(QStringLiteral("stream remove 0x%1")
                                   .arg(m_txStreamId, 0, 16));
        }
        m_client->disconnectFrom();
    }

    m_udp.reset();
    m_txStreamId = 0;
    m_step = Step::Idle;
    m_open = false;
    m_channels.clear();
    m_panadapters.clear();
    setState(BackendState::Idle);
}

// ── Comandi verso la radio ───────────────────────────────────────────────

void FlexBackend::setCenterFrequency(qint64 hz)
{
    if (m_centerHz == hz)
        return;
    m_centerHz = hz;
    if (m_client && m_client->isConnected() && !m_panStreamId.isEmpty()) {
        // La frequenza si dice al panadapter, in megahertz: è la sua unità, e
        // mandargliela in hertz produce una radio che va a sintonizzarsi
        // quattordici milioni di megahertz più in là.
        m_client->send(QStringLiteral("display pan set %1 center=%2")
                           .arg(m_panStreamId)
                           .arg(static_cast<double>(hz) / 1e6, 0, 'f', 6));
    }
    emit centerFrequencyChanged(hz);
}

void FlexBackend::setSampleRate(double rate)
{
    // Solo una delle velocità dichiarate: chiederne una fuori elenco fa
    // nascere il flusso a 48 kS/s senza che nessuno lo dica, e il DSP
    // calcolerebbe tutto sulla velocità sbagliata.
    const auto *found = std::find(std::begin(kSampleRates), std::end(kSampleRates), rate);
    if (found == std::end(kSampleRates) || qFuzzyCompare(m_sampleRate, rate))
        return;

    m_sampleRate = rate;
    if (m_client && m_client->isConnected() && m_step == Step::Streaming) {
        m_client->send(commandBindIqStream(m_daxChannel, m_panStreamId,
                                           static_cast<int>(rate), m_client->handle()));
    }
    emit sampleRateChanged(rate);
}

ChannelId FlexBackend::createRxChannel(const RxChannelConfig &config)
{
    const ChannelId id = m_nextChannelId++;
    m_channels.insert(id, config);
    return id;
}

void FlexBackend::destroyRxChannel(ChannelId channel)
{
    m_channels.remove(channel);
}

QList<ChannelId> FlexBackend::channels() const
{
    return m_channels.keys();
}

void FlexBackend::setFrequency(ChannelId channel, qint64 hz)
{
    auto it = m_channels.find(channel);
    if (it == m_channels.end())
        return;
    it->frequencyHz = hz;
}

void FlexBackend::setDemod(ChannelId channel, DemodMode mode)
{
    auto it = m_channels.find(channel);
    if (it == m_channels.end())
        return;
    it->mode = mode;
}

void FlexBackend::setFilter(ChannelId channel, int lowHz, int highHz)
{
    auto it = m_channels.find(channel);
    if (it == m_channels.end())
        return;
    it->filterLowHz = lowHz;
    it->filterHighHz = highHz;
}

PanId FlexBackend::createPanadapter(const PanConfig &config)
{
    const PanId id = m_nextPanId++;
    m_panadapters.insert(id, config);
    return id;
}

void FlexBackend::destroyPanadapter(PanId pan)
{
    m_panadapters.remove(pan);
}

void FlexBackend::setPtt(bool transmit)
{
    if (!m_client || !m_client->isConnected()) {
        if (transmit)
            reportError(BackendError::Code::TransportError,
                        tr("Non si trasmette senza canale di comando."));
        return;
    }

    if (transmit && m_txStreamId == 0) {
        // Mandare in aria una portante muta è peggio che non trasmettere: si
        // occupa la frequenza e non se ne accorge nessuno tranne i vicini.
        reportError(BackendError::Code::Unsupported,
                    tr("Il flusso audio di trasmissione non è stato aperto: "
                       "la radio andrebbe in aria senza voce."));
        return;
    }

    if (m_ptt == transmit)
        return;

    // `xmit` è il comando, e vale la pena dirlo qui: è l'unica riga di tutto
    // il programma che manda una radio in aria.
    m_client->send(transmit ? QStringLiteral("xmit 1") : QStringLiteral("xmit 0"));
    m_ptt = transmit;

    if (transmit) {
        m_txPacketCount = 0;
        m_txPending = 0;
        m_txPackets = 0;
        m_txDecimator.reset();
        if (!m_txTimer) {
            m_txTimer = new QTimer(this);
            m_txTimer->setInterval(kTxPumpMs);
            connect(m_txTimer, &QTimer::timeout, this, &FlexBackend::pumpTxAudio);
        }
        m_txTimer->start();

        // Il guinzaglio parte con la trasmissione e si ferma con lei.
        if (!m_txWatchdog) {
            m_txWatchdog = new QTimer(this);
            m_txWatchdog->setSingleShot(true);
            connect(m_txWatchdog, &QTimer::timeout, this, &FlexBackend::onTransmitWatchdog);
        }
        m_txWatchdog->start(kTransmitWatchdogSeconds * 1000);
    } else {
        if (m_txTimer)
            m_txTimer->stop();
        if (m_txWatchdog)
            m_txWatchdog->stop();
    }

    qCInfo(dsdrHal) << "flex: PTT" << (transmit ? "ON" : "OFF");
    emit pttChanged(transmit);
}

void FlexBackend::onTransmitWatchdog()
{
    if (!m_ptt)
        return;

    // Due minuti in aria senza che nessuno abbia chiuso: si chiude qui. Non è
    // un caso da manuale — è quello che succede quando il programma si impianta
    // o la rete cade, e una portante lasciata in frequenza non se ne va da
    // sola.
    qCWarning(dsdrHal) << "flex: trasmissione chiusa dal guinzaglio dopo"
                       << kTransmitWatchdogSeconds << "secondi";
    setPtt(false);
    reportError(BackendError::Code::InternalError,
                tr("Trasmissione chiusa d'ufficio dopo %1 secondi: nessuno l'aveva "
                   "chiusa.").arg(kTransmitWatchdogSeconds));
}

void FlexBackend::pumpTxAudio()
{
    if (!m_ptt || m_txStreamId == 0 || !m_udp || !m_txRing)
        return;

    while (m_txRing->available() > 0) {
        const std::size_t got = m_txRing->read(m_txScratch.data(), kTxChunkFrames);
        if (got == 0)
            break;

        // Il decimatore del progetto lavora su segnali complessi. Mettere la
        // voce sulla parte reale costa il doppio dei prodotti e non un'ora di
        // manutenzione: i tap sono reali, il risultato è lo stesso di un FIR
        // reale, e non c'è un secondo filtro da tenere allineato al primo.
        for (std::size_t i = 0; i < got; ++i)
            m_txComplexIn[i] = dsp::Complex(m_txScratch[i], 0.0f);

        const std::size_t produced =
            m_txDecimator.process(m_txComplexIn.data(), got, m_txComplexOut.data());

        for (std::size_t i = 0; i < produced; ++i) {
            m_txPacketBuffer[m_txPending++] = m_txComplexOut[i].real();
            if (m_txPending < static_cast<std::size_t>(kTxAudioSamplesPerPacket))
                continue;

            buildTxAudioPacket(m_txDatagram, m_txStreamId, m_txPacketCount,
                               m_txPacketBuffer.data(), kTxAudioSamplesPerPacket);
            m_txPacketCount = (m_txPacketCount + 1) & 0xF;
            m_udp->writeDatagram(m_txDatagram, m_radioAddress, kFlexDataPort);
            ++m_txPackets;
            m_txPending = 0;
        }
    }
}

SampleRing *FlexBackend::txStream()
{
    return m_txRing.get();
}

void FlexBackend::setTxFrequency(qint64 hz)
{
    // Qui c'è una cosa che va detta invece che nascosta.
    //
    // Su un Flex la frequenza di trasmissione è quella della *slice* marcata
    // TX, e questo backend le slice non le crea né le governa: apre un flusso
    // DAX IQ e un panadapter, che sono la ricezione. Mandare `slice tune` a
    // una slice che non abbiamo creato vorrebbe dire spostare la frequenza di
    // qualcun altro — di SmartSDR aperto accanto, che è il caso normale su
    // queste radio.
    //
    // Quindi non si sposta niente, e non si finge di averlo fatto: chi preme
    // PTT trasmette dove la radio è messa. Il valore si tiene solo per poterlo
    // dire nel diario e in `flex.status`.
    if (m_txFrequencyHz == hz)
        return;
    m_txFrequencyHz = hz;
    qCInfo(dsdrHal) << "flex: frequenza TX richiesta" << hz
                    << "— non inviata: la trasmissione segue la slice TX della radio";
}

SampleRing *FlexBackend::iqStream(ChannelId channel) const
{
    Q_UNUSED(channel)
    return m_iqRing.get();
}

SampleRing *FlexBackend::audioStream(ChannelId channel) const
{
    Q_UNUSED(channel)
    return nullptr;   // l'audio lo demoduliamo noi dall'IQ
}

SampleRing *FlexBackend::spectrumStream(PanId pan) const
{
    Q_UNUSED(pan)
    return nullptr;   // lo spettro lo calcola il client
}

QVariant FlexBackend::nativeCommand(const QString &command, const QVariantMap &args)
{
    if (command == QLatin1String("flex.status")) {
        QVariantMap status;
        status.insert(QStringLiteral("passo"), stepName(m_step));
        status.insert(QStringLiteral("pacchetti"), QVariant::fromValue(m_packets));
        status.insert(QStringLiteral("campioni"), QVariant::fromValue(m_frames));
        status.insert(QStringLiteral("buchi"), QVariant::fromValue(m_gaps));
        status.insert(QStringLiteral("portaUdp"), m_udpPort);
        status.insert(QStringLiteral("panadapter"), m_panStreamId);
        status.insert(QStringLiteral("formaAlternativa"), m_triedAlternateForm);
        status.insert(QStringLiteral("silenzio"), m_silenceReported);
        status.insert(QStringLiteral("flussoTx"), m_txStreamId != 0);
        status.insert(QStringLiteral("pacchettiTx"), QVariant::fromValue(m_txPackets));
        status.insert(QStringLiteral("inAria"), m_ptt);
        status.insert(QStringLiteral("frequenzaTxRichiesta"),
                      QVariant::fromValue(m_txFrequencyHz));
        return status;
    }

    // Una via per provare a mano un comando su una radio vera: è così che si
    // chiude la parte di sequenza che non abbiamo potuto verificare.
    if (command == QLatin1String("flex.send")) {
        const QString line = args.value(QStringLiteral("command")).toString();
        if (line.isEmpty() || !m_client || !m_client->isConnected())
            return false;
        qCInfo(dsdrHal) << "flex: comando a mano →" << qPrintable(line);
        return QVariant::fromValue(m_client->send(line));
    }

    if (command == QLatin1String("flex.setDaxChannel")) {
        m_daxChannel = std::clamp(args.value(QStringLiteral("channel"), 1).toInt(), 1, 4);
        return m_daxChannel;
    }

    return {};
}

} // namespace dsdr::hal::flex
