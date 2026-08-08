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

#include <algorithm>
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
    void paletteStartsFromTheBackground_data();
    void paletteStartsFromTheBackground();
    void paletteIndexIsClamped();
    void autoRangeFollowsTheSignal();
    void noiseStaysBelowTheBlackThreshold();
    void autoRangeIgnoresIsolatedSpurs();
    void autoRangeKeepsAUsableSpan();
    void manualRangeIsNotOverwritten();
    void toneControlsAreClamped();
    void theScaleKeepsAUsableSpan();
    void historyIsMeasuredNotAssumed();
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

void WaterfallTest::paletteStartsFromTheBackground_data()
{
    paletteIsReadable_data();
}

void WaterfallTest::paletteStartsFromTheBackground()
{
    QFETCH(int, palette);

    const QByteArray map = buildWaterfallColorMap(palette);
    const auto *data = reinterpret_cast<const uchar *>(map.constData());

    // Il livello più basso di un waterfall vuol dire «qui non c'è niente»: su
    // una banda quieta copre quasi tutto lo schermo. Se ha un colore, quel
    // colore diventa un velo steso sull'immagine — è quello che faceva Turbo,
    // che parte da un viola pieno perché nasce per le mappe di calore.
    const double floorLuminance = luminance(data);
    QVERIFY2(floorLuminance < 24.0,
             qPrintable(QStringLiteral("il livello zero ha luminanza %1: "
                                       "stende un velo di colore su tutta l'immagine")
                            .arg(floorLuminance)));

    // E non deve essere una tinta satura nemmeno se cupa: un fondo viola scuro
    // resta viola su un'area grande.
    const int maxComponent = std::max({data[0], data[1], data[2]});
    const int minComponent = std::min({data[0], data[1], data[2]});
    QVERIFY2(maxComponent - minComponent < 24,
             qPrintable(QStringLiteral("il livello zero è una tinta (%1,%2,%3), non un fondo")
                            .arg(data[0]).arg(data[1]).arg(data[2])));
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

    // Il fondo della scala sta sopra il fondo misurato, non sotto: è la
    // condizione perché il rumore resti nero. Vedi
    // PanadapterView::kFloorAboveNoiseDb.
    QVERIFY2(view.floorDb() > view.noiseFloorDb(),
             qPrintable(QStringLiteral("fondo scala %1 sotto il rumore %2: "
                                       "il waterfall torna un muro di colore")
                            .arg(view.floorDb())
                            .arg(view.noiseFloorDb())));
    QVERIFY(view.ceilingDb() > view.peakLevelDb());
}

