// SPDX-License-Identifier: GPL-3.0-or-later
// Banco di misura della riduzione di rumore spettrale (DSDR-SPEC-003 §6).
//
// Il criterio della specifica è un numero: guadagno di SNR segmentale ≥ 8 dB
// su un segnale a SNR 5 dB, senza artefatti musicali oltre soglia. Qui si
// misura quello, e prima ancora la proprietà su cui tutto poggia — che la
// ricostruzione sia trasparente quando il filtro non tocca niente.

#include "dsp/SpectralDenoiser.h"
#include "dsp/DspTypes.h"

#include <QTest>

#include <cmath>
#include <numeric>
#include <random>
#include <vector>

using namespace dsdr;
using namespace dsdr::dsp;

namespace {

constexpr double kRate = 48000.0;

std::vector<float> tone(double hz, std::size_t n, float amplitude)
{
    std::vector<float> out(n);
    for (std::size_t i = 0; i < n; ++i)
        out[i] = amplitude * static_cast<float>(std::sin(kTwoPi * hz * static_cast<double>(i) / kRate));
    return out;
}

/// Qualcosa che somiglia al parlato: sillabe che vanno e vengono, con la
/// formante che si sposta.
///
/// Il segnale di prova non è un dettaglio. Un NR spettrale stima il fondo per
/// minima statistica, quindi tratta come rumore **tutto ciò che è fermo**: con
/// un tono puro, o con armoniche fisse, si misurerebbe soltanto la propria
/// incapacità di distinguerle dal fruscio — che non è un difetto ma la
/// definizione stessa dello stadio. Serve un segnale che si muova, come la
/// voce, con pause in cui il filtro possa imparare il fondo.
std::vector<float> speechLike(std::size_t n, float amplitude)
{
    std::vector<float> out(n, 0.0f);
    double phase = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / kRate;

        // Sillabe da 300 ms separate da 200 ms di silenzio.
        const double cycle = std::fmod(t, 0.5);
        const double gate = cycle < 0.3
            ? 0.5 - 0.5 * std::cos(kTwoPi * cycle / 0.3)   // attacco e coda dolci
            : 0.0;
        if (gate <= 0.0) {
            phase = 0.0;
            continue;
        }

        // La formante scivola dentro la sillaba: è ciò che rende il parlato
        // non stazionario, e quindi distinguibile dal fondo.
        const double formant = 260.0 + 140.0 * (cycle / 0.3);
        phase += kTwoPi * formant / kRate;

        double sample = 0.0;
        for (int h = 1; h <= 4; ++h)
            sample += std::sin(phase * h) / h;
        out[i] = amplitude * static_cast<float>(gate * sample);
    }
    return out;
}

std::vector<float> noise(std::size_t n, float sigma, unsigned seed)
{
    std::vector<float> out(n);
    std::mt19937 gen(seed);
    std::normal_distribution<float> dist(0.0f, sigma);
    for (auto &s : out)
        s = dist(gen);
    return out;
}

double rms(const std::vector<float> &v, std::size_t from = 0)
{
    double sum = 0.0;
    for (std::size_t i = from; i < v.size(); ++i)
        sum += static_cast<double>(v[i]) * v[i];
    return std::sqrt(sum / static_cast<double>(v.size() - from));
}

double toDb(double ratio)
{
    return 20.0 * std::log10(std::max(ratio, 1e-12));
}

} // namespace

class TestSpectralDenoiser : public QObject
{
    Q_OBJECT

private slots:
    void reconstructsTransparentlyAtStrengthZero();
    void raisesSignalOverNoise();
    void keepsTheSignalItself();
    void steadyTonesAreTreatedAsNoise();
    void residualNoiseStaysSmooth();
    void unconfiguredDoesNothing();
};

