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
// In coda tre prove su una connessione vera, verso un finto server in tre
// versioni: `rigctld` pieno, un rigctl minimo — che è quello che espone
// DECODIUM 4, e parla solo la forma corta del protocollo — e qualcosa che
// ascolta su quella porta senza essere un CAT. Sono i tre casi che si trovano
// davvero, e il secondo è quello che ha fatto nascere il driver a due
// dialetti.

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

/// Un server rigctl finto, in tre versioni. Sono le tre che si incontrano.
///
///   Pieno    — `rigctld` vero: capisce il prefisso `+`, risponde in forma
///              estesa, sa dire le proprie capability e l'S-meter.
///   Minimo   — il protocollo corto e basta: i soli valori, una riga per
///              ciascuno, nessun esito, `+` ignorato, `dump_caps` e `STRENGTH`
///              non implementati. È quello che espone DECODIUM 4, ed è il
///              motivo per cui questo driver deve parlare due dialetti.
///   Muto     — qualcosa ascolta su quella porta, ma non è un CAT: `RPRT -4`
///              a tutto, frequenza compresa.
enum class Flavour { Full, Minimal, NoPtt, Mute };

class FakeRigctld : public QObject
{
    Q_OBJECT

public:
    explicit FakeRigctld(Flavour flavour, QObject *parent = nullptr)
        : QObject(parent)
        , m_flavour(flavour)
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
        if (m_flavour == Flavour::Mute) {
            socket->write("RPRT -4\n");
            return;
        }

        const bool extended = line.startsWith('+') && m_flavour == Flavour::Full;
        // Il server minimo il `+` lo ignora e basta: è quello che fa quello
        // vero di DECODIUM 4, e il driver deve accorgersene dalla forma della
        // risposta invece che da una dichiarazione.
        const QByteArray command = line.startsWith('+') ? line.mid(1) : line;

        if (command == "\\dump_caps") {
            if (m_flavour != Flavour::Full) {
                socket->write("RPRT -11\n");   // non implementato
                return;
            }
            socket->write("Caps dump for model:\t1035\n"
                          "Model name:\tFT-991A\n"
                          "Mfg name:\tYaesu\n"
                          "RPRT 0\n");
            return;
        }
        if (command == "\\get_freq") {
            socket->write(extended ? "Frequency: 14074000\nRPRT 0\n" : "14074000\n");
            return;
        }
        if (command == "\\get_mode") {
            socket->write(extended ? "Mode: PKTUSB\nPassband: 3000\nRPRT 0\n"
                                   : "PKTUSB\n3000\n");
            return;
        }
        if (command == "\\get_ptt") {
            if (m_flavour == Flavour::NoPtt) {
                socket->write("RPRT -11\n");
                return;
            }
            socket->write(extended ? "PTT: 0\nRPRT 0\n" : "0\n");
            return;
        }
        if (command == "\\get_level STRENGTH") {
            if (m_flavour != Flavour::Full) {
                socket->write("RPRT -11\n");
                return;
            }
            socket->write(extended ? "Level Value: 12\nRPRT 0\n" : "12\n");
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
    Flavour m_flavour;
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
    explicit DaemonThread(Flavour flavour)
        : m_flavour(flavour)
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
        FakeRigctld daemon(m_flavour);
        m_port = daemon.listen();
        m_ready.release();
        if (m_port != 0)
            exec();
    }

private:
    Flavour m_flavour;
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
    void aFastPttPollDoesNotNeedTheSlowRadioState();
    void theMinimalDialectWorksToo();
    void aRadioWithoutPttRemainsConnectedButDoesNotClaimRx();
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
    DaemonThread daemon(Flavour::Full);
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
    QVERIFY(state.pttKnown);
    QCOMPARE(state.signalDbm, RigctldDriver::dbmFromStrengthDb(12));

    QVERIFY(driver.setFrequency(7074000));
    QVERIFY(driver.setMode(DemodMode::Usb));
    QVERIFY(driver.setPtt(false));

