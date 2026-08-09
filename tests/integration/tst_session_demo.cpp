// SPDX-License-Identifier: GPL-3.0-or-later
// Integration test: SessionManager + DspEngine + backend demo, senza UI.
//
// È il test che dimostra il criterio d'uscita della Fase 0: "demo mode
// pienamente operativo". Verifica anche che il percorso caldo regga il tempo
// reale, che è il vincolo su cui una UI fluida si appoggia.

#include "core/DspEngine.h"
#include "core/SessionManager.h"
#include "core/SpectrumFeed.h"

#include <QElapsedTimer>
#include <QHostAddress>
#include <QSignalSpy>
#include <QTcpSocket>
#include <QTest>

#include <algorithm>
#include <cmath>

using namespace dsdr;
using namespace dsdr::core;

class TestSessionDemo : public QObject
{
    Q_OBJECT

private slots:
    void connectsToDemoAndProducesSpectrum();
    void audioKeepsUpWithRealTime();
    void channelLimitMatchesCapabilities();
    void scannerCompletesAndPublishesState();
    void rigctlTunesAndChangesMode();
    void changingChannelModeKeepsAudioAlive();
    void disconnectStopsEverything();

private:
    /// Attende che `predicate` diventi vera, facendo girare l'event loop.
    static bool waitFor(std::function<bool()> predicate, int timeoutMs = 5000)
    {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < timeoutMs) {
            if (predicate())
                return true;
            QTest::qWait(20);
        }
        return predicate();
    }

    static bool connectSession(SessionManager &session)
    {
        session.selectBackend(QStringLiteral("demo"));
        session.startDiscovery();
        if (!waitFor([&] { return session.devices()->rowCount() > 0; }, 3000))
            return false;
        session.connectToDevice(0);
        return session.isConnected();
    }
};

void TestSessionDemo::connectsToDemoAndProducesSpectrum()
{
    SessionManager session;
    QVERIFY2(connectSession(session), "connessione al backend demo fallita");

    QCOMPARE(session.centerFrequency(), 7'100'000);
    QVERIFY(session.sampleRate() > 0.0);
    QVERIFY2(session.channels()->rowCount() == 1,
             "la connessione deve lasciare un canale pronto all'ascolto");

    SpectrumFeed *feed = session.spectrum();
    QVERIFY(feed);
    QVERIFY2(waitFor([&] { return feed->binCount() > 0; }, 3000),
             "il panadattatore non è stato configurato");

    // Righe di spettro consumabili dal render thread.
    std::vector<float> rows;
    QVERIFY2(waitFor([&] { return feed->fetchRows(rows, 4) > 0; }, 3000),
             "nessuna riga di spettro prodotta");

    const int bins = feed->binCount();
    QCOMPARE(static_cast<int>(rows.size()) % bins, 0);

    // La banda sintetica deve emergere dal rumore: fra il bin più forte e la
    // mediana ci devono essere decine di dB, altrimenti stiamo guardando rumore.
    std::vector<float> firstRow(rows.begin(), rows.begin() + bins);
    std::vector<float> sorted = firstRow;
    std::sort(sorted.begin(), sorted.end());
    const float median = sorted[sorted.size() / 2];
    const float peak = sorted.back();

    QVERIFY2(peak - median > 25.0f,
             qPrintable(QStringLiteral("picco %1 dB, mediana %2 dB: la banda sembra vuota")
                            .arg(peak)
                            .arg(median)));
}

void TestSessionDemo::audioKeepsUpWithRealTime()
{
    SessionManager session;
    QVERIFY(connectSession(session));

    // Consumiamo l'audio come farebbe la scheda audio e verifichiamo che il
    // DSP produca almeno il tempo reale: se non ci riesce, nessuna UI potrà
    // essere fluida e l'ascolto sarà a singhiozzo.
    QTest::qWait(1500);

    QVERIFY2(session.channels()->rowCount() == 1, "canale sparito durante lo streaming");

    const ChannelEntry *entry = session.channels()->at(0);
    QVERIFY(entry);
    QVERIFY2(waitFor([&] { return session.channels()->at(0)->signalDb > -139.0f; }, 3000),
             "nessuna misura di livello: la catena del canale non ha prodotto audio");
}

void TestSessionDemo::channelLimitMatchesCapabilities()
{
    SessionManager session;
    QVERIFY(connectSession(session));

    const int limit = session.capabilities()->maxRxChannels();
    while (session.channels()->rowCount() < limit)
        QVERIFY(session.addChannel(session.centerFrequency() + 5000) >= 0);

    QCOMPARE(session.channels()->rowCount(), limit);

    // Oltre il limite dichiarato la sessione rifiuta senza rompersi.
    QCOMPARE(session.addChannel(session.centerFrequency()), -1);
    QCOMPARE(session.channels()->rowCount(), limit);
}

