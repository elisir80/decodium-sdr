// SPDX-License-Identifier: GPL-3.0-or-later
// L'equalizzatore parametrico: che faccia quello che la curva promette.
//
// Un equalizzatore sbagliato non fallisce: suona. Alza dove doveva abbassare,
// o alza di sei decibel dove ne mostrava tre, e chi ascolta se ne accorge solo
// confrontando — cioè mai. Le prove qui sotto misurano la risposta vera,
// facendo passare dei toni, e la confrontano con quella che la curva disegna:
// se le due divergono, è la curva a mentire, ed è la bugia peggiore perché è
// quella che si guarda mentre si regola.
#include "dsp/ParametricEq.h"

#include <QTest>

#include <cmath>
#include <vector>

#ifndef M_PI
#  define M_PI 3.14159265358979323846
#endif

using namespace dsdr::dsp;

namespace {

constexpr double kRate = 48000.0;

/// Fa passare un tono e ne misura il livello d'uscita in decibel.
///
/// I primi campioni si buttano: un biquad parte con lo stato a zero e ci mette
/// qualche decina di campioni ad assestarsi, e misurare lì darebbe un livello
/// più basso del vero — un errore che somiglia a un'attenuazione.
double toneLevelDb(ParametricEq &eq, double frequencyHz)
{
    constexpr std::size_t kSettle = 4096;
    constexpr std::size_t kMeasure = 8192;

    std::vector<float> block(2 * (kSettle + kMeasure));
    for (std::size_t i = 0; i < kSettle + kMeasure; ++i) {
        const auto value = static_cast<float>(
            std::sin(2.0 * M_PI * frequencyHz * static_cast<double>(i) / kRate));
        block[i * 2] = value;
        block[i * 2 + 1] = value;
    }

    eq.reset();
    eq.process(block.data(), kSettle + kMeasure);

    double sum = 0.0;
    for (std::size_t i = kSettle; i < kSettle + kMeasure; ++i)
        sum += static_cast<double>(block[i * 2]) * static_cast<double>(block[i * 2]);

    const double rms = std::sqrt(sum / static_cast<double>(kMeasure));
    // Il valore efficace di una sinusoide di ampiezza uno è 1/√2: il rapporto
    // con quello è il guadagno, e zero decibel significa «non ha fatto nulla».
    return 20.0 * std::log10(std::max(rms * std::sqrt(2.0), 1e-9));
}

} // namespace

class TestParametricEq : public QObject
{
    Q_OBJECT

private slots:
    void spentoNonTocca();
    void unaCampanaAlzaDoveDeve();
    void unaCampanaAbbassaDoveDeve();
    void laCurvaDiceLaVerita();
    void iLimitiSonoDelFiltro();
    void celleAZeroSonoTrasparenti();
};

void TestParametricEq::spentoNonTocca()
{
    ParametricEq eq;
    eq.configure(kRate, 2);
    eq.setBand(0, 1000.0, 12.0, 1.0);
    // Non acceso: il segnale deve uscire com'è entrato. Un equalizzatore che
    // lavora anche da spento è il difetto che nessuno cerca, perché nessuno
    // pensa di doverlo cercare.
    QVERIFY(!eq.isEnabled());
    QVERIFY(std::abs(toneLevelDb(eq, 1000.0)) < 0.05);
}

void TestParametricEq::unaCampanaAlzaDoveDeve()
{
    ParametricEq eq;
    eq.configure(kRate, 2);
    eq.setEnabled(true);
    eq.setBand(0, 1000.0, 6.0, 2.0);

    // Sul centro il guadagno è quello chiesto.
    QVERIFY2(std::abs(toneLevelDb(eq, 1000.0) - 6.0) < 0.3,
             qPrintable(QStringLiteral("al centro %1 dB").arg(toneLevelDb(eq, 1000.0))));

    // Lontano non tocca niente: una campana che alza tutto non è una campana,
    // è un volume.
    QVERIFY2(std::abs(toneLevelDb(eq, 100.0)) < 0.6,
             qPrintable(QStringLiteral("a 100 Hz %1 dB").arg(toneLevelDb(eq, 100.0))));
    QVERIFY2(std::abs(toneLevelDb(eq, 8000.0)) < 0.6,
             qPrintable(QStringLiteral("a 8 kHz %1 dB").arg(toneLevelDb(eq, 8000.0))));
}

