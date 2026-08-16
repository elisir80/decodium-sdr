// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — thread di ingest per SoapySDR.
//
// Mentre streamma, questo oggetto NON gira su un event loop: `readStream()` è
// bloccante e un ciclo che lo chiama non può contemporaneamente servire le
// connessioni queued. I comandi arrivano quindi da variabili atomiche, che il
// ciclo applica fra una lettura e l'altra — è anche l'unico modo corretto di
// toccare un device SoapySDR, che non è thread-safe.
#pragma once

#include "hal/backends/soapy/SoapyProfile.h"

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>

#include <atomic>
#include <limits>
#include <mutex>
#include <vector>

namespace SoapySDR {
class Device;
}

namespace dsdr::dsp {
template <typename T>
class SpscRing;
}

namespace dsdr::hal::soapy {

class SoapyWorker : public QObject
{
    Q_OBJECT

public:
    explicit SoapyWorker(dsp::SpscRing<float> *ring, QObject *parent = nullptr);
    ~SoapyWorker() override;

    // ── Comandi thread-safe (non slot: agiscono su atomiche) ─────────────

    void requestFrequency(qint64 hz);
    void requestSampleRate(double rate);
    void requestGain(double db);        ///< NaN o valore negativo = automatico
    /// Indice nell'elenco antenne del profilo; negativo = nessun cambiamento.
    /// Si passa un indice e non un nome perché una stringa non può essere
    /// scambiata fra thread con una semplice atomica.
    void requestAntenna(int index);
    /// Impostazioni specifiche del driver, applicate dal solo thread Soapy.
    void requestDeviceSetting(const QString &key, const QString &value);
    /// Raddrizza uno spettro IF invertito e trasla il canale scelto a zero.
    void requestBasebandTransform(double translationHz, bool spectrumInverted);
    void requestStop();

public slots:
    /// Apre il device, legge il profilo e avvia il ciclo di lettura.
    /// Da invocare una sola volta, in modo queued, dopo il moveToThread.
    void openAndRun(const QString &deviceArgs, qint64 frequencyHz, double sampleRate);

signals:
    void opened(const dsdr::hal::soapy::SoapyDeviceProfile &profile);
    void failed(const QString &message, bool fatal);
    void finished();
    void samplesProduced(quint32 frames, quint32 dropped, quint64 timestampNs);

private:
    SoapyDeviceProfile readProfile(SoapySDR::Device *device, const QString &deviceArgs) const;
    void applyPendingCommands(SoapySDR::Device *device);
    void runLoop(SoapySDR::Device *device);

    dsp::SpscRing<float> *m_ring = nullptr;
    QElapsedTimer m_clock;
    std::vector<float> m_buffer;   ///< CF32: già float interleaved, nessuna conversione

    std::atomic<bool> m_running{false};
    std::atomic<qint64> m_pendingFrequency{-1};
    std::atomic<double> m_pendingSampleRate{-1.0};
    std::atomic<double> m_pendingGain{-1.0};
    std::atomic<bool> m_gainAuto{true};
    std::atomic<bool> m_gainCommandPending{true};
    std::atomic<int> m_pendingAntenna{-1};
    std::atomic<double> m_pendingTranslationHz{std::numeric_limits<double>::quiet_NaN()};
    std::atomic<int> m_pendingSpectrumInverted{-1};
    std::mutex m_settingsMutex;
    QHash<QString, QString> m_pendingDeviceSettings;
    QStringList m_antennas;   ///< letto dal profilo, usato solo dal ciclo
    double m_safeAutoGainDb = 0.0;
    bool m_directSamplingActive = false;
    bool m_spectrumInverted = false;
    double m_activeSampleRate = 0.0;
    double m_basebandTranslationHz = 0.0;
    double m_oscillatorI = 1.0;
    double m_oscillatorQ = 0.0;
    double m_oscillatorStepI = 1.0;
    double m_oscillatorStepQ = 0.0;
    quint32 m_oscillatorNormaliseCounter = 0;
};

} // namespace dsdr::hal::soapy
