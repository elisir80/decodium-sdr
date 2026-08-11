// SPDX-License-Identifier: GPL-3.0-or-later
// Il dialogo con `rigctld`, verificato dove si sbaglia.
//
// Tre punti, e nessuno dei tre richiede un demone acceso:
//
//   • l'indirizzo, perché `host:porta` con dentro un IPv6 è ambiguo e chi lo
//     sbaglia si connette a una porta che non aveva scelto;
//   • le risposte estese, perché è da lì che si estraggono frequenza e modo, e
//     un campo letto male non fallisce — riporta un numero plausibile;
//   • l'S-meter, che arriva in decibel rispetto a S9 e deve restare tale:
//     sbagliare quel riferimento significa passare rapporti sbagliati a
//     persone che poi li ripassano ad altri.
//
// L'ultima prova apre una connessione vera verso un finto rigctld di quindici
// righe: serve a presidiare l'unica cosa che le funzioni pure non vedono, cioè
// che la sonda **non** si attacchi a qualcosa che non è rigctld — e il primo
// «qualcosa» in ordine di probabilità è il server rigctl di DECODIUM SDR, che
// sta sulla stessa porta di fabbrica.

#include "hal/backends/audiorig/RigctldDriver.h"

#include <QSemaphore>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QThread>

#include <atomic>
#include <cmath>

using namespace dsdr;
using namespace dsdr::hal::audiorig;

namespace {

/// Un rigctld finto: risponde in forma estesa a quello che sa, `RPRT -4` al
/// resto. `answersCaps` a false imita il nostro server rigctl, che di
/// `\dump_caps` non sa nulla.
class FakeRigctld : public QObject
{
    Q_OBJECT

public:
    explicit FakeRigctld(bool answersCaps, QObject *parent = nullptr)
        : QObject(parent)
        , m_answersCaps(answersCaps)
    {
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (m_server.hasPendingConnections()) {
                QTcpSocket *socket = m_server.nextPendingConnection();
                connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
                    while (socket->canReadLine())
                        reply(socket, socket->readLine().trimmed());
                });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
    }

    /// La porta su cui ascolta, o zero.
    quint16 listen()
    {
        return m_server.listen(QHostAddress::LocalHost) ? m_server.serverPort() : 0;
    }

private:
    void reply(QTcpSocket *socket, const QByteArray &line)
    {
        // Il driver manda sempre il prefisso `+`: senza, le risposte sarebbero
        // valori nudi e chi legge dovrebbe saperne a memoria l'ordine.
        if (!line.startsWith('+')) {
            socket->write("RPRT -4\n");
            return;
        }
        const QByteArray command = line.mid(1);

        if (command == "\\dump_caps") {
            if (!m_answersCaps) {
                socket->write("RPRT -4\n");
                return;
            }
            socket->write("Caps dump for model:\t1035\n"
                          "Model name:\tFT-991A\n"
                          "Mfg name:\tYaesu\n"
                          "RPRT 0\n");
            return;
        }
        if (command == "\\get_freq") {
            socket->write("Frequency: 14074000\nRPRT 0\n");
            return;
        }
        if (command == "\\get_mode") {
            socket->write("Mode: PKTUSB\nPassband: 3000\nRPRT 0\n");
            return;
        }
        if (command == "\\get_ptt") {
            socket->write("PTT: 0\nRPRT 0\n");
            return;
        }
        if (command == "\\get_level STRENGTH") {
            socket->write("Level Value: 12\nRPRT 0\n");
            return;
        }
        if (command.startsWith("\\set_freq") || command.startsWith("\\set_mode")
            || command.startsWith("\\set_ptt")) {
            socket->write("RPRT 0\n");
            return;
        }
        socket->write("RPRT -4\n");
    }

    QTcpServer m_server;
    bool m_answersCaps;
};

