// SPDX-License-Identifier: GPL-3.0-or-later
// La catena di trasmissione: microfono, manipolatore, modulatore, salita di
// frequenza.
//
// Quasi tutto qui si misura in decibel di reiezione: quanto resta della banda
// laterale che abbiamo detto di togliere, quanto resta dell'immagine che
// l'interpolazione crea. Sono i numeri per cui esiste una catena TX invece di
// una moltiplicazione — e sono anche i numeri che, se sbagliati, non si
// sentono nella propria radio ma solo in quella del vicino di canale.

#include "dsp/CwKeyer.h"
#include "dsp/FirDesign.h"
#include "dsp/FirInterpolator.h"
#include "dsp/InterpolatorChain.h"
#include "dsp/Modulator.h"
#include "dsp/SpeechProcessor.h"

#include <QTest>

#include <cmath>
#include <vector>

using namespace dsdr::dsp;

namespace {

constexpr double kAudioRate = 48000.0;

/// Ampiezza del segnale alla frequenza `hz`, misurata correlando con
/// l'esponenziale complesso corrispondente e finestrando con una Hann. La
/// finestra costa un po' di risoluzione ma abbatte le code della rettangolare,
/// che a −60 dB coprirebbero proprio ciò che vogliamo misurare.
double amplitudeAt(const std::vector<Complex> &signal, double hz, double sampleRate,
                   std::size_t skip = 0)
{
    if (signal.size() <= skip)
        return 0.0;
    const std::size_t n = signal.size() - skip;
    const double w = dsdr::dsp::kTwoPi * hz / sampleRate;

    double re = 0.0;
    double im = 0.0;
    double norm = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double window = 0.5 - 0.5 * std::cos(dsdr::dsp::kTwoPi * i / (n - 1));
        const double phase = -w * static_cast<double>(i);
        const Complex &z = signal[skip + i];
        re += window * (z.real() * std::cos(phase) - z.imag() * std::sin(phase));
        im += window * (z.real() * std::sin(phase) + z.imag() * std::cos(phase));
        norm += window;
    }
    return std::sqrt(re * re + im * im) / norm;
}

double toDb(double ratio)
{
    return 20.0 * std::log10(std::max(ratio, 1e-12));
}

std::vector<float> tone(double hz, double sampleRate, std::size_t frames,
                        float amplitude = 1.0f)
{
    std::vector<float> out(frames);
    for (std::size_t i = 0; i < frames; ++i) {
        out[i] = amplitude * static_cast<float>(
            std::sin(dsdr::dsp::kTwoPi * hz * static_cast<double>(i) / sampleRate));
    }
    return out;
}

} // namespace

class TestTxChain : public QObject
{
    Q_OBJECT

private slots:
    void interpolationMovesTheRateNotTheTone();
    void interpolationKillsTheImage();
    void interpolationKeepsTheLevel();
    void theSmallFactorGoesFirst();
    void upperSidebandKeepsOnlyTheUpperSide();
    void lowerSidebandKeepsOnlyTheLowerSide();
    void doubleSidebandKeepsBoth();
    void amCarriesACarrierAndDsbDoesNot();
    void fmKeepsTheEnvelopeAndTheDeviation();
    void cwPutsTheCarrierAtZero();
    void compressionRaisesTheAverageNotThePeak();
    void nothingEverLeavesAboveFullScale();
    void theRumbleDoesNotReachTheModulator();
    void theKeyEdgeTakesTheTimeItSaid();
    void theKeyerComesBackFromAnInterruptedEdge();
};

// ── Interpolazione ──────────────────────────────────────────────────────────

void TestTxChain::interpolationMovesTheRateNotTheTone()
{
    InterpolatorChain chain;
    QVERIFY(chain.configure(kAudioRate, 4, 4000.0));
    QCOMPARE(chain.outputRate(), kAudioRate * 4);

    const std::size_t frames = 8192;
    std::vector<Complex> in(frames);
    for (std::size_t i = 0; i < frames; ++i) {
        const double phase = dsdr::dsp::kTwoPi * 3000.0 * static_cast<double>(i) / kAudioRate;
        in[i] = Complex(static_cast<float>(std::cos(phase)),
                        static_cast<float>(std::sin(phase)));
    }

    std::vector<Complex> out(chain.maxOutput(frames));
    const std::size_t produced = chain.process(in.data(), frames, out.data());
    QCOMPARE(produced, frames * 4);

    // Il tono resta a 3 kHz: cambia la frequenza di campionamento, non il
    // segnale. Se il conto delle fasi fosse sbagliato lo troveremmo altrove.
    const double atTone = amplitudeAt(out, 3000.0, chain.outputRate(), 512);
    const double elsewhere = amplitudeAt(out, 9000.0, chain.outputRate(), 512);
    QVERIFY2(toDb(elsewhere / atTone) < -60.0,
             qPrintable(QStringLiteral("fuori posto a %1 dB").arg(toDb(elsewhere / atTone))));
}