void WaterfallTest::noiseStaysBelowTheBlackThreshold()
{
    PanadapterView view;
    view.setAutoRange(true);

    // Un fondo di rumore realistico con qualche segnale sopra: la situazione
    // normale su una banda HF viva.
    view.reportMeasuredLevels(syntheticRow(1024, -118.0f, -60.0f));

    QSignalSpy spy(&view, &PanadapterView::measuredLevelsChanged);
    QTRY_VERIFY(spy.count() >= 1);

    const qreal span = view.ceilingDb() - view.floorDb();
    QVERIFY2(span > 0.0, "scala degenere");

    // Questa è la normalizzazione che fa lo shader prima di applicare soglia,
    // contrasto e palette: livello → posizione nella scala. Se il rumore cade
    // sopra la soglia di nero riceve un colore, e con il rumore su quasi tutti
    // i bin l'immagine diventa il muro blu che questo test presidia.
    const auto normalized = [&view, span](qreal levelDb) {
        return (levelDb - view.floorDb()) / span;
    };

    const qreal noiseNorm = normalized(view.noiseFloorDb());
    QVERIFY2(noiseNorm <= 0.0,
             qPrintable(QStringLiteral("il fondo di rumore normalizza a %1: "
                                       "sopra lo zero riceve colore")
                            .arg(noiseNorm)));

    // E deve restare nero anche mentre respira: il rumore di una FFT non
    // mediata oscilla, e un waterfall che pulsa a ogni riga è inservibile.
    //
    // La garanzia vale alla scala di una banda viva, che è il caso per cui la
    // taratura è pensata. Su una banda muta l'auto-range comprime la scala al
    // minimo utile, e siccome `blackThreshold` è una frazione della scala e non
    // una quota in decibel, lì vale troppo pochi dB perché la disuguaglianza
    // regga. È un limite noto del parametro, non di questa taratura.
    const qreal breathingNorm =
        normalized(view.noiseFloorDb() + PanadapterView::kNoiseHeadroomDb);
    QVERIFY2(breathingNorm <= view.blackThreshold(),
             qPrintable(QStringLiteral("il rumore a +%1 dB normalizza a %2, "
                                       "sopra la soglia di nero %3")
                            .arg(PanadapterView::kNoiseHeadroomDb)
                            .arg(breathingNorm)
                            .arg(view.blackThreshold())));
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

void WaterfallTest::theScaleKeepsAUsableSpan()
{
    PanadapterView view;
    view.setFloorDb(-125.0);
    view.setCeilingDb(-25.0);

    // Il caso visto sul campo: la vetta portata a fondo corsa fin sotto il
    // fondo. Restavano tre decibel di dinamica, ogni bin diventava nero o
    // saturo e la scala mostrava tacche diverse con la stessa etichetta.
    view.setCeilingDb(-128.0);
    QVERIFY2(view.ceilingDb() - view.floorDb() >= 10.0,
             qPrintable(QStringLiteral("scala di %1 dB dopo aver abbassato la vetta")
                            .arg(view.ceilingDb() - view.floorDb())));

    // E nell'altro verso: il fondo spinto sopra la vetta.
    view.setFloorDb(-125.0);
    view.setCeilingDb(-25.0);
    view.setFloorDb(-20.0);
    QVERIFY2(view.ceilingDb() - view.floorDb() >= 10.0,
             qPrintable(QStringLiteral("scala di %1 dB dopo aver alzato il fondo")
                            .arg(view.ceilingDb() - view.floorDb())));

    // Restringere fin dove è lecito deve però restare possibile: il limite è
    // una rete di sicurezza, non una tutela contro le scelte dell'operatore.
    view.setFloorDb(-100.0);
    view.setCeilingDb(-88.0);
    QCOMPARE(view.ceilingDb(), -88.0);
    QCOMPARE(view.floorDb(), -100.0);
}

void WaterfallTest::historyIsMeasuredNotAssumed()
{
    PanadapterView view;

    // Finché non è stata misurata, la durata della storia è zero — e chi
    // disegna l'asse dei tempi non disegna niente. Un asse ricavato da un
    // ritmo supposto direbbe numeri sbagliati con l'aria di quelli giusti.
    QCOMPARE(view.historySeconds(), 0.0);

    // La prima chiamata fa solo partire il cronometro: non c'è ancora un
    // intervallo su cui dividere.
    view.reportRowsConsumed(1, 512);
    QCOMPARE(view.historySeconds(), 0.0);

    // Poco più di una finestra di misura, con un ritmo di circa 50 righe al
    // secondo: 512 righe di storia diventano una decina di secondi.
    QTest::qWait(1100);
    view.reportRowsConsumed(55, 512);

    QSignalSpy spy(&view, &PanadapterView::historySecondsChanged);
    QTRY_VERIFY(spy.count() >= 1);

    // Tolleranza larga di proposito: la finestra vera dipende da come lo
    // scheduler ha trattato la qWait, e un test che misura il tempo non deve
    // diventare un test che misura il carico della macchina.
    QVERIFY2(view.historySeconds() > 6.0 && view.historySeconds() < 16.0,
             qPrintable(QStringLiteral("storia misurata %1 s, attesa attorno a 10")
                            .arg(view.historySeconds())));
}

QTEST_MAIN(WaterfallTest)
#include "tst_waterfall.moc"
