// SPDX-License-Identifier: GPL-3.0-or-later
// Il motore di trasmissione, dal tasto al ring della radio.
//
// Qui non c'è microfono: si trasmette in CW, dove la sorgente è il tasto e
// tutto il resto della catena è lo stesso — processore di voce escluso. È
// l'unico modo di provare il percorso completo in CI, ed è anche quello in cui
// gli errori si vedono meglio: una portante deve stare esattamente dove è
// stata chiesta, e durare esattamente il tempo che le è stato dato.

#include "core/TxEngine.h"
#include "dsp/SpscRing.h"

#include <QEventLoop>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>

#include <cmath>
#include <vector>

using namespace dsdr;
using namespace dsdr::core;

namespace {

constexpr double kDeviceRate = 192000.0;

/// Fa girare il ciclo di eventi per `ms` millisecondi reali. Il motore TX
/// calcola il proprio debito sull'orologio, quindi il tempo che passa qui è
/// esattamente ciò che il test sta misurando.
void spin(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

double amplitudeAt(const std::vector<float> &interleaved, double hz, double sampleRate,
                   std::size_t skipFrames)
{
    const std::size_t frames = interleaved.size() / 2;
    if (frames <= skipFrames)
        return 0.0;
    const std::size_t n = frames - skipFrames;
    const double w = dsp::kTwoPi * hz / sampleRate;

    double re = 0.0;
    double im = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double phase = -w * static_cast<double>(i);
        const float sr = interleaved[(skipFrames + i) * 2];
        const float si = interleaved[(skipFrames + i) * 2 + 1];
        re += sr * std::cos(phase) - si * std::sin(phase);
        im += sr * std::sin(phase) + si * std::cos(phase);
    }
    return std::sqrt(re * re + im * im) / static_cast<double>(n);
}

} // namespace

class TestTxEngine : public QObject
{
    Q_OBJECT

private slots:
    void nothingLeavesWithThePttUp();
    void theCarrierLandsWhereItWasAsked();
    void theRateIsTheDeviceRate();
    void aRateThatIsNotAMultipleIsRefusedOutLoud();
};

void TestTxEngine::nothingLeavesWithThePttUp()
{
    dsp::SpscRing<float> ring(1 << 20);
    TxEngine engine;
    engine.attach(&ring, kDeviceRate);
    engine.start();

    dsp::TxSettings settings;
    settings.mode = DemodMode::Cw;
    engine.setSettings(settings);
    engine.setKeyDown(true);   // tasto premuto, ma il PTT è alzato

    spin(120);

    // Un tasto premuto senza PTT non deve mettere niente in aria: è la
    // differenza fra provare la manipolazione e trasmettere.
    QCOMPARE(ring.available(), std::size_t(0));
}

void TestTxEngine::theCarrierLandsWhereItWasAsked()
{
    dsp::SpscRing<float> ring(1 << 21);
    TxEngine engine;
    engine.attach(&ring, kDeviceRate);
    engine.start();

    dsp::TxSettings settings;
    settings.mode = DemodMode::Cw;
    engine.setSettings(settings);
    engine.setOffsetHz(12000.0);
    engine.setKeyDown(true);
    engine.setTransmitting(true);

    spin(200);

    std::vector<float> out(ring.available());
    const std::size_t got = ring.read(out.data(), out.size());
    out.resize(got);
    QVERIFY(got > 0);

    // Il primo blocco contiene il fronte di salita del manipolatore e la coda
    // dei filtri: si salta.
    const std::size_t skip = 8192;
    const double wanted = amplitudeAt(out, 12000.0, kDeviceRate, skip);
    const double elsewhere = amplitudeAt(out, -12000.0, kDeviceRate, skip);

    QVERIFY2(wanted > 0.1, qPrintable(QStringLiteral("portante a %1").arg(wanted)));
    QVERIFY2(elsewhere < wanted * 0.01,
             qPrintable(QStringLiteral("immagine a %1 contro %2").arg(elsewhere).arg(wanted)));
}

void TestTxEngine::theRateIsTheDeviceRate()
{
    dsp::SpscRing<float> ring(1 << 21);
    TxEngine engine;
    engine.attach(&ring, kDeviceRate);
    engine.start();

    dsp::TxSettings settings;
    settings.mode = DemodMode::Cw;
    engine.setSettings(settings);
    engine.setKeyDown(true);
    engine.setTransmitting(true);

    spin(300);
    engine.setTransmitting(false);

    const std::size_t frames = ring.available() / 2;
    const double seconds = static_cast<double>(frames) / kDeviceRate;

    // Trecento millisecondi di trasmissione devono produrre trecento
    // millisecondi di campioni. Il margine è largo perché il ciclo di eventi
    // non è puntuale, ma una catena che sbagliasse il fattore di
    // interpolazione sarebbe fuori di un fattore quattro, non del venti per
    // cento — e allora si vedrebbe come una portante fuori frequenza.
    QVERIFY2(seconds > 0.24 && seconds < 0.36,
             qPrintable(QStringLiteral("%1 s di campioni per 0,3 s di PTT").arg(seconds)));
}

void TestTxEngine::aRateThatIsNotAMultipleIsRefusedOutLoud()
{
    dsp::SpscRing<float> ring(1 << 20);
    TxEngine engine;
    QSignalSpy refused(&engine, &TxEngine::refused);

    // 250 kS/s non è un multiplo intero dei 48 kHz dell'audio. Serve un
    // ricampionatore a fattore razionale che non abbiamo: dirlo è meglio che
    // trasmettere alla velocità sbagliata, che in aria si presenta come una
    // portante fuori posto e una modulazione stonata.
    engine.attach(&ring, 250000.0);
    QCOMPARE(refused.count(), 1);

    engine.start();
    engine.setTransmitting(true);
    spin(80);

    QCOMPARE(ring.available(), std::size_t(0));
    // Il rifiuto si ripete alla richiesta di trasmettere: chi preme il PTT
    // deve sentirselo dire, non solo chi era presente quando è stata scelta
    // la velocità.
    QVERIFY(refused.count() >= 2);
}

QTEST_MAIN(TestTxEngine)
#include "tst_tx_engine.moc"
