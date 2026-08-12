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
    void theTuneCarrierIsSteady();
    void twoTonesStayTwo();
    void whatWasSaidComesBack();
    void theMonitorOnlySoundsWhileTransmitting();
    void theTwoCurvesSeeTwoDifferentThings();
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

void TestTxEngine::theTuneCarrierIsSteady()
{
    dsp::SpscRing<float> ring(1 << 21);
    TxEngine engine;
    engine.attach(&ring, kDeviceRate);
    engine.start();

    dsp::TxSettings settings;
    settings.mode = DemodMode::Usb;
    engine.setSettings(settings);
    engine.setOffsetHz(0.0);
    engine.setTestSignal(static_cast<int>(TxEngine::TestSignal::Tone));
    engine.setTransmitting(true);

    spin(250);

    std::vector<float> out(ring.available());
    out.resize(ring.read(out.data(), out.size()));
    QVERIFY(out.size() > 40000);

    const std::size_t skip = 8192;
    // Il tono di accordo sta a 1500 Hz sopra la portante: in USB è lì che
    // finisce un tono audio di 1500 Hz, ed è la riga che l'accordatore vede.
    const double carrier = amplitudeAt(out, 1500.0, kDeviceRate, skip);
    QVERIFY2(carrier > 0.1, qPrintable(QStringLiteral("portante a %1").arg(carrier)));

    // E dev'essere **ferma**: un accordatore automatico che leggesse
    // un'ampiezza che ondeggia non troverebbe mai il punto.
    double minimum = 1e9;
    double maximum = 0.0;
    for (std::size_t i = skip; i + 1 < out.size() / 2; ++i) {
        const double magnitude = std::hypot(out[i * 2], out[i * 2 + 1]);
        minimum = std::min(minimum, magnitude);
        maximum = std::max(maximum, magnitude);
    }
    QVERIFY2(maximum - minimum < maximum * 0.05,
             qPrintable(QStringLiteral("ampiezza fra %1 e %2").arg(minimum).arg(maximum)));
}

void TestTxEngine::twoTonesStayTwo()
{
    dsp::SpscRing<float> ring(1 << 21);
    TxEngine engine;
    engine.attach(&ring, kDeviceRate);
    engine.start();

    dsp::TxSettings settings;
    settings.mode = DemodMode::Usb;
    engine.setSettings(settings);
    engine.setOffsetHz(0.0);
    engine.setTestSignal(static_cast<int>(TxEngine::TestSignal::TwoTone));
    engine.setTransmitting(true);

    spin(250);

    std::vector<float> out(ring.available());
    out.resize(ring.read(out.data(), out.size()));
    QVERIFY(out.size() > 40000);

    const std::size_t skip = 8192;
    const double a = amplitudeAt(out, 700.0, kDeviceRate, skip);
    const double b = amplitudeAt(out, 1900.0, kDeviceRate, skip);

    QVERIFY2(a > 0.05 && b > 0.05,
             qPrintable(QStringLiteral("toni a %1 e %2").arg(a).arg(b)));
    QVERIFY2(std::abs(a - b) < a * 0.1,
             qPrintable(QStringLiteral("toni sbilanciati: %1 contro %2").arg(a).arg(b)));

    // I prodotti del terzo ordine cadono a 2·700−1900 = −500 e a
    // 2·1900−700 = 3100. Se la nostra catena ne producesse, la prova non
    // servirebbe a misurare il finale: misurerebbe noi.
    for (double product : {-500.0, 3100.0}) {
        const double level = amplitudeAt(out, product, kDeviceRate, skip);
        QVERIFY2(level < a * 0.01,
                 qPrintable(QStringLiteral("intermodulazione nostra a %1 Hz: %2")
                                .arg(product).arg(level)));
    }
}

void TestTxEngine::whatWasSaidComesBack()
{
    // Il registratore ha il suo test, che prova il buffer circolare. Questo
    // prova l'altra metà, che nessun test di unità può vedere: che la voce
    // passi davvero dal microfono al registratore mentre si trasmette, e che
    // ne esca dal ring del riascolto quando il PTT è libero. Sono due
    // travasi e un thread, ed è esattamente il tipo di collegamento che si
    // dimentica di fare e non se ne accorge nessuno finché non serve.
    dsp::SpscRing<float> ring(1 << 20);
    dsp::SpscRing<float> mic(1 << 16);

    TxEngine engine;
    engine.attach(&ring, kDeviceRate);
    engine.setMicSource(&mic, 48000.0);
    engine.start();

    // Un tono al posto della voce: qui non interessa il suono, interessa che
    // qualcosa di diverso da zero faccia tutto il giro.
    //
    // Il microfono si alimenta **mentre** si trasmette e non prima: premendo il
    // PTT il motore svuota la coda del microfono, perché quel che c'è dentro è
    // entrato mentre non si trasmetteva e mandarlo in aria adesso vorrebbe dire
    // trasmettere il passato. Riempirla prima darebbe una registrazione di
    // mezzo secondo di silenzio, e sarebbe il test a essere sbagliato.
    std::vector<float> block(4800);
    std::size_t phase = 0;

    engine.setTransmitting(true);
    for (int round = 0; round < 5; ++round) {
        for (std::size_t i = 0; i < block.size(); ++i, ++phase) {
            block[i] = 0.4f * static_cast<float>(
                std::sin(dsp::kTwoPi * 700.0 * static_cast<double>(phase) / 48000.0));
        }
        mic.write(block.data(), block.size());
        spin(90);
    }
    engine.setTransmitting(false);
    spin(20);

    QVERIFY2(engine.recorder().hasContent(),
             "il registratore non ha visto passare la voce");
    QVERIFY(engine.recorder().recordedSeconds() > 0.2);

    // Ora il riascolto. Il ring si riempie dal timer del motore, quindi si
    // lascia girare un po' e poi si guarda se ne è uscito qualcosa che suona.
    engine.playRecording(1);
    spin(120);

    QVERIFY(engine.recorder().isPlaying());

    std::vector<float> heard(8192);
    const std::size_t got = engine.localAudioStream()->read(heard.data(), heard.size());
    QVERIFY2(got > 0, "dal ring del riascolto non è uscito niente");

    float peak = 0.0f;
    for (std::size_t i = 0; i < got; ++i)
        peak = std::max(peak, std::abs(heard[i]));
    QVERIFY2(peak > 0.05f,
             qPrintable(QStringLiteral("il riascolto esce a %1: è silenzio").arg(peak)));

    // Stereo: i due canali portano la stessa cosa. Se ne uscisse uno solo, la
    // voce arriverebbe a metà livello e da un orecchio solo.
    QCOMPARE(heard[0], heard[1]);
    QCOMPARE(heard[2], heard[3]);

    // E si ferma quando glielo si dice.
    engine.stopRecordingPlayback();
    QVERIFY(!engine.recorder().isPlaying());
}

