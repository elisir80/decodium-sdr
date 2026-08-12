// SPDX-License-Identifier: GPL-3.0-or-later
// Gate, leveller e limiter: i tre stadi di dinamica della catena TX.
//
// Sono i tre che sbagliano in silenzio. Un gate con la soglia sbagliata taglia
// la coda delle parole, e chi trasmette è l'unico a non sentirlo; un leveller
// troppo veloce pompa; un limiter che non tiene il tetto lascia tosare il
// modulatore, e tosare in banda base vuol dire allargarsi sulle frequenze dei
// vicini. Nessuno dei tre produce un errore: producono un segnale.
#include "dsp/Leveller.h"
#include "dsp/Limiter.h"
#include "dsp/NoiseGate.h"

#include <QTest>

#include <cmath>
#include <vector>

#ifndef M_PI
#  define M_PI 3.14159265358979323846
#endif

using namespace dsdr::dsp;

namespace {

constexpr double kRate = 48000.0;

std::vector<float> tone(double frequencyHz, double amplitude, std::size_t frames)
{
    std::vector<float> out(frames);
    for (std::size_t i = 0; i < frames; ++i) {
        out[i] = static_cast<float>(
            amplitude * std::sin(2.0 * M_PI * frequencyHz * static_cast<double>(i) / kRate));
    }
    return out;
}

double peakOf(const std::vector<float> &block, std::size_t from)
{
    double peak = 0.0;
    for (std::size_t i = from; i < block.size(); ++i)
        peak = std::max(peak, std::abs(static_cast<double>(block[i])));
    return peak;
}

} // namespace

class TestTxDynamics : public QObject
{
    Q_OBJECT

private slots:
    void spentiNonToccano();
    void ilGateChiudeSulRumoreEApreSullaVoce();
    void ilGateNonSiChiudeDentroUnaFrase();
    void illevellerPortaAlBersaglio();
    void illevellerNonAlzaIlSilenzio();
    void ilLimiterTieneIlTetto();
    void ilLimiterDichiaraIlSuoRitardo();
};

void TestTxDynamics::spentiNonToccano()
{
    NoiseGate gate;
    Leveller leveller;
    Limiter limiter;
    gate.configure(kRate);
    leveller.configure(kRate);
    limiter.configure(kRate);

    auto block = tone(800.0, 0.5, 4800);
    const auto original = block;

    gate.process(block.data(), block.size());
    leveller.process(block.data(), block.size());
    limiter.process(block.data(), block.size());

    // Da spenti il segnale esce identico. Uno stadio che lavora anche in
    // bypass è il difetto che nessuno cerca, perché nessuno pensa di doverlo
    // cercare.
    for (std::size_t i = 0; i < block.size(); ++i)
        QCOMPARE(block[i], original[i]);
}

void TestTxDynamics::ilGateChiudeSulRumoreEApreSullaVoce()
{
    NoiseGate gate;
    gate.configure(kRate);
    gate.setEnabled(true);
    gate.setThresholdDb(-40.0);

    // Rumore sotto la soglia: dopo il rilascio deve restare quasi niente.
    auto quiet = tone(300.0, 0.002, static_cast<std::size_t>(kRate));   // −54 dBFS
    gate.process(quiet.data(), quiet.size());
    QVERIFY2(peakOf(quiet, quiet.size() / 2) < 1e-4,
             qPrintable(QStringLiteral("il gate lascia passare %1 di rumore")
                            .arg(peakOf(quiet, quiet.size() / 2))));
    QVERIFY(gate.opening() < 0.05);

    // Voce sopra la soglia: deve aprire, e in fretta.
    auto voice = tone(300.0, 0.3, 4800);
    gate.process(voice.data(), voice.size());
    QVERIFY2(gate.opening() > 0.95,
             qPrintable(QStringLiteral("il gate è aperto al %1%").arg(gate.opening() * 100)));
}

