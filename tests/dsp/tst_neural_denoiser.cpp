// SPDX-License-Identifier: GPL-3.0-or-later
// Lo stadio neurale (DSDR-SPEC-003 §8): che ci sia, che tolga rumore, che non
// mangi la voce, e che dichiari il vero sulla propria latenza.
//
// Il modello è di terze parti e non lo mettiamo alla prova noi: quello che si
// verifica qui è il *nostro* involucro — scala dei campioni, blocchi, coda,
// ritardo — perché è lì che si sbaglia. Un denoiser alimentato con la scala
// sbagliata non fallisce: lavora, e sembra soltanto inerte.

#include "dsp/NeuralDenoiser.h"
#include "dsp/DspTypes.h"

#include <QTest>

#include <cmath>
#include <random>
#include <vector>

using namespace dsdr;
using namespace dsdr::dsp;

namespace {

constexpr double kRate = 48000.0;

std::vector<float> noise(std::size_t n, float sigma, unsigned seed)
{
    std::vector<float> out(n);
    std::mt19937 gen(seed);
    std::normal_distribution<float> dist(0.0f, sigma);
    for (auto &s : out)
        s = dist(gen);
    return out;
}

/// Sillabe con formante che scivola: il tipo di segnale su cui la rete è
/// stata addestrata, e l'unico su cui ha senso misurarla.
std::vector<float> speechLike(std::size_t n, float amplitude)
{
    std::vector<float> out(n, 0.0f);
    double phase = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / kRate;
        const double cycle = std::fmod(t, 0.5);
        const double gate = cycle < 0.3 ? 0.5 - 0.5 * std::cos(kTwoPi * cycle / 0.3) : 0.0;
        if (gate <= 0.0) {
            phase = 0.0;
            continue;
        }
        const double formant = 220.0 + 180.0 * (cycle / 0.3);
        phase += kTwoPi * formant / kRate;
        double sample = 0.0;
        for (int h = 1; h <= 5; ++h)
            sample += std::sin(phase * h) / h;
        out[i] = amplitude * static_cast<float>(gate * sample);
    }
    return out;
}

} // namespace

class TestNeuralDenoiser : public QObject
{
    Q_OBJECT

private slots:
    void availabilityMatchesTheBuild();
    void refusesRatesItWasNotTrainedFor();
    void quietsTheHissBetweenSyllables();
    void keepsTheVoice();
    void reportsItsOwnLatency();
};

void TestNeuralDenoiser::availabilityMatchesTheBuild()
{
    NeuralDenoiser nr;
#ifdef DSDR_HAVE_RNNOISE
    QVERIFY2(NeuralDenoiser::isAvailable(),
             "compilato con RNNoise ma lo stadio si dichiara assente");
    QVERIFY(nr.configure(kRate));
    QVERIFY(nr.isConfigured());
#else
    QVERIFY2(!NeuralDenoiser::isAvailable(),
             "senza RNNoise lo stadio non può dichiararsi disponibile");
    QVERIFY(!nr.configure(kRate));
    QSKIP("build senza motore neurale");
#endif
}

void TestNeuralDenoiser::refusesRatesItWasNotTrainedFor()
{
    if (!NeuralDenoiser::isAvailable())
        QSKIP("build senza motore neurale");

    // La rete è addestrata a 48 kHz e non si adatta: a un altro ritmo
    // sentirebbe voci più acute di quelle che conosce. Meglio rifiutare che
    // lavorare su un mondo che non è il suo.
    NeuralDenoiser nr;
    QVERIFY(!nr.configure(24000.0));
    QVERIFY(!nr.configure(96000.0));
    QVERIFY(nr.configure(48000.0));
}

