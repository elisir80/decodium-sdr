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
#include <QObject>
#include <QString>

#include <atomic>
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
};

} // namespace dsdr::hal::soapy