void TestTxChain::interpolationKillsTheImage()
{
    InterpolatorChain chain;
    QVERIFY(chain.configure(kAudioRate, 4, 4000.0));

    const std::size_t frames = 8192;
    std::vector<Complex> in(frames);
    for (std::size_t i = 0; i < frames; ++i) {
        const double phase = dsdr::dsp::kTwoPi * 3000.0 * static_cast<double>(i) / kAudioRate;
        in[i] = Complex(static_cast<float>(std::cos(phase)),
                        static_cast<float>(std::sin(phase)));
    }

    std::vector<Complex> out(chain.maxOutput(frames));
    chain.process(in.data(), frames, out.data());

    const double atTone = amplitudeAt(out, 3000.0, chain.outputRate(), 512);

    // Senza filtro l'interpolazione replicherebbe il segnale attorno a ogni
    // multiplo della frequenza d'ingresso: 48000+3000, 96000−3000, e così via.
    // È esattamente ciò che esce dall'antenna se il filtro non c'è.
    for (double image : {51000.0, 45000.0, 99000.0, 93000.0}) {
        const double level = toDb(amplitudeAt(out, image, chain.outputRate(), 512) / atTone);
        QVERIFY2(level < -60.0,
                 qPrintable(QStringLiteral("immagine a %1 Hz solo %2 dB sotto")
                                .arg(image).arg(level)));
    }
}

void TestTxChain::interpolationKeepsTheLevel()
{
    FirInterpolator stage;
    // Passa-basso a 4 kHz progettato a 192 kHz: è il caso d'uso reale.
    stage.configure(designLowpass(4000.0, 192000.0, 129, 8.0), 4);

    const std::size_t frames = 4096;
    std::vector<Complex> in(frames);
    for (std::size_t i = 0; i < frames; ++i) {
        const double phase = dsdr::dsp::kTwoPi * 1000.0 * static_cast<double>(i) / kAudioRate;
        in[i] = Complex(static_cast<float>(std::cos(phase)),
                        static_cast<float>(std::sin(phase)));
    }

    std::vector<Complex> out(stage.maxOutput(frames));
    QCOMPARE(stage.process(in.data(), frames, out.data()), frames * 4);

    // Gli zeri inseriti dividono l'energia per L; il guadagno L la rimette a
    // posto. Senza, una trasmissione a 768 kS/s uscirebbe 24 dB sotto.
    const double level = amplitudeAt(out, 1000.0, 192000.0, 256);
    QVERIFY2(std::abs(toDb(level)) < 0.5,
             qPrintable(QStringLiteral("livello fuori di %1 dB").arg(toDb(level))));
}

void TestTxChain::theSmallFactorGoesFirst()
{
    // Il costo di due stadi vale R·(a + L): mettere davanti il fattore piccolo
    // è la sola scelta che conti, e va nella direzione opposta alla discesa.
    QCOMPARE(InterpolatorChain::factorize(16), (std::vector<int>{2, 8}));
    QCOMPARE(InterpolatorChain::factorize(12), (std::vector<int>{2, 6}));
    // Sei sta in un solo stadio: spezzarlo in 2 e 3 aggiungerebbe un filtro
    // per risparmiare nulla.
    QCOMPARE(InterpolatorChain::factorize(6), (std::vector<int>{6}));
    QCOMPARE(InterpolatorChain::factorize(1), (std::vector<int>{}));
}

// ── Modulazione ─────────────────────────────────────────────────────────────

