// SPDX-License-Identifier: GPL-3.0-or-later
// I tre filtri che un ricevitore HF deve avere: blanker, riduzione di rumore,
// notch. Si misurano, non si ascoltano: ogni prova confronta un rapporto in dB
// prima e dopo, perché «sembra più pulito» non è un criterio.

#include "dsp/LmsFilter.h"
#include "dsp/NoiseBlanker.h"
#include "dsp/NotchFilter.h"

#include <QTest>

#include <cmath>
#include <random>
#include <vector>

using namespace dsdr;
using namespace dsdr::dsp;

namespace {

constexpr double kAudioRate = 48000.0;

std::vector<float> tone(double frequencyHz, double rate, std::size_t n, float amplitude)
{
    std::vector<float> out(n);
    for (std::size_t i = 0; i < n; ++i) {
        out[i] = amplitude
               * static_cast<float>(std::sin(kTwoPi * frequencyHz
                                             * static_cast<double>(i) / rate));
    }
    return out;
}

void addNoise(std::vector<float> &signal, float sigma, unsigned seed)
{
    std::mt19937 gen(seed);
    std::normal_distribution<float> dist(0.0f, sigma);
    for (auto &s : signal)
        s += dist(gen);
}

/// Ampiezza della componente a `frequencyHz`, per correlazione: misura solo la
/// riga che interessa, senza farsi influenzare da tutto il resto.
double amplitudeAt(const std::vector<float> &signal, double frequencyHz, double rate,
                   std::size_t from = 0)
{
    double re = 0.0;
    double im = 0.0;
    const std::size_t n = signal.size() - from;
    for (std::size_t i = 0; i < n; ++i) {
        const double phase = kTwoPi * frequencyHz * static_cast<double>(i + from) / rate;
        re += signal[from + i] * std::cos(phase);
        im += signal[from + i] * std::sin(phase);
    }
    return 2.0 * std::sqrt(re * re + im * im) / static_cast<double>(n);
}

double rms(const std::vector<float> &signal, std::size_t from = 0)
{
    double sum = 0.0;
    for (std::size_t i = from; i < signal.size(); ++i)
        sum += static_cast<double>(signal[i]) * signal[i];
    return std::sqrt(sum / static_cast<double>(signal.size() - from));
}

double toDb(double ratio)
{
    return 20.0 * std::log10(std::max(ratio, 1e-12));
}

} // namespace

class TestNoiseFilters : public QObject
{
    Q_OBJECT

private slots:
    void blankerRemovesImpulsesAndKeepsTheSignal();
    void blankerLeavesACleanBandAlone();
    void blankerDoesNotFireOnAStrongSteadySignal();
    void blankerStitchesInsteadOfPunchingHoles();
    void blankerRetreatsFromEventsTooLongToBeImpulses();
    void noiseReductionRaisesTheSignalOverTheNoise();
    void autoNotchRemovesAHeterodyne();
    void autoNotchKeepsWhatIsNotALine();
    void manualNotchCutsTheLineAndSparesTheRest();
    void unconfiguredFiltersDoNothing();
};

void TestNoiseFilters::blankerRemovesImpulsesAndKeepsTheSignal()
{
    constexpr std::size_t kFrames = 48000;
    constexpr double kIqRate = 48000.0;

    // Portante debole più scariche rade e violente: la situazione in cui il
    // blanker deve guadagnarsi lo spazio che occupa.
    std::vector<Complex> iq(kFrames);
    for (std::size_t i = 0; i < kFrames; ++i) {
        const double phase = kTwoPi * 1000.0 * static_cast<double>(i) / kIqRate;
        iq[i] = Complex(0.05f * static_cast<float>(std::cos(phase)),
                        0.05f * static_cast<float>(std::sin(phase)));
    }
    for (std::size_t i = 500; i < kFrames; i += 1000)
        iq[i] += Complex(3.0f, -3.0f);

    NoiseBlanker blanker;
    blanker.configure(kIqRate);
    blanker.setThreshold(4.0);

    const std::size_t suppressed = blanker.process(iq.data(), kFrames);
    QVERIFY2(suppressed > 0, "nessun impulso riconosciuto");

    // Deve togliere le scariche senza svuotare la banda: qualche campione per
    // impulso, non un campione su dieci.
    QVERIFY2(blanker.lastSuppressedRatio() < 0.05,
             qPrintable(QStringLiteral("soppressione troppo estesa: %1")
                            .arg(blanker.lastSuppressedRatio())));

    // Nessun residuo di ampiezza enorme: è ciò che faceva chiudere l'AGC.
    float peak = 0.0f;
    for (std::size_t i = 0; i < kFrames; ++i)
        peak = std::max(peak, std::sqrt(magnitudeSquared(iq[i])));
    QVERIFY2(peak < 0.2f,
             qPrintable(QStringLiteral("picco residuo troppo alto: %1").arg(peak)));

    // E la portante deve essere ancora lì: un blanker che pulisce togliendo
    // anche il segnale ha risolto il problema sbagliato.
    double power = 0.0;
    for (std::size_t i = 0; i < kFrames; ++i)
        power += magnitudeSquared(iq[i]);
    power /= static_cast<double>(kFrames);
    QVERIFY2(std::sqrt(power) > 0.04,
             qPrintable(QStringLiteral("segnale perso insieme al disturbo: %1")
                            .arg(std::sqrt(power))));
}

