// SPDX-License-Identifier: GPL-3.0-or-later
// La linea grigia: geometria che si controlla contro numeri noti.
//
// Una mappa di propagazione sbagliata non dà un errore: dà una mappa. Il
// terminatore compare, si muove, sembra plausibile — e manda a chiamare
// mezz'ora prima o dopo il momento buono. Tutto quello che si può verificare
// contro un valore indipendente qui è verificato: i solstizi, gli equinozi,
// gli estremi dell'equazione del tempo, e distanze fra città che chiunque può
// riscontrare su un'altra fonte.
#include "core/Greyline.h"

#include <QTest>

#include <cmath>

using namespace dsdr;
using namespace dsdr::core;

namespace {

QDateTime utc(int year, int month, int day, int hour, int minute = 0)
{
    return QDateTime(QDate(year, month, day), QTime(hour, minute), QTimeZone::UTC);
}

} // namespace

class TestGreyline : public QObject
{
    Q_OBJECT

private slots:
    void aiSolstiziIlSoleStaDoveDeve();
    void agliEquinoziIlSoleStaSullEquatore();
    void aMezzogiornoIlSoleStaSulMeridianoDiGreenwich();
    void ilTerminatoreHaIlSoleAllOrizzonte();
    void leBandeSonoDischiConcentrici();
    void laDistanzaTornaConValoriNoti();
    void laRottaBreveELaLungaSonoOpposte();
    void ilLocatoreVaEtorna();
    void unLocatoreSbagliatoNonFiniceNelGolfoDiGuinea();
    void lOrtodromiaSiSpezzaSullAntimeridiano();
    void ilPercorsoInLineaGrigiaSiMisura();
};

void TestGreyline::aiSolstiziIlSoleStaDoveDeve()
{
    Greyline g;

    // Al solstizio di giugno il Sole sta sul tropico del Cancro, +23.44 gradi.
    // È il controllo più semplice che esista e prende l'errore più grosso che
    // si possa fare: un segno invertito nell'obliquità.
    g.setUtc(utc(2026, 6, 21, 12));
    QVERIFY2(std::abs(g.subsolar().y() - 23.44) < 0.1,
             qPrintable(QStringLiteral("declinazione a giugno: %1").arg(g.subsolar().y())));

    g.setUtc(utc(2026, 12, 21, 12));
    QVERIFY2(std::abs(g.subsolar().y() + 23.44) < 0.1,
             qPrintable(QStringLiteral("declinazione a dicembre: %1").arg(g.subsolar().y())));
}

void TestGreyline::agliEquinoziIlSoleStaSullEquatore()
{
    Greyline g;

    g.setUtc(utc(2026, 3, 20, 14, 46));
    QVERIFY2(std::abs(g.subsolar().y()) < 0.2,
             qPrintable(QStringLiteral("equinozio di marzo: %1").arg(g.subsolar().y())));

    g.setUtc(utc(2026, 9, 23, 0, 6));
    QVERIFY2(std::abs(g.subsolar().y()) < 0.2,
             qPrintable(QStringLiteral("equinozio di settembre: %1").arg(g.subsolar().y())));
}

void TestGreyline::aMezzogiornoIlSoleStaSulMeridianoDiGreenwich()
{
    Greyline g;

    // A mezzogiorno UTC il punto subsolare sta sul meridiano zero, meno lo
    // scarto dell'equazione del tempo. A metà aprile quello scarto è quasi
    // nullo, e allora il Sole sta davvero lì.
    g.setUtc(utc(2026, 4, 15, 12));
    QVERIFY2(std::abs(g.subsolar().x()) < 1.0,
             qPrintable(QStringLiteral("longitudine subsolare ad aprile: %1")
                            .arg(g.subsolar().x())));

    // A inizio novembre l'equazione del tempo tocca il massimo, +16 minuti
    // circa: il Sole passa a Greenwich **prima** di mezzogiorno, quindi a
    // mezzogiorno è già oltre, verso ovest — longitudine negativa di circa
    // quattro gradi. Ignorare questo termine sposterebbe il terminatore di
    // quattrocento chilometri alle nostre latitudini, e nessuno se ne
    // accorgerebbe guardando la mappa.
    g.setUtc(utc(2026, 11, 3, 12));
    const double lon = g.subsolar().x();
    QVERIFY2(lon < -3.0 && lon > -5.0,
             qPrintable(QStringLiteral("longitudine subsolare a novembre: %1").arg(lon)));
}