void TestTxChain::upperSidebandKeepsOnlyTheUpperSide()
{
    Modulator modulator;
    QVERIFY(modulator.configure(kAudioRate));

    TxSettings settings;
    settings.mode = dsdr::DemodMode::Usb;
    modulator.setSettings(settings);

    const auto audio = tone(1000.0, kAudioRate, 16384, 0.5f);
    std::vector<Complex> out(audio.size());
    modulator.process(audio.data(), audio.size(), out.data());

    const double wanted = amplitudeAt(out, 1000.0, kAudioRate, 2048);
    const double unwanted = amplitudeAt(out, -1000.0, kAudioRate, 2048);
    const double rejection = toDb(unwanted / wanted);
    QVERIFY2(rejection < -50.0,
             qPrintable(QStringLiteral("banda laterale indesiderata a %1 dB").arg(rejection)));
}

void TestTxChain::lowerSidebandKeepsOnlyTheLowerSide()
{
    Modulator modulator;
    QVERIFY(modulator.configure(kAudioRate));

    TxSettings settings;
    settings.mode = dsdr::DemodMode::Lsb;
    modulator.setSettings(settings);

    const auto audio = tone(1000.0, kAudioRate, 16384, 0.5f);
    std::vector<Complex> out(audio.size());
    modulator.process(audio.data(), audio.size(), out.data());

    const double wanted = amplitudeAt(out, -1000.0, kAudioRate, 2048);
    const double unwanted = amplitudeAt(out, 1000.0, kAudioRate, 2048);
    QVERIFY2(toDb(unwanted / wanted) < -50.0,
             qPrintable(QStringLiteral("banda laterale indesiderata a %1 dB")
                            .arg(toDb(unwanted / wanted))));

    // E la banda laterale scelta deve essere davvero l'altra: un LSB che
    // trasmette in USB è il genere di errore che nessuno vede in laboratorio
    // e tutti sentono in aria.
    QVERIFY(wanted > unwanted * 100.0);
}

void TestTxChain::doubleSidebandKeepsBoth()
{
    Modulator modulator;
    QVERIFY(modulator.configure(kAudioRate));

    TxSettings settings;
    settings.mode = dsdr::DemodMode::Dsb;
    modulator.setSettings(settings);

    const auto audio = tone(1000.0, kAudioRate, 16384, 0.5f);
    std::vector<Complex> out(audio.size());
    modulator.process(audio.data(), audio.size(), out.data());

    const double upper = amplitudeAt(out, 1000.0, kAudioRate, 2048);
    const double lower = amplitudeAt(out, -1000.0, kAudioRate, 2048);
    QVERIFY2(std::abs(toDb(upper / lower)) < 0.5,
             qPrintable(QStringLiteral("bande sbilanciate di %1 dB").arg(toDb(upper / lower))));
}

void TestTxChain::amCarriesACarrierAndDsbDoesNot()
{
    const auto audio = tone(1000.0, kAudioRate, 16384, 0.5f);
    std::vector<Complex> out(audio.size());

    Modulator modulator;
    QVERIFY(modulator.configure(kAudioRate));

    TxSettings am;
    am.mode = dsdr::DemodMode::Am;
    modulator.setSettings(am);
    modulator.process(audio.data(), audio.size(), out.data());
    const double amCarrier = amplitudeAt(out, 0.0, kAudioRate, 2048);
    const double amSideband = amplitudeAt(out, 1000.0, kAudioRate, 2048);

    TxSettings dsb;
    dsb.mode = dsdr::DemodMode::Dsb;
    modulator.setSettings(dsb);
    modulator.reset();
    modulator.process(audio.data(), audio.size(), out.data());
    const double dsbCarrier = amplitudeAt(out, 0.0, kAudioRate, 2048);

    QVERIFY2(amCarrier > amSideband,
             "in AM la portante deve essere il segnale più forte");
    QVERIFY2(toDb(dsbCarrier / amCarrier) < -40.0,
             qPrintable(QStringLiteral("la DSB porta una portante a %1 dB")
                            .arg(toDb(dsbCarrier / amCarrier))));
}