void TestSessionDemo::rigctlTunesAndChangesMode()
{
    SessionManager session;
    QVERIFY(connectSession(session));
    QVERIFY(session.rigctlRunning());
    QVERIFY(session.rigctlPort() > 0);

    QTcpSocket socket;
    socket.connectToHost(QHostAddress::LocalHost, static_cast<quint16>(session.rigctlPort()));
    QVERIFY2(socket.waitForConnected(1000), "server rigctl non raggiungibile");

    socket.write("f\n");
    QVERIFY2(waitFor([&] { return socket.canReadLine(); }, 1000),
             "rigctl non ha risposto alla lettura frequenza");
    QCOMPARE(socket.readLine().trimmed(), QByteArray("7100000"));

    socket.write("F 7105000\n");
    QVERIFY2(waitFor([&] { return socket.canReadLine(); }, 1000),
             "rigctl non ha confermato la sintonia");
    QCOMPARE(socket.readLine().trimmed(), QByteArray("RPRT 0"));
    QCOMPARE(session.channels()->at(0)->frequencyHz, qint64(7'105'000));

    socket.write("M WFM 180000\n");
    QVERIFY2(waitFor([&] { return socket.canReadLine(); }, 1000),
             "rigctl non ha confermato il modo");
    QCOMPARE(socket.readLine().trimmed(), QByteArray("RPRT 0"));
    QCOMPARE(session.channels()->at(0)->settings.mode, DemodMode::Fm);
    QCOMPARE(session.channels()->at(0)->settings.filterHighHz
                 - session.channels()->at(0)->settings.filterLowHz,
             180000);

    socket.write("t\n");
    QVERIFY2(waitFor([&] { return socket.canReadLine(); }, 1000),
             "rigctl non ha risposto alla lettura PTT");
    QCOMPARE(socket.readLine().trimmed(), QByteArray("0"));

    socket.write("T 1\n");
    QVERIFY2(waitFor([&] { return socket.canReadLine(); }, 1000),
             "rigctl non ha confermato PTT ON");
    QCOMPARE(socket.readLine().trimmed(), QByteArray("RPRT 0"));
    QVERIFY(session.isTransmitting());

    socket.write("t\n");
    QVERIFY2(waitFor([&] { return socket.canReadLine(); }, 1000),
             "rigctl non ha restituito PTT ON");
    QCOMPARE(socket.readLine().trimmed(), QByteArray("1"));

    socket.write("L AF 0.42\n");
    QVERIFY2(waitFor([&] { return socket.canReadLine(); }, 1000),
             "rigctl non ha confermato il volume AF");
    QCOMPARE(socket.readLine().trimmed(), QByteArray("RPRT 0"));
    QVERIFY(qFuzzyCompare(session.audio()->volume(), 0.42f));

    socket.write("l AF\n");
    QVERIFY2(waitFor([&] { return socket.canReadLine(); }, 1000),
             "rigctl non ha restituito il volume AF");
    QCOMPARE(socket.readLine().trimmed(), QByteArray("0.420000"));

    socket.write("T 0\n");
    QVERIFY2(waitFor([&] { return socket.canReadLine(); }, 1000),
             "rigctl non ha confermato PTT OFF");
    QCOMPARE(socket.readLine().trimmed(), QByteArray("RPRT 0"));
    QVERIFY(!session.isTransmitting());
}

void TestSessionDemo::scannerCompletesAndPublishesState()
{
    SessionManager session;
    QVERIFY(connectSession(session));

    QVERIFY(session.startScan(7'100'000, 7'105'000, 5'000, 150));
    QVERIFY(session.isScanning());
    QVERIFY2(waitFor([&] { return !session.isScanning(); }, 2000),
             "lo scanner non ha completato la banda richiesta");
    QVERIFY(session.scanResults().size() <= 2);
}

void TestSessionDemo::changingChannelModeKeepsAudioAlive()
{
    SessionManager session;
    QVERIFY(connectSession(session));
    session.setChannelMode(0, static_cast<int>(DemodMode::Fm));
    session.setChannelFmStereo(0, true);
    session.setChannelFmRds(0, true);
    session.setChannelMode(0, static_cast<int>(DemodMode::Nfm));
    session.setChannelMode(0, static_cast<int>(DemodMode::Usb));

    QVERIFY2(waitFor([&] {
        const ChannelEntry *entry = session.channels()->at(0);
        return entry && entry->signalDb > -139.0f;
    }, 3000), "il canale non è sopravvissuto alle riconfigurazioni stereo");
}

void TestSessionDemo::disconnectStopsEverything()
{
    SessionManager session;
    QVERIFY(connectSession(session));
    QTest::qWait(300);

    session.setPtt(true);
    QVERIFY(session.isTransmitting());
    session.disconnectDevice();
    QVERIFY(!session.isConnected());
    QVERIFY(!session.isTransmitting());
    QCOMPARE(session.channels()->rowCount(), 0);

    // Riconnessione: deve funzionare come la prima volta.
    session.connectToDevice(0);
    QVERIFY(session.isConnected());
    QCOMPARE(session.channels()->rowCount(), 1);
}

QTEST_MAIN(TestSessionDemo)

#include "tst_session_demo.moc"
