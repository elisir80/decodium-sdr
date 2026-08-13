// SPDX-License-Identifier: GPL-3.0-or-later
// Il registro delle condizioni: quello che rende onesto un confronto.
//
// Un registro che sbaglia non dà un errore: dà un grafico. Due curve che si
// scostano, e chi le guarda conclude che stasera la banda è rumorosa quando in
// mezzo è cambiato il guadagno, o il ricevitore, o la banda stessa. Sono le
// tre cose che questi test presidiano, perché sono le tre che non si vedono
// guardando il disegno.
#include "core/BandConditions.h"

#include <QFile>
#include <QStandardPaths>
#include <QTest>

#include <cmath>

using namespace dsdr;
using namespace dsdr::core;

namespace {

constexpr qint64 k40m = 7'100'000;
constexpr qint64 k20m = 14'200'000;

/// Riempie il quarto d'ora in corso e lo chiude.
void writeBucket(BandConditions &registry, qint64 hz, double floorDbfs,
                 double gainDb, const QString &device, int samples = 5)
{
    for (int i = 0; i < samples; ++i)
        registry.observe(hz, floorDbfs, gainDb, device);
    registry.flush();
}

double at(const QVariantList &curve, int bucket)
{
    return curve.value(bucket).toDouble();
}

} // namespace

class TestBandConditions : public QObject
{
    Q_OBJECT

private slots:
    /// I test non scrivono nella cartella dati dell'operatore.
    ///
    /// Sembra ovvio e non lo era: la prima versione di questo file ha lasciato
    /// un `condizioni.dat` vero dentro il profilo di chi compilava, e la prova
    /// seguente lo ha riletto e ci si è rotta sopra. Un test che scrive dove
    /// scrive il programma non prova il programma: prova la somma dei due.
    void initTestCase();

    /// Ogni prova parte da un registro vuoto: si vuole verificare come nasce,
    /// non come si somma a quella di prima.
    void init();

    void fuoriBandaNonSiAnnotaNiente();
    void laBandaSiRiconosceDallaFrequenza();
    void ilGuadagnoVieneTolto();
    void laMedianaIgnoraIlFrigorifero();
    void cambiareBandaChiudeIlQuartoDOra();
    void unFondoNonStimatoNonEUnFondo();
    void ilConfrontoNasceVuoto();
};

void TestBandConditions::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
}

void TestBandConditions::init()
{
    QFile::remove(BandConditions::storagePath());
}

void TestBandConditions::fuoriBandaNonSiAnnotaNiente()
{
    BandConditions registry;

    // Fuori dalle bande amatoriali non c'è un confronto da fare: la
    // radiodiffusione a onde medie ha un fondo che dipende da dove si punta
    // l'antenna, non dall'ora.
    registry.observe(9'500'000, -100.0, 0.0, QStringLiteral("prova"));
    registry.flush();

    QVERIFY(!registry.onBand());
    QVERIFY(registry.bandName().isEmpty());
    QVERIFY(registry.today().isEmpty());
}

void TestBandConditions::laBandaSiRiconosceDallaFrequenza()
{
    BandConditions registry;

    writeBucket(registry, k40m, -100.0, 0.0, QStringLiteral("prova"));
    QCOMPARE(registry.bandName(), QStringLiteral("40m"));
    QVERIFY(registry.onBand());

    writeBucket(registry, k20m, -105.0, 0.0, QStringLiteral("prova"));
    QCOMPARE(registry.bandName(), QStringLiteral("20m"));

    // E i due registri non si sono mescolati: quello dei venti non deve
    // portarsi dentro il fondo dei quaranta.
    const int bucket = registry.currentBucket();
    QVERIFY2(std::abs(at(registry.today(), bucket) + 105.0) < 0.01,
             "la banda corrente mostra il fondo di un'altra");
}

void TestBandConditions::ilGuadagnoVieneTolto()
{
    BandConditions registry;
    const int bucket = registry.currentBucket();

    // Stesso rumore all'antenna, ma la guardia contro la saturazione ha tolto
    // dodici decibel: al convertitore si legge dodici in meno. Se il registro
    // annotasse il numero grezzo, il grafico direbbe che la banda è
    // migliorata — e sarebbe il grafico del proprio AGC, non della banda.
    writeBucket(registry, k40m, -112.0, 12.0, QStringLiteral("prova"));
    const double referred = at(registry.today(), bucket);

    QVERIFY2(std::abs(referred + 100.0) < 0.01,
             qPrintable(QStringLiteral("riferito all'antenna: %1 invece di −100")
                            .arg(referred)));
}