void TestGreyline::ilTerminatoreHaIlSoleAllOrizzonte()
{
    Greyline g;
    g.setUtc(utc(2026, 5, 10, 8, 30));

    // Ogni punto del disco più esterno deve avere il Sole esattamente
    // all'orizzonte. È la prova che chiude il cerchio fra le due metà del
    // calcolo: la geometria dei dischi e la formula dell'altitudine sono
    // scritte in modo indipendente, e se non tornano fra loro una delle due è
    // sbagliata.
    const QVariantList band = g.terminator();
    QVERIFY(band.size() > 100);

    double worst = 0.0;
    for (const QVariant &value : band) {
        const QPointF p = value.toPointF();
        worst = std::max(worst, std::abs(g.sunAltitude(p.y(), p.x())));
    }
    QVERIFY2(worst < 0.001,
             qPrintable(QStringLiteral("sul terminatore il Sole esce a %1 gradi")
                            .arg(worst)));
}

void TestGreyline::leBandeSonoDischiConcentrici()
{
    Greyline g;
    g.setUtc(utc(2026, 8, 1, 18));

    // I quattro dischi corrispondono ai quattro crepuscoli: 0, −6, −12, −18.
    // Sono dischi e non anelli, ed è la cosa che si sbaglia leggendo la
    // definizione: un anello ritagliato darebbe una sfumatura al contrario.
    const auto altitudeOn = [&g](const QVariantList &band) {
        const QPointF p = band.first().toPointF();
        return g.sunAltitude(p.y(), p.x());
    };

    QVERIFY(std::abs(altitudeOn(g.civilBand()) - 0.0) < 0.01);
    QVERIFY(std::abs(altitudeOn(g.nauticalBand()) + 6.0) < 0.01);
    QVERIFY(std::abs(altitudeOn(g.astroBand()) + 12.0) < 0.01);
    QVERIFY(std::abs(altitudeOn(g.nightBand()) + 18.0) < 0.01);
}

void TestGreyline::laDistanzaTornaConValoriNoti()
{
    Greyline g;

    // Roma → New York: circa 6900 km, ed è un numero che chiunque può
    // riscontrare altrove. Serve a prendere l'errore che nessuna formula
    // segnala — latitudine e longitudine invertite — che darebbe comunque una
    // distanza plausibile.
    const double km = g.distanceKm(41.9, 12.5, 40.7, -74.0);
    QVERIFY2(std::abs(km - 6900.0) < 60.0,
             qPrintable(QStringLiteral("Roma–New York: %1 km").arg(km)));

    // Un percorso corto, dove la formula del coseno perderebbe i decimali.
    const double breve = g.distanceKm(41.90, 12.50, 41.91, 12.50);
    QVERIFY2(std::abs(breve - 1.11) < 0.05,
             qPrintable(QStringLiteral("un centesimo di grado: %1 km").arg(breve)));

    // Mezzo giro di mondo: gli antipodi distano circa 20015 km.
    QVERIFY(std::abs(g.distanceKm(0, 0, 0, 180) - 20015.0) < 20.0);
}

void TestGreyline::laRottaBreveELaLungaSonoOpposte()
{
    Greyline g;

    // Da Roma verso nord: la rotta è zero. Verso est lungo l'equatore sarebbe
    // novanta, ma alle nostre latitudini la rotta iniziale verso un punto a
    // est è meno di novanta — la Terra è curva, e questo è il motivo per cui
    // «rotta iniziale» non è una precisazione da manuale.
    QVERIFY(std::abs(g.bearing(41.9, 12.5, 51.9, 12.5)) < 0.01);
    QVERIFY(std::abs(g.bearing(41.9, 12.5, 31.9, 12.5) - 180.0) < 0.01);

    const double breve = g.bearing(41.9, 12.5, 35.7, 139.7);   // Roma → Tokyo
    const double lunga = g.longPathBearing(41.9, 12.5, 35.7, 139.7);
    QVERIFY(std::abs(std::fmod(lunga - breve + 360.0, 360.0) - 180.0) < 0.01);

    // Roma → Tokyo passa per nord-est, sopra la Siberia: è la rotta che
    // qualunque tabella dà attorno ai 45 gradi. Una rotta calcolata sulla
    // mappa piatta darebbe est, e chi punta l'antenna non troverebbe nessuno.
    QVERIFY2(breve > 30.0 && breve < 60.0,
             qPrintable(QStringLiteral("Roma–Tokyo: %1 gradi").arg(breve)));
}

