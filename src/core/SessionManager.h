// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — sessione: tiene insieme backend, DSP, audio e modelli.
//
// È l'unico oggetto che QML deve conoscere. Non include alcun header di
// backend concreto: parla solo con IRadioBackend e con il registro
// (CONSTITUTION §4).
#pragma once

// I tipi esposti come Q_PROPERTY devono essere completi: moc genera un
// metatype per ciascun puntatore e una forward declaration non basta.
#include "audio/AudioGraph.h"
#include "audio/AudioRouter.h"
#include "core/CapabilitiesInfo.h"
#include "core/ChannelModel.h"
#include "core/DeviceListModel.h"
#include "core/IqRecorder.h"
#include "core/LanguageManager.h"
#include "core/SpectrumFeed.h"
#include "dsp/ChannelProcessor.h"
#include "hal/Frames.h"

#include <QObject>
#include <QVariantList>
#include <QTimer>
#include <QTcpServer>

#include <vector>

namespace dsdr::hal {
class IRadioBackend;
class RadioScout;
}

class QThread;
class QTcpSocket;

namespace dsdr::audio {
class MicSource;
}

#include "dsp/neural/ModelStore.h"

namespace dsdr::dsp::neural {
class NeuralNrStage;
}

namespace dsdr::core {

class DspEngine;
class TxEngine;

class SessionManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QStringList iqModuleNames READ iqModuleNames NOTIFY iqModuleNamesChanged)
    Q_PROPERTY(QVariantList iqModuleCatalog READ iqModuleCatalog NOTIFY iqModuleCatalogChanged)

    Q_PROPERTY(QVariantList availableBackends READ availableBackends CONSTANT)
    Q_PROPERTY(QString backendId READ backendId NOTIFY backendChanged)
    Q_PROPERTY(QString backendName READ backendName NOTIFY backendChanged)

    Q_PROPERTY(dsdr::core::DeviceListModel *devices READ devices CONSTANT)
    Q_PROPERTY(dsdr::core::ChannelModel *channels READ channels CONSTANT)
    Q_PROPERTY(dsdr::core::CapabilitiesInfo *capabilities READ capabilities CONSTANT)
    Q_PROPERTY(dsdr::core::SpectrumFeed *spectrum READ spectrum CONSTANT)
    Q_PROPERTY(dsdr::core::SpectrumFeed *audioSpectrum READ audioSpectrum CONSTANT)

    // ── Analisi dell'audio ──────────────────────────────────────────────
    Q_PROPERTY(double audioToneHz READ audioToneHz NOTIFY audioToneChanged)
    Q_PROPERTY(double audioToneDb READ audioToneDb NOTIFY audioToneChanged)
    Q_PROPERTY(double audioThdPercent READ audioThdPercent NOTIFY audioToneChanged)
    Q_PROPERTY(double audioPeakDb READ audioPeakDb NOTIFY audioLevelsChanged)
    Q_PROPERTY(double audioRmsDb READ audioRmsDb NOTIFY audioLevelsChanged)

    /// Lo spettro di ciò che si sta trasmettendo. In mezzo duplex la radio si
    /// assorda mentre trasmette: senza questo, il panadattatore resterebbe una
    /// riga piatta proprio nei secondi in cui servirebbe di più.
    Q_PROPERTY(dsdr::core::SpectrumFeed *txSpectrum READ txSpectrum CONSTANT)
    Q_PROPERTY(dsdr::audio::AudioRouter *audio READ audio CONSTANT)
    Q_PROPERTY(dsdr::core::IqRecorder *recorder READ recorder CONSTANT)
    Q_PROPERTY(dsdr::core::IqRecorder *audioRecorder READ audioRecorder CONSTANT)
    Q_PROPERTY(dsdr::core::LanguageManager *language READ language CONSTANT)

    Q_PROPERTY(bool connected READ isConnected NOTIFY connectionChanged)
    Q_PROPERTY(bool discovering READ isDiscovering NOTIFY discoveringChanged)
    Q_PROPERTY(bool transmitting READ isTransmitting NOTIFY transmittingChanged)
    Q_PROPERTY(QString deviceName READ deviceName NOTIFY connectionChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
    Q_PROPERTY(bool scanning READ isScanning NOTIFY scanningChanged)
    Q_PROPERTY(QVariantList scanResults READ scanResults NOTIFY scanResultsChanged)

    // ── Radio viste in rete ─────────────────────────────────────────────
    //
    // Non sono device: sono radio che esistono e che questa versione non sa
    // ancora aprire. Stanno in un elenco a parte proprio per questo — in
    // quello dei device finisce solo ciò che si può usare (CONSTITUTION §7) —
    // e servono a rispondere alla domanda «l'ho collegata, perché non la
    // vedo?», che altrimenti non ha risposta.
    Q_PROPERTY(QVariantList networkRadios READ networkRadios NOTIFY networkRadiosChanged)
    Q_PROPERTY(bool scoutingNetwork READ scoutingNetwork NOTIFY networkRadiosChanged)
    Q_PROPERTY(bool rigctlRunning READ rigctlRunning NOTIFY rigctlChanged)
    Q_PROPERTY(int rigctlPort READ rigctlPort NOTIFY rigctlChanged)

    Q_PROPERTY(qint64 centerFrequency READ centerFrequency WRITE setCenterFrequency
                   NOTIFY centerFrequencyChanged)
    Q_PROPERTY(double sampleRate READ sampleRate WRITE setSampleRate NOTIFY sampleRateChanged)

    // ── Macchina del tempo ──────────────────────────────────────────────
    //
    // Il motore tiene in memoria gli ultimi secondi di banda: `replayDelay`
    // dice di quanto si sta ascoltando indietro, `replayHistory` fin dove si
    // potrebbe tornare. Sono due numeri diversi e vanno mostrati entrambi:
    // promettere trenta secondi dieci secondi dopo la connessione sarebbe una
    // bugia che si scopre solo premendo.
    Q_PROPERTY(double replayDelaySeconds READ replayDelaySeconds NOTIFY replayChanged)
    Q_PROPERTY(double replayHistorySeconds READ replayHistorySeconds NOTIFY replayChanged)
    Q_PROPERTY(double replayCapacitySeconds READ replayCapacitySeconds NOTIFY sampleRateChanged)
    Q_PROPERTY(bool replaying READ replaying NOTIFY replayChanged)

    // ── Noise blanker, di catena e non di canale (SPEC-003 §4) ──────────
    Q_PROPERTY(bool noiseBlanker READ noiseBlanker NOTIFY noiseBlankerChanged)
    Q_PROPERTY(double noiseBlankerThreshold READ noiseBlankerThreshold
                   NOTIFY noiseBlankerChanged)
    Q_PROPERTY(double noiseBlankerActivity READ noiseBlankerActivity NOTIFY replayChanged)

    // ── Salute del collegamento ─────────────────────────────────────────
    //
    // Quanto del flusso atteso sta arrivando davvero, da 0 a 1, e se la
    // sorgente sta dall'altra parte di una rete. Il secondo serve al primo:
    // su una radio attaccata al bus il rapporto sta incollato a uno e mostrarlo
    // sarebbe un numero che non dice mai niente.
    Q_PROPERTY(double streamHealth READ streamHealth NOTIFY streamHealthChanged)
    Q_PROPERTY(bool streamOverNetwork READ streamOverNetwork NOTIFY connectionChanged)

    // ── Guardia contro la saturazione (SPEC-003 §3) ─────────────────────
    Q_PROPERTY(bool overloaded READ overloaded NOTIFY overloadChanged)
    Q_PROPERTY(double peakDbfs READ peakDbfs NOTIFY overloadChanged)
    Q_PROPERTY(int overloadMode READ overloadMode WRITE setOverloadMode NOTIFY overloadChanged)

    /// Il device offre un modo di togliere guadagno dalla HAL. Finché è falso
    /// la guardia può solo avvertire — e la UI deve dirlo, invece di mostrare
    /// un automatismo che non scatterà mai (CONSTITUTION §7).
    Q_PROPERTY(bool canCorrectGain READ canCorrectGain NOTIFY connectionChanged)

    // ── Misure di trasmissione ──────────────────────────────────────────
    //
    // Arrivano dal seam con `meterUpdate`, e finora non le raccoglieva
    // nessuno: i campi c'erano in `MeterFrame` e si fermavano lì. Uno
    // strumento di potenza senza queste è un disegno.
    //
    // `txMetersAvailable` distingue «zero watt» da «non lo so», che su un
    // wattmetro sono cose opposte: il primo si mostra, il secondo si dichiara
    // (CONSTITUTION §7). Diventa vero quando un backend manda una misura di
    // potenza, e torna falso quando la sessione si chiude.
    Q_PROPERTY(bool txMetersAvailable READ txMetersAvailable NOTIFY txMetersChanged)
    Q_PROPERTY(double txForwardWatt READ txForwardWatt NOTIFY txMetersChanged)
    Q_PROPERTY(double txReflectedWatt READ txReflectedWatt NOTIFY txMetersChanged)
    Q_PROPERTY(double txSwr READ txSwr NOTIFY txMetersChanged)

    // ── Stadio neurale (SPEC-003 §8) ────────────────────────────────────
    //
    // `neuralAvailable` è una proprietà della compilazione: senza il motore la
    // UI non mostra l'interruttore, invece di offrirne uno che non può fare
    // nulla (CONSTITUTION §7).
    Q_PROPERTY(bool neuralAvailable READ neuralAvailable CONSTANT)
    Q_PROPERTY(bool neuralEnabled READ neuralEnabled NOTIFY neuralChanged)
    Q_PROPERTY(double neuralLoad READ neuralLoad NOTIFY neuralChanged)

    /// Lo stato dello stadio, come nome: Bypass, Warmup, Engaged, Degraded.
    /// La UI ne fa un distintivo — chi lo vede giallo sa che la macchina non
    /// ce la fa, invece di credere che la riduzione sia accesa.
    Q_PROPERTY(QString neuralState READ neuralState NOTIFY neuralChanged)
    Q_PROPERTY(double neuralLatencyMs READ neuralLatencyMs NOTIFY neuralChanged)
    Q_PROPERTY(double neuralIntensity READ neuralIntensity WRITE setNeuralIntensity
                   NOTIFY neuralChanged)
    Q_PROPERTY(dsdr::dsp::neural::ModelStore *neuralModels READ neuralModels CONSTANT)

    /// Come è collegato l'audio, in forma leggibile: serve a rispondere alla
    /// domanda «quello che sto registrando è passato dalla rete?».
    Q_PROPERTY(QStringList audioRoutes READ audioRoutes NOTIFY connectionChanged)

    // ── Trasmissione ────────────────────────────────────────────────────
    //
    // Esistono sempre, ma la UI le mostra solo se `capabilities.canTransmit`:
    // un pannello TX disabilitato su un ricevitore puro sarebbe una promessa
    // non mantenuta (CONSTITUTION §7).
    //
    // Il canale di trasmissione è uno dei canali di ricezione — il suo, non
    // un'entità separata: si trasmette dove si stava ascoltando, con il modo
    // con cui si stava ascoltando, che è ciò che l'operatore si aspetta.
    Q_PROPERTY(int txChannel READ txChannel WRITE setTxChannel NOTIFY txChanged)
    Q_PROPERTY(double micGainDb READ micGainDb WRITE setMicGainDb NOTIFY txChanged)
    Q_PROPERTY(double txCompressionDb READ txCompressionDb WRITE setTxCompressionDb
                   NOTIFY txChanged)
    Q_PROPERTY(double txDrive READ txDrive WRITE setTxDrive NOTIFY txChanged)
    Q_PROPERTY(bool micActive READ micActive NOTIFY txChanged)
    Q_PROPERTY(QString micDeviceName READ micDeviceName NOTIFY txChanged)

    /// Gli ingressi audio disponibili e quello scelto per la trasmissione.
    ///
    /// Non basta prendere il predefinito di sistema: su una macchina con dei
    /// cavi audio virtuali — e chi usa i modi digitali ne ha sempre — il
    /// predefinito è quasi mai il microfono, e si finisce a trasmettere quello
    /// che passa di lì senza capire perché.
    Q_PROPERTY(QVariantList micDevices READ micDevices NOTIFY txChanged)
    Q_PROPERTY(QString micDeviceId READ micDeviceId WRITE setMicDeviceId NOTIFY txChanged)
    Q_PROPERTY(double micLevel READ micLevel NOTIFY txMetersChanged)
    Q_PROPERTY(double txCompressionMeter READ txCompressionMeter NOTIFY txMetersChanged)
    Q_PROPERTY(double txLevel READ txLevel NOTIFY txMetersChanged)

    /// Trasmissione di prova in corso, e quanti secondi restano prima che la
    /// sicura la chiuda.
    Q_PROPERTY(bool tuning READ tuning NOTIFY txChanged)
    Q_PROPERTY(int tuneSecondsLeft READ tuneSecondsLeft NOTIFY txMetersChanged)

    /// In CW il PTT non basta: serve il tasto, e la UI deve mostrarlo. La
    /// distinzione la fa il modo del canale di trasmissione, non l'operatore.
    Q_PROPERTY(bool txCw READ txCw NOTIFY txChanged)
    /// Modo e frequenza su cui si trasmetterebbe adesso, già formattati: chi
    /// preme il PTT deve poterlo leggere senza cercarlo altrove.
    Q_PROPERTY(QString txSummary READ txSummary NOTIFY txChanged)

    /// Quanto guadagno la guardia ha tolto finora, in dB.
    Q_PROPERTY(double gainReductionDb READ gainReductionDb NOTIFY overloadChanged)
    Q_PROPERTY(int spectrumAveraging READ spectrumAveraging WRITE setSpectrumAveraging
                   NOTIFY spectrumAveragingChanged)

public:
    explicit SessionManager(QObject *parent = nullptr);
    ~SessionManager() override;

    QVariantList availableBackends() const;
    QString backendId() const { return m_backendId; }
    QString backendName() const;

    DeviceListModel *devices() { return &m_devices; }
    ChannelModel *channels() { return &m_channels; }
    CapabilitiesInfo *capabilities() { return &m_capabilities; }
    SpectrumFeed *spectrum() const;

    /// Lo spettro dell'audio che si sta ascoltando: passa dai filtri del
    /// canale, dall'AGC e dalla riduzione di rumore, quindi mostra quello che
    /// esce dagli altoparlanti e non quello che arriva dall'antenna.
    SpectrumFeed *audioSpectrum() const;

    /// La riga più forte dello spettro audio, e il suo livello. Zero quando
    /// non c'è niente che emerga dal fondo: un tono inventato è peggio di
    /// nessun tono.
    double audioToneHz() const { return m_audioToneHz; }
    double audioToneDb() const { return m_audioToneDb; }

    /// Distorsione armonica totale in percentuale, o −1 quando non c'è un tono
    /// su cui misurarla. Su del rumore le «armoniche» sono altro rumore, e il
    /// numero che ne uscirebbe sarebbe preciso e privo di significato.
    double audioThdPercent() const { return m_audioThdPercent; }

    /// Picco e valore efficace dell'audio, in dBFS. La distanza fra i due è il
    /// fattore di cresta: su una voce sta attorno ai dodici decibel, su una
    /// portante a tre.
    double audioPeakDb() const { return m_audioPeakDb; }
    double audioRmsDb() const { return m_audioRmsDb; }

    /// Gli ultimi campioni audio, ridotti a `points` valori fra −1 e 1.
    ///
    /// `spanMs` è la base dei tempi: quanti millisecondi di audio stanno in
    /// tutta la larghezza. Senza, un oscilloscopio mostra sempre la stessa
    /// finestra e su un tono di ottocento hertz ci finiscono settanta cicli —
    /// che a cinquecento punti diventano una banda piena, cioè niente.
    ///
    /// `trigger` aggancia il disegno alla prima salita per lo zero. Su un
    /// segnale periodico la traccia sta ferma e si legge; senza, scivola di
    /// lato a ogni fotogramma e non si distingue una forma d'onda dal rumore.
    ///
    /// La riduzione non è una decimazione: di ogni gruppo si prende il
    /// campione di modulo maggiore, così una punta che dura un campione solo
    /// resta visibile. Prendendone uno ogni N si mostrerebbe una forma d'onda
    /// più pulita di quella che c'è, e sarebbe una bugia proprio sul dettaglio
    /// che si sta cercando.
    ///
    /// Il ring lo si svuota qui, sul thread GUI: è il suo unico consumatore.
    Q_INVOKABLE QVariantList audioWaveform(int points = 512, double spanMs = 20.0,
                                           bool trigger = true);
    SpectrumFeed *txSpectrum() const;
    audio::AudioRouter *audio() const { return m_audio; }
    IqRecorder *recorder() { return &m_recorder; }
    IqRecorder *audioRecorder() { return &m_audioRecorder; }
    LanguageManager *language() { return &m_language; }

    bool isConnected() const { return m_connected; }
    bool isDiscovering() const { return m_discovering; }
    bool isTransmitting() const { return m_transmitting; }
    QString deviceName() const { return m_deviceName; }
    QString statusMessage() const { return m_statusMessage; }
    bool isScanning() const { return m_scanning; }
    QVariantList scanResults() const { return m_scanResults; }
    bool rigctlRunning() const { return m_rigctlServer.isListening(); }
    int rigctlPort() const { return m_rigctlServer.serverPort(); }

    qint64 centerFrequency() const { return m_centerFrequency; }
    void setCenterFrequency(qint64 hz);
    double sampleRate() const { return m_sampleRate; }
    void setSampleRate(double rate);

    /// Quante FFT si mediano per riga di waterfall.
    ///
    /// Il valore vive in `SpectrumFeed`, che sta sul thread del DSP: la UI non
    /// può legarcisi direttamente — QML rifiuta di connettersi a un oggetto di
    /// un altro thread, e avrebbe ragione. Passa di qui, dove una copia sul
    /// thread della UI risponde alle letture e il feed riceve solo la scrittura,
    /// che è atomica di suo.
    int spectrumAveraging() const { return m_spectrumAveraging; }
    void setSpectrumAveraging(int frames);

    // ── Azioni dalla UI ─────────────────────────────────────────────────

    Q_INVOKABLE void selectBackend(const QString &backendId);
    Q_INVOKABLE void startDiscovery();

    /// Cerca in rete le radio delle famiglie che conosciamo, anche quelle che
    /// non sappiamo aprire.
    Q_INVOKABLE void scoutNetwork(int seconds = 6);

    /// Prova a parlare con una radio trovata in rete: apre il canale di
    /// comando, ascolta quello che la radio dice di sé e lo rimette
    /// nell'elenco. Dal «c'è» al «ci parlo», che è la differenza fra un
    /// problema di rete e un problema di programma.
    Q_INVOKABLE void probeNetworkRadio(const QString &address);
    QVariantList networkRadios() const { return m_networkRadios; }
    bool scoutingNetwork() const;
    Q_INVOKABLE void connectToDevice(int deviceRow);
    Q_INVOKABLE void disconnectDevice();
    Q_INVOKABLE bool startScan(qint64 startHz, qint64 endHz, qint64 stepHz,
                               int dwellMs = 350);
    Q_INVOKABLE void stopScan();
    Q_INVOKABLE bool startRigctl(int port = 4532);
    Q_INVOKABLE void stopRigctl();

    double replayDelaySeconds() const { return m_replayDelay; }
    double replayHistorySeconds() const { return m_replayHistory; }
    double replayCapacitySeconds() const;
    bool replaying() const { return m_replayDelay > 0.05; }

    /// Quanto del flusso atteso arriva davvero, da 0 a 1.
    ///
    /// È il rapporto fra i campioni contati nell'ultimo secondo e quelli che la
    /// frequenza di campionamento dichiarata prometteva. Su una sorgente locale
    /// sta incollato a uno; su una di rete scende appena il collegamento
    /// comincia a perdere pezzi — e scende **prima** che si senta qualcosa, che
    /// è l'unico momento in cui saperlo serve.
    ///
    /// Vale −1 finché non c'è una misura: un secondo di attesa dopo la
    /// connessione, e dopo ogni riconfigurazione.
    double streamHealth() const { return m_streamHealth; }

    /// Se la sorgente aperta sta dall'altra parte di una rete.
    bool streamOverNetwork() const;

    bool overloaded() const { return m_overloaded; }
    double peakDbfs() const { return m_peakDbfs; }
    int overloadMode() const { return m_overloadMode; }
    void setOverloadMode(int mode);
    bool canCorrectGain() const;
    double gainReductionDb() const { return m_gainReductionDb; }

    bool txMetersAvailable() const { return m_txMetersAvailable; }
    double txForwardWatt() const { return m_txForwardWatt; }
    double txReflectedWatt() const { return m_txReflectedWatt; }
    double txSwr() const { return m_txSwr; }

    bool neuralAvailable() const;
    bool neuralEnabled() const { return m_neuralEnabled; }
    double neuralLoad() const;
    QString neuralState() const;
    double neuralLatencyMs() const;
    double neuralIntensity() const { return m_neuralIntensity; }
    void setNeuralIntensity(double db);
    dsp::neural::ModelStore *neuralModels() const { return m_neuralModels; }
    QStringList audioRoutes() const;

    /// Accende lo stadio neurale sull'audio. Non tocca il percorso IQ verso i
    /// decoder digitali: quello resta lineare per costruzione (SPEC-003 §8.3).
    Q_INVOKABLE void setNeuralNr(bool enabled);

    // ── Trasmissione ────────────────────────────────────────────────────

    int txChannel() const { return m_txChannel; }
    void setTxChannel(int row);
    double micGainDb() const { return m_micGainDb; }
    void setMicGainDb(double db);
    double txCompressionDb() const { return m_txCompressionDb; }
    void setTxCompressionDb(double db);
    double txDrive() const { return m_txDrive; }
    void setTxDrive(double drive);
    bool micActive() const;
    QString micDeviceName() const;
    QVariantList micDevices() const;
    QString micDeviceId() const { return m_micDeviceId; }
    void setMicDeviceId(const QString &id);
    double micLevel() const;
    double txCompressionMeter() const;
    double txLevel() const;
    bool txCw() const;
    bool tuning() const { return m_tuning; }
    int tuneSecondsLeft() const;
    QString txSummary() const;

    /// Tasto CW. Separato dal PTT perché in CW il PTT lo alza e lo abbassa il
    /// manipolatore, non l'operatore.
    Q_INVOKABLE void setCwKeyDown(bool down);

    /// Trasmissione di prova: portante per accordare, o due toni per vedere se
    /// il finale è lineare. `seconds` è la sicura — a zero vale il valore
    /// predefinito, e non esiste un modo di chiedere «per sempre».
    Q_INVOKABLE void startTune(bool twoTone = false, double seconds = 0.0);
    Q_INVOKABLE void stopTune();

    bool noiseBlanker() const { return m_nbEnabled; }
    double noiseBlankerThreshold() const { return m_nbThreshold; }
    double noiseBlankerActivity() const;

    /// Accende il soppressore di impulsi sull'intera banda. La soglia è in
    /// multipli del livello tipico: 4 di fabbrica, campo utile 2–8.
    Q_INVOKABLE void setNoiseBlanker(bool enabled, double threshold);

    /// Torna indietro di `seconds` rispetto a dove si sta ascoltando adesso.
    /// Premuto due volte riavvolge due volte, come ci si aspetta da un tasto.
    Q_INVOKABLE void rewind(double seconds);

    /// Porta l'ascolto a un ritardo preciso, per la barra di scorrimento.
    Q_INVOKABLE void setReplayDelay(double seconds);

    /// Ritorno al presente.
    Q_INVOKABLE void returnToLive();

    /// Sintonizza: sposta il centro della banda campionata e ci porta il
    /// ricevitore attivo, creandolo se non ce n'è ancora nessuno.
    ///
    /// È il gesto di chi opera, distinto da `setCenterFrequency`, che muove
    /// soltanto la finestra sullo spettro. La differenza non è teorica: un
    /// canale lasciato fuori dalla banda campionata non viene demodulato, e
    /// dal pannello non si vede che è successo.
    Q_INVOKABLE void tuneTo(qint64 hz);

    Q_INVOKABLE int addChannel(qint64 frequencyHz);
    Q_INVOKABLE void removeChannel(int row);
    Q_INVOKABLE void setChannelFrequency(int row, qint64 hz);
    Q_INVOKABLE void nudgeChannel(int row, qint64 deltaHz);
    Q_INVOKABLE void setChannelMode(int row, int mode);
    Q_INVOKABLE void setChannelFilter(int row, int lowHz, int highHz);

    /// Sposta il passa-banda senza cambiarne la larghezza (IF shift): serve
    /// quando l'interferenza sta da un lato solo.
    Q_INVOKABLE void setChannelPassbandShift(int row, double hz);
    Q_INVOKABLE void setChannelAgcMode(int row, int mode);
    Q_INVOKABLE void setChannelAgcThreshold(int row, double thresholdDb);

    /// La soglia AGC-T si regola da sé seguendo il fondo del rumore.
    Q_INVOKABLE void setChannelAgcAutoThreshold(int row, bool enabled);
    Q_INVOKABLE void setChannelAgcAttack(int row, double milliseconds);
    Q_INVOKABLE void setChannelAgcDecay(int row, double milliseconds);
    Q_INVOKABLE void setChannelAmCarrierAgc(int row, bool enabled);
    Q_INVOKABLE void setChannelVolume(int row, double volume);
    Q_INVOKABLE void setChannelMuted(int row, bool muted);
    Q_INVOKABLE void setChannelSquelch(int row, bool enabled, double thresholdDb);

    // ── Filtri di disturbo ──────────────────────────────────────────────
    //
    // Uno per comando, e ognuno acceso o spento dall'operatore. Nessuno è
    // gratis: la riduzione di rumore colora la voce, il notch automatico si
    // porta via anche le note CW. Accenderli tutti di fabbrica farebbe suonare
    // meglio il ricevitore in vetrina e peggio in aria.
    //
    // Il noise blanker non è fra questi: agisce su tutta la banda e sta nella
    // catena, non nel canale (SPEC-003 §4) — vedi `setNoiseBlanker`.
    Q_INVOKABLE void setChannelNoiseReduction(int row, bool enabled, double strength);
    Q_INVOKABLE void setChannelAutoNotch(int row, bool enabled);

    /// Filtro di picco sulla nota CW: esalta il tono di battimento e
    /// allontana il resto (SPEC-003 §7).
    Q_INVOKABLE void setChannelPeakFilter(int row, bool enabled, double q);

    /// Ascolto binaurale in CW: i due canali portano le componenti in
    /// quadratura, e le stazioni si separano nello spazio (SPEC-003 §7).
    Q_INVOKABLE void setChannelBinaural(int row, bool enabled);

    /// Banda laterale della AM sincrona: 0 entrambe, 1 inferiore, 2 superiore.
    Q_INVOKABLE void setChannelSamSideband(int row, int sideband);
    /// Mette un notch su una frequenza RF assoluta. Resta lì anche quando il
    /// ricevitore si sposta: è un disturbo, non un tono audio (SPEC-003 §5).
    Q_INVOKABLE void addChannelNotch(int row, qint64 frequencyHz, double widthHz = 120.0);
    Q_INVOKABLE void removeChannelNotch(int row, int notchIndex);
    Q_INVOKABLE void clearChannelNotches(int row);

    // ── Catena FM broadcast, RDS e CTCSS ────────────────────────────────
    Q_INVOKABLE void setChannelAudioHighPassEnabled(int row, bool enabled);
    Q_INVOKABLE void setChannelAudioHighPassHz(int row, double hertz);
    Q_INVOKABLE void setChannelFmStereo(int row, bool enabled);
    Q_INVOKABLE void setChannelFmAudioLowPass(int row, bool enabled);
    Q_INVOKABLE void setChannelFmDeemphasis(int row, double microseconds);
    Q_INVOKABLE void setChannelFmRds(int row, bool enabled);
    Q_INVOKABLE void setChannelRdsAutomaticAf(int row, bool enabled);
    Q_INVOKABLE void setChannelRdsRegion(int row, int region);
    /// Passa alla prima alternativa RDS diversa dalla frequenza corrente.
    /// È un cambio manuale: il client non interrompe l'ascolto sondando
    /// automaticamente le frequenze, comportamento potenzialmente invasivo.
    Q_INVOKABLE void followRdsAf(int row);
    Q_INVOKABLE void setChannelSquelchEnabled(int row, bool enabled);
    Q_INVOKABLE void setChannelSquelchThreshold(int row, double thresholdDb);
    Q_INVOKABLE void setChannelCtcssEnabled(int row, bool enabled);
    Q_INVOKABLE void setChannelCtcssDecodeOnly(int row, bool enabled);
    Q_INVOKABLE void setChannelCtcssTone(int row, double toneHz);
    Q_INVOKABLE void setChannelFmIfNoiseReductionEnabled(int row, bool enabled);
    Q_INVOKABLE void setChannelFmIfNoiseReductionPreset(int row, int preset);

    Q_INVOKABLE void setPtt(bool transmit);

    /// Aggiunge un indirizzo da sondare alla prossima discovery, per i backend
    /// che dichiarano `remoteCapable`. Il core non sa quale protocollo ci sia
    /// dietro: usa la convenzione "net.addEndpoint" (CONSTITUTION §7).
    Q_INVOKABLE bool addRemoteEndpoint(const QString &endpoint);
    Q_INVOKABLE QStringList remoteEndpoints() const;

    /// Avvia la registrazione del flusso IQ (RF-17). Con `path` vuoto il nome
    /// viene generato da data e frequenza nella cartella predefinita.
    Q_INVOKABLE bool startRecording(const QString &path = QString());
    Q_INVOKABLE void stopRecording();
    Q_INVOKABLE bool toggleRecording();
    Q_INVOKABLE bool startAudioRecording(const QString &path = QString());
    Q_INVOKABLE void stopAudioRecording();
    Q_INVOKABLE bool toggleAudioRecording();
    Q_INVOKABLE bool loadIqModule(const QString &path);
    Q_INVOKABLE void unloadIqModules();
    Q_INVOKABLE void loadIqModulesFromStandardPaths();
    QStringList iqModuleNames() const { return m_iqModuleNames; }
    QVariantList iqModuleCatalog() const { return m_iqModuleCatalog; }

    /// Nomi dei modi, per popolare i selettori senza duplicare la tabella in QML.
    Q_INVOKABLE QStringList modeNames() const;
    Q_INVOKABLE QStringList agcModeNames() const;

    /// Comando nativo del backend: usabile SOLO dai pannelli backend-specifici.
    Q_INVOKABLE QVariant nativeCommand(const QString &command, const QVariantMap &args);

signals:
    void iqModuleNamesChanged();
    void iqModuleCatalogChanged();
    void backendChanged();
    void connectionChanged();
    void discoveringChanged();
    void scanningChanged();
    void scanResultsChanged();
    void rigctlChanged();
    void transmittingChanged();
    void networkRadiosChanged();
    void txChanged();
    void txMetersChanged();
    /// La trasmissione è stata rifiutata, e per quale motivo.
    void txRefused(const QString &reason);
    void statusMessageChanged();
    void centerFrequencyChanged();
    void sampleRateChanged();
    void replayChanged();
    void noiseBlankerChanged();
    void streamHealthChanged();
    void audioToneChanged();
    void audioLevelsChanged();
    void neuralChanged();
    void overloadChanged();
    void spectrumAveragingChanged();
    void errorReported(const QString &message, bool fatal);

private:
    void setStatus(const QString &message);
    void setDiscovering(bool discovering);
    void teardownBackend();
    void pushChannelToEngine(int row);

    /// Riporta al motore TX il canale su cui si trasmette: offset dal
    /// centro, modo e banda. Si chiama a ogni cosa che li cambi — anche
    /// a un cambio di centro banda, che sposta l'offset senza che il
    /// canale si sia mosso.
    void pushTxConfig();

    /// Da che parte del VFO sta il segnale, quando la sorgente è audio.
    void pushAudioSideband(DemodMode mode);

    /// Apre il microfono scelto, o il predefinito se non ce n'è uno.
    bool startMicrophone();
    void refreshChannelOffsets();
    void advanceScan();
    void handleAutomaticRdsAf(ChannelId id, bool synced, const QString &pi);
    void probeNextRdsAf();
    void finishRdsAfProbe(bool keepCandidate);
    void handleRigctlLine(QTcpSocket *socket, const QByteArray &line);
    void onBackendError(const hal::BackendError &error);

    hal::IRadioBackend *m_backend = nullptr;
    QString m_backendId;

    DeviceListModel m_devices;
    ChannelModel m_channels;
    CapabilitiesInfo m_capabilities;
    IqRecorder m_recorder;
    IqRecorder m_audioRecorder;
    LanguageManager m_language;

    DspEngine *m_engine = nullptr;
    QThread *m_dspThread = nullptr;
    TxEngine *m_tx = nullptr;
    QThread *m_txThread = nullptr;
    audio::MicSource *m_mic = nullptr;
    hal::RadioScout *m_scout = nullptr;
    QVariantList m_networkRadios;
    dsp::neural::NeuralNrStage *m_neural = nullptr;
    dsp::neural::ModelStore *m_neuralModels = nullptr;
    double m_neuralIntensity = 100.0;
    audio::AudioGraph m_audioGraph;
    QThread *m_neuralThread = nullptr;
    audio::AudioRouter *m_audio = nullptr;

    QString m_deviceName;
    QString m_statusMessage;
    qint64 m_centerFrequency = 0;
    double m_sampleRate = 0.0;
    int m_spectrumAveraging = SpectrumFeed::kDefaultAveraging;
    double m_replayDelay = 0.0;      ///< di quanto si sta ascoltando indietro
    double m_replayHistory = 0.0;    ///< fin dove si potrebbe tornare
    double m_nbThreshold = 4.0;
    double m_peakDbfs = -160.0;
    double m_gainReductionDb = 0.0;
    int m_overloadMode = 0;
    bool m_nbEnabled = false;
    bool m_neuralEnabled = false;
    double m_streamHealth = -1.0;
    double m_audioToneHz = 0.0;
    double m_audioToneDb = -140.0;
    double m_audioThdPercent = -1.0;
    double m_audioPeakDb = -140.0;
    double m_audioRmsDb = -140.0;

    /// Finestra scorrevole degli ultimi campioni audio, per l'oscilloscopio.
    /// Vive sul thread GUI e si riempie svuotando il ring del motore.
    std::vector<float> m_scopeWindow;
    std::vector<float> m_scopeScratch;
    bool m_overloaded = false;

    bool m_txMetersAvailable = false;
    double m_txForwardWatt = 0.0;
    double m_txReflectedWatt = 0.0;
    double m_txSwr = 1.0;
    bool m_connected = false;
    bool m_discovering = false;
    bool m_transmitting = false;
    int m_txChannel = 0;
    double m_micGainDb = 6.0;
    double m_txCompressionDb = 6.0;
    /// Livello d'uscita di partenza per un SDR, che lavora a fondo scala.
    /// Verso una radio tradizionale si parte molto più bassi: vedi
    /// `connectToDevice`.
    static constexpr double kDefaultTxDrive = 0.9;
    double m_txDrive = kDefaultTxDrive;
    QString m_micDeviceId;   ///< vuoto = ingresso predefinito del sistema
    bool m_tuning = false;
    QTimer m_tuneTimer;
    QTimer m_scanTimer;
    QTimer m_rdsAfProbeTimer;
    std::vector<qint64> m_rdsAfCandidates;
    std::size_t m_rdsAfCandidateIndex = 0;
    int m_rdsAfProbeRow = -1;
    qint64 m_rdsAfOriginalFrequency = 0;
    qint64 m_rdsAfCandidateFrequency = 0;
    double m_rdsAfOriginalSignalDb = -160.0;
    QString m_rdsAfOriginalPi;
    QString m_rdsAfProbeList;
    QString m_rdsAfRejectedPi;
    QString m_rdsAfRejectedList;
    qint64 m_rdsAfRejectedFrequency = 0;
    bool m_rdsAfProbeActive = false;
    QStringList m_iqModuleNames;
    QVariantList m_iqModuleCatalog;
    bool m_scanning = false;
    int m_scanRow = -1;
    qint64 m_scanFrequency = 0;
    qint64 m_scanEnd = 0;
    qint64 m_scanStep = 0;
    qint64 m_scanLastHit = -1;
    double m_scanThresholdDb = -75.0;
    QVariantList m_scanResults;
    QTcpServer m_rigctlServer;
};

} // namespace dsdr::core