void TestTxChain::fmKeepsTheEnvelopeAndTheDeviation()
{
    Modulator modulator;
    QVERIFY(modulator.configure(kAudioRate));

    TxSettings settings;
    settings.mode = dsdr::DemodMode::Nfm;
    settings.fmDeviationHz = 3000.0;
    modulator.setSettings(settings);

    const auto audio = tone(1000.0, kAudioRate, 16384, 1.0f);
    std::vector<Complex> out(audio.size());
    modulator.process(audio.data(), audio.size(), out.data());

    // Ampiezza costante: è la definizione stessa della modulazione di
    // frequenza, e un modulatore che la viola sta trasmettendo anche in AM.
    for (std::size_t i = 2048; i < out.size(); ++i)
        QVERIFY(std::abs(std::abs(out[i]) - 1.0f) < 1e-3f);

    // La deviazione di picco si legge dalla derivata della fase.
    double peakDeviation = 0.0;
    for (std::size_t i = 2049; i < out.size(); ++i) {
        double delta = std::arg(out[i]) - std::arg(out[i - 1]);
        while (delta > dsdr::dsp::kPi)
            delta -= dsdr::dsp::kTwoPi;
        while (delta < -dsdr::dsp::kPi)
            delta += dsdr::dsp::kTwoPi;
        peakDeviation = std::max(peakDeviation,
                                 std::abs(delta) * kAudioRate / dsdr::dsp::kTwoPi);
    }
    // Il filtro di banda toglie qualcosa all'ampiezza dell'audio, quindi la
    // deviazione arriva poco sotto il nominale: il 10% è tolleranza, non
    // approssimazione della misura.
    QVERIFY2(peakDeviation > 2700.0 && peakDeviation < 3300.0,
             qPrintable(QStringLiteral("deviazione di picco %1 Hz").arg(peakDeviation)));
}

void TestTxChain::cwPutsTheCarrierAtZero()
{
    Modulator modulator;
    QVERIFY(modulator.configure(kAudioRate));

    TxSettings settings;
    settings.mode = dsdr::DemodMode::Cw;
    modulator.setSettings(settings);

    // Tasto premuto: l'inviluppo è costante a uno.
    std::vector<float> envelope(4096, 1.0f);
    std::vector<Complex> out(envelope.size());
    modulator.process(envelope.data(), envelope.size(), out.data());

    // La portante CW sta a frequenza zero. Un filtro 300–2700 la
    // cancellerebbe del tutto — e il difetto sarebbe «la CW non trasmette»,
    // senza altra traccia.
    QCOMPARE(modulator.lastPeak(), 1.0f);
    QVERIFY(amplitudeAt(out, 0.0, kAudioRate) > 0.99);
}

// ── Processore di voce ──────────────────────────────────────────────────────

void TestTxChain::compressionRaisesTheAverageNotThePeak()
{
    // Un segnale con dinamica: mezzo blocco piano, mezzo blocco a un decimo.
    //
    // Il livello è quello di una voce normale, non di una a fondo scala. Con
    // un ingresso già a 0,8 il compressore non ha spazio: la parte forte
    // finisce contro il limitatore e il guadagno di recupero non si realizza.
    // Non è un difetto — sopra il fondo scala non c'è niente — ma misurarlo lì
    // vorrebbe dire misurare il limitatore credendo di misurare il
    // compressore.
    const std::size_t frames = 48000;
    std::vector<float> loud = tone(800.0, kAudioRate, frames, 0.3f);
    for (std::size_t i = frames / 2; i < frames; ++i)
        loud[i] *= 0.1f;

    auto measure = [](std::vector<float> audio, double compression) {
        SpeechProcessor processor;
        processor.configure(kAudioRate);
        processor.setCompressionDb(compression);
        processor.process(audio.data(), audio.size());

        double peak = 0.0;
        double sum = 0.0;
        // Si salta l'attacco: il compressore ha bisogno di qualche
        // millisecondo per arrivare a regime, e includerlo misurerebbe il
        // transitorio invece della compressione.
        for (std::size_t i = 4800; i < audio.size(); ++i) {
            peak = std::max<double>(peak, std::abs(audio[i]));
            sum += static_cast<double>(audio[i]) * audio[i];
        }
        const double rms = std::sqrt(sum / (audio.size() - 4800));
        return std::pair<double, double>{peak, rms};
    };

    const auto flat = measure(loud, 0.0);
    const auto compressed = measure(loud, 10.0);

    QVERIFY2(compressed.second > flat.second * 1.5,
             qPrintable(QStringLiteral("il valore efficace è passato da %1 a %2")
                            .arg(flat.second).arg(compressed.second)));
    // Il picco può salire un poco — il recupero non è chirurgico — ma non deve
    // andare oltre il fondo scala: è tutto il punto del limitatore a valle.
    QVERIFY2(compressed.first <= 1.0,
             qPrintable(QStringLiteral("picco a %1").arg(compressed.first)));
}