/// Il demone finto su un thread suo.
///
/// Non è un vezzo: il driver aspetta le risposte bloccando il proprio thread,
/// e un server che vivesse lì non arriverebbe mai ad accettare la connessione.
/// Il dialogo si bloccherebbe e la prova fallirebbe raccontando che il driver
/// non funziona — mentre a non funzionare sarebbe la prova.
class DaemonThread : public QThread
{
public:
    explicit DaemonThread(bool answersCaps)
        : m_answersCaps(answersCaps)
    {
    }

    /// Avvia e aspetta che sia in ascolto. Falso se la porta non si è aperta.
    bool start()
    {
        QThread::start();
        m_ready.acquire();
        return m_port != 0;
    }

    void stop()
    {
        quit();
        wait();
    }

    QString endpoint() const
    {
        return QStringLiteral("127.0.0.1:") + QString::number(m_port);
    }

protected:
    void run() override
    {
        FakeRigctld daemon(m_answersCaps);
        m_port = daemon.listen();
        m_ready.release();
        if (m_port != 0)
            exec();
    }

private:
    bool m_answersCaps;
    QSemaphore m_ready;
    std::atomic<quint16> m_port{0};
};

} // namespace

class TestRigctld : public QObject
{
    Q_OBJECT

private slots:
    void theAddressSplitsIntoHostAndPort();
    void anIpv6AddressKeepsItsColons();
    void aBrokenAddressIsRefused();
    void theExtendedReplyGivesUpItsFields();
    void theModelComesFromTheCapsDump();
    void everyModeSurvivesTheRoundTrip();
    void theDataModesLandOnThePacketOnes();
    void theStrengthLandsOnTheRightSignalLevel();
    void aFakeDaemonAnswersTheWholePoll();
    void whatIsNotARigctldIsNotARadio();
};

// ── L'indirizzo ──────────────────────────────────────────────────────────

void TestRigctld::theAddressSplitsIntoHostAndPort()
{
    QString host;
    quint16 port = 0;

    QVERIFY(RigctldDriver::splitEndpoint(QStringLiteral("127.0.0.1:4533"), host, port));
    QCOMPARE(host, QStringLiteral("127.0.0.1"));
    QCOMPARE(port, quint16(4533));

    // Senza porta si intende quella di fabbrica del demone: chi scrive solo
    // l'indirizzo di una macchina sta dicendo «il rigctld che sta lì».
    QVERIFY(RigctldDriver::splitEndpoint(QStringLiteral("shack.lan"), host, port));
    QCOMPARE(host, QStringLiteral("shack.lan"));
    QCOMPARE(port, quint16(4532));
}

void TestRigctld::anIpv6AddressKeepsItsColons()
{
    QString host;
    quint16 port = 0;

    // Senza le parentesi quadre i due punti dell'indirizzo sarebbero
    // indistinguibili da quello della porta, e `::1` diventerebbe l'host `:`
    // sulla porta 1 — cioè una connessione da qualche altra parte.
    QVERIFY(RigctldDriver::splitEndpoint(QStringLiteral("[::1]:4532"), host, port));
    QCOMPARE(host, QStringLiteral("::1"));
    QCOMPARE(port, quint16(4532));

    QVERIFY(RigctldDriver::splitEndpoint(QStringLiteral("[fe80::1]"), host, port));
    QCOMPARE(host, QStringLiteral("fe80::1"));
    QCOMPARE(port, quint16(4532));
}

void TestRigctld::aBrokenAddressIsRefused()
{
    QString host;
    quint16 port = 0;

    QVERIFY(!RigctldDriver::splitEndpoint(QString(), host, port));
    QVERIFY(!RigctldDriver::splitEndpoint(QStringLiteral("localhost:zero"), host, port));
    QVERIFY(!RigctldDriver::splitEndpoint(QStringLiteral("localhost:0"), host, port));
    QVERIFY(!RigctldDriver::splitEndpoint(QStringLiteral("localhost:99999"), host, port));
}

