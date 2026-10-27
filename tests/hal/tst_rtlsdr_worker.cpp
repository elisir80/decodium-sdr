// SPDX-License-Identifier: GPL-3.0-or-later
#include "hal/backends/rtlsdr/RtlSdrWorker.h"

#include <QtTest>
#include <cmath>

namespace dsdr::hal::rtlsdr {
class RtlSdrWorkerTest : public QObject
{
    Q_OBJECT
private slots:
    void translationSurvivesRateChange();
};

void RtlSdrWorkerTest::translationSurvivesRateChange()
{
    RtlSdrWorker worker(nullptr);
    worker.m_activeSampleRate = 1'024'000;
    // Il callback puo' consumare la traslazione prima che il cambio rate
    // interrompa read_async. La successiva riconfigurazione deve ricalcolare
    // l'oscillatore anche se non rimane alcuna richiesta di traslazione.
    worker.requestBasebandTransform(384'000, false);
    worker.applyPendingBasebandTransform();
    worker.m_activeSampleRate = 1'536'000;
    worker.applyPendingBasebandTransform(true);
    QVERIFY(std::abs(worker.m_oscillatorStepI) < 1e-12);
    QVERIFY(std::abs(worker.m_oscillatorStepQ - 1.0) < 1e-12);
}
}

QTEST_MAIN(dsdr::hal::rtlsdr::RtlSdrWorkerTest)
#include "tst_rtlsdr_worker.moc"
