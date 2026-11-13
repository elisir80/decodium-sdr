// SPDX-License-Identifier: GPL-3.0-or-later
#include "SyntheticSignal.h"
#include "dsp/NoiseBlanker.h"
#include <QTest>
#include <algorithm>
#include <cmath>
#include <stdexcept>

using namespace dsdr;

class TestSyntheticSignal : public QObject {
    Q_OBJECT
private slots:
    void replayAndChunking();
    void toneAndModulation();
    void noiseAndValidation();
    void impulsePlacement();
    void blankerAB();
};

void TestSyntheticSignal::replayAndChunking()
{
    test::SyntheticSignalConfig c;
    c.noiseRms = 0.1; c.seed = 42; c.impulsePeriod = 67; c.impulseOffset = 13;
    c.impulse = {2.0f, -1.0f};
    test::SyntheticSignal gen(c);
    const auto whole = gen.generate(1000);
    gen.reset();
    auto parts = gen.generate(123);
    const auto tail = gen.generate(877);
    parts.insert(parts.end(), tail.begin(), tail.end());
    QVERIFY(whole == parts);
    c.seed++;
    QVERIFY(whole != test::SyntheticSignal(c).generate(1000));
    gen.reset();
    QVERIFY(whole == gen.generate(1000));
}

void TestSyntheticSignal::toneAndModulation()
{
    test::SyntheticSignalConfig c;
    c.sampleRate = 8000; c.carrierHz = 1000; c.amplitude = 0.5;
    auto tone = test::SyntheticSignal(c).generate(9);
    QVERIFY(std::abs(tone[0] - dsp::Complex(0.5f, 0.0f)) < 1e-6f);
    QVERIFY(std::abs(tone[2] - dsp::Complex(0.0f, 0.5f)) < 1e-6f);
    c.carrierHz = 0; c.modulationHz = 1000; c.amDepth = 0.5;
    auto am = test::SyntheticSignal(c).generate(3);
    QVERIFY(std::abs(am[2].real() - 0.75f) < 1e-6f);
    c.amDepth = 0; c.fmDeviationHz = 1000;
    auto fm = test::SyntheticSignal(c).generate(5);
    QVERIFY(std::abs(std::arg(fm[2]) - 1.0) < 1e-6);
    QVERIFY(std::abs(std::abs(fm[2]) - 0.5f) < 1e-6f);
}

void TestSyntheticSignal::noiseAndValidation()
{
    test::SyntheticSignalConfig c;
    c.amplitude = 0; c.noiseRms = 0.2;
    auto samples = test::SyntheticSignal(c).generate(100000);
    double mean = 0, power = 0;
    for (auto s : samples) { mean += s.real(); power += dsp::magnitudeSquared(s); }
    mean /= samples.size(); power /= samples.size();
    QVERIFY(std::abs(mean) < 0.003);
    QVERIFY(std::abs(power - 0.08) < 0.002);
    c.sampleRate = 0;
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, test::SyntheticSignal{c});
    c.sampleRate = 48000; c.fmDeviationHz = 1; c.modulationHz = 0;
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, test::SyntheticSignal{c});
}

void TestSyntheticSignal::impulsePlacement()
{
    test::SyntheticSignalConfig c;
    c.amplitude = 0; c.impulseOffset = 2; c.impulsePeriod = 3;
    c.impulse = {1.0f, -0.5f};
    const auto samples = test::SyntheticSignal(c).generate(8);
    for (std::size_t i = 0; i < samples.size(); ++i)
        QVERIFY(samples[i] == ((i == 2 || i == 5) ? c.impulse : dsp::Complex{}));
}

void TestSyntheticSignal::blankerAB()
{
    test::SyntheticSignalConfig c;
    c.sampleRate = 48000; c.carrierHz = 1000; c.amplitude = 0.1;
    const auto clean = test::SyntheticSignal(c).generate(12000);
    c.impulsePeriod = 500; c.impulseOffset = 1000; c.impulse = {2.0f, 0.0f};
    const auto input = test::SyntheticSignal(c).generate(12000);
    auto output = input;
    dsp::NoiseBlanker blanker;
    blanker.configure(c.sampleRate);
    const auto removed = blanker.process(output.data(), output.size());
    double before = 0, after = 0;
    for (std::size_t i = 0; i < input.size(); ++i) {
        before += std::norm(input[i] - clean[i]);
        after += std::norm(output[i] - clean[i]);
    }
    QVERIFY(removed > 0);
    QVERIFY(after < before * 0.1);
}

QTEST_APPLESS_MAIN(TestSyntheticSignal)
#include "tst_synthetic_signal.moc"
