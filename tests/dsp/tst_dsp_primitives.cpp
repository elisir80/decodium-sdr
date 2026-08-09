// SPDX-License-Identifier: GPL-3.0-or-later
// Vettori noti per le primitive DSP (RNF-07).

#include "dsp/Agc.h"
#include "dsp/AudioHighPass.h"
#include "dsp/ComplexFir.h"
#include "dsp/FirDesign.h"
#include "dsp/Nco.h"
#include "dsp/SpscRing.h"

#include <QTest>

#include <cmath>
#include <numeric>
#include <vector>

using namespace dsdr;
using namespace dsdr::dsp;

namespace {

std::vector<Complex> makeTone(double frequencyHz, double sampleRate, std::size_t n, float amplitude = 1.0f)
{
    std::vector<Complex> out(n);
    const double w = kTwoPi * frequencyHz / sampleRate;
    for (std::size_t i = 0; i < n; ++i) {
        const double phase = w * static_cast<double>(i);
        out[i] = Complex(amplitude * static_cast<float>(std::cos(phase)),
                         amplitude * static_cast<float>(std::sin(phase)));
    }
    return out;
}

float rms(const Complex *data, std::size_t n)
{
    double sum = 0.0;
    for (std::size_t i = 0; i < n; ++i)
        sum += magnitudeSquared(data[i]);
    return static_cast<float>(std::sqrt(sum / static_cast<double>(n)));
}

} // namespace

class TestDspPrimitives : public QObject
{
    Q_OBJECT

private slots:
    // ── SpscRing ─────────────────────────────────────────────────────────

    void ringRoundTrip();
    void ringWrapsAcrossBoundary();
    void ringReportsOverrunInsteadOfCorrupting();

    // ── Progetto dei filtri ──────────────────────────────────────────────

    void lowpassHasUnityDcGain();
    void lowpassIsSymmetric();
    void tapEstimateGrowsWithNarrowerTransition();
    void audioHighPassRejectsLowFrequency();

    // ── NCO ──────────────────────────────────────────────────────────────

    void ncoTranslatesToneToBaseband();
    void ncoKeepsUnitMagnitudeOverLongRun();

    // ── Filtro complesso ─────────────────────────────────────────────────

    void bandpassPassesWantedSidebandAndRejectsMirror();

    // ── AGC ──────────────────────────────────────────────────────────────

    void agcConvergesTowardsTargetLevel();
    void agcThresholdStopsNoiseAmplification();
    void agcAcceptsCustomTiming();
};

void TestDspPrimitives::ringRoundTrip()
{
    SpscRing<float> ring(1024);
    QCOMPARE(ring.capacity(), std::size_t(1024));
    QCOMPARE(ring.available(), std::size_t(0));

    std::vector<float> in(100);
    std::iota(in.begin(), in.end(), 0.0f);
    QCOMPARE(ring.write(in.data(), in.size()), in.size());
    QCOMPARE(ring.available(), in.size());

    std::vector<float> out(100, -1.0f);
    QCOMPARE(ring.read(out.data(), out.size()), in.size());
    QCOMPARE(out, in);
    QCOMPARE(ring.available(), std::size_t(0));
}

void TestDspPrimitives::ringWrapsAcrossBoundary()
{
    SpscRing<float> ring(16);
    std::vector<float> block(12);
    std::vector<float> out(12);

    // Tre giri completi: ogni giro sposta il punto di wrap.
    for (int round = 0; round < 3; ++round) {
        for (std::size_t i = 0; i < block.size(); ++i)
            block[i] = static_cast<float>(round * 100 + static_cast<int>(i));

        QCOMPARE(ring.write(block.data(), block.size()), block.size());
        QCOMPARE(ring.read(out.data(), out.size()), block.size());
        QCOMPARE(out, block);
    }
}

void TestDspPrimitives::ringReportsOverrunInsteadOfCorrupting()
{
    SpscRing<float> ring(8);
    std::vector<float> in(20, 1.0f);

    // Il ring accetta solo ciò che entra e lo dichiara: mai sovrascrittura
    // silenziosa dei campioni non ancora letti.
    const std::size_t written = ring.write(in.data(), in.size());
    QCOMPARE(written, std::size_t(8));
    QCOMPARE(ring.available(), std::size_t(8));
    QCOMPARE(ring.space(), std::size_t(0));
    QCOMPARE(ring.write(in.data(), 1), std::size_t(0));
}

