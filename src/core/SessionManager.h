// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — sessione: tiene insieme backend, DSP, audio e modelli.
//
// È l'unico oggetto che QML deve conoscere. Non include alcun header di
// backend concreto: parla solo con IRadioBackend e con il registro
// (CONSTITUTION §4).
#pragma once

// I tipi esposti come Q_PROPERTY devono essere completi: moc genera un
// metatype per ciascun puntatore e una forward declaration non basta.
#include "audio/AudioRouter.h"
#include "core/CapabilitiesInfo.h"
#include "core/ChannelModel.h"
#include "core/DeviceListModel.h"
#include "core/IqRecorder.h"
#include "core/LanguageManager.h"
#include "core/SpectrumFeed.h"
#include "hal/Frames.h"

#include <QObject>
#include <QVariantList>

namespace dsdr::hal {
class IRadioBackend;
}

class QThread;

namespace dsdr::core {

class DspEngine;

class SessionManager : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QVariantList availableBackends READ availableBackends CONSTANT)
    Q_PROPERTY(QString backendId READ backendId NOTIFY backendChanged)
    Q_PROPERTY(QString backendName READ backendName NOTIFY backendChanged)

    Q_PROPERTY(dsdr::core::DeviceListModel *devices READ devices CONSTANT)
    Q_PROPERTY(dsdr::core::ChannelModel *channels READ channels CONSTANT)
    Q_PROPERTY(dsdr::core::CapabilitiesInfo *capabilities READ capabilities CONSTANT)
    Q_PROPERTY(dsdr::core::SpectrumFeed *spectrum READ spectrum CONSTANT)
    Q_PROPERTY(dsdr::audio::AudioRouter *audio READ audio CONSTANT)
    Q_PROPERTY(dsdr::core::IqRecorder *recorder READ recorder CONSTANT)
    Q_PROPERTY(dsdr::core::LanguageManager *language READ language CONSTANT)

    Q_PROPERTY(bool connected READ isConnected NOTIFY connectionChanged)
    Q_PROPERTY(bool discovering READ isDiscovering NOTIFY discoveringChanged)
    Q_PROPERTY(bool transmitting READ isTransmitting NOTIFY transmittingChanged)
    Q_PROPERTY(QString deviceName READ deviceName NOTIFY connectionChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)

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
    audio::AudioRouter *audio() const { return m_audio; }
    IqRecorder *recorder() { return &m_recorder; }
    LanguageManager *language() { return &m_language; }

    bool isConnected() const { return m_connected; }
    bool isDiscovering() const { return m_discovering; }
    bool isTransmitting() const { return m_transmitting; }
    QString deviceName() const { return m_deviceName; }
    QString statusMessage() const { return m_statusMessage; }

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
    Q_INVOKABLE void connectToDevice(int deviceRow);
    Q_INVOKABLE void disconnectDevice();

    double replayDelaySeconds() const { return m_replayDelay; }
    double replayHistorySeconds() const { return m_replayHistory; }
    double replayCapacitySeconds() const;
    bool replaying() const { return m_replayDelay > 0.05; }

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
    Q_INVOKABLE void setChannelAgcMode(int row, int mode);
    Q_INVOKABLE void setChannelAgcThreshold(int row, double thresholdDb);
    Q_INVOKABLE void setChannelVolume(int row, double volume);
    Q_INVOKABLE void setChannelMuted(int row, bool muted);
    Q_INVOKABLE void setChannelSquelch(int row, bool enabled, double thresholdDb);

    // ── Filtri di disturbo ──────────────────────────────────────────────
    //
    // Uno per comando, e ognuno acceso o spento dall'operatore. Nessuno è
    // gratis: il blanker tronca, la riduzione di rumore colora la voce, il
    // notch automatico si porta via anche le note CW. Accenderli tutti di
    // fabbrica farebbe suonare meglio il ricevitore in vetrina e peggio in
    // aria.
    Q_INVOKABLE void setChannelNoiseReduction(int row, bool enabled, double strength);
    Q_INVOKABLE void setChannelAutoNotch(int row, bool enabled);
    Q_INVOKABLE void setChannelNotch(int row, bool enabled, double frequencyHz,
                                     double widthHz);

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

    /// Nomi dei modi, per popolare i selettori senza duplicare la tabella in QML.
    Q_INVOKABLE QStringList modeNames() const;
    Q_INVOKABLE QStringList agcModeNames() const;

    /// Comando nativo del backend: usabile SOLO dai pannelli backend-specifici.
    Q_INVOKABLE QVariant nativeCommand(const QString &command, const QVariantMap &args);

signals:
    void backendChanged();
    void connectionChanged();
    void discoveringChanged();
    void transmittingChanged();
    void statusMessageChanged();
    void centerFrequencyChanged();
    void sampleRateChanged();
    void replayChanged();
    void noiseBlankerChanged();
    void spectrumAveragingChanged();
    void errorReported(const QString &message, bool fatal);

private:
    void setStatus(const QString &message);
    void setDiscovering(bool discovering);
    void teardownBackend();
    void pushChannelToEngine(int row);
    void refreshChannelOffsets();
    void onBackendError(const hal::BackendError &error);

    hal::IRadioBackend *m_backend = nullptr;
    QString m_backendId;

    DeviceListModel m_devices;
    ChannelModel m_channels;
    CapabilitiesInfo m_capabilities;
    IqRecorder m_recorder;
    LanguageManager m_language;

    DspEngine *m_engine = nullptr;
    QThread *m_dspThread = nullptr;
    audio::AudioRouter *m_audio = nullptr;

    QString m_deviceName;
    QString m_statusMessage;
    qint64 m_centerFrequency = 0;
    double m_sampleRate = 0.0;
    int m_spectrumAveraging = SpectrumFeed::kDefaultAveraging;
    double m_replayDelay = 0.0;      ///< di quanto si sta ascoltando indietro
    double m_replayHistory = 0.0;    ///< fin dove si potrebbe tornare
    double m_nbThreshold = 4.0;
    bool m_nbEnabled = false;
    bool m_connected = false;
    bool m_discovering = false;
    bool m_transmitting = false;
};

} // namespace dsdr::core
