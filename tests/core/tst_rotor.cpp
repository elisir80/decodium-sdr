// SPDX-License-Identifier: GPL-3.0-or-later
// Il rotore: quello che si può provare senza avere un palo.
//
// Un rotore è la cosa meno provabile che ci sia — c'è un motore in cima a una
// torre, e chi scrive il codice quasi mai ce l'ha sotto mano. Un server finto
// non prova che l'antenna gira: quello lo prova solo l'antenna. Prova tutto il
// resto, che è dove stanno gli errori, e prova soprattutto le tre cose che su
// un palo si pagano care — che non si muova da solo, che non lo si martelli, e
// che FERMA fermi davvero.
#include "RotctldMockServer.h"

#include "core/RotorController.h"

#include <QCoreApplication>
#include <QTcpServer>
#include <QTcpSocket>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTest>

using namespace dsdr;
using namespace dsdr::core;
using namespace dsdr::test;

namespace {

/// Fa girare il ciclo di eventi finché una condizione si avvera, o finché
/// scade il tempo. Il dialogo con il rotore è asincrono: aspettare un tempo
/// fisso renderebbe i test lenti quando va bene e traballanti quando va male.
template <typename Predicate>
bool waitFor(Predicate predicate, int timeoutMs = 3000)
{
    QElapsedTimer clock;
    clock.start();
    while (!predicate() && clock.elapsed() < timeoutMs)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return predicate();
}

} // namespace

class TestRotor : public QObject
{
    Q_OBJECT

private slots:
    void siCollegaAUnRotctldVero();
    void siCollegaAncheAUnServerMinimo();
    void laPosizioneArrivaEsiAggiorna();
    void puntareMandaIlComandoUnaVoltaSola();
    void fermaCancellaQuelCheEraInCoda();
    void nonSiMuoveDaSolo();
    void seIlServerTaceNonSiRestaAppesi();
    void seIlCollegamentoCadeLoDice();
    void unServerRigctlNonEUnRotore();
};

void TestRotor::siCollegaAUnRotctldVero()
{
    RotctldMockServer server(RotctldMockServer::Dialect::Extended);
    QVERIFY(server.listen());
    server.setPosition(137.0, 0.0);

    RotorController rotor;
    rotor.setPort(server.port());
    rotor.connectToRotor();

    QVERIFY(waitFor([&] { return rotor.isConnected(); }));

    // Con il dialetto esteso si può chiedere che rotore sia. È l'unico modo di
    // sapere se ha l'elevazione senza domandarlo a chi opera.
    QVERIFY(waitFor([&] { return !rotor.model().isEmpty(); }));
    QCOMPARE(rotor.model(), QStringLiteral("GS-232B"));
    QVERIFY2(rotor.hasElevation(), "il finto rotore dichiara 180 gradi di elevazione");
}

void TestRotor::siCollegaAncheAUnServerMinimo()
{
    // Non tutto quello che parla rotctl è rotctld: mezzo ecosistema espone un
    // server minimo che del protocollo implementa la parte corta e ignora il
    // `+`. Chi non lo prevede si collega, non riceve niente, e resta a
    // guardare un indicatore fermo.
    RotctldMockServer server(RotctldMockServer::Dialect::Short);
    QVERIFY(server.listen());
    server.setPosition(42.5, 0.0);

    RotorController rotor;
    rotor.setPort(server.port());
    rotor.connectToRotor();

    QVERIFY2(waitFor([&] { return rotor.isConnected(); }),
             "il dialetto corto non è stato riconosciuto");
    QVERIFY(waitFor([&] { return std::abs(rotor.azimuth() - 42.5) < 0.1; }));

    // E qui non si inventa un'elevazione: il server minimo non sa dire che
    // rotore sia, e un indicatore fermo sullo zero sembrerebbe un dato.
    QVERIFY(!rotor.hasElevation());
    QVERIFY(rotor.model().isEmpty());
}

void TestRotor::laPosizioneArrivaEsiAggiorna()
{
    RotctldMockServer server(RotctldMockServer::Dialect::Extended);
    QVERIFY(server.listen());
    server.setPosition(10.0, 0.0);

    RotorController rotor;
    rotor.setPort(server.port());
    rotor.setPollMs(200);
    rotor.connectToRotor();
    QVERIFY(waitFor([&] { return std::abs(rotor.azimuth() - 10.0) < 0.1; }));

    server.setPosition(200.0, 30.0);
    QVERIFY2(waitFor([&] { return std::abs(rotor.azimuth() - 200.0) < 0.1; }),
             "la posizione non viene riletta");
}

void TestRotor::puntareMandaIlComandoUnaVoltaSola()
{
    RotctldMockServer server(RotctldMockServer::Dialect::Extended);
    QVERIFY(server.listen());

    RotorController rotor;
    rotor.setPort(server.port());
    rotor.connectToRotor();
    QVERIFY(waitFor([&] { return rotor.isConnected(); }));

    rotor.pointTo(275.0);
    QVERIFY(waitFor([&] { return server.moveCount() == 1; }));
    QVERIFY(std::abs(server.targetAzimuth() - 275.0) < 0.1);

    // Dieci comandi di fila, come li manderebbe qualcuno che trascina un
    // cursore. Ne deve partire uno solo: ogni comando di posizione chiude dei
    // relè in un controller, e quelli hanno una vita finita.
    for (int i = 0; i < 10; ++i)
        rotor.pointTo(100.0 + i);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 200);

    QCOMPARE(server.moveCount(), 1);
    QVERIFY2(!rotor.trouble().isEmpty(),
             "il comando è stato ignorato e nessuno lo dice");
}