void TestGreyline::ilLocatoreVaEtorna()
{
    Greyline g;

    // JN71DC: il locatore di casa. Deve tornare vicino a 41.08 N, 14.25 E.
    const QPointF p = g.fromLocator(QStringLiteral("JN71DC"));
    QVERIFY2(std::abs(p.y() - 41.08) < 0.03 && std::abs(p.x() - 14.25) < 0.05,
             qPrintable(QStringLiteral("JN71DC: %1 %2").arg(p.y()).arg(p.x())));

    // E il ritorno deve dare lo stesso quadrato. Un giro che non chiude vuol
    // dire che uno dei due verso sbaglia di mezzo quadrato, e su una mappa
    // mondiale mezzo quadrato non si vede.
    QCOMPARE(g.toLocator(p.y(), p.x()), QStringLiteral("JN71DC"));

    // Qualche riferimento che si trova su qualunque tabella.
    QCOMPARE(g.toLocator(51.50, -0.13), QStringLiteral("IO91WM"));  // Londra
    QCOMPARE(g.toLocator(-33.87, 151.21), QStringLiteral("QF56OD")); // Sydney
}

void TestGreyline::unLocatoreSbagliatoNonFiniceNelGolfoDiGuinea()
{
    Greyline g;

    // Un locatore non valido deve dare un punto non valido, non zero-zero:
    // zero-zero è nel golfo di Guinea, ed è un posto plausibile in cui
    // piazzare per sbaglio una stazione senza che nessuno se ne accorga.
    for (const QString &bad : {QStringLiteral(""), QStringLiteral("JN"),
                               QStringLiteral("ZZ99"), QStringLiteral("JN7X"),
                               QStringLiteral("JN71DCX")}) {
        const QPointF p = g.fromLocator(bad);
        QVERIFY2(std::isnan(p.x()) && std::isnan(p.y()),
                 qPrintable(QStringLiteral("«%1» è passato per buono").arg(bad)));
    }
}

void TestGreyline::lOrtodromiaSiSpezzaSullAntimeridiano()
{
    Greyline g;

    // Un percorso che non scavalca resta un tratto solo.
    QCOMPARE(g.greatCircle(41.9, 12.5, 51.5, -0.1, 60).size(), 1);

    // Uno che scavalca ne dà due. Senza questa divisione, in equirettangolare
    // comparirebbe una riga che attraversa tutta la mappa — e sembrerebbe un
    // percorso, non un artefatto.
    const QVariantList crossing = g.greatCircle(35.7, 139.7, 37.8, -122.4, 120);
    QVERIFY2(crossing.size() == 2,
             qPrintable(QStringLiteral("Tokyo–San Francisco in %1 tratti")
                            .arg(crossing.size())));
}

void TestGreyline::ilPercorsoInLineaGrigiaSiMisura()
{
    Greyline g;
    g.setUtc(utc(2026, 3, 20, 6));

    // Un percorso interamente in pieno giorno non ha niente in linea grigia.
    // Il subsolare a quest'ora sta attorno ai 90 gradi est: due punti lì
    // attorno sono in pieno Sole.
    const double giorno = g.greylineOverlapKm(0.0, 88.0, 5.0, 92.0);
    QCOMPARE(giorno, 0.0);

    // Un percorso che segue il terminatore ne ha quasi tutto. All'equinozio il
    // terminatore corre lungo un meridiano, quindi due punti sulla stessa
    // longitudine del terminatore e a latitudini diverse ci stanno dentro per
    // intero — ed è esattamente il caso in cui la linea grigia serve.
    const double subsolarLon = g.subsolar().x();
    const double terminatorLon = subsolarLon - 90.0;
    const double lungo = g.greylineOverlapKm(-30.0, terminatorLon,
                                             30.0, terminatorLon);
    const double totale = g.distanceKm(-30.0, terminatorLon, 30.0, terminatorLon);
    QVERIFY2(lungo > totale * 0.9,
             qPrintable(QStringLiteral("lungo il terminatore: %1 di %2 km")
                            .arg(lungo).arg(totale)));
}

QTEST_MAIN(TestGreyline)
#include "tst_greyline.moc"
