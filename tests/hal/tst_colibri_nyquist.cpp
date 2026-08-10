// SPDX-License-Identifier: GPL-3.0-or-later
// Dove va messo il DDC del ColibriNANO per ricevere sopra i 61,44 MHz.
//
// L'ADC campiona a 122,88 MHz e non ha un mescolatore davanti: oltre metà di
// quel ritmo non c'è più niente da sintonizzare, ma i segnali continuano ad
// arrivare ripiegati dentro la prima zona. Sintonizzarli è aritmetica di tre
// righe, e sbagliarla non fa fallire niente: si chiede 144,300 e si ascolta
// due megahertz più in là, con lo spettro a rovescio, dando la colpa
// all'antenna.

#include "hal/backends/colibri/ColibriBackend.h"

#include <QTest>

using namespace dsdr::hal;

class TestColibriNyquist : public QObject
{
    Q_OBJECT

private slots:
    void theFirstZoneIsTunedDirectly();
    void theSecondZoneFoldsBackAndInverts();
    void theThirdZoneFoldsForward();
    void theFourthZoneInvertsAgain();
    void theEdgesBelongToTheZoneBelow();
    void twoMetersLandsWhereItShould();
    void theExtendedRangeOpensAndCloses();
};

void TestColibriNyquist::theFirstZoneIsTunedDirectly()
{
    // Sotto 61,44 MHz non c'è niente da ripiegare: il DDC va dove gli si dice.
    for (qint64 hz : {100'000LL, 7'100'000LL, 28'500'000LL, 55'000'000LL}) {
        const auto tuning = ColibriBackend::tuningFor(hz);
        QCOMPARE(tuning.deviceHz, hz);
        QVERIFY(!tuning.inverted);
        QCOMPARE(tuning.zone, 1);
    }
}

void TestColibriNyquist::theSecondZoneFoldsBackAndInverts()
{
    // 70 MHz sta 8,56 MHz sopra il bordo, e si presenta 8,56 MHz **sotto**
    // 61,44: la frequenza ripiegata scende mentre quella vera sale, ed è per
    // questo che lo spettro esce a rovescio.
    const auto tuning = ColibriBackend::tuningFor(70'000'000);
    QCOMPARE(tuning.deviceHz, 52'880'000);
    QVERIFY(tuning.inverted);
    QCOMPARE(tuning.zone, 2);

    // Salendo di un megahertz la frequenza del DDC scende di un megahertz: è
    // la firma del ripiegamento, e se non fosse così l'inversione non
    // servirebbe.
    const auto higher = ColibriBackend::tuningFor(71'000'000);
    QCOMPARE(higher.deviceHz, tuning.deviceHz - 1'000'000);
}

void TestColibriNyquist::theThirdZoneFoldsForward()
{
    // Oltre 122,88 MHz si ricomincia da capo, dritti.
    const auto tuning = ColibriBackend::tuningFor(130'000'000);
    QCOMPARE(tuning.deviceHz, 7'120'000);
    QVERIFY(!tuning.inverted);
    QCOMPARE(tuning.zone, 3);
}

void TestColibriNyquist::theFourthZoneInvertsAgain()
{
    const auto tuning = ColibriBackend::tuningFor(200'000'000);
    QCOMPARE(tuning.deviceHz, 45'760'000);
    QVERIFY(tuning.inverted);
    QCOMPARE(tuning.zone, 4);
}

void TestColibriNyquist::theEdgesBelongToTheZoneBelow()
{
    // Esattamente a metà del ritmo di campionamento non si ripiega ancora: è
    // il caso limite in cui un `<` al posto di un `<=` sposterebbe tutto di
    // una zona senza che nessuno se ne accorga.
    const auto edge = ColibriBackend::tuningFor(61'440'000);
    QCOMPARE(edge.deviceHz, 61'440'000);
    QVERIFY(!edge.inverted);

    const auto beyond = ColibriBackend::tuningFor(61'440'001);
    QCOMPARE(beyond.deviceHz, 61'439'999);
    QVERIFY(beyond.inverted);
}

void TestColibriNyquist::twoMetersLandsWhereItShould()
{
    // Il caso per cui questa funzione esiste: la chiamata in FM sui due metri.
    // 144,300 − 122,880 = 21,420 MHz, terza zona, spettro dritto.
    const auto tuning = ColibriBackend::tuningFor(144'300'000);
    QCOMPARE(tuning.deviceHz, 21'420'000);
    QVERIFY(!tuning.inverted);
    QCOMPARE(tuning.zone, 3);
}

void TestColibriNyquist::theExtendedRangeOpensAndCloses()
{
    // Non serve il device: la copertura dichiarata è una proprietà del
    // backend, ed è quella che la UI legge per decidere fin dove si può
    // digitare una frequenza.
    ColibriBackend backend;

    QCOMPARE(backend.capabilities().maxFrequencyHz, 55'000'000LL);
    QVERIFY(!backend.capabilities().coversFrequency(144'300'000));

    const QVariant opened = backend.nativeCommand(
        QStringLiteral("colibri.setExtendedRange"),
        QVariantMap{{QStringLiteral("enabled"), true}});
    QCOMPARE(opened.toBool(), true);

    QCOMPARE(backend.capabilities().maxFrequencyHz, 245'760'000LL);
    QVERIFY2(backend.capabilities().coversFrequency(144'300'000),
             "con le zone aperte i due metri devono rientrare");

    const QVariantMap state = backend.nativeCommand(
        QStringLiteral("colibri.nyquist"), QVariantMap()).toMap();
    QCOMPARE(state.value(QStringLiteral("extended")).toBool(), true);
    QCOMPARE(state.value(QStringLiteral("maxHz")).toLongLong(), 245'760'000LL);

    // E si richiude tornando dentro la copertura dichiarata: una capability
    // che resta larga dopo aver spento l'interruttore mentirebbe alla UI.
    backend.nativeCommand(QStringLiteral("colibri.setExtendedRange"),
                          QVariantMap{{QStringLiteral("enabled"), false}});
    QCOMPARE(backend.capabilities().maxFrequencyHz, 55'000'000LL);
}

QTEST_MAIN(TestColibriNyquist)
#include "tst_colibri_nyquist.moc"
