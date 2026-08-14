// SPDX-License-Identifier: GPL-3.0-or-later
#include "hal/backends/audiorig/AudiorigBackend.h"
#include "hal/backends/audiorig/CatController.h"
#include "hal/backends/audiorig/CivDriver.h"
#include "hal/backends/audiorig/NewcatDriver.h"
#include "hal/backends/audiorig/RigctldDriver.h"
#include "audio/AudioOut.h"
#include "audio/MicSource.h"
#include "hal/HalLog.h"

#include <QAudioDevice>
#include <QMap>
#include <QSerialPort>
#include <QSerialPortInfo>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <cmath>

namespace dsdr::hal::audiorig {

namespace {

/// La finestra che una radio tradizionale concede: la sua passata. In SSB
/// stanno tre kilohertz scarsi, ed è tutto ciò che lo spettro potrà mostrare.
/// Dichiararlo è il contratto onesto del backend (SPEC-004 §2.1).
constexpr double kAudioRate = 48000.0;

/// Cadenza con cui si annuncia l'audio disponibile: ~21 ms, la stessa misura
/// del blocco degli altri backend.
constexpr int kPublishIntervalMs = 20;

/// Il driver CAT che porta questo nome.
///
/// Restava da fare: `open()` costruiva sempre un NewcatDriver, anche quando la
/// sonda aveva trovato una Icom — la radio compariva nell'elenco e poi non
/// rispondeva a nessun comando. Con tre driver la scelta va fatta per nome, e
/// il nome è quello che la sonda ha messo nel descrittore.
std::unique_ptr<ICatDriver> makeCatDriver(const QString &driverId)
{
    if (driverId == QLatin1String("civ"))
        return std::make_unique<CivDriver>();
    if (driverId == QLatin1String("rigctld"))
        return std::make_unique<RigctldDriver>();
    return std::make_unique<NewcatDriver>();
}

/// Gli indirizzi dove cercare un `rigctld`.
///
/// La 4532 è la porta di fabbrica del demone. Sulla stessa porta ascolta anche
/// il server rigctl di DECODIUM SDR, e infatti quando è acceso il demone non
/// riesce a prenderla e chi lo avvia sceglie la 4533: si guardano entrambe.
/// Attaccarsi a sé stessi non può succedere — la sonda pretende una risposta a
/// `\dump_caps`, che il nostro server non implementa.
///
/// `DSDR_RIGCTLD` sostituisce l'elenco, separando gli indirizzi con la
/// virgola: serve a chi tiene il demone su un'altra macchina, che è il caso
/// per cui rigctld esiste.
///
/// La porta su cui ascolta il *nostro* server rigctl si salta: sondarla
/// significa connettersi a sé stessi. La sonda se ne accorgerebbe comunque, ma
/// è lavoro sprecato e nel log compare un client che non esiste — che è il modo
/// più efficace di far cercare a qualcuno un programma che non c'è.
QStringList rigctldEndpoints()
{
    QStringList wanted;
    const QString configured = qEnvironmentVariable("DSDR_RIGCTLD");
    if (!configured.trimmed().isEmpty())
        wanted = configured.split(QLatin1Char(','), Qt::SkipEmptyParts);
    else
        wanted = {QStringLiteral("127.0.0.1:4532"), QStringLiteral("127.0.0.1:4533")};

    const QString ours = qEnvironmentVariable("DSDR_RIGCTL_SERVER_PORT");
    if (ours.isEmpty())
        return wanted;

    QStringList kept;
    for (const QString &endpoint : std::as_const(wanted)) {
        QString host;
        quint16 port = 0;
        const bool valid = RigctldDriver::splitEndpoint(endpoint.trimmed(), host, port);
        const bool isLocal = host == QLatin1String("127.0.0.1")
            || host == QLatin1String("localhost") || host == QLatin1String("::1");
        if (valid && isLocal && QString::number(port) == ours)
            continue;
        kept.append(endpoint);
    }
    return kept;
}

/// Il codec di una radio si riconosce dalla descrizione. Non è una certezza —
/// per questo l'ingresso resta scegliibile dal pannello — ma indovinarlo
/// giusto è la differenza fra «collega e funziona» e una tendina da leggere.
bool looksLikeRadioCodec(const QString &description)
{
    static const QStringList hints = {
        QStringLiteral("USB Audio CODEC"),   // FT-991A, FT-891, IC-7300
        QStringLiteral("USB AUDIO"),
        QStringLiteral("Texas Instruments"), // PCM2900 di molte interfacce
        QStringLiteral("SignaLink"),
        QStringLiteral("PCM"),
    };
    for (const QString &hint : hints) {
        if (description.contains(hint, Qt::CaseInsensitive))
            return true;
    }
    return false;
}

/// Il dispositivo da usare con questa radio: prima uno che sembri un codec,
/// altrimenti nessuno — meglio chiedere che ascoltare la stanza credendo di
/// ascoltare la banda, o parlare negli altoparlanti credendo di trasmettere.
QAudioDevice pick(const QList<QAudioDevice> &devices, const QString &preferredId)
{
    if (!preferredId.isEmpty()) {
        for (const QAudioDevice &device : devices) {
            if (QString::fromUtf8(device.id()) == preferredId)
                return device;
        }
    }
    for (const QAudioDevice &device : devices) {
        if (looksLikeRadioCodec(device.description()))
            return device;
    }
    return QAudioDevice();
}

QAudioDevice pickInput(const QString &preferredId)
{
    return pick(dsdr::audio::MicSource::inputs(), preferredId);
}

QAudioDevice pickOutput(const QString &preferredId)
{
    return pick(dsdr::audio::AudioOut::outputs(), preferredId);
}

} // namespace

AudiorigBackend::AudiorigBackend(QObject *parent)
    : IRadioBackend(parent)
    , m_capture(std::make_unique<dsdr::audio::MicSource>())
    , m_playback(std::make_unique<dsdr::audio::AudioOut>())
{
}

AudiorigBackend::~AudiorigBackend()
{
    // Prima la sonda, poi il resto: finché quel thread gira tiene `this`, e
    // distruggere sotto di lui è il genere di errore che non si manifesta
    // dove è stato commesso.
    stopDiscovery();
    if (m_prober)
        m_prober->wait();
    close();
}

QString AudiorigBackend::displayName() const
{
    return m_radioModel.isEmpty() ? QStringLiteral("Radio via audio + CAT") : m_radioModel;
}

BackendCapabilities AudiorigBackend::capabilities() const
{
    BackendCapabilities caps;

    // SPEC-004 §2.1, alla lettera. L'onestà è il punto: un canale solo, niente
    // vista di banda in tempo reale, e uno spettro largo quanto la passata
    // della radio. La UI capability-driven fa il resto senza casi speciali.
    caps.maxRxChannels = 1;
    caps.coherentRx = false;
    caps.maxPanadapters = 1;
    caps.tx = TxSupport::Ptt;

    // La radio demodula e modula: da noi vuole audio, non banda base.
    caps.demod = DspLocation::Device;
    caps.modulation = DspLocation::Device;
    caps.agc = DspLocation::Device;
    // Lo spettro lo calcoliamo noi, ma **solo sulla passata**: è una finestra
    // di tre kilohertz attorno al VFO, non una vista di banda.
    caps.spectrum = DspLocation::Client;

    caps.sampleRates = {kAudioRate};
    caps.defaultSampleRate = kAudioRate;
    // I limiti sono quelli di una radio HF/VHF generica finché il CAT non dice
    // il modello: allargarli oltre servirebbe solo a promettere di più.
    caps.minFrequencyHz = 30'000;
    caps.maxFrequencyHz = 470'000'000;
    caps.hasHardwareFilters = true;    // sono i filtri della radio
    caps.hasPreamp = false;            // si comandano dalla radio, non da qui
    caps.hasAttenuator = false;
    caps.adcBits = 0;                  // non c'è un nostro convertitore
    caps.maxGainReductionDb = 0.0;
    caps.multiClient = false;          // la porta CAT si apre in esclusiva
    caps.remoteCapable = false;        // lo diventerà con DECOLINK
    caps.supportsRecording = true;
    caps.nativePanels = {QStringLiteral("AudiorigPanel")};
    return caps;
}

void AudiorigBackend::setState(BackendState state)
{
    if (m_state == state)
        return;
    m_state = state;
    emit stateChanged(state);
}

void AudiorigBackend::reportError(BackendError::Code code, const QString &message, bool fatal)
{
    qCWarning(dsdrHal) << "audiorig:" << message;
    emit errorOccurred(BackendError{code, message, QString(), fatal});
    if (fatal)
        setState(BackendState::Error);
}

void AudiorigBackend::startDiscovery()
{
    if (m_discovering || (m_prober && m_prober->isRunning()))
        return;

    // Una via d'uscita, e non è un dettaglio da configurazione.
    //
    // Sondare le porte seriali significa aprirle, e aprire una porta su
    // Windows alza DTR e RTS per un istante prima che il programma possa
    // abbassarli: su una radio con «CAT RTS» attivo è un colpo di
    // trasmissione. Chi non vuole che accada — perché ha la radio accesa
    // accanto, o perché sta facendo girare i test — deve poterlo impedire
    // senza rinunciare al resto del programma.
    //
    // Serve anche alla suite: la conformance apre un backend per ogni prova, e
    // una sonda per prova la faceva durare due minuti.
    // La sonda seriale si può spegnere; quella di rete no, perché non ha
    // niente da cui proteggersi: una connessione TCP rifiutata non alza
    // nessuna linea su nessun cavo.
    const bool probeSerial = !qEnvironmentVariableIsSet("DSDR_AUDIORIG_NO_PROBE");
    if (!probeSerial) {
        qCInfo(dsdrHal) << "audiorig: sonda delle porte seriali disattivata "
                           "(DSDR_AUDIORIG_NO_PROBE)";
    }

    m_discovering = true;
    m_abortDiscovery.store(false, std::memory_order_release);
    setState(BackendState::Discovering);

    // Le porte si sondano su un thread a parte: sei velocità per porta, ognuna
    // con la sua attesa, sono secondi interi. Sul thread del seam sarebbero
    // secondi di applicazione ferma, e nessuno collegherebbe la cosa alla
    // porta seriale.
    QThread *prober = QThread::create([this, probeSerial] {
        /// Le porte che non si sono aperte, con il motivo. Si raccolgono e si
        /// dicono in fondo: una per volta sarebbero sei righe uguali a porta.
        QMap<QString, QString> busy;

        /// Quante radio ha trovato questa ricerca, su tutte le vie.
        int devices = 0;

        // ── rigctld, sulla rete ─────────────────────────────────────────
        //
        // Prima delle seriali: è la via con cui una radio che nessuno di noi
        // ha sul tavolo diventa comandabile, e costa una connessione TCP che
        // su localhost o fallisce o riesce in un millesimo di secondo.
        for (const QString &endpoint : rigctldEndpoints()) {
            if (m_abortDiscovery.load(std::memory_order_acquire))
                break;

            RigctldDriver driver;
            if (driver.probe(endpoint.trimmed()) < 0)
                continue;

            DeviceDescriptor device;
            device.backendId = backendId();
            // L'indirizzo *è* l'identità: è quello che resta uguale fra un
            // riavvio e l'altro, e due demoni su due porte sono due radio.
            device.deviceId = QStringLiteral("rigctld@") + endpoint.trimmed();
            device.model = driver.radioModel();
            // Il nome del demone non entra nell'etichetta: dall'altra parte
            // può esserci rigctld o un rigctl minimo — DECODIUM 4 ne espone
            // uno — e dirlo sbagliato è peggio che non dirlo. L'indirizzo sì:
            // con due server accesi è l'unica cosa che li distingue.
            device.displayName = tr("%1 · %2")
                                     .arg(driver.radioModel(), endpoint.trimmed());
            device.transport = QStringLiteral("net");
            device.address = endpoint.trimmed();
            device.extra.insert(QStringLiteral("catPort"), endpoint.trimmed());
            device.extra.insert(QStringLiteral("catBaud"), 0);
            device.extra.insert(QStringLiteral("catDriver"), driver.driverId());

            const QAudioDevice input = pickInput(QString());
            if (!input.isNull()) {
                device.extra.insert(QStringLiteral("audioInput"),
                                    QString::fromUtf8(input.id()));
                device.extra.insert(QStringLiteral("audioInputName"), input.description());
            }

            driver.close();
            ++devices;
            QMetaObject::invokeMethod(this, [this, device] {
                emit deviceFound(device);
            }, Qt::QueuedConnection);
        }

        // ── Le porte seriali ────────────────────────────────────────────
        const QList<QSerialPortInfo> ports =
            probeSerial ? QSerialPortInfo::availablePorts() : QList<QSerialPortInfo>();
        for (const QSerialPortInfo &info : ports) {
            if (m_abortDiscovery.load(std::memory_order_acquire))
                break;
            if (info.isNull())
                continue;

            // Due lingue, non una. Le Yaesu parlano newcat, le Icom CI-V, e
            // sulla stessa porta seriale non c'è modo di sapere quale sia
            // senza provare.
            //
            // Ciascun driver apre la porta **una volta sola** e prova le sue
            // velocità cambiandole sulla porta aperta. Non è
            // un'ottimizzazione: su Windows ogni apertura alza DTR e RTS per
            // qualche millisecondo prima che si possano abbassare, e su una
            // radio con «CAT RTS» attivo quelle linee sono il PTT. La prima
            // stesura apriva undici volte per porta — undici colpi di
            // trasmissione su una radio che nessuno stava usando.
            std::unique_ptr<ICatDriver> drivers[2];
            drivers[0] = std::make_unique<NewcatDriver>();
            drivers[1] = std::make_unique<CivDriver>();

            bool found = false;
            bool opened = false;
            for (auto &candidate : drivers) {
                if (found || m_abortDiscovery.load(std::memory_order_acquire))
                    break;

                ICatDriver &driver = *candidate;
                const int rate = driver.probe(info.portName());
                if (rate < 0) {
                    // Distinguere «non risponde» da «non si apre» è la
                    // differenza fra cercare il guasto nella radio e trovarlo
                    // nel programma che tiene la porta. Sul banco di chi
                    // scrive questo codice la porta CAT del FT-991A è contesa
                    // con un altro programma dell'ecosistema, e per mesi la
                    // ricerca non ha detto altro che «zero device».
                    if (!driver.errorString().isEmpty()
                        && !driver.errorString().startsWith(QLatin1String("nessuna radio"))) {
                        busy.insert(info.portName(), driver.errorString());
                    }
                    continue;
                }
                opened = true;

                DeviceDescriptor device;
                device.backendId = backendId();
                // La chiave deve restare stabile fra riavvii: il numero di
                // serie dell'adattatore lo è, il nome della porta no — su
                // Windows COM4 diventa COM7 al cambio di presa.
                device.deviceId = info.serialNumber().isEmpty()
                    ? info.portName()
                    : info.serialNumber();
                device.model = driver.radioModel();
                device.displayName = driver.radioModel();
                device.serial = info.serialNumber();
                device.transport = QStringLiteral("usb");
                device.address = info.portName();
                device.extra.insert(QStringLiteral("catPort"), info.portName());
                device.extra.insert(QStringLiteral("catBaud"), rate);
                device.extra.insert(QStringLiteral("catDriver"), driver.driverId());

                const QAudioDevice input = pickInput(QString());
                if (!input.isNull()) {
                    device.extra.insert(QStringLiteral("audioInput"),
                                        QString::fromUtf8(input.id()));
                    device.extra.insert(QStringLiteral("audioInputName"),
                                        input.description());
                }

                driver.close();
                found = true;
                ++devices;
                QMetaObject::invokeMethod(this, [this, device] {
                    emit deviceFound(device);
                }, Qt::QueuedConnection);
            }
            Q_UNUSED(found)
            Q_UNUSED(opened)
        }

        // Le porte che non si sono nemmeno aperte: una riga per ciascuna, e un
        // avviso all'operatore. Una porta occupata non è un guasto della radio,
        // ma senza dirlo è indistinguibile da uno.
        //
        // L'avviso però parte solo se la ricerca è tornata a mani vuote. Con la
        // radio già raggiunta per un'altra via — il CAT di rete, che è proprio
        // quello che si usa quando la porta seriale ce l'ha un altro programma
        // — la porta occupata non è un problema, è la configurazione voluta, e
        // segnalarla vorrebbe dire mandare a cercare un guasto che non c'è.
        if (!busy.isEmpty() && devices == 0) {
            for (auto it = busy.cbegin(); it != busy.cend(); ++it)
                qCWarning(dsdrHal) << "audiorig: porta" << it.key() << "non apribile —" << it.value();

            const QStringList names = busy.keys();
            QMetaObject::invokeMethod(this, [this, names] {
                reportError(BackendError::Code::PermissionDenied,
                            tr("La porta %1 è occupata da un altro programma: "
                               "chiudilo e riprova la ricerca.")
                                .arg(names.join(QStringLiteral(", "))));
            }, Qt::QueuedConnection);
        }

        QMetaObject::invokeMethod(this, [this] {
            m_discovering = false;
            setState(m_open ? BackendState::Streaming : BackendState::Idle);
            emit discoveryFinished();
        }, Qt::QueuedConnection);
    });

    connect(prober, &QThread::finished, prober, &QObject::deleteLater);
    prober->setObjectName(QStringLiteral("dsdr-audiorig-probe"));
    m_prober = prober;
    prober->start();
}

void AudiorigBackend::stopDiscovery()
{
    // Il flag si legge fra una porta e l'altra: la sonda si ferma al prossimo
    // confine invece che a metà di un'apertura, così nessuna porta seriale
    // resta aperta dietro di lei.
    m_abortDiscovery.store(true, std::memory_order_release);
    m_discovering = false;
}

void AudiorigBackend::open(const DeviceDescriptor &device)
{
    close();

    m_device = device;
    m_radioModel = device.model;
    setState(BackendState::Connecting);

    // ── Piano dati ──────────────────────────────────────────────────────
    const QAudioDevice input =
        pickInput(device.extra.value(QStringLiteral("audioInput")).toString());
    if (input.isNull()) {
        reportError(BackendError::Code::NotFound,
                    tr("Nessun ingresso audio riconoscibile come codec di una radio. "
                       "Scegline uno dal pannello."),
                    true);
        return;
    }
    if (!m_capture->start(input)) {
        reportError(BackendError::Code::NotFound, m_capture->errorString(), true);
        return;
    }
    m_sampleRate = m_capture->sampleRate();

    // L'uscita verso il codec della radio: è il percorso di trasmissione, e
    // senza di lei il PTT manda la radio in portante e non esce una parola —
    // il motore TX riempie diligentemente un ring che nessuno svuota.
    //
    // Si apre insieme alla ricezione e resta aperta: aprirla al PTT
    // costerebbe i primi decimi di secondo di ogni chiamata, che sono
    // esattamente quelli in cui si dice il nominativo.
    const QAudioDevice output =
        pickOutput(device.extra.value(QStringLiteral("audioOutput")).toString());
    if (output.isNull()) {
        reportError(BackendError::Code::NotFound,
                    tr("Nessuna uscita audio riconoscibile come codec di una radio: "
                       "la ricezione funziona, la trasmissione no."));
    } else if (!m_playback->start(output)) {
        reportError(BackendError::Code::NotFound, m_playback->errorString());
    }

    // ── Piano di controllo ──────────────────────────────────────────────
    const QString catDriverId = device.extra.value(QStringLiteral("catDriver")).toString();
    auto controller = new CatController(makeCatDriver(catDriverId));
    m_catThread = new QThread(this);
    m_catThread->setObjectName(QStringLiteral("dsdr-audiorig-cat"));
    controller->moveToThread(m_catThread);
    connect(m_catThread, &QThread::finished, controller, &QObject::deleteLater);
    connect(controller, &CatController::stateRead, this, &AudiorigBackend::onCatState);
    connect(controller, &CatController::lost, this, &AudiorigBackend::onCatLost);
    connect(controller, &CatController::opened, this,
            [this](const QString &model, const QString &port, int baud) {
                m_radioModel = model;
                // Zero baud vuol dire che non è una seriale: su rigctld la
                // velocità di linea la governa il demone, e stamparne una
                // farebbe cercare a qualcuno una porta che non esiste.
                if (baud > 0)
                    qCInfo(dsdrHal) << "audiorig: CAT aperto su" << port << baud << "baud —" << model;
                else
                    qCInfo(dsdrHal) << "audiorig: CAT aperto su" << port << "—" << model;
                emit capabilitiesChanged();
            });
    m_cat = controller;
    m_catThread->start();

    const QString port = device.extra.value(QStringLiteral("catPort")).toString();
    const int baud = device.extra.value(QStringLiteral("catBaud")).toInt();
    QMetaObject::invokeMethod(controller, "open", Qt::QueuedConnection,
                              Q_ARG(QString, port), Q_ARG(int, baud));

    // L'audio si annuncia a cadenza fissa: i campioni sono già nel ring, il
    // segnale porta solo il descrittore (§4.1).
    m_publishTimer = new QTimer(this);
    m_publishTimer->setInterval(kPublishIntervalMs);
    connect(m_publishTimer, &QTimer::timeout, this, &AudiorigBackend::publishAudio);
    m_publishTimer->start();

    m_open = true;
    m_sequence = 0;
    m_publishedFrames = 0;
    setState(BackendState::Streaming);
}

void AudiorigBackend::close()
{
    if (m_publishTimer) {
        m_publishTimer->stop();
        m_publishTimer->deleteLater();
        m_publishTimer = nullptr;
    }

    if (m_cat) {
        // Il PTT si lascia prima di chiudere: una radio abbandonata in
        // trasmissione è il modo migliore per rovinare un finale.
        QMetaObject::invokeMethod(m_cat, "setPtt", Qt::BlockingQueuedConnection,
                                  Q_ARG(bool, false));
        QMetaObject::invokeMethod(m_cat, "close", Qt::BlockingQueuedConnection);
    }
    if (m_catThread) {
        m_catThread->quit();
        m_catThread->wait();
        delete m_catThread;
        m_catThread = nullptr;
        m_cat = nullptr;
    }

    m_capture->stop();
    m_playback->stop();
    m_channels.clear();
    m_panadapters.clear();

    if (m_open) {
        m_open = false;
        m_ptt = false;
        setState(BackendState::Idle);
    }
}

void AudiorigBackend::publishAudio()
{
    if (!m_open)
        return;

    const std::size_t available = m_capture->ring()->available();
    if (available == 0)
        return;

    AudioFrame frame;
    frame.channel = m_channels.isEmpty() ? kInvalidChannel : m_channels.begin().key();
    frame.sequence = ++m_sequence;
    frame.sampleRate = m_sampleRate;
    frame.frameCount = static_cast<quint32>(available);
    frame.channelCount = 1;
    emit audioFrameReady(frame);

    MeterFrame meters;
    meters.channel = frame.channel;
    // L'S-meter viene dalla radio, non dall'audio: è calibrato dal costruttore
    // e non risente del volume, che l'operatore muove di continuo. La
    // conversione in punti S dipende dal modello (SPEC-004 §8.2) e per ora si
    // consegna la lettura grezza normalizzata.
    //
    // Con rigctld il livello arriva già in decibel rispetto a S9, cioè tarato
    // dal profilo che hamlib ha di quella radio: si consegna così com'è.
    // Riportarlo sulla scala grezza e riconvertirlo qui vorrebbe dire perdere
    // proprio la taratura — la scala grezza si ferma a −60 dBm, che è S9+13, e
    // qualunque segnale più forte arriverebbe schiacciato lì.
    if (std::isfinite(m_signalDbm))
        meters.signalDbm = static_cast<float>(m_signalDbm);
    else if (m_sMeterRaw >= 0)
        meters.signalDbm = -127.0f + static_cast<float>(m_sMeterRaw) * (67.0f / 255.0f);
    emit meterUpdate(meters);
}

void AudiorigBackend::setCenterFrequency(qint64 hz)
{
    if (m_centerHz == hz)
        return;
    m_centerHz = hz;
    if (m_cat) {
        QMetaObject::invokeMethod(m_cat, "setFrequency", Qt::QueuedConnection,
                                  Q_ARG(qint64, hz));
    }
    emit centerFrequencyChanged(hz);
}

void AudiorigBackend::setSampleRate(double rate)
{
    // La frequenza è quella del codec e non si sceglie: dichiararne una sola
    // in `sampleRates` significa che la UI non offre alternative, e accettare
    // qui un valore diverso renderebbe falsa quella dichiarazione.
    Q_UNUSED(rate)
}

ChannelId AudiorigBackend::createRxChannel(const RxChannelConfig &config)
{
    if (m_channels.size() >= 1) {
        reportError(BackendError::Code::ResourceExhausted,
                    tr("Una radio tradizionale ha un ricevitore solo."));
        return kInvalidChannel;
    }
    const ChannelId id = m_nextChannelId++;
    m_channels.insert(id, config);
    return id;
}

void AudiorigBackend::destroyRxChannel(ChannelId channel)
{
    m_channels.remove(channel);
}

QList<ChannelId> AudiorigBackend::channels() const
{
    return m_channels.keys();
}

void AudiorigBackend::setFrequency(ChannelId channel, qint64 hz)
{
    // Il canale *è* il VFO: non c'è una banda dentro cui spostarsi. Muovere il
    // canale muove la radio, ed è esattamente il click-to-tune inverso della
    // specifica (§4).
    Q_UNUSED(channel)
    setCenterFrequency(hz);
}

void AudiorigBackend::setDemod(ChannelId channel, DemodMode mode)
{
    Q_UNUSED(channel)
    if (m_radioMode == mode)
        return;
    m_radioMode = mode;
    if (m_cat) {
        QMetaObject::invokeMethod(m_cat, "setMode", Qt::QueuedConnection,
                                  Q_ARG(int, static_cast<int>(mode)));
    }
}

void AudiorigBackend::setFilter(ChannelId channel, int lowHz, int highHz)
{
    // I filtri sono quelli della radio e si comandano dai suoi menù: fingere
    // di applicarli qui darebbe una manopola che non muove niente.
    Q_UNUSED(channel)
    Q_UNUSED(lowHz)
    Q_UNUSED(highHz)
}

PanId AudiorigBackend::createPanadapter(const PanConfig &config)
{
    if (m_panadapters.size() >= 1)
        return kInvalidPan;
    const PanId id = m_nextPanId++;
    m_panadapters.insert(id, config);
    return id;
}

void AudiorigBackend::destroyPanadapter(PanId pan)
{
    m_panadapters.remove(pan);
}

void AudiorigBackend::setPtt(bool transmit)
{
    if (m_ptt == transmit)
        return;
    m_ptt = transmit;

    if (!transmit) {
        // Si svuota **rilasciando** il PTT, non premendolo: l'uscita audio
        // resta aperta e svuota il ring da sé, ma se non si fosse aperta il
        // ring resterebbe pieno e la chiamata dopo comincerebbe con la coda
        // di questa. Farlo premendo il PTT mangerebbe invece il primo blocco,
        // che il motore TX ha già scritto.
        m_playback->ring()->clear();
    }

    if (m_cat) {
        QMetaObject::invokeMethod(m_cat, "setPtt", Qt::QueuedConnection,
                                  Q_ARG(bool, transmit));
    }
    emit pttChanged(transmit);
}

void AudiorigBackend::setTxFrequency(qint64 hz)
{
    // Half-duplex con un VFO solo: la frequenza di trasmissione è quella di
    // ricezione. Lo split è della radio, e passa dai suoi comandi.
    setCenterFrequency(hz);
}

SampleRing *AudiorigBackend::txStream()
{
    // Il ring dell'uscita audio, non una copia: chi trasmette scrive
    // direttamente dove il driver legge.
    return m_playback->ring();
}

SampleRing *AudiorigBackend::iqStream(ChannelId channel) const
{
    // Non esiste percorso IQ, ed è tutto il punto di questo backend.
    Q_UNUSED(channel)
    return nullptr;
}

SampleRing *AudiorigBackend::audioStream(ChannelId channel) const
{
    Q_UNUSED(channel)
    return m_capture->ring();
}

SampleRing *AudiorigBackend::spectrumStream(PanId pan) const
{
    Q_UNUSED(pan)
    return nullptr;   // lo spettro lo calcola il client, dall'audio
}

void AudiorigBackend::onCatState(qint64 frequencyHz, int mode, bool transmitting,
                                 int sMeterRaw, double signalDbm)
{
    m_sMeterRaw = sMeterRaw;
    m_signalDbm = signalDbm;
    m_radioMode = static_cast<DemodMode>(mode);

    if (frequencyHz > 0 && frequencyHz != m_centerHz) {
        // Il VFO l'ha mosso l'operatore sulla radio: la finestra dello spettro
        // lo segue, con le etichette giuste. È il §4 della specifica, e viene
        // gratis dal fatto che la frequenza la dice sempre il CAT.
        m_centerHz = frequencyHz;
        emit centerFrequencyChanged(frequencyHz);
    }

    if (transmitting != m_ptt) {
        // Il PTT può essere stato premuto sul microfono della radio: da qui si
        // scopre, e la UI resta allineata a ciò che la radio sta facendo
        // davvero invece che a ciò che le abbiamo chiesto.
        m_ptt = transmitting;
        emit pttChanged(transmitting);
    }
}

void AudiorigBackend::onCatLost(const QString &reason)
{
    reportError(BackendError::Code::TransportError, reason, true);
}

QVariant AudiorigBackend::nativeCommand(const QString &command, const QVariantMap &args)
{
    auto describe = [](const QList<QAudioDevice> &devices) {
        QVariantList list;
        for (const QAudioDevice &device : devices) {
            QVariantMap entry;
            entry.insert(QStringLiteral("id"), QString::fromUtf8(device.id()));
            entry.insert(QStringLiteral("name"), device.description());
            entry.insert(QStringLiteral("likelyRadio"),
                         looksLikeRadioCodec(device.description()));
            list.append(entry);
        }
        return list;
    };

    // ── La radio dichiarata a mano ───────────────────────────────────────
    //
    // Il rilevamento sonda le porte e annuncia una radio solo quando qualcuno
    // risponde. È la cosa giusta — non si mette nell'elenco un apparato che
    // non c'è — ma lascia fuori il caso più frequente che ci sia: **la porta
    // è occupata da un altro programma**. Su una stazione dove gira anche
    // DECODIUM 4 la seriale ce l'ha lui, la sonda trova «Accesso negato», e
    // l'elenco resta vuoto senza che si possa fare niente.
    //
    // Da qui si dichiara la radio invece di cercarla: driver, porta,
    // velocità. Non è un aggiramento del rilevamento, è il caso in cui il
    // rilevamento non può funzionare — e chi opera la propria stazione sa che
    // radio ha molto meglio di quanto possa scoprirlo una sonda.

    if (command == QLatin1String("audiorig.serialPorts")) {
        QVariantList ports;
        for (const QSerialPortInfo &info : QSerialPortInfo::availablePorts()) {
            QVariantMap entry;
            entry.insert(QStringLiteral("port"), info.portName());
            entry.insert(QStringLiteral("description"), info.description());
            entry.insert(QStringLiteral("manufacturer"), info.manufacturer());

            // Se è occupata si dice, e non si nasconde la voce: «COM5 occupata
            // da un altro programma» è un'informazione, una tendina senza COM5
            // è un mistero. È esattamente il caso della porta CAT tenuta da
            // DECODIUM 4.
            QSerialPort probe(info);
            const bool free = probe.open(QIODevice::ReadWrite);
            if (free)
                probe.close();
            entry.insert(QStringLiteral("busy"), !free);
            ports.append(entry);
        }
        return ports;
    }

    if (command == QLatin1String("audiorig.catDrivers")) {
        // I nomi che `makeCatDriver` riconosce, con un'etichetta leggibile.
        // Stanno qui e non in QML perché chi aggiunge un driver tocca questo
        // file, e una tendina scritta altrove resterebbe indietro in silenzio.
        return QVariantList{
            QVariantMap{{QStringLiteral("id"), QStringLiteral("newcat")},
                        {QStringLiteral("label"), tr("Yaesu · CAT (newcat)")}},
            QVariantMap{{QStringLiteral("id"), QStringLiteral("civ")},
                        {QStringLiteral("label"), tr("Icom · CI-V")}},
            QVariantMap{{QStringLiteral("id"), QStringLiteral("rigctld")},
                        {QStringLiteral("label"), tr("Hamlib · rigctld in rete")}},
        };
    }

    if (command == QLatin1String("audiorig.declare")) {
        const QString driverId = args.value(QStringLiteral("driver")).toString();
        const QString port = args.value(QStringLiteral("port")).toString().trimmed();
        if (port.isEmpty())
            return false;

        DeviceDescriptor device;
        device.backendId = backendId();
        device.deviceId = QStringLiteral("manuale@") + port;
        device.model = args.value(QStringLiteral("model")).toString();
        if (device.model.isEmpty())
            device.model = tr("Radio dichiarata");
        device.displayName = tr("%1 · %2").arg(device.model, port);
        device.transport = driverId == QLatin1String("rigctld")
            ? QStringLiteral("net") : QStringLiteral("serial");
        device.address = port;
        device.extra.insert(QStringLiteral("catPort"), port);
        device.extra.insert(QStringLiteral("catBaud"),
                            args.value(QStringLiteral("baud"), 0).toInt());
        device.extra.insert(QStringLiteral("catDriver"), driverId);

        // L'audio resta quello che si sceglie dal pannello: qui si dichiara il
        // piano di controllo, e mescolare le due cose vorrebbe dire far
        // ricominciare da capo chi ha già scelto l'ingresso giusto.
        const QAudioDevice input =
            pickInput(args.value(QStringLiteral("audioInput")).toString());
        if (!input.isNull()) {
            device.extra.insert(QStringLiteral("audioInput"),
                                QString::fromUtf8(input.id()));
            device.extra.insert(QStringLiteral("audioInputName"), input.description());
        }

        qCInfo(dsdrHal) << "audiorig: radio dichiarata a mano" << driverId << port
                        << args.value(QStringLiteral("baud")).toInt();
        emit deviceFound(device);
        return true;
    }

    if (command == QLatin1String("audiorig.inputs"))
        return describe(dsdr::audio::MicSource::inputs());

    if (command == QLatin1String("audiorig.outputs"))
        return describe(dsdr::audio::AudioOut::outputs());

    if (command == QLatin1String("audiorig.setAudioOutput")) {
        const QString id = args.value(QStringLiteral("id")).toString();
        const QAudioDevice output = pickOutput(id);
        if (output.isNull())
            return false;
        m_playback->stop();
        const bool ok = m_playback->start(output);
        if (ok) {
            m_device.extra.insert(QStringLiteral("audioOutput"),
                                  QString::fromUtf8(output.id()));
        }
        return ok;
    }

    if (command == QLatin1String("audiorig.status")) {
        QVariantMap status;
        status.insert(QStringLiteral("radio"), m_radioModel);
        status.insert(QStringLiteral("catPort"),
                      m_device.extra.value(QStringLiteral("catPort")));
        status.insert(QStringLiteral("catBaud"),
                      m_device.extra.value(QStringLiteral("catBaud")));
        status.insert(QStringLiteral("audioInput"), m_capture->deviceName());
        status.insert(QStringLiteral("audioActive"), m_capture->isActive());
        status.insert(QStringLiteral("audioOutput"), m_playback->deviceName());
        status.insert(QStringLiteral("txAudioActive"), m_playback->isActive());
        status.insert(QStringLiteral("txUnderruns"),
                      QVariant::fromValue(m_playback->underrunCount()));
        status.insert(QStringLiteral("sMeterRaw"), m_sMeterRaw);
        status.insert(QStringLiteral("micOverruns"),
                      QVariant::fromValue(m_capture->overrunCount()));
        return status;
    }

    if (command == QLatin1String("audiorig.setAudioInput")) {
        const QString id = args.value(QStringLiteral("id")).toString();
        const QAudioDevice input = pickInput(id);
        if (input.isNull())
            return false;
        m_capture->stop();
        const bool ok = m_capture->start(input);
        if (ok) {
            m_device.extra.insert(QStringLiteral("audioInput"),
                                  QString::fromUtf8(input.id()));
        }
        return ok;
    }

    return IRadioBackend::nativeCommand(command, args);
}

} // namespace dsdr::hal::audiorig