void TestNoiseFilters::blankerLeavesACleanBandAlone()
{
    constexpr std::size_t kFrames = 24000;
    std::vector<Complex> iq(kFrames);
    std::mt19937 gen(7);
    std::normal_distribution<float> dist(0.0f, 0.1f);
    for (auto &sample : iq)
        sample = Complex(dist(gen), dist(gen));

    const std::vector<Complex> before = iq;

    NoiseBlanker blanker;
    blanker.configure(48000.0);
    blanker.setThreshold(4.0);
    blanker.process(iq.data(), kFrames);

    // Su rumore gaussiano senza impulsi la soglia non deve scattare quasi mai:
    // un blanker che lavora sempre è un distorsore.
    QVERIFY2(blanker.lastSuppressedRatio() < 0.005,
             qPrintable(QStringLiteral("scatti su banda pulita: %1")
                            .arg(blanker.lastSuppressedRatio())));
    Q_UNUSED(before)
}

void TestNoiseFilters::blankerDoesNotFireOnAStrongSteadySignal()
{
    // Una stazione locale forte non è un impulso: la media insegue il livello
    // e il blanker deve tacere. Se scattasse, l'unico modo di ascoltare i
    // segnali forti sarebbe spegnerlo.
    constexpr std::size_t kFrames = 48000;
    std::vector<Complex> iq(kFrames);
    for (std::size_t i = 0; i < kFrames; ++i) {
        const double phase = kTwoPi * 800.0 * static_cast<double>(i) / 48000.0;
        iq[i] = Complex(0.9f * static_cast<float>(std::cos(phase)),
                        0.9f * static_cast<float>(std::sin(phase)));
    }

    NoiseBlanker blanker;
    blanker.configure(48000.0);
    blanker.setThreshold(4.0);
    blanker.process(iq.data(), kFrames);

    QVERIFY2(blanker.lastSuppressedRatio() < 0.02,
             qPrintable(QStringLiteral("scatti su segnale forte e stabile: %1")
                            .arg(blanker.lastSuppressedRatio())));
}

void TestNoiseFilters::blankerStitchesInsteadOfPunchingHoles()
{
    // SPEC-003 §4.1: si interpola, non si azzera. Uno zero è un gradino, e un
    // gradino è a banda larga quanto l'impulso che si voleva togliere: il
    // «clic» resterebbe, solo con un timbro diverso.
    //
    // La prova è geometrica: su una portante pulita con un solo impulso, dopo
    // il blanker il salto massimo fra campioni consecutivi non deve superare
    // quello che la portante ha di suo.
    constexpr std::size_t kFrames = 4096;
    constexpr double kIqRate = 48000.0;

    std::vector<Complex> iq(kFrames);
    for (std::size_t i = 0; i < kFrames; ++i) {
        const double phase = kTwoPi * 500.0 * static_cast<double>(i) / kIqRate;
        iq[i] = Complex(0.3f * static_cast<float>(std::cos(phase)),
                        0.3f * static_cast<float>(std::sin(phase)));
    }

    double naturalStep = 0.0;
    for (std::size_t i = 1; i < kFrames; ++i)
        naturalStep = std::max(naturalStep,
                               static_cast<double>(std::sqrt(magnitudeSquared(iq[i] - iq[i - 1]))));

    iq[2000] += Complex(5.0f, 5.0f);

    NoiseBlanker blanker;
    blanker.configure(kIqRate);
    blanker.setThreshold(4.0);
    QVERIFY(blanker.process(iq.data(), kFrames) > 0);

    double worstStep = 0.0;
    for (std::size_t i = 1; i < kFrames; ++i)
        worstStep = std::max(worstStep,
                             static_cast<double>(std::sqrt(magnitudeSquared(iq[i] - iq[i - 1]))));

    QVERIFY2(worstStep < naturalStep * 3.0,
             qPrintable(QStringLiteral("gradino lasciato dal blanker: %1 contro %2 naturale")
                            .arg(worstStep).arg(naturalStep)));

    // E il buco non deve essere un silenzio: dove c'era l'impulso ora deve
    // esserci il segnale, ricucito.
    const float amplitude = std::sqrt(magnitudeSquared(iq[2000]));
    QVERIFY2(amplitude > 0.15f,
             qPrintable(QStringLiteral("al posto dell'impulso è rimasto un vuoto: %1")
                            .arg(amplitude)));
}