void TestSpectralDenoiser::reconstructsTransparentlyAtStrengthZero()
{
    // La proprietà su cui poggia tutto il resto: analisi e sintesi con radice
    // di Hann e mezza finestra di salto devono ricostruire il segnale
    // *identico* quando il guadagno vale uno. Se qui c'è un errore, ogni
    // misura successiva sta misurando il difetto della ricostruzione.
    // Rumore e non un tono: con un segnale periodico il ritardo che minimizza
    // l'errore è ambiguo a meno di un periodo, e si finirebbe a misurare un
    // alias invece della latenza vera.
    constexpr std::size_t kFrames = 24000;
    const auto input = noise(kFrames, 0.3f, 5);
    auto audio = input;

    SpectralDenoiser nr;
    QVERIFY(nr.configure(kRate, 512));
    nr.setStrength(0.0);
    nr.process(audio.data(), audio.size());

    // Il ritardo si misura invece di darlo per scontato: è una proprietà del
    // filtro, e un test che lo assume sbagliato misura il proprio errore di
    // allineamento invece della trasparenza della ricostruzione.
    int bestDelay = 0;
    double bestError = 1e30;
    double reference = 0.0;
    for (std::size_t i = 4096; i + 2048 < kFrames; ++i)
        reference += static_cast<double>(input[i]) * input[i];

    for (int delay = 0; delay <= 1024; ++delay) {
        double error = 0.0;
        for (std::size_t i = 4096; i + 2048 < kFrames; ++i) {
            const double diff = audio[i + static_cast<std::size_t>(delay)] - input[i];
            error += diff * diff;
        }
        if (error < bestError) {
            bestError = error;
            bestDelay = delay;
        }
    }

    const double errorDb = toDb(std::sqrt(bestError / std::max(reference, 1e-12)));
    QVERIFY2(errorDb < -40.0,
             qPrintable(QStringLiteral("ricostruzione non trasparente: errore %1 dB "
                                       "al ritardo migliore di %2 campioni")
                            .arg(errorDb).arg(bestDelay)));

    // E il ritardo dichiarato deve essere quello vero: ci si costruiscono
    // sopra gli allineamenti di chi userà questo stadio.
    QCOMPARE(bestDelay, nr.latencySamples());
}

void TestSpectralDenoiser::raisesSignalOverNoise()
{
    // Il criterio della specifica: SNR di partenza 5 dB, guadagno ≥ 8 dB.
    constexpr std::size_t kFrames = 96000;      // due secondi
    const auto clean = speechLike(kFrames, 0.3f);
    const auto hiss = noise(kFrames, 0.0f, 1);  // segnaposto, sostituito sotto

    // Rumore calibrato per un SNR di 5 dB sul segnale pulito.
    const double cleanRms = rms(clean);
    const double wantedNoiseRms = cleanRms / std::pow(10.0, 5.0 / 20.0);
    auto noisy = clean;
    const auto hissScaled = noise(kFrames, static_cast<float>(wantedNoiseRms), 7);
    for (std::size_t i = 0; i < kFrames; ++i)
        noisy[i] += hissScaled[i];

    const double snrBefore = toDb(cleanRms / rms(hissScaled));
    QVERIFY2(std::abs(snrBefore - 5.0) < 1.0,
             qPrintable(QStringLiteral("banco mal calibrato: SNR %1 dB").arg(snrBefore)));

    auto processed = noisy;
    SpectralDenoiser nr;
    QVERIFY(nr.configure(kRate, 512));
    nr.setStrength(7.0);
    nr.process(processed.data(), processed.size());

    // Il residuo si misura là dove il filtro ha già imparato il fondo.
    const std::size_t settle = kFrames / 2;
    std::vector<float> residual(processed.begin() + static_cast<std::ptrdiff_t>(settle),
                                processed.end());
    std::vector<float> reference(clean.begin() + static_cast<std::ptrdiff_t>(settle),
                                 clean.end());

    // Rapporto fra ciò che resta del segnale e ciò che resta del rumore: si
    // stima confrontando l'uscita con il pulito allineato.
    double signalPower = 0.0;
    double errorPower = 0.0;
    const int delay = nr.latencySamples();
    for (std::size_t i = 0; i + static_cast<std::size_t>(delay) < residual.size(); ++i) {
        const double s = reference[i];
        const double d = residual[i + static_cast<std::size_t>(delay)] - s;
        signalPower += s * s;
        errorPower += d * d;
    }

    const double snrAfter = toDb(std::sqrt(signalPower / std::max(errorPower, 1e-12)));
    const double gain = snrAfter - snrBefore;

    QVERIFY2(gain > 8.0,
             qPrintable(QStringLiteral("guadagno di soli %1 dB (da %2 a %3)")
                            .arg(gain).arg(snrBefore).arg(snrAfter)));
}