void TestRotor::fermaCancellaQuelCheEraInCoda()
{
    RotctldMockServer server(RotctldMockServer::Dialect::Extended);
    QVERIFY(server.listen());

    RotorController rotor;
    rotor.setPort(server.port());
    rotor.connectToRotor();
    QVERIFY(waitFor([&] { return rotor.isConnected(); }));

    rotor.pointTo(300.0);
    rotor.stop();
    QVERIFY(waitFor([&] { return server.stopCount() == 1; }));

    // Il bersaglio si azzera: senza, l'indicatore continuerebbe a dire «in
    // movimento» verso un punto che nessuno vuole più raggiungere.
    QCOMPARE(rotor.targetAzimuth(), -1.0);
    QVERIFY(!rotor.isMoving());
}

void TestRotor::nonSiMuoveDaSolo()
{
    RotctldMockServer server(RotctldMockServer::Dialect::Extended);
    QVERIFY(server.listen());

    RotorController rotor;
    rotor.setPort(server.port());
    rotor.connectToRotor();
    QVERIFY(waitFor([&] { return rotor.isConnected(); }));

    // Si lascia girare tutto per un po', con le letture che vanno e vengono.
    // Nessun comando di movimento deve partire: collegarsi a un rotore non è
    // chiedergli di andare da qualche parte, e un'antenna che parte da sola
    // all'avvio del programma è una sorpresa su un palo.
    QElapsedTimer idle;
    idle.start();
    while (idle.elapsed() < 900)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);

    QCOMPARE(server.moveCount(), 0);
    QCOMPARE(server.stopCount(), 0);
}

void TestRotor::seIlServerTaceNonSiRestaAppesi()
{
    RotctldMockServer server(RotctldMockServer::Dialect::Extended);
    QVERIFY(server.listen());

    RotorController rotor;
    rotor.setPort(server.port());
    rotor.connectToRotor();
    QVERIFY(waitFor([&] { return rotor.isConnected(); }));

    // Un server che smette di rispondere non chiude il socket: resta lì. Senza
    // un guinzaglio la coda si bloccherebbe per sempre, e l'indicatore
    // resterebbe sul valore di mezz'ora prima sembrando giusto.
    server.goSilent();
    rotor.pointTo(180.0);

    QVERIFY2(waitFor([&] { return !rotor.trouble().isEmpty(); }, 6000),
             "il rotore tace e nessuno se ne accorge");
}

void TestRotor::seIlCollegamentoCadeLoDice()
{
    RotctldMockServer server(RotctldMockServer::Dialect::Extended);
    QVERIFY(server.listen());
    server.setPosition(0.0, 0.0);

    RotorController rotor;
    rotor.setPort(server.port());
    rotor.connectToRotor();
    QVERIFY(waitFor([&] { return rotor.isConnected(); }));
    QVERIFY(waitFor([&] { return rotor.azimuth() >= 0.0; }));

    rotor.pointTo(180.0);
    QVERIFY(waitFor([&] { return rotor.isMoving(); }));

    // Il rotore sta ancora andando e da qui non lo si ferma più. Dirlo è
    // l'unica cosa utile: chi opera saprà se deve scendere in torre.
    server.hangUp();
    QVERIFY2(waitFor([&] { return !rotor.isConnected(); }),
             "il collegamento è caduto e il controller non se n'è accorto");
    QVERIFY2(!rotor.trouble().isEmpty(),
             "il rotore gira e nessuno dice che non lo comandiamo più");
}

void TestRotor::unServerRigctlNonEUnRotore()
{
    // Il caso vero, e capita: 4532 è rigctld, 4533 è rotctld, e su una
    // stazione ci può stare un server rigctl sulla porta che ci si aspettava
    // fosse del rotore. Chiedere una posizione a una radio non dà una
    // posizione: dà un rifiuto, e collegarsi lo stesso vorrebbe dire un
    // indicatore che non si muoverà mai senza che nessuno dica perché.
    QTcpServer radio;
    QVERIFY(radio.listen(QHostAddress::LocalHost, 0));

    QObject::connect(&radio, &QTcpServer::newConnection, &radio, [&radio] {
        QTcpSocket *client = radio.nextPendingConnection();
        QObject::connect(client, &QTcpSocket::readyRead, client, [client] {
            client->readAll();
            // Quello che risponde un rigctl a un comando che non conosce.
            client->write(QByteArrayLiteral("RPRT -11\n"));
        });
    });

    RotorController rotor;
    rotor.setPort(radio.serverPort());
    rotor.connectToRotor();

    QVERIFY2(waitFor([&] { return !rotor.trouble().isEmpty(); }, 5000),
             "si è collegato a una radio credendola un rotore");
    QVERIFY(!rotor.isConnected());
}

QTEST_MAIN(TestRotor)
#include "tst_rotor.moc"
