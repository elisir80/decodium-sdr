// SPDX-License-Identifier: GPL-3.0-or-later
#include "hal/backends/demo/ConfigurableSignal.h"
#include "hal/backends/demo/DemoBackend.h"
#include "dsp/SpscRing.h"
#include <QElapsedTimer>
#include <QDir>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <cmath>
#include <vector>

using namespace dsdr;
using namespace dsdr::hal::demo;
class TestDemoSignal : public QObject {
    Q_OBJECT
private slots:
    void replayAndAB();
    void effectsAB();
    void liveBackendAndPersistence();
    void maxRateCost();
};
void TestDemoSignal::replayAndAB()
{
    QVariantMap c = ConfigurableSignal::defaults();
    c["enabled"] = true; c["noiseRms"] = 0.1; c["seed"] = 17;
    c["impulsePeriod"] = 127; c["impulseAmplitude"] = 0.3;
    ConfigurableSignal generator;
    generator.configure(192000, c);
    std::vector<dsp::Complex> a(8192), b(8192);
    generator.generate(a.data(), a.size());
    generator.configure(192000, c);
    generator.generate(b.data(), 123);
    generator.generate(b.data() + 123, b.size() - 123);
    QVERIFY(a == b);
    c["seed"] = 18;
    generator.configure(192000, c);
    generator.generate(b.data(), b.size());
    QVERIFY(a != b);
    c["noiseRms"] = 0.; c["impulseAmplitude"] = 0.; c["mode"] = "am";
    generator.configure(192000, c);
    generator.generate(a.data(), a.size());
    c["amDepth"] = 0.;
    generator.configure(192000, c);
    generator.generate(b.data(), b.size());
    QVERIFY(std::abs(a[48]) > std::abs(b[48]) * 1.4f);
    c["mode"] = "fm";
    generator.configure(192000, c);
    generator.generate(a.data(), a.size());
    QVERIFY(std::abs(std::abs(a[100]) - 0.2f) < 1e-5f);
    QVERIFY(std::abs(a[100] - b[100]) > 0.01f);
}
void TestDemoSignal::effectsAB()
{
    constexpr double rate = 8000.0;
    auto c = ConfigurableSignal::defaults();
    c["offsetHz"] = 1000.0; c["amplitude"] = 0.2;
    ConfigurableSignal g;
    std::vector<dsp::Complex> baseline(8001), changed(8001), replay(8001);
    auto generate = [&](std::vector<dsp::Complex> &out) {
        g.configure(rate, c); g.generate(out.data(), out.size());
    };
    generate(baseline);

    // A/B instantaneous frequency: quarter cycle is the positive drift peak.
    c["driftHz"] = 100.0; c["driftPeriodSec"] = 1.0;
    generate(changed);
    auto frequencyAt = [&](const std::vector<dsp::Complex> &v, std::size_t i) {
        return std::arg(std::conj(v[i]) * v[i + 1]) * rate / dsp::kTwoPi;
    };
    QVERIFY(std::abs(frequencyAt(baseline, 2000) - 1000.0) < 0.01);
    QVERIFY(std::abs(frequencyAt(changed, 2000) - 1100.0) < 0.01);
    QVERIFY(std::abs(frequencyAt(changed, 6000) - 900.0) < 0.01);

    c["driftHz"] = 0.0; c["interfererOffsetHz"] = 2000.0;
    c["interfererAmplitude"] = 0.1;
    generate(changed);
    dsp::Complex line(0, 0);
    for (int i = 0; i < 8000; ++i) {
        const double phase = -dsp::kTwoPi * 2000.0 * i / rate;
        line += (changed[i] - baseline[i]) * dsp::Complex(std::cos(phase), std::sin(phase));
    }
    QVERIFY(std::abs(std::abs(line) / 8000.0 - 0.1) < 1e-5);

    c["interfererAmplitude"] = 0.0; c["fadingDepth"] = 0.75; c["fadingPeriodSec"] = 1.0;
    generate(changed);
    QVERIFY(std::abs(std::abs(changed[0]) - 0.2) < 1e-6);
    QVERIFY(std::abs(std::abs(changed[4000]) - 0.05) < 1e-6);
    QVERIFY(std::abs(std::abs(baseline[4000]) - 0.2) < 1e-6);

    c["driftHz"] = 100.0; c["interfererAmplitude"] = 0.1;
    c["noiseRms"] = 0.03; c["seed"] = 99;
    generate(changed);
    g.configure(rate, c); g.generate(replay.data(), 1234);
    g.generate(replay.data() + 1234, replay.size() - 1234);
    QVERIFY(changed == replay);
    QVariantMap output; QString error;
    auto old = ConfigurableSignal::defaults();
    for (const auto *key : {"driftHz", "driftPeriodSec", "interfererOffsetHz",
                             "interfererAmplitude", "fadingDepth", "fadingPeriodSec"}) old.remove(key);
    QVERIFY(ConfigurableSignal::validate(old, output, error));
    QCOMPARE(output.value("driftHz").toDouble(), 0.0);
    QCOMPARE(output.value("fadingDepth").toDouble(), 0.0);
    old["fadingPeriodSec"] = 0.0;
    QVERIFY(!ConfigurableSignal::validate(old, output, error));
}
void TestDemoSignal::maxRateCost()
{
    auto c = ConfigurableSignal::defaults();
    c["mode"] = "fm"; c["noiseRms"] = 0.1;
    c["driftHz"] = 100.; c["interfererAmplitude"] = 0.1; c["fadingDepth"] = 0.5;
    ConfigurableSignal g;
    g.configure(1536000, c);
    std::vector<dsp::Complex> samples(1536000);
    QElapsedTimer timer; timer.start();
    g.generate(samples.data(), samples.size());
    qInfo() << "FM, noise and effects, 1.536 million samples, ms:" << timer.elapsed();
    QVERIFY(std::isfinite(samples.back().real()));
}
void TestDemoSignal::liveBackendAndPersistence()
{
    const QString testDir = qEnvironmentVariable("DSDR_TEST_SETTINGS_DIR", QDir::tempPath());
    QTemporaryDir dir(testDir + QStringLiteral("/demo-settings-XXXXXX"));
    QVERIFY(dir.isValid());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
    QCoreApplication::setOrganizationName("DemoSignalTest");
    QCoreApplication::setApplicationName("DemoSignalTest");
    QVariantMap c = ConfigurableSignal::defaults();
    c["enabled"] = true; c["amplitude"] = 0.4; c["offsetHz"] = 2000.;
    c["driftHz"] = 30.; c["driftPeriodSec"] = 8.; c["interfererOffsetHz"] = 5000.;
    c["fadingDepth"] = 0.2; c["fadingPeriodSec"] = 4.;
    c["fadingDepth"] = 0.; c["driftHz"] = 0.;
    {
        DemoBackend backend;
        QVERIFY(backend.nativeCommand("demo.synthetic.apply", c).toMap().value("ok").toBool());
        QVERIFY(!backend.capabilities().canTransmit());
        backend.setPtt(true);
        QVERIFY(!backend.ptt());
        QVariantMap invalid = c; invalid["amplitude"] = 9.;
        QVERIFY(!backend.nativeCommand("demo.synthetic.apply", invalid).toMap().value("ok").toBool());
        QCOMPARE(backend.nativeCommand("demo.synthetic.status", {}).toMap().value("amplitude").toDouble(), 0.4);
        backend.open(DemoBackend::syntheticDevices().first());
        QTRY_VERIFY_WITH_TIMEOUT(backend.iqStream()->available() >= 4096, 3000);
        std::vector<float> samples(4096);
        QCOMPARE(backend.iqStream()->read(samples.data(), samples.size()), samples.size());
        double power = 0;
        for (float v : samples) power += v * v;
        power /= samples.size() / 2;
        QVERIFY(std::abs(power - 0.16) < 0.001);
        c["amplitude"] = 0.1;
        QElapsedTimer timer; timer.start();
        QVERIFY(backend.nativeCommand("demo.synthetic.apply", c).toMap().value("ok").toBool());
        QVERIFY(timer.elapsed() < 250);
        // Drain old buffered blocks until the asynchronously applied B appears.
        bool observedB = false;
        QElapsedTimer deadline; deadline.start();
        while (deadline.elapsed() < 3000 && !observedB) {
            QCoreApplication::processEvents();
            if (backend.iqStream()->available() >= samples.size()) {
                backend.iqStream()->read(samples.data(), samples.size());
                double bPower = 0;
                for (float v : samples) bPower += v * v;
                bPower /= samples.size() / 2;
                observedB = std::abs(bPower - 0.01) < 0.001;
            } else {
                QTest::qWait(5);
            }
        }
        QVERIFY(observedB);
        // A second live change isolates the interferer in the RX stream.
        c["amplitude"] = 0.; c["interfererAmplitude"] = 0.3;
        c["driftHz"] = 30.; c["fadingDepth"] = 0.5;
        QVERIFY(backend.nativeCommand("demo.synthetic.apply", c).toMap().value("ok").toBool());
        bool observedInterferer = false;
        deadline.restart();
        while (deadline.elapsed() < 3000 && !observedInterferer) {
            QCoreApplication::processEvents();
            if (backend.iqStream()->available() >= samples.size()) {
                backend.iqStream()->read(samples.data(), samples.size());
                double interfererPower = 0;
                for (float v : samples) interfererPower += v * v;
                interfererPower /= samples.size() / 2;
                observedInterferer = std::abs(interfererPower - 0.09) < 0.001;
            } else {
                QTest::qWait(5);
            }
        }
        QVERIFY(observedInterferer);
        backend.close();
    }
    {
        DemoBackend restored;
        const auto saved = restored.nativeCommand("demo.synthetic.status", {}).toMap();
        QVERIFY(saved.value("enabled").toBool());
        QCOMPARE(saved.value("amplitude").toDouble(), 0.0);
        QCOMPARE(saved.value("driftHz").toDouble(), 30.0);
        QCOMPARE(saved.value("interfererAmplitude").toDouble(), 0.3);
        QCOMPARE(saved.value("fadingDepth").toDouble(), 0.5);
        QCOMPARE(saved.value("driftPeriodSec").toDouble(), 8.0);
        QCOMPARE(saved.value("interfererOffsetHz").toDouble(), 5000.0);
        QCOMPARE(saved.value("fadingPeriodSec").toDouble(), 4.0);
        QVERIFY(!restored.capabilities().canTransmit());
    }
}
QTEST_GUILESS_MAIN(TestDemoSignal)
#include "tst_demo_signal.moc"