void TestSpectralDenoiser::keepsTheSignalItself()
{
    // Si può vincere qualunque misura di rumore azzerando tutto: quello che
    // conta è che la voce esca ancora, anche col comando al massimo.
    constexpr std::size_t kFrames = 192000;    // quattro secondi, otto sillabe
    const auto clean = speechLike(kFrames, 0.3f);
    auto audio = clean;
    const auto hiss = noise(kFrames, 0.05f, 3);
    for (std::size_t i = 0; i < kFrames; ++i)
        audio[i] += hiss[i];

    SpectralDenoiser nr;
    QVERIFY(nr.configure(kRate, 512));
    nr.setStrength(10.0);              // il massimo: il caso peggiore
    nr.process(audio.data(), audio.size());

    // Si confronta l'energia dentro le sillabe, non su tutto: fuori il filtro
    // *deve* togliere, ed è quello che gli si chiede.
    double before = 0.0;
    double after = 0.0;
    const int delay = nr.latencySamples();
    for (std::size_t i = kFrames / 2; i + static_cast<std::size_t>(delay) < kFrames; ++i) {
        if (std::abs(clean[i]) < 0.05f)
            continue;
        before += static_cast<double>(clean[i]) * clean[i];
        const double out = audio[i + static_cast<std::size_t>(delay)];
        after += out * out;
    }

    QVERIFY2(after > 0.25 * before,
             qPrintable(QStringLiteral("voce attenuata: energia da %1 a %2")
                            .arg(std::sqrt(before)).arg(std::sqrt(after))));
}

void TestSpectralDenoiser::steadyTonesAreTreatedAsNoise()
{
    // Il limite dello stadio, messo per iscritto perché non venga scambiato
    // per un difetto: ciò che sta fermo *è* fondo, per come il fondo viene
    // stimato. Una portante o una nota CW continua vengono attenuate come il
    // fruscio — ed è il motivo per cui in CW questo stadio si tiene basso e si
    // lavora di filtro stretto e APF, non di riduzione spettrale.
    constexpr std::size_t kFrames = 192000;
    auto audio = tone(800.0, kFrames, 0.3f);

    SpectralDenoiser nr;
    QVERIFY(nr.configure(kRate, 512));
    nr.setStrength(8.0);
    nr.process(audio.data(), audio.size());

    const double residual = rms(audio, kFrames * 3 / 4);
    QVERIFY2(residual < 0.15,
             qPrintable(QStringLiteral("il tono fermo è sopravvissuto (%1): "
                                       "la stima del fondo non lo sta vedendo")
                            .arg(residual)));
}

void TestSpectralDenoiser::residualNoiseStaysSmooth()
{
    // Gli artefatti musicali sono bin isolati che sopravvivono e vanno e
    // vengono: il residuo «brilla». Si misurano come varianza dell'energia da
    // un blocco all'altro — un fondo liscio ha varianza bassa, i campanellini
    // la fanno esplodere.
    constexpr std::size_t kFrames = 96000;
    auto audio = noise(kFrames, 0.1f, 11);

    SpectralDenoiser nr;
    QVERIFY(nr.configure(kRate, 512));
    nr.setStrength(8.0);
    nr.process(audio.data(), audio.size());

    // Energia per blocchi da 512 nella seconda metà, dove il fondo è appreso.
    std::vector<double> energies;
    for (std::size_t i = kFrames / 2; i + 512 < kFrames; i += 512) {
        double e = 0.0;
        for (std::size_t k = 0; k < 512; ++k)
            e += static_cast<double>(audio[i + k]) * audio[i + k];
        energies.push_back(std::sqrt(e / 512.0));
    }
    QVERIFY(energies.size() > 10);

    const double mean = std::accumulate(energies.begin(), energies.end(), 0.0)
                      / static_cast<double>(energies.size());
    double variance = 0.0;
    for (double e : energies)
        variance += (e - mean) * (e - mean);
    variance /= static_cast<double>(energies.size());

    // Coefficiente di variazione: quanto il residuo «respira» rispetto al suo
    // livello medio. Sopra l'unità non è più un fondo, sono campanellini.
    const double coefficient = std::sqrt(variance) / std::max(mean, 1e-9);
    QVERIFY2(coefficient < 1.0,
             qPrintable(QStringLiteral("residuo instabile, artefatti musicali: %1")
                            .arg(coefficient)));
}

void TestSpectralDenoiser::unconfiguredDoesNothing()
{
    SpectralDenoiser nr;
    QVERIFY(!nr.isConfigured());

    auto audio = tone(1000.0, 512, 0.5f);
    const auto original = audio;
    nr.process(audio.data(), audio.size());
    QCOMPARE(audio, original);

    // E una configurazione impossibile viene rifiutata invece di produrre una
    // catena mezza costruita.
    QVERIFY(!nr.configure(48000.0, 100));   // non potenza di due
    QVERIFY(!nr.configure(0.0, 512));
}

QTEST_APPLESS_MAIN(TestSpectralDenoiser)

#include "tst_spectral_denoiser.moc"
