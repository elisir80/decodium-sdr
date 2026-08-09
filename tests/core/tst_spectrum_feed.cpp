// SPDX-License-Identifier: GPL-3.0-or-later
// Verifica il ponte fra il DSP e il panadattatore: la media fra le righe e il
// contratto del ring che le trasporta.
//
// La media non è un abbellimento: cambia quante righe al secondo escono dal
// feed, e con esse la storia che il waterfall mostra. Le regole che seguono
// sono quelle che si rompono in silenzio — una riga parziale consegnata come
// se fosse completa, un accumulo che sopravvive a un cambio di geometria.

#include "core/SpectrumFeed.h"

#include <QSignalSpy>
#include <QTest>

#include <cmath>
#include <numeric>
#include <random>
#include <vector>

using namespace dsdr::core;

namespace {

constexpr int kBins = 64;

/// Riga piatta a un livello dato: quando conta solo il valore medio, il rumore
/// è solo un modo di rendere il test difficile da leggere.
std::vector<float> flatRow(float db)
{
    return std::vector<float>(kBins, db);
}

/// Deviazione standard di una riga: è la misura di quanto il fondo «respira»,
/// cioè esattamente ciò che la media serve a ridurre.
double spread(const std::vector<float> &row, std::size_t offset, std::size_t count)
{
    const double mean =
        std::accumulate(row.begin() + offset, row.begin() + offset + count, 0.0) / count;
    double sum = 0.0;
    for (std::size_t i = offset; i < offset + count; ++i)
        sum += (row[i] - mean) * (row[i] - mean);
    return std::sqrt(sum / count);
}

} // namespace

class SpectrumFeedTest : public QObject
{
    Q_OBJECT

private slots:
    void mediaSpentaConsegnaOgniTrasformata();
    void mediaConsegnaUnaRigaOgniNTrasformate();
    void mediaRestituisceLaMediaAritmeticaInDecibel();
    void mediaCalmaIlFondoDiRumore();
    void abbassareLaMediaSbloccaLAccumuloInCorso();
    void laGeometriaNuovaAzzeraLAccumulo();
    void laMediaRestaInIntervallo();
};

void SpectrumFeedTest::mediaSpentaConsegnaOgniTrasformata()
{
    SpectrumFeed feed;
    feed.configure(kBins, 96000.0, 14'100'000);
    feed.setAveraging(1);

    QSignalSpy spy(&feed, &SpectrumFeed::framesAvailable);

    std::vector<float> out;
    for (int i = 0; i < 5; ++i)
        feed.publish(flatRow(-100.0f + i).data());

    QCOMPARE(spy.count(), 5);
    QCOMPARE(feed.fetchRows(out, 16), 5);

    // E nell'ordine in cui sono arrivate: un waterfall che rimescola le righe
    // mostrerebbe un passato che non è mai esistito.
    for (int i = 0; i < 5; ++i)
        QCOMPARE(out[static_cast<std::size_t>(i) * kBins], -100.0f + i);
}

void SpectrumFeedTest::mediaConsegnaUnaRigaOgniNTrasformate()
{
    SpectrumFeed feed;
    feed.configure(kBins, 96000.0, 14'100'000);
    feed.setAveraging(4);

    QSignalSpy spy(&feed, &SpectrumFeed::framesAvailable);
    std::vector<float> out;

    // Le prime tre trasformate non escono: sono dentro l'accumulo. Nemmeno il
    // segnale deve partire, o il render thread si sveglierebbe per niente.
    for (int i = 0; i < 3; ++i)
        feed.publish(flatRow(-100.0f).data());
    QCOMPARE(spy.count(), 0);
    QCOMPARE(feed.fetchRows(out, 16), 0);

    feed.publish(flatRow(-100.0f).data());
    QCOMPARE(spy.count(), 1);
    QCOMPARE(feed.fetchRows(out, 16), 1);

    // Ventiquattro trasformate, sei righe: è il conto che allunga la storia
    // del waterfall, ed è giusto che sia così.
    for (int i = 0; i < 24; ++i)
        feed.publish(flatRow(-100.0f).data());
    QCOMPARE(feed.fetchRows(out, 16), 6);
}

void SpectrumFeedTest::mediaRestituisceLaMediaAritmeticaInDecibel()
{
    SpectrumFeed feed;
    feed.configure(kBins, 96000.0, 14'100'000);
    feed.setAveraging(4);

    // Quattro livelli la cui media in decibel è −100: se qualcuno un giorno
    // decidesse di mediare in potenza, il risultato uscirebbe più alto e
    // questo test lo direbbe.
    feed.publish(flatRow(-115.0f).data());
    feed.publish(flatRow(-105.0f).data());
    feed.publish(flatRow(-95.0f).data());
    feed.publish(flatRow(-85.0f).data());

    std::vector<float> out;
    QCOMPARE(feed.fetchRows(out, 4), 1);
    for (int bin = 0; bin < kBins; ++bin)
        QVERIFY(std::abs(out[static_cast<std::size_t>(bin)] + 100.0f) < 1e-3f);
}

