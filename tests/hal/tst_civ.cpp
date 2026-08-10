// SPDX-License-Identifier: GPL-3.0-or-later
// CI-V: l'aritmetica BCD e la tabella dei modi delle Icom.
//
// Il CI-V è un bus, e da lì vengono le due cose che si sbagliano. La frequenza
// viaggia in BCD, cinque byte, **dal meno significativo**: invertirli non
// produce un errore, produce una frequenza plausibile — 14,074 che diventa
// 47,041 — e la si attribuisce alla radio. E la tabella dei modi non ha un
// codice per i modi dati: chi ne inventa uno manda la radio in RTTY.

#include "hal/backends/audiorig/CivDriver.h"

#include <QTest>

using namespace dsdr;
using namespace dsdr::hal::audiorig;

class TestCiv : public QObject
{
    Q_OBJECT

private slots:
    void aFrequencyGoesOutLeastSignificantFirst();
    void everyFrequencySurvivesTheRoundTrip();
    void whatIsNotBcdIsRefused();
    void theFrameCarriesBothAddresses();
    void theAddressNamesTheModel();
    void everyModeSurvivesTheRoundTrip();
    void theDataModesStayOnSideband();
};

void TestCiv::aFrequencyGoesOutLeastSignificantFirst()
{
    // 14.074.000 Hz. In BCD, dal byte meno significativo:
    //   00 40 07 14 00
    // cioè unità/decine, centinaia/migliaia, e così via. L'ordine è quello del
    // protocollo, ed è l'inverso di come si scrive un numero.
    const QByteArray bcd = CivDriver::frequencyToBcd(14'074'000);
    QCOMPARE(bcd.size(), 5);
    QCOMPARE(static_cast<quint8>(bcd.at(0)), quint8(0x00));
    QCOMPARE(static_cast<quint8>(bcd.at(1)), quint8(0x40));
    QCOMPARE(static_cast<quint8>(bcd.at(2)), quint8(0x07));
    QCOMPARE(static_cast<quint8>(bcd.at(3)), quint8(0x14));
    QCOMPARE(static_cast<quint8>(bcd.at(4)), quint8(0x00));
}

void TestCiv::everyFrequencySurvivesTheRoundTrip()
{
    // Andata e ritorno su tutta la copertura di una Icom: se una cifra si
    // perde per strada, la radio va dove non le si è chiesto.
    const qint64 frequencies[] = {
        1'810'000,      // 160 m
        7'074'000,      // FT8 sui 40
        14'074'000,
        28'500'000,
        50'313'000,     // FT8 sui 6
        144'300'000,    // la chiamata sui 2 m
        1'296'000'000,  // 23 cm: cinque byte bastano fino a 9,9 GHz
        0,
    };

    for (qint64 hz : frequencies) {
        const qint64 back = CivDriver::bcdToFrequency(CivDriver::frequencyToBcd(hz));
        QVERIFY2(back == hz,
                 qPrintable(QStringLiteral("%1 è tornata indietro come %2")
                                .arg(hz).arg(back)));
    }
}

void TestCiv::whatIsNotBcdIsRefused()
{
    // Una cifra maggiore di nove non è un numero: è un pacchetto sbagliato, o
    // un'eco letta per errore. Restituire comunque un valore lo farebbe
    // passare per buono, e la frequenza mostrata sarebbe inventata.
    QByteArray broken(5, char(0));
    broken[2] = char(0xAB);
    QCOMPARE(CivDriver::bcdToFrequency(broken), qint64(-1));

    QCOMPARE(CivDriver::bcdToFrequency(QByteArray()), qint64(-1));
    QCOMPARE(CivDriver::bcdToFrequency(QByteArray(3, char(0))), qint64(-1));
}

void TestCiv::theFrameCarriesBothAddresses()
{
    // Preambolo doppio, destinatario, mittente, carico, chiusura. Sul bus
    // tutti sentono tutti: senza i due indirizzi non si saprebbe né a chi
    // parla un telaio né chi l'ha mandato.
    const QByteArray frame = CivDriver::buildFrame(0x94, QByteArray(1, char(0x03)));

    QCOMPARE(frame.size(), 6);
    QCOMPARE(static_cast<quint8>(frame.at(0)), quint8(0xFE));
    QCOMPARE(static_cast<quint8>(frame.at(1)), quint8(0xFE));
    QCOMPARE(static_cast<quint8>(frame.at(2)), quint8(0x94));   // la radio
    QCOMPARE(static_cast<quint8>(frame.at(3)), quint8(0xE0));   // il computer
    QCOMPARE(static_cast<quint8>(frame.at(4)), quint8(0x03));
    QCOMPARE(static_cast<quint8>(frame.at(5)), quint8(0xFD));
}

void TestCiv::theAddressNamesTheModel()
{
    QCOMPARE(CivDriver::modelFromAddress(0x94), QStringLiteral("IC-7300"));
    QCOMPARE(CivDriver::modelFromAddress(0x98), QStringLiteral("IC-7610"));
    QCOMPARE(CivDriver::modelFromAddress(0x8E), QStringLiteral("IC-7851"));

    // Una Icom a un indirizzo che non conosciamo si usa lo stesso: il modello
    // è un'etichetta, il protocollo è quello.
    QCOMPARE(CivDriver::modelFromAddress(0x42), QStringLiteral("Icom 0x42"));

    // E l'indirizzo dell'IC-7300 dev'essere il primo che si prova: è la radio
    // più diffusa, e sondarne otto costa una attesa ciascuna.
    QCOMPARE(CivDriver::candidateAddresses().first(), quint8(0x94));
}

void TestCiv::everyModeSurvivesTheRoundTrip()
{
    const DemodMode modes[] = {
        DemodMode::Lsb, DemodMode::Usb, DemodMode::Am,
        DemodMode::Cw, DemodMode::Cwr, DemodMode::Fm, DemodMode::Nfm,
    };

    for (DemodMode mode : modes) {
        const quint8 code = CivDriver::codeFromMode(mode);
        QVERIFY2(CivDriver::modeFromCode(code) == mode,
                 qPrintable(QStringLiteral("il modo %1 torna come %2")
                                .arg(static_cast<int>(mode))
                                .arg(static_cast<int>(CivDriver::modeFromCode(code)))));
    }
}

void TestCiv::theDataModesStayOnSideband()
{
    // Su una Icom i modi dati non hanno un codice proprio: si trasmette in
    // banda laterale con l'ingresso audio USB, e il modo resta USB o LSB.
    // Mandare il codice della RTTY porterebbe la radio dove non si voleva, e
    // il segnale uscirebbe da un'altra parte.
    QCOMPARE(CivDriver::codeFromMode(DemodMode::DigU), quint8(0x01));
    QCOMPARE(CivDriver::codeFromMode(DemodMode::DigL), quint8(0x00));

    // In lettura invece la RTTY si riconosce, perché la radio può essere in
    // quel modo per conto suo.
    QCOMPARE(CivDriver::modeFromCode(0x04), DemodMode::DigL);
    QCOMPARE(CivDriver::modeFromCode(0x08), DemodMode::DigU);

    // Un codice che non conosciamo non deve produrre un modo a caso.
    QCOMPARE(CivDriver::modeFromCode(0x7F), DemodMode::Usb);
}

QTEST_MAIN(TestCiv)
#include "tst_civ.moc"