// ── Le risposte ──────────────────────────────────────────────────────────

void TestRigctld::theExtendedReplyGivesUpItsFields()
{
    const QByteArray reply = "Frequency: 14074000\nRPRT 0\n";
    QCOMPARE(RigctldDriver::fieldValue(reply, QStringLiteral("Frequency")),
             QStringLiteral("14074000"));

    // Un campo che non c'è dà stringa vuota e non un valore a caso: è così che
    // il driver si accorge che la radio non sa dire l'S-meter.
    QVERIFY(RigctldDriver::fieldValue(reply, QStringLiteral("Level Value")).isEmpty());

    // Due campi nella stessa risposta: il modo e la larghezza arrivano
    // insieme, e prendere il secondo per il primo è l'errore che il protocollo
    // corto rende facile.
    const QByteArray mode = "Mode: USB\nPassband: 2400\nRPRT 0\n";
    QCOMPARE(RigctldDriver::fieldValue(mode, QStringLiteral("Mode")),
             QStringLiteral("USB"));
    QCOMPARE(RigctldDriver::fieldValue(mode, QStringLiteral("Passband")),
             QStringLiteral("2400"));
}

void TestRigctld::theModelComesFromTheCapsDump()
{
    const QByteArray caps = "Caps dump for model:\t1035\n"
                            "Model name:\tFT-991A\n"
                            "Mfg name:\tYaesu\n"
                            "RPRT 0\n";
    QCOMPARE(RigctldDriver::modelFromCaps(caps), QStringLiteral("FT-991A"));

    // Senza il nome del modello resta il costruttore: è meno, ma è vero.
    QCOMPARE(RigctldDriver::modelFromCaps("Mfg name:\tIcom\nRPRT 0\n"),
             QStringLiteral("Icom"));
    QVERIFY(RigctldDriver::modelFromCaps("RPRT -4\n").isEmpty());
}

// ── I modi ───────────────────────────────────────────────────────────────

void TestRigctld::everyModeSurvivesTheRoundTrip()
{
    const DemodMode modes[] = {DemodMode::Usb, DemodMode::Lsb, DemodMode::Cw,
                               DemodMode::Cwr, DemodMode::Am,  DemodMode::Sam,
                               DemodMode::Fm,  DemodMode::Dsb, DemodMode::DigU,
                               DemodMode::DigL};
    for (const DemodMode mode : modes) {
        const QString name = RigctldDriver::nameFromMode(mode);
        QVERIFY2(!name.isEmpty(), qPrintable(QStringLiteral("modo %1 senza nome")
                                                 .arg(static_cast<int>(mode))));
        QCOMPARE(RigctldDriver::modeFromName(name), mode);
    }
}

void TestRigctld::theDataModesLandOnThePacketOnes()
{
    // I modi «pacchetto» sono la banda larga senza elaborazione della voce: è
    // quello che DigU e DigL significano da noi, ed è quello che una radio
    // moderna vuole per le digitali. Mandarle in USB significa parlarci sopra
    // con il compressore acceso.
    QCOMPARE(RigctldDriver::nameFromMode(DemodMode::DigU), QStringLiteral("PKTUSB"));
    QCOMPARE(RigctldDriver::nameFromMode(DemodMode::DigL), QStringLiteral("PKTLSB"));
    QCOMPARE(RigctldDriver::modeFromName(QStringLiteral("PKTUSB")), DemodMode::DigU);

    // Anche l'RTTY è una portante dati, non una voce.
    QCOMPARE(RigctldDriver::modeFromName(QStringLiteral("RTTY")), DemodMode::DigL);
    QCOMPARE(RigctldDriver::modeFromName(QStringLiteral("RTTYR")), DemodMode::DigU);

    // Un modo che Hamlib conosce e noi no non deve fermare niente: si ricade
    // su USB, che è dove la radio quasi sempre già sta.
    QCOMPARE(RigctldDriver::modeFromName(QStringLiteral("ECSSUSB")), DemodMode::Usb);
    QCOMPARE(RigctldDriver::modeFromName(QString()), DemodMode::Usb);
}