/// Trasmette `rounds` blocchi di tono al posto della voce, alimentando il
/// microfono mentre si trasmette — che è l'unico ordine che funzioni: premendo
/// il PTT il motore svuota la coda del microfono.
static void speak(TxEngine &engine, dsp::SpscRing<float> &mic, double amplitude,
                  int rounds = 5)
{
    std::vector<float> block(4800);
    std::size_t phase = 0;
    engine.setTransmitting(true);
    for (int round = 0; round < rounds; ++round) {
        for (std::size_t i = 0; i < block.size(); ++i, ++phase) {
            block[i] = static_cast<float>(amplitude
                * std::sin(dsp::kTwoPi * 700.0 * static_cast<double>(phase) / 48000.0));
        }
        mic.write(block.data(), block.size());
        spin(90);
    }
}

void TestTxEngine::theMonitorOnlySoundsWhileTransmitting()
{
    dsp::SpscRing<float> ring(1 << 20);
    dsp::SpscRing<float> mic(1 << 16);

    TxEngine engine;
    engine.attach(&ring, kDeviceRate);
    engine.setMicSource(&mic, 48000.0);
    engine.start();

    // Acceso ma con il PTT alzato: non deve uscire un campione. Un monitor che
    // suona a riposo si porterebbe dietro il rientro acustico su un
    // altoparlante, e chi lo sente non ha modo di collegarlo a questo tasto.
    engine.setMonitorEnabled(true);
    spin(80);
    std::vector<float> heard(8192);
    QCOMPARE(engine.localAudioStream()->read(heard.data(), heard.size()), std::size_t(0));

    speak(engine, mic, 0.4);

    const std::size_t got = engine.localAudioStream()->read(heard.data(), heard.size());
    QVERIFY2(got > 0, "il monitor è acceso e non esce niente");
    float peak = 0.0f;
    for (std::size_t i = 0; i < got; ++i)
        peak = std::max(peak, std::abs(heard[i]));
    QVERIFY2(peak > 0.02f,
             qPrintable(QStringLiteral("il monitor esce a %1: è silenzio").arg(peak)));

    // Alzando il PTT il ring si svuota: quello che era in coda non deve
    // continuare a suonare dopo che si è smesso di parlare.
    engine.setTransmitting(false);
    spin(30);
    QCOMPARE(engine.localAudioStream()->available(), std::size_t(0));
}

void TestTxEngine::theTwoCurvesSeeTwoDifferentThings()
{
    dsp::SpscRing<float> ring(1 << 20);
    dsp::SpscRing<float> mic(1 << 16);

    TxEngine engine;
    engine.attach(&ring, kDeviceRate);
    engine.setMicSource(&mic, 48000.0);
    engine.start();

    // A riposo le due curve stanno sul fondo: uno spettro fermo su una misura
    // vecchia è indistinguibile da uno spettro giusto.
    QVERIFY(engine.voiceBinDb(false, 30) < -100.0f);
    QVERIFY(engine.voiceBinDb(true, 30) < -100.0f);

    // Il compressore acceso e spinto: è il caso in cui le due curve devono
    // separarsi, perché è quello che il confronto esiste per mostrare.
    engine.setCompressionDb(20.0);
    speak(engine, mic, 0.05);
    engine.setTransmitting(false);

    // La barra del tono di prova: 700 Hz su bin da 23,4 Hz è la trentesima.
    const int bin = static_cast<int>(std::lround(
        700.0 / (48000.0 / TxEngine::kVoiceFftSize)));
    const float dry = engine.voiceBinDb(false, bin);
    const float wet = engine.voiceBinDb(true, bin);

    QVERIFY2(dry > -100.0f,
             qPrintable(QStringLiteral("la curva «prima» non ha visto niente: %1").arg(dry)));
    QVERIFY2(wet > -100.0f,
             qPrintable(QStringLiteral("la curva «dopo» non ha visto niente: %1").arg(wet)));

    // Venti decibel di compressione su una voce debole: il «dopo» deve stare
    // sopra il «prima». Se le due curve coincidessero, il confronto starebbe
    // guardando due volte lo stesso punto della catena — ed è un difetto che
    // nessuno noterebbe, perché due curve sovrapposte sembrano una catena
    // trasparente.
    QVERIFY2(wet > dry + 6.0f,
             qPrintable(QStringLiteral("prima %1 dB, dopo %2 dB: la catena non si vede")
                            .arg(dry).arg(wet)));
}

QTEST_MAIN(TestTxEngine)
#include "tst_tx_engine.moc"