void TestNoiseFilters::blankerRetreatsFromEventsTooLongToBeImpulses()
{
    // SPEC-003 §4.1: oltre 500 µs non è un impulso. Una stazione locale che
    // apre non deve essere cancellata dal blanker — è il difetto storico dei
    // NB aggressivi, e il motivo per cui in tanti li tengono spenti.
    constexpr std::size_t kFrames = 48000;
    constexpr double kIqRate = 48000.0;    // 500 µs = 24 campioni

    std::vector<Complex> iq(kFrames);
    for (std::size_t i = 0; i < kFrames; ++i) {
        const double phase = kTwoPi * 700.0 * static_cast<double>(i) / kIqRate;
        iq[i] = Complex(0.05f * static_cast<float>(std::cos(phase)),
                        0.05f * static_cast<float>(std::sin(phase)));
    }

    // Una raffica lunga dieci millisecondi: troppo per essere una scarica.
    for (std::size_t i = 20000; i < 20480; ++i)
        iq[i] *= 40.0f;

    NoiseBlanker blanker;
    blanker.configure(kIqRate);
    blanker.setThreshold(4.0);
    blanker.process(iq.data(), kFrames);

    QVERIFY2(blanker.lastRefusedEvents() > 0,
             "il blanker non si è accorto che l'evento era troppo lungo");

    // Il segnale forte deve essere ancora lì, praticamente intatto.
    double power = 0.0;
    for (std::size_t i = 20100; i < 20400; ++i)
        power += magnitudeSquared(iq[i]);
    power /= 300.0;
    QVERIFY2(std::sqrt(power) > 1.0,
             qPrintable(QStringLiteral("segnale forte cancellato dal blanker: %1")
                            .arg(std::sqrt(power))));
}

void TestNoiseFilters::noiseReductionRaisesTheSignalOverTheNoise()
{
    constexpr std::size_t kFrames = 96000;    // due secondi
    auto audio = tone(1000.0, kAudioRate, kFrames, 0.2f);
    addNoise(audio, 0.2f, 42);

    const std::size_t settle = kFrames / 2;   // il filtro deve prima imparare
    const double beforeTone = amplitudeAt(audio, 1000.0, kAudioRate, settle);
    const double beforeAll = rms(audio, settle);
    const double beforeNoise = std::sqrt(std::max(1e-12,
        beforeAll * beforeAll - beforeTone * beforeTone / 2.0));

    LmsFilter nr;
    nr.configure(64, 16);
    nr.setRate(0.05f);
    nr.process(audio.data(), kFrames, LmsFilter::Output::Prediction);

    const double afterTone = amplitudeAt(audio, 1000.0, kAudioRate, settle);
    const double afterAll = rms(audio, settle);
    const double afterNoise = std::sqrt(std::max(1e-12,
        afterAll * afterAll - afterTone * afterTone / 2.0));

    const double gainDb = toDb(afterTone / afterNoise) - toDb(beforeTone / beforeNoise);
    QVERIFY2(gainDb > 6.0,
             qPrintable(QStringLiteral("la riduzione di rumore ha guadagnato solo %1 dB")
                            .arg(gainDb)));

    // E il segnale deve restare udibile: si può vincere qualunque confronto
    // azzerando tutto, ma allora non c'è più niente da ascoltare.
    QVERIFY2(afterTone > 0.5 * beforeTone,
             qPrintable(QStringLiteral("segnale attenuato da %1 a %2")
                            .arg(beforeTone).arg(afterTone)));
}