void TestBandConditions::laMedianaIgnoraIlFrigorifero()
{
    BandConditions registry;
    const int bucket = registry.currentBucket();

    // Un quarto d'ora di fondo a −100 con dentro due secondi di accensione di
    // un frigorifero. La media parlerebbe del frigorifero; la mediana parla
    // della banda, ed è il motivo per cui si usa quella.
    for (int i = 0; i < 20; ++i)
        registry.observe(k40m, -100.0, 0.0, QStringLiteral("prova"));
    registry.observe(k40m, -40.0, 0.0, QStringLiteral("prova"));
    registry.observe(k40m, -42.0, 0.0, QStringLiteral("prova"));
    registry.flush();

    const double value = at(registry.today(), bucket);
    QVERIFY2(std::abs(value + 100.0) < 0.01,
             qPrintable(QStringLiteral("il quarto d'ora vale %1: il frigorifero "
                                       "è entrato nella misura").arg(value)));
}

void TestBandConditions::cambiareBandaChiudeIlQuartoDOra()
{
    BandConditions registry;
    const int bucket = registry.currentBucket();

    // Mezzo quarto d'ora sui quaranta e mezzo sui venti non è una misura di
    // nessuna delle due bande. Il quarto d'ora si chiude prima di cambiare, e
    // ciascuna banda si tiene la parte che le compete.
    for (int i = 0; i < 6; ++i)
        registry.observe(k40m, -98.0, 0.0, QStringLiteral("prova"));
    for (int i = 0; i < 6; ++i)
        registry.observe(k20m, -118.0, 0.0, QStringLiteral("prova"));
    registry.flush();

    QCOMPARE(registry.bandName(), QStringLiteral("20m"));
    QVERIFY2(std::abs(at(registry.today(), bucket) + 118.0) < 0.01,
             "i venti si sono portati dentro il fondo dei quaranta");

    // E tornando sui quaranta si ritrova quello che c'era.
    registry.observe(k40m, -98.0, 0.0, QStringLiteral("prova"));
    QCOMPARE(registry.bandName(), QStringLiteral("40m"));
    QVERIFY2(std::abs(at(registry.today(), bucket) + 98.0) < 0.01,
             "i quaranta hanno perso la propria misura");
}

void TestBandConditions::unFondoNonStimatoNonEUnFondo()
{
    BandConditions registry;
    const int bucket = registry.currentBucket();

    // L'inseguitore del fondo parte dal valore di riposo e scende: nei primi
    // secondi dopo la connessione dice soltanto da dove è partito. Annotarlo
    // metterebbe nel registro un quarto d'ora falso a ogni riavvio, e su una
    // curva di ventiquattro ore quei quarti d'ora si vedono.
    for (int i = 0; i < 10; ++i)
        registry.observe(k40m, -140.0, 0.0, QStringLiteral("prova"));
    registry.flush();

    // La banda si riconosce lo stesso — il pannello deve poter dire «40m»
    // subito — ma nel registro non entra niente.
    QCOMPARE(registry.bandName(), QStringLiteral("40m"));
    QVERIFY2(!std::isfinite(at(registry.today(), bucket)),
             "un fondo non ancora stimato è finito nel registro");
}

void TestBandConditions::ilConfrontoNasceVuoto()
{
    BandConditions registry;

    // Il primo giorno non c'è niente con cui confrontarsi, e va detto invece
    // di mostrare una riga piatta che sembra una misura.
    writeBucket(registry, k40m, -100.0, 0.0, QStringLiteral("prova"));

    QCOMPARE(registry.typicalDays(), 0);
    QVERIFY(!registry.hasDeparture());

    // La curva tipica esiste come vettore — la UI la disegna comunque — ma è
    // tutta NaN, e un tratto si disegna solo dove ci sono due punti veri.
    const QVariantList typical = registry.typical();
    QCOMPARE(typical.size(), BandConditions::kBuckets);
    for (const QVariant &value : typical)
        QVERIFY(!std::isfinite(value.toDouble()));
}

QTEST_MAIN(TestBandConditions)
#include "tst_band_conditions.moc"