void TestDspPrimitives::lowpassHasUnityDcGain()
{
    const std::vector<float> taps = designLowpass(3000.0, 48000.0, 63, 8.0);
    const double sum = std::accumulate(taps.begin(), taps.end(), 0.0);
    QVERIFY2(std::abs(sum - 1.0) < 1e-5,
             qPrintable(QStringLiteral("guadagno DC = %1").arg(sum)));
}

void TestDspPrimitives::lowpassIsSymmetric()
{
    const std::vector<float> taps = designLowpass(3000.0, 48000.0, 63, 8.0);
    QCOMPARE(taps.size() % 2, std::size_t(1)); // fase lineare: lunghezza dispari
    for (std::size_t i = 0; i < taps.size() / 2; ++i)
        QVERIFY(std::abs(taps[i] - taps[taps.size() - 1 - i]) < 1e-6f);
}

void TestDspPrimitives::tapEstimateGrowsWithNarrowerTransition()
{
    const int wide = estimateTaps(4000.0, 48000.0, 80.0);
    const int narrow = estimateTaps(500.0, 48000.0, 80.0);
    QVERIFY(narrow > wide);
    QVERIFY(wide >= 15);
    QCOMPARE(narrow % 2, 1);
}

void TestDspPrimitives::audioHighPassRejectsLowFrequency()
{
    constexpr double kRate = 48000.0;
    constexpr std::size_t kSamples = 48000;
    AudioHighPass filter;
    QVERIFY(filter.configure(kRate, 300.0, 100.0));

    std::vector<float> low(kSamples);
    for (std::size_t i = 0; i < kSamples; ++i)
        low[i] = std::sin(kTwoPi * 100.0 * static_cast<double>(i) / kRate);
    for (float &sample : low)
        sample = filter.process(sample);

    double lowPower = 0.0;
    for (std::size_t i = 4096; i < low.size(); ++i)
        lowPower += static_cast<double>(low[i]) * low[i];

    filter.reset();
    std::vector<float> high(kSamples);
    for (std::size_t i = 0; i < kSamples; ++i)
        high[i] = std::sin(kTwoPi * 1000.0 * static_cast<double>(i) / kRate);
    for (float &sample : high)
        sample = filter.process(sample);

    double highPower = 0.0;
    for (std::size_t i = 4096; i < high.size(); ++i)
        highPower += static_cast<double>(high[i]) * high[i];

    QVERIFY2(highPower > lowPower * 20.0,
             qPrintable(QStringLiteral("potenza 100 Hz=%1, 1 kHz=%2")
                            .arg(lowPower).arg(highPower)));
}

void TestDspPrimitives::ncoTranslatesToneToBaseband()
{
    constexpr double kRate = 48000.0;
    constexpr double kTone = 5000.0;
    const std::vector<Complex> input = makeTone(kTone, kRate, 4096);

    Nco nco;
    nco.configure(kRate, kTone);

    std::vector<Complex> output(input.size());
    nco.mixDown(input.data(), output.data(), input.size());

    // Dopo il DDC il tono è a DC: parte immaginaria nulla, reale costante.
    for (std::size_t i = 0; i < output.size(); i += 97) {
        QVERIFY2(std::abs(output[i].imag()) < 1e-3f,
                 qPrintable(QStringLiteral("imag=%1 a i=%2").arg(output[i].imag()).arg(i)));
        QVERIFY(std::abs(output[i].real() - 1.0f) < 1e-3f);
    }
}

void TestDspPrimitives::ncoKeepsUnitMagnitudeOverLongRun()
{
    Nco nco;
    nco.configure(192000.0, 12345.0);

    std::vector<Complex> ones(1 << 16, Complex(1.0f, 0.0f));
    std::vector<Complex> out(ones.size());
    nco.mixDown(ones.data(), out.data(), ones.size());

    // Senza rinormalizzazione l'accumulo in float farebbe derivare il modulo.
    const float magnitude = std::sqrt(magnitudeSquared(out.back()));
    QVERIFY2(std::abs(magnitude - 1.0f) < 1e-3f,
             qPrintable(QStringLiteral("modulo finale = %1").arg(magnitude)));
}