void SpectrumFeedTest::mediaCalmaIlFondoDiRumore()
{
    // Il motivo per cui la media esiste. Un fondo di rumore che oscilla di
    // qualche decibel da una riga all'altra è tutto ciò che si vede su una
    // banda quieta: mediando, il fondo si ferma e quel che resta a muoversi
    // sono i segnali.
    std::mt19937 rng(20260809);
    std::normal_distribution<float> noise(-118.0f, 5.0f);

    const auto noisyRow = [&] {
        std::vector<float> row(kBins);
        for (float &value : row)
            value = noise(rng);
        return row;
    };

    SpectrumFeed plain;
    plain.configure(kBins, 96000.0, 14'100'000);
    plain.setAveraging(1);

    SpectrumFeed averaged;
    averaged.configure(kBins, 96000.0, 14'100'000);
    averaged.setAveraging(SpectrumFeed::kMaxAveraging);

    for (int i = 0; i < SpectrumFeed::kMaxAveraging; ++i) {
        const std::vector<float> row = noisyRow();
        plain.publish(row.data());
        averaged.publish(row.data());
    }

    std::vector<float> plainRows;
    std::vector<float> averagedRows;
    QCOMPARE(plain.fetchRows(plainRows, 16), SpectrumFeed::kMaxAveraging);
    QCOMPARE(averaged.fetchRows(averagedRows, 16), 1);

    const double before = spread(plainRows, 0, kBins);
    const double after = spread(averagedRows, 0, kBins);

    // Il guadagno teorico è la radice di N — con otto trasformate, poco meno di
    // tre volte. Il test chiede la metà di quel guadagno: abbastanza da
    // accorgersi se la media smettesse di mediare, non così stretto da
    // dipendere dal seme del generatore.
    QVERIFY2(after < before / 1.4,
             qPrintable(QStringLiteral("respiro del fondo %1 dB contro %2 dB senza media: "
                                       "la media non sta mediando")
                            .arg(after).arg(before)));
}

void SpectrumFeedTest::abbassareLaMediaSbloccaLAccumuloInCorso()
{
    SpectrumFeed feed;
    feed.configure(kBins, 96000.0, 14'100'000);
    feed.setAveraging(8);

    for (int i = 0; i < 3; ++i)
        feed.publish(flatRow(-100.0f).data());

    std::vector<float> out;
    QCOMPARE(feed.fetchRows(out, 4), 0);

    // Abbassata la media, l'accumulo già raccolto basta e avanza: la riga esce
    // alla prima trasformata utile invece di restare in ostaggio del valore
    // che era in vigore quando l'accumulo era cominciato.
    feed.setAveraging(2);
    feed.publish(flatRow(-100.0f).data());
    QCOMPARE(feed.fetchRows(out, 4), 1);
}

void SpectrumFeedTest::laGeometriaNuovaAzzeraLAccumulo()
{
    SpectrumFeed feed;
    feed.configure(kBins, 96000.0, 14'100'000);
    feed.setAveraging(4);

    for (int i = 0; i < 3; ++i)
        feed.publish(flatRow(-60.0f).data());

    // Cambia la FFT: le righe accumulate descrivono un'altra banda e non devono
    // finire mescolate a quelle nuove.
    feed.configure(kBins * 2, 192000.0, 14'100'000);

    std::vector<float> wide(kBins * 2, -120.0f);
    std::vector<float> out;
    for (int i = 0; i < 4; ++i)
        feed.publish(wide.data());

    QCOMPARE(feed.fetchRows(out, 4), 1);
    QCOMPARE(out.size(), static_cast<std::size_t>(kBins) * 2);
    for (const float value : out)
        QVERIFY2(std::abs(value + 120.0f) < 1e-3f,
                 "nella riga nuova è rimasto qualcosa della geometria vecchia");
}

void SpectrumFeedTest::laMediaRestaInIntervallo()
{
    SpectrumFeed feed;

    // Zero e i negativi arrivano da preferenze salvate a mano o da una
    // versione futura: devono ricadere su «nessuna media», non spegnere il
    // feed dividendo per zero.
    feed.setAveraging(0);
    QCOMPARE(feed.averaging(), 1);
    feed.setAveraging(-4);
    QCOMPARE(feed.averaging(), 1);

    feed.setAveraging(1000);
    QCOMPARE(feed.averaging(), SpectrumFeed::kMaxAveraging);
}

QTEST_MAIN(SpectrumFeedTest)
#include "tst_spectrum_feed.moc"
