// SPDX-License-Identifier: GPL-3.0-or-later
// La sonda CAT lavora su un thread proprio. Il cambio sorgente non può
// trasformare una risposta lenta di rigctld in un blocco dell'interfaccia.

#include "hal/backends/audiorig/AudiorigBackend.h"

#include <QElapsedTimer>
#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QTimer>

#include <memory>

using namespace dsdr::hal::audiorig;

class TestAudiorigDiscovery : public QObject
{
    Q_OBJECT

private slots:
    void cancellingSlowProbeDoesNotBlockDestruction();
};

void TestAudiorigDiscovery::cancellingSlowProbeDoesNotBlockDestruction()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    bool peerConnected = false;

    // Un rigctld che accetta la connessione ma tarda a rispondere: è la forma
    // controllabile del timeout che l'operatore incontrava cambiando sorgente.
    connect(&server, &QTcpServer::newConnection, &server, [&server, &peerConnected] {
        while (server.hasPendingConnections()) {
            QTcpSocket *peer = server.nextPendingConnection();
            peerConnected = true;
            connect(peer, &QTcpSocket::readyRead, peer, [peer] {
                const QByteArray request = peer->readAll();
                if (request.contains("\\get_freq")) {
                    QTimer::singleShot(400, peer, [peer] {
                        if (peer->state() == QAbstractSocket::ConnectedState)
                            peer->write("Frequency: 14074000\\nRPRT 0\\n");
                    });
                } else if (request.contains("\\dump_caps")) {
                    peer->write("Model name: Test rig\\nRPRT 0\\n");
                }
            });
        }
    });

    qputenv("DSDR_AUDIORIG_NO_PROBE", "1");
    qputenv("DSDR_RIGCTLD", QStringLiteral("127.0.0.1:%1").arg(server.serverPort()).toUtf8());

    auto backend = std::make_unique<AudiorigBackend>();
    backend->startDiscovery();
    QTRY_VERIFY_WITH_TIMEOUT(peerConnected, 1000);

    // Prima della correzione il distruttore attendeva qui la risposta lenta
    // del CAT. Ora annulla la consegna e restituisce immediatamente il thread
    // grafico; la sonda termina da sola in sottofondo.
    QElapsedTimer elapsed;
    elapsed.start();
    backend.reset();
    QVERIFY2(elapsed.elapsed() < 250,
             qPrintable(QStringLiteral("distruzione backend bloccata per %1 ms")
                            .arg(elapsed.elapsed())));

    // Lascia terminare la sonda posticipata: il test verifica anche che non
    // serva più un wait sincrono per chiudere correttamente il suo thread.
    QTest::qWait(750);
    qunsetenv("DSDR_AUDIORIG_NO_PROBE");
    qunsetenv("DSDR_RIGCTLD");
}

QTEST_MAIN(TestAudiorigDiscovery)
#include "tst_audiorig_discovery.moc"