void TestNoiseFilters::autoNotchRemovesAHeterodyne()
{
    constexpr std::size_t kFrames = 96000;

    // Eterodina a 1500 Hz sopra un fondo di rumore: il caso del fischio che
    // arriva e non se ne va più.
    auto audio = tone(1500.0, kAudioRate, kFrames, 0.5f);
    addNoise(audio, 0.05f, 11);

    const std::size_t settle = kFrames / 2;
    const double before = amplitudeAt(audio, 1500.0, kAudioRate, settle);

    LmsFilter anf;
    anf.configure(64, 8);
    anf.setRate(0.05f);
    anf.process(audio.data(), kFrames, LmsFilter::Output::Error);

    const double after = amplitudeAt(audio, 1500.0, kAudioRate, settle);
    const double attenuationDb = toDb(before / std::max(after, 1e-9));

    QVERIFY2(attenuationDb > 15.0,
             qPrintable(QStringLiteral("eterodina attenuata di soli %1 dB")
                            .arg(attenuationDb)));
}

void TestNoiseFilters::autoNotchKeepsWhatIsNotALine()
{
    // Un notch automatico toglie le righe: *tutte*, anche quelle che si vuole
    // ascoltare — ed è il motivo per cui in CW lo si spegne, e per cui deve
    // esistere un interruttore invece di stare sempre acceso.
    //
    // Quello che non deve toccare è ciò che riga non è: il fondo, il parlato,
    // tutto ciò che occupa banda invece di stare su una frequenza sola. Se
    // attenuasse anche quello sarebbe un filtro passa-niente.
    constexpr std::size_t kFrames = 96000;

    std::vector<float> noise(kFrames, 0.0f);
    addNoise(noise, 0.1f, 23);

    std::vector<float> audio = noise;
    const auto whistle = tone(1500.0, kAudioRate, kFrames, 0.5f);
    for (std::size_t i = 0; i < kFrames; ++i)
        audio[i] += whistle[i];

    LmsFilter anf;
    anf.configure(64, 8);
    anf.setRate(0.05f);
    anf.process(audio.data(), kFrames, LmsFilter::Output::Error);

    const std::size_t settle = kFrames / 2;
    const double residual = rms(audio, settle);
    const double reference = rms(noise, settle);

    // Il fondo deve uscire com'è entrato, entro qualche dB: né cancellato né
    // amplificato dall'adattamento.
    const double deltaDb = toDb(residual / reference);
    QVERIFY2(std::abs(deltaDb) < 3.0,
             qPrintable(QStringLiteral("il fondo è stato alterato di %1 dB").arg(deltaDb)));
}

void TestNoiseFilters::manualNotchCutsTheLineAndSparesTheRest()
{
    constexpr std::size_t kFrames = 48000;

    // Due toni: quello da togliere a 1000 Hz e quello da tenere a 600 Hz, che
    // è la nota tipica di ascolto in CW.
    std::vector<float> audio(kFrames, 0.0f);
    const auto unwanted = tone(1000.0, kAudioRate, kFrames, 0.5f);
    const auto wanted = tone(600.0, kAudioRate, kFrames, 0.5f);
    for (std::size_t i = 0; i < kFrames; ++i)
        audio[i] = unwanted[i] + wanted[i];

    NotchFilter notch;
    notch.configure(kAudioRate);
    notch.setNotch(1000.0, 120.0);
    notch.process(audio.data(), kFrames);

    const std::size_t settle = 4800;   // si scarta il transitorio del filtro
    const double removed = amplitudeAt(audio, 1000.0, kAudioRate, settle);
    const double kept = amplitudeAt(audio, 600.0, kAudioRate, settle);

    QVERIFY2(toDb(0.5 / std::max(removed, 1e-9)) > 25.0,
             qPrintable(QStringLiteral("riga attenuata di soli %1 dB")
                            .arg(toDb(0.5 / std::max(removed, 1e-9)))));
    QVERIFY2(kept > 0.35,
             qPrintable(QStringLiteral("la nota da tenere è stata intaccata: %1")
                            .arg(kept)));
}

void TestNoiseFilters::unconfiguredFiltersDoNothing()
{
    // Un filtro mai configurato deve lasciar passare il segnale intatto, non
    // azzerarlo: è lo stato in cui si trova la catena fra una riconfigurazione
    // e l'altra.
    std::vector<float> audio = tone(1000.0, kAudioRate, 512, 0.5f);
    const std::vector<float> original = audio;

    LmsFilter lms;
    lms.process(audio.data(), audio.size(), LmsFilter::Output::Prediction);
    QCOMPARE(audio, original);

    NotchFilter notch;
    notch.process(audio.data(), audio.size());
    QCOMPARE(audio, original);

    std::vector<Complex> iq(16, Complex(1.0f, 1.0f));
    NoiseBlanker blanker;
    QCOMPARE(blanker.process(iq.data(), iq.size()), std::size_t(0));
    QCOMPARE(iq[0].real(), 1.0f);
}

QTEST_APPLESS_MAIN(TestNoiseFilters)

#include "tst_noise_filters.moc"
