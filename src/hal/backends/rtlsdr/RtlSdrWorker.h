// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — ingest diretto librtlsdr.
#pragma once

#include "hal/backends/rtlsdr/RtlSdrProfile.h"

#include <rtl-sdr.h>

#include <QElapsedTimer>
#include <QObject>

#include <atomic>
#include <limits>
#include <vector>

namespace dsdr::dsp {
template <typename T>
class SpscRing;
}

namespace dsdr::hal::rtlsdr {

class RtlSdrWorker : public QObject
{
    Q_OBJECT

public:
    explicit RtlSdrWorker(dsp::SpscRing<float> *ring, QObject *parent = nullptr);
    ~RtlSdrWorker() override;

    void requestFrequency(qint64 hz);
    void requestSampleRate(double rate);
    void requestGain(double db); ///< valore negativo = automatico
    void requestPpm(int ppm);
    void requestBiasTee(bool enabled);
    void requestDirectSampling(int mode); ///< 0 off, 1 I, 2 Q
    void requestOffsetTuning(bool enabled);
    /// Raddrizza un'IF invertita e porta il canale scelto a 0 Hz prima del DSP.
    void requestBasebandTransform(double translationHz, bool spectrumInverted);
    /// Ferma la lettura USB e scarta ogni campione finche' la radio e' in TX.
    /// Non e' un attenuatore RF: e' il gate software che evita di consegnare
    /// al DSP dati ricevuti mentre l'uscita IF non e' garantita sicura.
    void requestStreamPause(bool paused);
    void requestStop();

public slots:
    void openAndRun(int deviceIndex, const QString &serial, qint64 frequencyHz,
                    double sampleRate);

signals:
    void opened(const RtlSdrDeviceProfile &profile);
    void failed(const QString &message, bool fatal);
    void finished();
    void samplesProduced(quint32 frames, quint32 dropped, quint64 timestampNs);

private:
    static void asyncCallback(unsigned char *buffer, quint32 length, void *context);
    void onSamples(unsigned char *buffer, quint32 length);
    RtlSdrDeviceProfile readProfile(rtlsdr_dev_t *device, int deviceIndex,
                                    const QString &serial) const;
    void applyPendingCommands(rtlsdr_dev_t *device);
    /// Solo stato DSP: e' sicuro applicarlo dal callback senza fermare USB.
    void applyPendingBasebandTransform();
    /// Interrompe read_async: la riconfigurazione viene applicata nel thread
    /// del ricevitore, prima di riaprire il flusso USB.
    void requestReconfigure();
    void fail(const QString &message, bool fatal);

    dsp::SpscRing<float> *m_ring = nullptr;
    QElapsedTimer m_clock;
    std::vector<float> m_scratch;
    QList<int> m_gainSteps;

    std::atomic<bool> m_running{false};
    std::atomic<bool> m_reconfigureRequested{false};
    std::atomic<bool> m_streamPaused{false};
    std::atomic<rtlsdr_dev_t *> m_device{nullptr};
    std::atomic<qint64> m_pendingFrequency{-1};
    std::atomic<double> m_pendingSampleRate{-1.0};
    std::atomic<int> m_pendingGainTenths{-2}; ///< -2 none, -1 auto
    std::atomic<int> m_pendingPpm{std::numeric_limits<int>::min()};
    std::atomic<int> m_pendingBiasTee{-1};
    std::atomic<int> m_pendingDirectSampling{-1};
    std::atomic<int> m_pendingOffsetTuning{-1};
    std::atomic<double> m_pendingTranslationHz{std::numeric_limits<double>::quiet_NaN()};
    std::atomic<int> m_pendingSpectrumInverted{-1};

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

} // namespace dsdr::hal::rtlsdr
