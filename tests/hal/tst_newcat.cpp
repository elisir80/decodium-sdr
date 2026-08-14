// SPDX-License-Identifier: GPL-3.0-or-later
// La traduzione fra il protocollo newcat e il nostro vocabolario.
//
// È la parte del backend `audiorig` che si può verificare senza una radio
// attaccata, ed è anche quella che si sbaglia: una tabella di quattordici
// codici in cui DATA-USB e RTTY-USB finiscono nello stesso posto e CW-R sta
// tre righe più in là di CW. Un errore qui non fa fallire niente — manda in
// aria il modo sbagliato.

#include "hal/backends/audiorig/NewcatDriver.h"

#include <QTest>

using namespace dsdr;
using namespace dsdr::hal::audiorig;

class TestNewcat : public QObject
{
    Q_OBJECT

private slots:
    void theIdentityReplyNamesTheRadio();
    void anIdentityThatIsNotOneIsRefused();
    void everyModeSurvivesTheRoundTrip();
    void theDataModesLandOnTheDigitalOnes();
    void whatARadioCannotDoFallsBackInsteadOfFailing();
    void aRadioOnTheBenchAnswers();
};

void TestNewcat::theIdentityReplyNamesTheRadio()
{
    // La radio di riferimento del progetto, quella sul banco.
    QCOMPARE(NewcatDriver::modelFromId("ID0670;"), QStringLiteral("FT-991A"));
    QCOMPARE(NewcatDriver::modelFromId("ID0650;"), QStringLiteral("FT-891"));

    // Una Yaesu che risponde newcat ma non conosciamo si usa lo stesso,
    // dicendo il codice: rifiutarla vorrebbe dire non funzionare su un
    // modello nuovo solo perché è uscito dopo di noi.
    QCOMPARE(NewcatDriver::modelFromId("ID9999;"), QStringLiteral("Yaesu 9999"));
}

void TestNewcat::anIdentityThatIsNotOneIsRefused()
{
    // Sondando le porte si incontra di tutto: un mouse, un modem, un lettore
    // di codici a barre. Nessuno di questi deve diventare una radio.
    QVERIFY(NewcatDriver::modelFromId("").isEmpty());
    QVERIFY(NewcatDriver::modelFromId("OK;").isEmpty());
    QVERIFY(NewcatDriver::modelFromId("ID06;").isEmpty());
    QVERIFY(NewcatDriver::modelFromId("rumore casuale").isEmpty());
}

void TestNewcat::everyModeSurvivesTheRoundTrip()
{
    // Andata e ritorno per i modi che esistono da entrambe le parti: se un
    // codice si perde per strada, l'operatore chiede USB e si ritrova in AM.
    const DemodMode modes[] = {
        DemodMode::Lsb, DemodMode::Usb, DemodMode::Cw, DemodMode::Cwr,
        DemodMode::Fm, DemodMode::Nfm, DemodMode::Am,
        DemodMode::DigU, DemodMode::DigL,
    };

    for (DemodMode mode : modes) {
        const char code = NewcatDriver::codeFromMode(mode);
        QVERIFY2(NewcatDriver::modeFromCode(code) == mode,
                 qPrintable(QStringLiteral("il modo %1 torna indietro come %2 (codice %3)")
                                .arg(static_cast<int>(mode))
                                .arg(static_cast<int>(NewcatDriver::modeFromCode(code)))
                                .arg(code)));
    }
}

void TestNewcat::theDataModesLandOnTheDigitalOnes()
{
    // DATA-USB e RTTY-USB sono due modi della radio e un modo solo da noi:
    // hanno la banda larga e nessuna elaborazione della voce, che è
    // esattamente ciò che DigU significa.
    QCOMPARE(NewcatDriver::modeFromCode('C'), DemodMode::DigU);   // DATA-USB
    QCOMPARE(NewcatDriver::modeFromCode('9'), DemodMode::DigU);   // RTTY-USB
    QCOMPARE(NewcatDriver::modeFromCode('8'), DemodMode::DigL);   // DATA-LSB
    QCOMPARE(NewcatDriver::modeFromCode('6'), DemodMode::DigL);   // RTTY-LSB

    // E nella direzione della trasmissione si sceglie DATA, non RTTY: è quello
    // che vuole chi manda FT8 dal codec USB.
    QCOMPARE(NewcatDriver::codeFromMode(DemodMode::DigU), 'C');
    QCOMPARE(NewcatDriver::codeFromMode(DemodMode::DigL), '8');
}

void TestNewcat::whatARadioCannotDoFallsBackInsteadOfFailing()
{
    // DSB e IQ non esistono su una radio tradizionale. Il canale però è già
    // stato creato, e lasciarlo senza modo sarebbe peggio che sceglierne uno
    // vicino: si va in USB e si tira avanti.
    QCOMPARE(NewcatDriver::codeFromMode(DemodMode::Dsb), '2');
    QCOMPARE(NewcatDriver::codeFromMode(DemodMode::Iq), '2');

    // La SAM diventa AM: è la stessa emissione, con una demodulazione diversa
    // che la radio fa a modo suo.
    QCOMPARE(NewcatDriver::codeFromMode(DemodMode::Sam), '5');

    // Un codice che non conosciamo non deve produrre un modo a caso.
    QCOMPARE(NewcatDriver::modeFromCode('Z'), DemodMode::Usb);
}

void TestNewcat::aRadioOnTheBenchAnswers()
{
    // Con una radio attaccata questo test parla con lei davvero. Senza, si
    // salta: la CI non ha un FT-991A, e un test che fallisse per assenza di
    // hardware insegnerebbe a ignorare i fallimenti.
    //
    //   DSDR_NEWCAT_PORT=COM5 ./build/bin/tst_newcat
    const QString port = qEnvironmentVariable("DSDR_NEWCAT_PORT");
    if (port.isEmpty())
        QSKIP("nessuna porta indicata in DSDR_NEWCAT_PORT");

    NewcatDriver driver;
    QString found;
    for (int baud : driver.candidateBaudRates()) {
        CatSerialConfig serial;
        serial.baudRate = baud;
        if (driver.open(port, serial)) {
            found = QStringLiteral("%1 a %2 baud").arg(driver.radioModel()).arg(baud);
            break;
        }
        qInfo() << baud << "baud:" << driver.errorString();
    }

    QVERIFY2(!found.isEmpty(),
             qPrintable(QStringLiteral("nessuna risposta su %1").arg(port)));
    qInfo() << "radio trovata:" << found;

    CatState state;
    QVERIFY2(driver.poll(state), "la radio non risponde alla lettura di stato");
    qInfo() << "frequenza" << state.frequencyHz
            << "modo" << static_cast<int>(state.mode)
            << "TX" << state.transmitting
            << "S-meter" << state.sMeterRaw;

    QVERIFY2(state.frequencyHz > 0, "frequenza non letta");
    driver.close();
}

QTEST_MAIN(TestNewcat)
#include "tst_newcat.moc"