void TestTxDynamics::ilGateNonSiChiudeDentroUnaFrase()
{
    NoiseGate gate;
    gate.configure(kRate);
    gate.setEnabled(true);
    gate.setThresholdDb(-40.0);

    // Apre su una sillaba, poi una pausa di cinquanta millisecondi — quella
    // che ci sta fra due sillabe. La tenuta deve reggerla: chiudersi lì fa un
    // buco in mezzo a una parola, e si sente più del rumore che si toglie.
    auto syllable = tone(300.0, 0.3, 4800);
    gate.process(syllable.data(), syllable.size());

    std::vector<float> pause(static_cast<std::size_t>(kRate * 0.05), 0.0f);
    gate.process(pause.data(), pause.size());

    QVERIFY2(gate.opening() > 0.9,
             qPrintable(QStringLiteral("dopo 50 ms di pausa il gate è al %1%")
                            .arg(gate.opening() * 100)));
}

void TestTxDynamics::illevellerPortaAlBersaglio()
{
    Leveller leveller;
    leveller.configure(kRate);
    leveller.setEnabled(true);
    leveller.setTargetDb(-18.0);

    // Una voce lontana dal microfono: −38 dBFS. Dopo qualche secondo il
    // leveller deve averla portata attorno al bersaglio.
    auto block = tone(300.0, 0.0126, static_cast<std::size_t>(kRate * 5));
    leveller.process(block.data(), block.size());

    const double outPeak = peakOf(block, block.size() - 4800);
    const double outDb = 20.0 * std::log10(std::max(outPeak, 1e-9));
    QVERIFY2(std::abs(outDb + 18.0) < 3.0,
             qPrintable(QStringLiteral("la voce esce a %1 dBFS invece di −18").arg(outDb)));
    QVERIFY(leveller.gainDb() > 10.0);
}

void TestTxDynamics::illevellerNonAlzaIlSilenzio()
{
    Leveller leveller;
    leveller.configure(kRate);
    leveller.setEnabled(true);
    leveller.setTargetDb(-18.0);

    // Silenzio quasi puro: un AGC senza fondo alzerebbe finché il rumore
    // arriva al bersaglio, cioè farebbe entrare la stanza fra una frase e
    // l'altra — proprio quello che il gate davanti serviva a togliere.
    auto quiet = tone(300.0, 0.0005, static_cast<std::size_t>(kRate * 3));  // −66 dBFS
    leveller.process(quiet.data(), quiet.size());

    QVERIFY2(leveller.gainDb() < 1.0,
             qPrintable(QStringLiteral("il leveller ha alzato il silenzio di %1 dB")
                            .arg(leveller.gainDb())));
}

void TestTxDynamics::ilLimiterTieneIlTetto()
{
    Limiter limiter;
    limiter.configure(kRate);
    limiter.setEnabled(true);
    limiter.setCeilingDb(-1.0);

    // Un segnale che va oltre il fondo scala: niente deve uscire sopra il
    // tetto. È l'unica promessa che questo stadio fa, e l'unica che conta.
    auto loud = tone(700.0, 1.6, static_cast<std::size_t>(kRate * 0.5));
    limiter.process(loud.data(), loud.size());

    const double ceiling = std::pow(10.0, -1.0 / 20.0);
    const double peak = peakOf(loud, static_cast<std::size_t>(kRate * 0.05));
    QVERIFY2(peak <= ceiling * 1.05,
             qPrintable(QStringLiteral("il picco esce a %1, il tetto è %2")
                            .arg(peak).arg(ceiling)));
    QVERIFY(limiter.reductionDb() > 3.0);
}

void TestTxDynamics::ilLimiterDichiaraIlSuoRitardo()
{
    Limiter limiter;
    limiter.configure(kRate);

    // Il ritardo va dichiarato: chi somma le latenze della catena deve poterlo
    // leggere, e un anticipo non dichiarato è un ritardo che qualcuno cercherà
    // altrove.
    QVERIFY(limiter.latencyMs() > 1.0);
    QVERIFY(limiter.latencyMs() < 5.0);
}

QTEST_MAIN(TestTxDynamics)
#include "tst_tx_dynamics.moc"