// ── L'S-meter ────────────────────────────────────────────────────────────

void TestRigctld::theStrengthLandsOnTheRightSignalLevel()
{
    // S9 è −73 dBm: la definizione IARU sotto i 30 MHz, e il punto a cui
    // hamlib riferisce la propria misura.
    QCOMPARE(RigctldDriver::dbmFromStrengthDb(0), -73.0);

    // Un decibel resta un decibel: è tutto quello che questa conversione deve
    // garantire, ed è la ragione per cui non passa dalla scala grezza 0…255.
    // Quella si ferma a −60 dBm, cioè S9+13: un locale a S9+40 ci arriverebbe
    // schiacciato contro il fondo scala, e nessuno se ne accorgerebbe — il
    // numero resterebbe plausibile.
    QCOMPARE(RigctldDriver::dbmFromStrengthDb(20) - RigctldDriver::dbmFromStrengthDb(0),
             20.0);
    QCOMPARE(RigctldDriver::dbmFromStrengthDb(60), -13.0);   // S9+60
    QCOMPARE(RigctldDriver::dbmFromStrengthDb(-48), -121.0); // S1
}

// ── Il dialogo vero ──────────────────────────────────────────────────────

void TestRigctld::aFakeDaemonAnswersTheWholePoll()
{
    DaemonThread daemon(true);
    QVERIFY(daemon.start());

    RigctldDriver driver;
    const int result = driver.probe(daemon.endpoint());
    if (result != 0) {
        daemon.stop();
        QCOMPARE(result, 0);
    }
    QVERIFY(driver.isOpen());
    QCOMPARE(driver.radioModel(), QStringLiteral("FT-991A"));

    CatState state;
    QVERIFY(driver.poll(state));
    QCOMPARE(state.frequencyHz, 14074000LL);
    QCOMPARE(state.mode, DemodMode::DigU);
    QCOMPARE(state.transmitting, false);
    QCOMPARE(state.signalDbm, RigctldDriver::dbmFromStrengthDb(12));

    QVERIFY(driver.setFrequency(7074000));
    QVERIFY(driver.setMode(DemodMode::Usb));
    QVERIFY(driver.setPtt(false));

    driver.close();
    QVERIFY(!driver.isOpen());
    daemon.stop();
}

void TestRigctld::whatIsNotARigctldIsNotARadio()
{
    // Il server rigctl di DECODIUM SDR sta sulla stessa porta di fabbrica del
    // demone e risponde al protocollo corto, ma non sa nulla di `dump_caps`.
    // Senza questa verifica il programma si attaccherebbe a sé stesso: si
    // vedrebbe comparire una radio che non esiste, e ogni comando tornerebbe
    // indietro da dove è partito.
    DaemonThread notADaemon(false);
    QVERIFY(notADaemon.start());

    RigctldDriver driver;
    const int result = driver.probe(notADaemon.endpoint());
    const bool open = driver.isOpen();
    const QString error = driver.errorString();
    // Il thread si ferma prima delle verifiche: una QCOMPARE che fallisce esce
    // dalla funzione, e un QThread distrutto mentre gira è un errore fatale
    // che seppellisce quello vero sotto un altro messaggio.
    driver.close();
    notADaemon.stop();

    QCOMPARE(result, -1);
    QVERIFY(!open);
    QVERIFY(!error.isEmpty());

    // E un indirizzo dove non ascolta nessuno non deve costare più di una
    // connessione rifiutata.
    RigctldDriver nowhere;
    QCOMPARE(nowhere.probe(QStringLiteral("127.0.0.1:1")), -1);
}

QTEST_MAIN(TestRigctld)
#include "tst_rigctld.moc"
