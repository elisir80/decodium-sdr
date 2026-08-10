// SPDX-License-Identifier: GPL-3.0-or-later
#include "hal/backends/audiorig/AudiorigBackend.h"
#include "hal/backends/audiorig/CatController.h"
#include "hal/backends/audiorig/CivDriver.h"
#include "hal/backends/audiorig/NewcatDriver.h"
#include "audio/AudioOut.h"
#include "audio/MicSource.h"
#include "hal/HalLog.h"

#include <QAudioDevice>
#include <QSerialPortInfo>
#include <QThread>
#include <QTimer>

#include <algorithm>

namespace dsdr::hal::audiorig {

namespace {

/// La finestra che una radio tradizionale concede: la sua passata. In SSB
/// stanno tre kilohertz scarsi, ed è tutto ciò che lo spettro potrà mostrare.
/// Dichiararlo è il contratto onesto del backend (SPEC-004 §2.1).
constexpr double kAudioRate = 48000.0;

/// Cadenza con cui si annuncia l'audio disponibile: ~21 ms, la stessa misura
/// del blocco degli altri backend.
constexpr int kPublishIntervalMs = 20;

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
    if (qEnvironmentVariableIsSet("DSDR_AUDIORIG_NO_PROBE")) {
        qCInfo(dsdrHal) << "audiorig: sonda delle porte seriali disattivata "
                           "(DSDR_AUDIORIG_NO_PROBE)";
        setState(m_open ? BackendState::Streaming : BackendState::Idle);
        emit discoveryFinished();
        return;
    }
    m_discovering = true;
    m_abortDiscovery.store(false, std::memory_order_release);
    setState(BackendState::Discovering);

    // Le porte si sondano su un thread a parte: sei velocità per porta, ognuna
    // con la sua attesa, sono secondi interi. Sul thread del seam sarebbero
    // secondi di applicazione ferma, e nessuno collegherebbe la cosa alla
    // porta seriale.
    QThread *prober = QThread::create([this] {
        const QList<QSerialPortInfo> ports = QSerialPortInfo::availablePorts();
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
            for (auto &candidate : drivers) {
                if (found || m_abortDiscovery.load(std::memory_order_acquire))
                    break;

                ICatDriver &driver = *candidate;
                const int rate = driver.probe(info.portName());
                if (rate < 0)
                    continue;

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
                QMetaObject::invokeMethod(this, [this, device] {
                    emit deviceFound(device);
                }, Qt::QueuedConnection);
            }
            Q_UNUSED(found)
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
    auto controller = new CatController(std::make_unique<NewcatDriver>());
    m_catThread = new QThread(this);
    m_catThread->setObjectName(QStringLiteral("dsdr-audiorig-cat"));
    controller->moveToThread(m_catThread);
    connect(m_catThread, &QThread::finished, controller, &QObject::deleteLater);
    connect(controller, &CatController::stateRead, this, &AudiorigBackend::onCatState);
    connect(controller, &CatController::lost, this, &AudiorigBackend::onCatLost);
    connect(controller, &CatController::opened, this,
            [this](const QString &model, const QString &port, int baud) {
                m_radioModel = model;
                qCInfo(dsdrHal) << "audiorig: CAT aperto su" << port << baud << "baud —" << model;
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
    if (m_sMeterRaw >= 0)
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

void AudiorigBackend::onCatState(qint64 frequencyHz, int mode, bool transmitting, int sMeterRaw)
{
    m_sMeterRaw = sMeterRaw;
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