void TestNeuralDenoiser::quietsTheHissBetweenSyllables()
{
    if (!NeuralDenoiser::isAvailable())
        QSKIP("build senza motore neurale");

    constexpr std::size_t kFrames = 192000;    // quattro secondi
    const auto clean = speechLike(kFrames, 0.25f);
    auto audio = clean;
    const auto hiss = noise(kFrames, 0.08f, 5);
    for (std::size_t i = 0; i < kFrames; ++i)
        audio[i] += hiss[i];

    NeuralDenoiser nr;
    QVERIFY(nr.configure(kRate));
    nr.process(audio.data(), audio.size());

    // Si misura nelle pause, dove il pulito è silenzio: lì tutto ciò che
    // resta è rumore, e il confronto non è ambiguo.
    double before = 0.0;
    double after = 0.0;
    std::size_t counted = 0;
    const auto delay = static_cast<std::size_t>(nr.latencySamples());
    for (std::size_t i = kFrames / 2; i + delay < kFrames; ++i) {
        if (std::abs(clean[i]) > 0.01f)
            continue;
        before += static_cast<double>(hiss[i]) * hiss[i];
        const double out = audio[i + delay];
        after += out * out;
        ++counted;
    }
    QVERIFY2(counted > 1000, "poche pause da misurare: banco mal costruito");

    const double reduction = 10.0 * std::log10(std::max(after, 1e-12)
                                               / std::max(before, 1e-12));
    QVERIFY2(reduction < -6.0,
             qPrintable(QStringLiteral("il fruscio nelle pause è sceso di soli %1 dB")
                            .arg(-reduction)));
}

void TestNeuralDenoiser::keepsTheVoice()
{
    if (!NeuralDenoiser::isAvailable())
        QSKIP("build senza motore neurale");

    constexpr std::size_t kFrames = 192000;
    const auto clean = speechLike(kFrames, 0.25f);
    auto audio = clean;
    const auto hiss = noise(kFrames, 0.08f, 9);
    for (std::size_t i = 0; i < kFrames; ++i)
        audio[i] += hiss[i];

    NeuralDenoiser nr;
    QVERIFY(nr.configure(kRate));
    nr.process(audio.data(), audio.size());

    // Dentro le sillabe l'energia deve restare: uno stadio che azzera tutto
    // vincerebbe qualunque misura di rumore e non servirebbe a nulla.
    double voice = 0.0;
    double output = 0.0;
    const auto delay = static_cast<std::size_t>(nr.latencySamples());
    for (std::size_t i = kFrames / 2; i + delay < kFrames; ++i) {
        if (std::abs(clean[i]) < 0.05f)
            continue;
        voice += static_cast<double>(clean[i]) * clean[i];
        const double out = audio[i + delay];
        output += out * out;
    }

    QVERIFY2(output > 0.2 * voice,
             qPrintable(QStringLiteral("voce quasi cancellata: energia %1 contro %2")
                            .arg(std::sqrt(output)).arg(std::sqrt(voice))));
}

void TestNeuralDenoiser::reportsItsOwnLatency()
{
    if (!NeuralDenoiser::isAvailable())
        QSKIP("build senza motore neurale");

    // Il ritardo dichiarato dev'essere quello vero: ci si allineano altri
    // flussi, e uno scarto di un blocco basta a far perdere il passo.
    constexpr std::size_t kFrames = 48000;
    const auto input = noise(kFrames, 0.2f, 13);
    auto audio = input;

    NeuralDenoiser nr;
    QVERIFY(nr.configure(kRate));
    nr.process(audio.data(), audio.size());

    int bestDelay = -1;
    double bestScore = 0.0;
    for (int delay = 0; delay <= 2048; delay += 1) {
        double cross = 0.0;
        for (std::size_t i = 24000; i + static_cast<std::size_t>(delay) < kFrames; ++i)
            cross += static_cast<double>(input[i]) * audio[i + static_cast<std::size_t>(delay)];
        if (std::abs(cross) > bestScore) {
            bestScore = std::abs(cross);
            bestDelay = delay;
        }
    }

    // La correlazione su rumore e il riduttore floating point possono
    // selezionare il campione adiacente su architetture diverse. Il contratto
    // di latenza resta al campione dichiarato; una tolleranza di un solo
    // campione evita un falso negativo senza mascherare un ritardo reale.
    QVERIFY2(std::abs(bestDelay - nr.latencySamples()) <= 1,
             qPrintable(QStringLiteral("ritardo misurato %1, dichiarato %2")
                            .arg(bestDelay).arg(nr.latencySamples())));
}

QTEST_APPLESS_MAIN(TestNeuralDenoiser)

#include "tst_neural_denoiser.moc"
