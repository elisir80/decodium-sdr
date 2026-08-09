// SPDX-License-Identifier: GPL-3.0-or-later
// La guardia contro la saturazione, su vettori sintetici (SPEC-003 §3).
//
// Quello che si verifica qui non è «riconosce il clipping» — quello è facile —
// ma che non sia nervosa: una guardia che interviene su ogni picco fa ballare
// il guadagno, e l'operatore vede la radio cambiare sensibilità da sola senza
// capire perché. Le prove sono quasi tutte su ciò che *non* deve fare.

#include "dsp/OverloadGuard.h"

#include <QTest>

#include <cmath>
#include <vector>

using namespace dsdr;
using namespace dsdr::dsp;

namespace {

constexpr double kRate = 48000.0;
constexpr std::size_t kWindow = 4800;    // 100 ms

/// Blocco di ampiezza costante: il modulo è quello, la fase non conta.
std::vector<Complex> level(float amplitude, std::size_t n)
{
    return std::vector<Complex>(n, Complex(amplitude, 0.0f));
}

/// Alimenta la guardia con `windows` finestre piene a una data ampiezza.
void feedWindows(OverloadGuard &guard, float amplitude, int windows)
{
    const auto block = level(amplitude, kWindow);
    for (int i = 0; i < windows; ++i)
        guard.feed(block.data(), block.size());
}

} // namespace

class TestOverloadGuard : public QObject
{
    Q_OBJECT

private slots:
    void staysQuietWithHeadroom();
    void needsThreeWindowsBeforeActing();
    void ignoresAnIsolatedPeak();
    void givesBackOnlyWhatItTook();
    void neverExceedsTheOperatorLevel();
    void warnOnlyLightsTheLampWithoutTouchingGain();
    void offMeasuresNothing();
    void stopsReducingAtSomePoint();
};

void TestOverloadGuard::staysQuietWithHeadroom()
{
    OverloadGuard guard;
    guard.configure(kRate);

    // −6 dBFS: né saturazione né margine da restituire. È dove si vuole stare.
    feedWindows(guard, 0.5f, 20);

    QVERIFY2(!guard.overloaded(), "saturazione dichiarata a −6 dBFS");
    QCOMPARE(guard.takeRequestDb(), 0.0);
    QCOMPARE(guard.interventions(), 0);
    QVERIFY(std::abs(guard.peakDbfs() + 6.0f) < 0.5f);
}

void TestOverloadGuard::needsThreeWindowsBeforeActing()
{
    OverloadGuard guard;
    guard.configure(kRate);

    // Due finestre in saturazione: la spia si accende, ma il guadagno non si
    // tocca ancora.
    feedWindows(guard, 0.99f, 2);
    QVERIFY2(guard.overloaded(), "la spia non si è accesa in saturazione");
    QCOMPARE(guard.takeRequestDb(), 0.0);

    // La terza fa scattare l'intervento, e sono sei dB in meno.
    feedWindows(guard, 0.99f, 1);
    QCOMPARE(guard.takeRequestDb(), -6.0);
    QCOMPARE(guard.appliedReductionDb(), 6.0);
    QCOMPARE(guard.interventions(), 1);

    // La richiesta si consuma leggendola: chiederla due volte non deve
    // togliere dodici dB.
    QCOMPARE(guard.takeRequestDb(), 0.0);
}

void TestOverloadGuard::ignoresAnIsolatedPeak()
{
    OverloadGuard guard;
    guard.configure(kRate);

    // Una scarica, un'apertura di portante: un decimo di secondo sopra la
    // soglia in mezzo a una banda tranquilla non deve muovere niente.
    for (int i = 0; i < 10; ++i) {
        feedWindows(guard, 0.99f, 1);
        feedWindows(guard, 0.5f, 2);
    }

    QCOMPARE(guard.takeRequestDb(), 0.0);
    QCOMPARE(guard.interventions(), 0);
}

void TestOverloadGuard::givesBackOnlyWhatItTook()
{
    OverloadGuard guard;
    guard.configure(kRate);

    feedWindows(guard, 0.99f, 3);
    QCOMPARE(guard.takeRequestDb(), -6.0);

    // Trenta secondi di margine abbondante: si restituiscono tre dB per volta.
    feedWindows(guard, 0.05f, 300);
    QCOMPARE(guard.takeRequestDb(), 3.0);
    QCOMPARE(guard.appliedReductionDb(), 3.0);

    feedWindows(guard, 0.05f, 300);
    QCOMPARE(guard.takeRequestDb(), 3.0);
    QCOMPARE(guard.appliedReductionDb(), 0.0);

    // Restituito tutto, si ferma: il livello dell'operatore è un tetto.
    feedWindows(guard, 0.05f, 600);
    QCOMPARE(guard.takeRequestDb(), 0.0);
    QCOMPARE(guard.appliedReductionDb(), 0.0);
}

void TestOverloadGuard::neverExceedsTheOperatorLevel()
{
    OverloadGuard guard;
    guard.configure(kRate);

    // Banda vuota da subito, senza che la guardia abbia mai tolto niente:
    // non deve inventarsi guadagno da regalare.
    feedWindows(guard, 0.001f, 1200);

    QCOMPARE(guard.takeRequestDb(), 0.0);
    QCOMPARE(guard.appliedReductionDb(), 0.0);
    QCOMPARE(guard.interventions(), 0);
}

void TestOverloadGuard::warnOnlyLightsTheLampWithoutTouchingGain()
{
    // È la modalità dei device senza controllo di guadagno — e la scelta di
    // chi vuole decidere da sé. La spia deve funzionare lo stesso: sapere di
    // essere in saturazione è metà del rimedio.
    OverloadGuard guard;
    guard.configure(kRate);
    guard.setMode(OverloadGuard::Mode::WarnOnly);

    feedWindows(guard, 0.99f, 10);

    QVERIFY2(guard.overloaded(), "in sola avvertenza la spia deve accendersi");
    QCOMPARE(guard.takeRequestDb(), 0.0);
    QCOMPARE(guard.appliedReductionDb(), 0.0);
}

void TestOverloadGuard::offMeasuresNothing()
{
    OverloadGuard guard;
    guard.configure(kRate);
    guard.setMode(OverloadGuard::Mode::Off);

    feedWindows(guard, 0.99f, 10);

    QVERIFY(!guard.overloaded());
    QCOMPARE(guard.takeRequestDb(), 0.0);
}

void TestOverloadGuard::stopsReducingAtSomePoint()
{
    // Se il segnale resta in saturazione qualunque cosa si faccia, il problema
    // non è il guadagno: continuare a togliere renderebbe sordo il ricevitore
    // senza risolvere niente, e la guardia si ferma.
    OverloadGuard guard;
    guard.configure(kRate);

    double total = 0.0;
    for (int i = 0; i < 200; ++i) {
        feedWindows(guard, 0.99f, 3);
        total += guard.takeRequestDb();
    }

    QVERIFY2(guard.appliedReductionDb() <= 30.0,
             qPrintable(QStringLiteral("riduzione accumulata senza freno: %1 dB")
                            .arg(guard.appliedReductionDb())));
    QVERIFY2(total >= -30.0,
             qPrintable(QStringLiteral("richieste totali oltre il limite: %1 dB").arg(total)));
    QVERIFY2(guard.overloaded(), "la spia deve restare accesa: il problema c'è ancora");
}

QTEST_APPLESS_MAIN(TestOverloadGuard)

#include "tst_overload_guard.moc"