void TestDspPrimitives::bandpassPassesWantedSidebandAndRejectsMirror()
{
    constexpr double kRate = 48000.0;

    // Passa-banda 300–2700 Hz: è la banda USB tipica.
    const std::vector<Complex> taps = designBandpass(300.0, 2700.0, kRate, 255, kaiserBeta(80.0));
    ComplexFir filter;
    filter.setTaps(taps);

    const std::vector<Complex> wanted = makeTone(1500.0, kRate, 8192);
    std::vector<Complex> out(wanted.size());
    filter.process(wanted.data(), out.data(), wanted.size());
    const float passLevel = rms(out.data() + 1024, out.size() - 1024);

    // Lo specchio a -1500 Hz è la banda laterale che NON vogliamo: è la
    // ragione per cui i coefficienti sono complessi.
    filter.reset();
    const std::vector<Complex> mirror = makeTone(-1500.0, kRate, 8192);
    filter.process(mirror.data(), out.data(), mirror.size());
    const float rejectLevel = rms(out.data() + 1024, out.size() - 1024);

    QVERIFY2(passLevel > 0.9f, qPrintable(QStringLiteral("passante = %1").arg(passLevel)));
    const float rejectionDb = 20.0f * std::log10(passLevel / std::max(rejectLevel, 1e-9f));
    QVERIFY2(rejectionDb > 60.0f,
             qPrintable(QStringLiteral("reiezione dello specchio = %1 dB").arg(rejectionDb)));
}

void TestDspPrimitives::agcConvergesTowardsTargetLevel()
{
    constexpr double kRate = 48000.0;
    Agc agc;
    agc.configure(kRate);
    agc.setMode(AgcMode::Fast);
    agc.setThresholdDb(-120.0);

    // Segnale debole costante: l'AGC deve portarlo a livello utile.
    std::vector<float> audio(static_cast<std::size_t>(kRate)); // 1 s
    for (std::size_t i = 0; i < audio.size(); ++i)
        audio[i] = 0.01f * std::sin(kTwoPi * 700.0 * static_cast<double>(i) / kRate);

    agc.process(audio.data(), audio.size());

    double sum = 0.0;
    const std::size_t tail = audio.size() / 4;
    for (std::size_t i = audio.size() - tail; i < audio.size(); ++i)
        sum += static_cast<double>(audio[i]) * audio[i];
    const double outputRms = std::sqrt(sum / tail);

    QVERIFY2(outputRms > 0.15 && outputRms < 0.45,
             qPrintable(QStringLiteral("RMS in uscita = %1").arg(outputRms)));
}

void TestDspPrimitives::agcThresholdStopsNoiseAmplification()
{
    constexpr double kRate = 48000.0;
    std::vector<float> quiet(static_cast<std::size_t>(kRate), 0.0f);
    for (std::size_t i = 0; i < quiet.size(); ++i)
        quiet[i] = 1e-5f * std::sin(kTwoPi * 300.0 * static_cast<double>(i) / kRate);

    // Con soglia alta il guadagno resta limitato: è il comportamento AGC-T,
    // ciò che permette di "abbassare" il rumore di banda senza toccare il volume.
    Agc gated;
    gated.configure(kRate);
    gated.setMode(AgcMode::Medium);
    gated.setThresholdDb(-40.0);
    std::vector<float> gatedAudio = quiet;
    gated.process(gatedAudio.data(), gatedAudio.size());

    Agc open;
    open.configure(kRate);
    open.setMode(AgcMode::Medium);
    open.setThresholdDb(-130.0);
    std::vector<float> openAudio = quiet;
    open.process(openAudio.data(), openAudio.size());

    QVERIFY2(gated.gainDb() < open.gainDb() - 20.0f,
             qPrintable(QStringLiteral("gated=%1 dB, open=%2 dB")
                            .arg(gated.gainDb())
                            .arg(open.gainDb())));
}

void TestDspPrimitives::agcAcceptsCustomTiming()
{
    Agc agc;
    agc.configure(48000.0);
    agc.setMode(AgcMode::Medium);
    agc.setAttackMs(35.0);
    agc.setDecayMs(875.0);
    QCOMPARE(agc.attackMs(), 35.0);
    QCOMPARE(agc.decayMs(), 875.0);

    agc.setDecayMs(0.0);
    QCOMPARE(agc.decayMs(), 0.0);
}

QTEST_APPLESS_MAIN(TestDspPrimitives)

#include "tst_dsp_primitives.moc"
