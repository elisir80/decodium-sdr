// SPDX-License-Identifier: GPL-3.0-or-later
// Verifica le due parti del waterfall che non richiedono una GPU: le tabelle
// di colore e la scala automatica.
//
// Il rendering vero si guarda a occhio; queste due invece hanno una regola
// precisa da rispettare, ed è quella che si rompe in silenzio quando qualcuno
// ritocca una palette o cambia un percentile.

#include "app/PanadapterView.h"
#include "app/WaterfallPalette.h"

#include <QSignalSpy>
#include <QTest>

#include <cmath>
#include <random>
#include <vector>

using namespace dsdr::app;

namespace {

/// Luminanza percepita, con i pesi consueti: è quello che l'occhio usa per
/// ordinare due colori quando la tinta cambia.
double luminance(const uchar *rgba)
{
    return 0.2126 * rgba[0] + 0.7152 * rgba[1] + 0.0722 * rgba[2];
}

/// Riga di spettro con un fondo di rumore e qualche segnale.
///
/// `signalWidth` è quanti bin occupa ciascun segnale: nella realtà una
/// portante non cade mai in un bin solo, la finestra della FFT la distribuisce
/// su qualcuno intorno, e questo conta per come l'auto-range la vede.
std::vector<float> syntheticRow(int bins, float noiseDb, float peakDb, int signalCount = 5,
                                int signalWidth = 4)
{
    std::mt19937 rng(1234);
    std::normal_distribution<float> jitter(0.0f, 1.5f);

    std::vector<float> row(static_cast<std::size_t>(bins));
    for (int i = 0; i < bins; ++i)
        row[static_cast<std::size_t>(i)] = noiseDb + jitter(rng);

    for (int s = 0; s < signalCount; ++s) {
        const int center = bins / (signalCount + 1) * (s + 1);
        for (int k = 0; k < signalWidth; ++k) {
            const int bin = center + k - signalWidth / 2;
            if (bin >= 0 && bin < bins)
                row[static_cast<std::size_t>(bin)] = peakDb;
        }
    }

    return row;
}

} // namespace

class WaterfallTest : public QObject
{
    Q_OBJECT

private slots:
    void paletteIsReadable_data();
    void paletteIsReadable();
    void paletteIndexIsClamped();
    void autoRangeFollowsTheSignal();
    void autoRangeIgnoresIsolatedSpurs();
    void autoRangeKeepsAUsableSpan();
    void manualRangeIsNotOverwritten();
    void toneControlsAreClamped();
};

void WaterfallTest::paletteIsReadable_data()
{
    QTest::addColumn<int>("palette");
    const QStringList names = waterfallPaletteNames();
    for (int i = 0; i < names.size(); ++i)
        QTest::newRow(names.at(i).toUtf8().constData()) << i;
}

void WaterfallTest::paletteIsReadable()
{
    QFETCH(int, palette);

    const QByteArray map = buildWaterfallColorMap(palette);
    QCOMPARE(map.size(), kColorMapSize * 4);

    const auto *data = reinterpret_cast<const uchar *>(map.constData());
    const auto at = [data](int i) { return luminance(data + i * 4); };

    // La scala nel suo insieme deve schiarire: il livello massimo non può
    // essere più scuro del fondo, o l'immagine si legge al contrario.
    QVERIFY2(at(kColorMapSize - 1) > at(0),
             qPrintable(QStringLiteral("la cima (%1) è più scura del fondo (%2)")
                            .arg(at(kColorMapSize - 1)).arg(at(0))));

    // Il criterio vero è questo: livelli lontani devono dare colori diversi.
    // Non basta guardare la luminanza — Turbo e Raptor affidano quasi tutta la
    // scala alla tinta — ma due punti a un quarto di scala di distanza che
    // finiscono sullo stesso colore rendono illeggibile quella zona.
    constexpr int kApart = kColorMapSize / 4;
    for (int i = 0; i + kApart < kColorMapSize; ++i) {
        const auto *a = data + i * 4;
        const auto *b = data + (i + kApart) * 4;
        const double distance = std::sqrt(std::pow(double(a[0]) - b[0], 2)
                                          + std::pow(double(a[1]) - b[1], 2)
                                          + std::pow(double(a[2]) - b[2], 2));
        QVERIFY2(distance > 40.0,
                 qPrintable(QStringLiteral("livelli %1 e %2 quasi indistinguibili (%3)")
                                .arg(i).arg(i + kApart).arg(distance)));
    }
}

void WaterfallTest::paletteIndexIsClamped()
{
    // Un indice fuori scala arriva dalle preferenze salvate da una versione
    // con più palette: deve ricadere su una tabella valida, non oltre la fine
    // del vettore.
    QCOMPARE(buildWaterfallColorMap(-3).size(), kColorMapSize * 4);
    QCOMPARE(buildWaterfallColorMap(9999).size(), kColorMapSize * 4);

    PanadapterView view;
    view.setPaletteIndex(9999);
    QCOMPARE(view.paletteIndex(), waterfallPaletteNames().size() - 1);
    view.setPaletteIndex(-1);
    QCOMPARE(view.paletteIndex(), 0);
}