void TestTxChain::nothingEverLeavesAboveFullScale()
{
    // Trenta decibel di guadagno su un segnale già forte: la situazione di chi
    // ha alzato il microfono al massimo. Deve essere brutta da sentire, non
    // capace di far uscire il segnale dalla propria banda.
    auto audio = tone(1000.0, kAudioRate, 24000, 0.9f);

    SpeechProcessor processor;
    QVERIFY(processor.configure(kAudioRate));
    processor.setMicGainDb(30.0);
    processor.setCompressionDb(0.0);
    processor.process(audio.data(), audio.size());

    for (float sample : audio)
        QVERIFY2(std::abs(sample) <= 1.0f,
                 qPrintable(QStringLiteral("campione a %1").arg(sample)));
    QVERIFY(processor.lastLimited());
}

void TestTxChain::theRumbleDoesNotReachTheModulator()
{
    auto rumble = tone(50.0, kAudioRate, 24000, 0.5f);
    auto voice = tone(1000.0, kAudioRate, 24000, 0.5f);

    SpeechProcessor processor;
    QVERIFY(processor.configure(kAudioRate));
    processor.setCompressionDb(0.0);
    processor.process(rumble.data(), rumble.size());

    SpeechProcessor other;
    QVERIFY(other.configure(kAudioRate));
    other.setCompressionDb(0.0);
    other.process(voice.data(), voice.size());

    auto peakOf = [](const std::vector<float> &audio) {
        double peak = 0.0;
        for (std::size_t i = 4800; i < audio.size(); ++i)
            peak = std::max<double>(peak, std::abs(audio[i]));
        return peak;
    };

    const double attenuation = toDb(peakOf(rumble) / peakOf(voice));
    QVERIFY2(attenuation < -20.0,
             qPrintable(QStringLiteral("il rimbombo passa a %1 dB").arg(attenuation)));
}

// ── Manipolazione ───────────────────────────────────────────────────────────

void TestTxChain::theKeyEdgeTakesTheTimeItSaid()
{
    CwKeyer keyer;
    QVERIFY(keyer.configure(kAudioRate));
    keyer.setRiseMs(5.0);

    std::vector<float> envelope(1024, 0.0f);
    keyer.setKeyDown(true);
    keyer.process(envelope.data(), envelope.size());

    // 5 ms a 48 kHz sono 240 campioni: prima non deve essere arrivato a fondo,
    // dopo non deve mancarci nulla.
    QVERIFY(envelope[120] > 0.4f && envelope[120] < 0.6f);
    QVERIFY(envelope[239] > 0.999f);
    QVERIFY(!keyer.isIdle());

    // Nessun gradino: il salto fra campioni consecutivi resta piccolo, ed è
    // questo — non la durata — che tiene il click fuori dall'aria.
    for (std::size_t i = 1; i < 400; ++i)
        QVERIFY(std::abs(envelope[i] - envelope[i - 1]) < 0.02f);

    keyer.setKeyDown(false);
    keyer.process(envelope.data(), envelope.size());
    QVERIFY(envelope[300] < 1e-6f);
    QVERIFY(keyer.isIdle());
}

void TestTxChain::theKeyerComesBackFromAnInterruptedEdge()
{
    CwKeyer keyer;
    QVERIFY(keyer.configure(kAudioRate));
    keyer.setRiseMs(5.0);

    std::vector<float> envelope(120, 0.0f);
    keyer.setKeyDown(true);
    keyer.process(envelope.data(), envelope.size()); // metà salita
    const float halfway = envelope.back();
    QVERIFY(halfway > 0.4f && halfway < 0.6f);

    // Tasto alzato a metà fronte: si torna indietro da dove si era, non da
    // fondo scala. Un manipolatore che ripartisse da uno produrrebbe proprio
    // il gradino che il fronte serve a evitare.
    keyer.setKeyDown(false);
    keyer.process(envelope.data(), 1);
    QVERIFY(envelope[0] < halfway);
    QVERIFY(halfway - envelope[0] < 0.02f);
}

QTEST_MAIN(TestTxChain)
#include "tst_tx_chain.moc"
