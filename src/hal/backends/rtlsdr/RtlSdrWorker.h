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
    void fail(const QString &message, bool fatal);

    dsp::SpscRing<float> *m_ring = nullptr;
    QElapsedTimer m_clock;
    std::vector<float> m_scratch;
    QList<int> m_gainSteps;

    std::atomic<bool> m_running{false};
    std::atomic<rtlsdr_dev_t *> m_device{nullptr};
    std::atomic<qint64> m_pendingFrequency{-1};
    std::atomic<double> m_pendingSampleRate{-1.0};
    std::atomic<int> m_pendingGainTenths{-2}; ///< -2 none, -1 auto
    std::atomic<int> m_pendingPpm{std::numeric_limits<int>::min()};
    std::atomic<int> m_pendingBiasTee{-1};
    std::atomic<int> m_pendingDirectSampling{-1};
    std::atomic<int> m_pendingOffsetTuning{-1};
};

} // namespace dsdr::hal::rtlsdr