void WaterfallTest::autoRangeFollowsTheSignal()
{
    PanadapterView view;
    view.setAutoRange(true);

    // Cinque segnali larghi quattro bin: il 2% della banda, abbastanza da
    // superare il 99,5° percentile su cui si legge il picco.
    view.reportMeasuredLevels(syntheticRow(1024, -118.0f, -42.0f));
    QVERIFY2(view.noiseFloorDb() > -125.0 && view.noiseFloorDb() < -110.0,
             qPrintable(QStringLiteral("fondo misurato %1").arg(view.noiseFloorDb())));
    QVERIFY2(view.peakLevelDb() > -60.0,
             qPrintable(QStringLiteral("picco misurato %1").arg(view.peakLevelDb())));

    // La pubblicazione passa dal ciclo eventi: dal thread di rendering non si
    // emette nulla direttamente.
    QSignalSpy spy(&view, &PanadapterView::measuredLevelsChanged);
    QTRY_VERIFY(spy.count() >= 1);

    QVERIFY(view.floorDb() < view.noiseFloorDb());
    QVERIFY(view.ceilingDb() > view.peakLevelDb());
}

void WaterfallTest::autoRangeIgnoresIsolatedSpurs()
{
    PanadapterView view;
    view.setAutoRange(true);

    // Due bin isolati fortissimi: uno spurio del ricevitore, un impulso.
    // Devono restare fuori dalla misura, altrimenti basta un disturbo per
    // spalancare la scala e schiacciare tutto il traffico vero sul fondo.
    // Il prezzo è che una CW strettissima su banda vuota non apre la scala da
    // sola: satura in cima invece di sparire, che è il verso giusto in cui
    // sbagliare.
    view.reportMeasuredLevels(syntheticRow(2048, -120.0f, 0.0f, /*signalCount=*/2, /*signalWidth=*/1));

    QVERIFY2(view.peakLevelDb() < -80.0,
             qPrintable(QStringLiteral("uno spurio ha aperto la scala: picco %1")
                            .arg(view.peakLevelDb())));
}

void WaterfallTest::autoRangeKeepsAUsableSpan()
{
    PanadapterView view;
    view.setAutoRange(true);

    // Banda muta: fondo e picco coincidono. Senza un minimo, la scala si
    // chiuderebbe a zero e ogni bin finirebbe saturo o nero.
    std::vector<float> flat(512, -100.0f);
    view.reportMeasuredLevels(flat);

    QSignalSpy spy(&view, &PanadapterView::measuredLevelsChanged);
    QTRY_VERIFY(spy.count() >= 1);

    QVERIFY2(view.ceilingDb() - view.floorDb() >= 25.0,
             qPrintable(QStringLiteral("intervallo %1 dB").arg(view.ceilingDb() - view.floorDb())));
}

void WaterfallTest::manualRangeIsNotOverwritten()
{
    PanadapterView view;
    view.setFloorDb(-130.0);
    view.setCeilingDb(-20.0);

    // Con la scala automatica spenta la misura continua — serve a mostrare
    // all'utente dove starebbe la scala — ma non tocca i cursori.
    view.reportMeasuredLevels(syntheticRow(1024, -118.0f, -42.0f));

    QSignalSpy spy(&view, &PanadapterView::measuredLevelsChanged);
    QTRY_VERIFY(spy.count() >= 1);

    QCOMPARE(view.floorDb(), -130.0);
    QCOMPARE(view.ceilingDb(), -20.0);
    QVERIFY(view.noiseFloorDb() < -100.0);
}

void WaterfallTest::toneControlsAreClamped()
{
    PanadapterView view;

    view.setGamma(0.0);
    QVERIFY(view.gamma() > 0.0);
    view.setGamma(100.0);
    QVERIFY(view.gamma() <= 3.0);

    // Una soglia al 100% spegnerebbe l'immagine.
    view.setBlackThreshold(1.0);
    QVERIFY(view.blackThreshold() < 1.0);
    view.setBlackThreshold(-1.0);
    QCOMPARE(view.blackThreshold(), 0.0);

    // L'inclinazione fuori intervallo mostrerebbe la superficie di taglio o
    // dallo zenit, dove la vista in rilievo non dice più nulla.
    view.setTilt(0.0);
    QVERIFY(view.tilt() >= 15.0);
    view.setTilt(180.0);
    QVERIFY(view.tilt() <= 85.0);
}

QTEST_MAIN(WaterfallTest)
#include "tst_waterfall.moc"
