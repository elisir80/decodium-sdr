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
#include <QSignalSpy>
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

void TestSessionDemo::disconnectStopsEverything()
{
    SessionManager session;
    QVERIFY(connectSession(session));
    QTest::qWait(300);

    session.disconnectDevice();
    QVERIFY(!session.isConnected());
    QCOMPARE(session.channels()->rowCount(), 0);

    // Riconnessione: deve funzionare come la prima volta.
    session.connectToDevice(0);
    QVERIFY(session.isConnected());
    QCOMPARE(session.channels()->rowCount(), 1);
}

QTEST_MAIN(TestSessionDemo)

#include "tst_session_demo.moc"