void TestParametricEq::unaCampanaAbbassaDoveDeve()
{
    ParametricEq eq;
    eq.configure(kRate, 2);
    eq.setEnabled(true);
    eq.setBand(2, 2000.0, -9.0, 3.0);

    QVERIFY2(std::abs(toneLevelDb(eq, 2000.0) + 9.0) < 0.3,
             qPrintable(QStringLiteral("al centro %1 dB").arg(toneLevelDb(eq, 2000.0))));
}

void TestParametricEq::laCurvaDiceLaVerita()
{
    ParametricEq eq;
    eq.configure(kRate, 2);
    eq.setEnabled(true);
    eq.setBand(0, 300.0, 5.0, 1.2);
    eq.setBand(3, 2500.0, -7.0, 2.0);
    // Un giro a vuoto per far ricalcolare i coefficienti sul thread audio: è
    // lì che avviene, ed è da lì che la curva li legge.
    std::vector<float> warm(64, 0.0f);
    eq.process(warm.data(), 32);

    // La curva è quello che si guarda mentre si trascina un punto. Se mostra
    // una campana diversa da quella che si sente, si regola per anni un
    // filtro che non è quello disegnato.
    for (const double hz : {120.0, 300.0, 700.0, 1500.0, 2500.0, 4000.0}) {
        const double drawn = eq.responseDb(hz);
        const double measured = toneLevelDb(eq, hz);
        QVERIFY2(std::abs(drawn - measured) < 0.4,
                 qPrintable(QStringLiteral("a %1 Hz la curva dice %2 dB e il filtro fa %3 dB")
                                .arg(hz).arg(drawn).arg(measured)));
    }
}

void TestParametricEq::iLimitiSonoDelFiltro()
{
    ParametricEq eq;
    eq.configure(kRate, 2);

    // I limiti li mette il filtro, non la UI: una UI che se ne dimentica uno
    // produce un rumore che nessuno collega alla manopola che l'ha causato.
    eq.setBand(0, 1.0, 99.0, 0.001);
    QVERIFY(eq.bandFrequency(0) >= 20.0);
    QVERIFY(eq.bandGainDb(0) <= ParametricEq::kMaxGainDb);
    QVERIFY(eq.bandQ(0) >= 0.3);

    eq.setBand(0, 1e9, -99.0, 1e6);
    QVERIFY(eq.bandFrequency(0) < kRate / 2.0);
    QVERIFY(eq.bandGainDb(0) >= -ParametricEq::kMaxGainDb);
    QVERIFY(eq.bandQ(0) <= 12.0);

    // Un indice fuori scala non deve scrivere in memoria altrui.
    eq.setBand(-1, 1000.0, 3.0, 1.0);
    eq.setBand(ParametricEq::kBands, 1000.0, 3.0, 1.0);
    QCOMPARE(eq.bandGainDb(-1), 0.0);
}

void TestParametricEq::celleAZeroSonoTrasparenti()
{
    ParametricEq eq;
    eq.configure(kRate, 2);
    eq.setEnabled(true);

    // Cinque celle in cascata, tutte a zero: il segnale deve uscire identico.
    // È il caso normale — l'equalizzatore acceso con la curva piatta — ed è
    // quello in cui un errore di un decimo di decibel per cella diventa mezzo
    // decibel senza che nessuno abbia toccato niente.
    for (const double hz : {100.0, 800.0, 3000.0}) {
        QVERIFY2(std::abs(toneLevelDb(eq, hz)) < 0.05,
                 qPrintable(QStringLiteral("a %1 Hz la catena piatta fa %2 dB")
                                .arg(hz).arg(toneLevelDb(eq, hz))));
    }
}

QTEST_MAIN(TestParametricEq)
#include "tst_parametric_eq.moc"
