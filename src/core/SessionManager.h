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
#include <QTimer>
#include <QTcpServer>

#include <vector>

namespace dsdr::hal {
class IRadioBackend;
}

class QThread;
class QTcpSocket;

namespace dsdr::core {

class DspEngine;

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
    Q_PROPERTY(bool rigctlRunning READ rigctlRunning NOTIFY rigctlChanged)
    Q_PROPERTY(int rigctlPort READ rigctlPort NOTIFY rigctlChanged)

    Q_PROPERTY(qint64 centerFrequency READ centerFrequency WRITE setCenterFrequency
                   NOTIFY centerFrequencyChanged)
    Q_PROPERTY(double sampleRate READ sampleRate WRITE setSampleRate NOTIFY sampleRateChanged)

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

    // ── Azioni dalla UI ─────────────────────────────────────────────────

    Q_INVOKABLE void selectBackend(const QString &backendId);
    Q_INVOKABLE void startDiscovery();
    Q_INVOKABLE void connectToDevice(int deviceRow);
    Q_INVOKABLE void disconnectDevice();
    Q_INVOKABLE bool startScan(qint64 startHz, qint64 endHz, qint64 stepHz,
                               int dwellMs = 350);
    Q_INVOKABLE void stopScan();
    Q_INVOKABLE bool startRigctl(int port = 4532);
    Q_INVOKABLE void stopRigctl();

    Q_INVOKABLE int addChannel(qint64 frequencyHz);
    Q_INVOKABLE void removeChannel(int row);
    Q_INVOKABLE void setChannelFrequency(int row, qint64 hz);
    Q_INVOKABLE void nudgeChannel(int row, qint64 deltaHz);
    Q_INVOKABLE void setChannelMode(int row, int mode);
    Q_INVOKABLE void setChannelFilter(int row, int lowHz, int highHz);
    Q_INVOKABLE void setChannelAgcMode(int row, int mode);
    Q_INVOKABLE void setChannelAgcThreshold(int row, double thresholdDb);
    Q_INVOKABLE void setChannelAgcAttack(int row, double milliseconds);
    Q_INVOKABLE void setChannelAgcDecay(int row, double milliseconds);
    Q_INVOKABLE void setChannelAmCarrierAgc(int row, bool enabled);
    Q_INVOKABLE void setChannelVolume(int row, double volume);
    Q_INVOKABLE void setChannelMuted(int row, bool muted);
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
    Q_INVOKABLE void setChannelNoiseBlankerEnabled(int row, bool enabled);
    Q_INVOKABLE void setChannelNoiseBlankerThreshold(int row, double thresholdDb);
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
    void statusMessageChanged();
    void centerFrequencyChanged();
    void sampleRateChanged();
    void errorReported(const QString &message, bool fatal);

private:
    void setStatus(const QString &message);
    void setDiscovering(bool discovering);
    void teardownBackend();
    void pushChannelToEngine(int row);
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
    audio::AudioRouter *m_audio = nullptr;

    QString m_deviceName;
    QString m_statusMessage;
    qint64 m_centerFrequency = 0;
    double m_sampleRate = 0.0;
    bool m_connected = false;
    bool m_discovering = false;
    bool m_transmitting = false;
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