    driver.close();
    QVERIFY(!driver.isOpen());
    daemon.stop();
}

void TestRigctld::aFastPttPollDoesNotNeedTheSlowRadioState()
{
    DaemonThread daemon(Flavour::Full);
    QVERIFY(daemon.start());

    RigctldDriver driver;
    const int result = driver.probe(daemon.endpoint());
    if (result != 0) {
        driver.close();
        daemon.stop();
        QCOMPARE(result, 0);
    }

    CatState state;
    state.frequencyHz = 12345678;
    state.mode = DemodMode::Cw;
    QVERIFY(driver.pollPtt(state));
    QVERIFY(state.pttKnown);
    QVERIFY(!state.transmitting);
    // Il campione rapido non deve far passare il VFO dal percorso lento.
    QCOMPARE(state.frequencyHz, 12345678LL);
    QCOMPARE(state.mode, DemodMode::Cw);

    driver.close();
    daemon.stop();
}

void TestRigctld::theMinimalDialectWorksToo()
{
    // Il caso che ha fatto nascere questo pezzo di codice. «rigctl_net» non è
    // solo `rigctld`: DECODIUM 4 tiene la porta seriale della radio ed espone
    // un server rigctl minimo, che del protocollo implementa la parte corta —
    // i soli valori, nessun esito, `+` ignorato, `dump_caps` assente. Una
    // prima stesura pretendeva la forma estesa e un `dump_caps` riuscito: su
    // quel server non trovava niente, e il CAT restava irraggiungibile con la
    // radio accesa a mezzo metro.
    DaemonThread daemon(Flavour::Minimal);
    QVERIFY(daemon.start());

    RigctldDriver driver;
    const int result = driver.probe(daemon.endpoint());
    if (result != 0) {
        daemon.stop();
        QCOMPARE(result, 0);
    }
    QVERIFY(driver.isOpen());

    // Senza `dump_caps` non si può sapere che radio sia, e non ci si inventa
    // un modello: si dice quello che si sa.
    QVERIFY(!driver.radioModel().isEmpty());

    CatState state;
    QVERIFY(driver.poll(state));
    QCOMPARE(state.frequencyHz, 14074000LL);
    // Il modo e la larghezza arrivano come due righe nude: contarle male
    // significa leggere «3000» come modo alla domanda dopo, e da lì in poi
    // ogni risposta è quella della domanda precedente.
    QCOMPARE(state.mode, DemodMode::DigU);
    QCOMPARE(state.transmitting, false);
    QVERIFY(state.pttKnown);

    // Questo server l'S-meter non ce l'ha: si smette di chiederlo, e il
    // livello resta quello misurato sull'audio.
    QVERIFY(!std::isfinite(state.signalDbm));

    QVERIFY(driver.setFrequency(7074000));
    QVERIFY(driver.setPtt(false));

    driver.close();
    daemon.stop();
}

void TestRigctld::aRadioWithoutPttRemainsConnectedButDoesNotClaimRx()
{
    DaemonThread daemon(Flavour::NoPtt);
    QVERIFY(daemon.start());

    RigctldDriver driver;
    const int result = driver.probe(daemon.endpoint());
    if (result != 0) {
        daemon.stop();
        QCOMPARE(result, 0);
    }

    CatState state;
    QVERIFY(driver.poll(state));
    QCOMPARE(state.frequencyHz, 14074000LL);
    QVERIFY(!state.pttKnown);
    QVERIFY(!state.transmitting);

    driver.close();
    daemon.stop();
}

void TestRigctld::whatIsNotARigctldIsNotARadio()
{
    // Qualcosa ascolta su quella porta e non è un CAT. La prova è la
    // frequenza: è l'unica cosa che ogni rigctl implementa — `dump_caps` no,
    // e pretenderlo escluderebbe metà dei server veri.
    DaemonThread notADaemon(Flavour::Mute);
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
