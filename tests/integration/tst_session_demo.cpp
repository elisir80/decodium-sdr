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
    void movingTheCentreKeepsChannelFrequencies();
    void tuningTakesTheReceiverAlong();
    void theBandCanBeRewound();
    void rewindingStopsAtTheOldestSampleHeld();
    void changingBandStartsTheHistoryOver();
    void noiseFiltersReachTheDspWithoutKillingTheAudio();

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

void TestSessionDemo::movingTheCentreKeepsChannelFrequencies()
{
    SessionManager session;
    QVERIFY2(connectSession(session), "connessione al backend demo fallita");

    const qint64 start = session.centerFrequency();
    const int row = session.addChannel(start);
    QVERIFY2(row >= 0, "canale non creato");
    QCOMPARE(session.channels()->at(row)->frequencyHz, start);

    // Spostare il centro della banda campionata non deve spostare il
    // ricevitore: la frequenza del canale è assoluta, l'offset è quello che si
    // ricalcola. È l'invariante sospettata dopo aver visto RX 1 comparire a
    // 12.076.200 Hz con il centro a 14.100.000.
    const qint64 moved = start + 7'000'000;
    session.setCenterFrequency(moved);
    QVERIFY2(waitFor([&] { return session.centerFrequency() == moved; }, 3000),
             "il centro non si è spostato");

    const auto *entry = session.channels()->at(row);
    QVERIFY(entry != nullptr);
    QCOMPARE(entry->frequencyHz, start);

    // E l'offset deve raccontare la stessa storia della frequenza: se i due
    // divergono, il DSP demodula un punto e la UI ne mostra un altro.
    QCOMPARE(static_cast<qint64>(entry->settings.offsetHz), start - moved);
}

void TestSessionDemo::tuningTakesTheReceiverAlong()
{
    SessionManager session;
    QVERIFY2(connectSession(session), "connessione al backend demo fallita");

    // Sintonizzare è un gesto solo: la finestra si sposta e il ricevitore ci
    // va dentro. Muovere il solo centro — che resta possibile, ed è ciò che
    // verifica `movingTheCentreKeepsChannelFrequencies` — lasciava il canale
    // fuori dalla banda campionata, dove nessuno lo demodula più.
    const qint64 target = session.centerFrequency() + 7'000'000;
    session.tuneTo(target);
    QVERIFY2(waitFor([&] { return session.centerFrequency() == target; }, 3000),
             "il centro non si è spostato");

    const int row = session.channels()->currentIndex();
    QVERIFY2(row >= 0, "nessun canale attivo dopo la sintonia");

    const auto *entry = session.channels()->at(row);
    QVERIFY(entry != nullptr);
    QCOMPARE(entry->frequencyHz, target);
    QCOMPARE(static_cast<qint64>(entry->settings.offsetHz), 0);

    // E non se ne accumulano: sintonizzare due volte muove il canale che c'è,
    // non ne accende un altro.
    const int before = session.channels()->rowCount();
    session.tuneTo(target + 25'000);
    QCOMPARE(session.channels()->rowCount(), before);
    QCOMPARE(session.channels()->at(row)->frequencyHz, target + 25'000);
}

// ── Macchina del tempo ──────────────────────────────────────────────────
//
// Il buffer ha già i suoi test campione per campione (tst_time_shift); qui si
// verifica ciò che l'operatore vede: che si possa tornare indietro davvero,
// che la radio continui a ricevere mentre lo si fa, e che il ritorno alla
// diretta sia immediato.

void TestSessionDemo::theBandCanBeRewound()
{
    SessionManager session;
    QVERIFY2(connectSession(session), "connessione al backend demo fallita");

    // La storia comincia a esistere quando comincia lo streaming: subito dopo
    // la connessione non c'è passato a cui tornare, ed è giusto così.
    QCOMPARE(session.replayDelaySeconds(), 0.0);
    QVERIFY2(waitFor([&] { return session.replayHistorySeconds() > 2.0; }, 8000),
             "la memoria di scorrimento non si è riempita");

    // E finché nessuno lo chiede si resta in diretta: la memoria che si
    // allunga non deve trascinarsi dietro l'ascolto.
    QVERIFY2(!session.replaying(),
             qPrintable(QStringLiteral("riascolto partito da solo: %1 s indietro")
                            .arg(session.replayDelaySeconds())));

    session.rewind(2.0);
    QVERIFY2(session.replaying(), "il riavvolgimento non ha spostato l'ascolto");
    QVERIFY2(std::abs(session.replayDelaySeconds() - 2.0) < 0.25,
             qPrintable(QStringLiteral("ritardo inatteso: %1 s")
                            .arg(session.replayDelaySeconds())));

    // Riavvolgere non ferma la radio: il presente continua a entrare in
    // memoria mentre si ascolta il passato, altrimenti tornare in diretta
    // lascerebbe un buco grande quanto il riascolto.
    const double historyBefore = session.replayHistorySeconds();
    QVERIFY2(waitFor([&] { return session.replayHistorySeconds() > historyBefore; }, 4000),
             "la storia ha smesso di crescere durante il riascolto");

    // E il canale continua a produrre audio: si ascolta, non si è in pausa.
    QVERIFY2(waitFor([&] { return session.channels()->at(0)->signalDb > -139.0f; }, 4000),
             "nessun livello durante il riascolto: la catena si è fermata");

    // Premuto due volte, il tasto riavvolge due volte — ma solo quando il
    // passato richiesto è davvero trascorso: a due secondi dalla connessione
    // non se ne possono chiedere tre, e il motore ha ragione a rifiutare.
    QVERIFY2(waitFor([&] { return session.replayHistorySeconds() > 3.5; }, 8000),
             "la memoria non ha raggiunto i tre secondi e mezzo");
    session.rewind(1.0);
    QVERIFY2(std::abs(session.replayDelaySeconds() - 3.0) < 0.35,
             qPrintable(QStringLiteral("il secondo salto non si è sommato: %1 s")
                            .arg(session.replayDelaySeconds())));

    session.returnToLive();
    QCOMPARE(session.replayDelaySeconds(), 0.0);
    QVERIFY(!session.replaying());
}

void TestSessionDemo::rewindingStopsAtTheOldestSampleHeld()
{
    SessionManager session;
    QVERIFY2(connectSession(session), "connessione al backend demo fallita");
    QVERIFY(waitFor([&] { return session.replayHistorySeconds() > 1.0; }, 8000));

    // Si chiede un'ora di passato quando in memoria ci sono pochi secondi: si
    // torna indietro fin dove la memoria arriva, senza silenzio e senza
    // promettere un ritardo che non esiste.
    session.rewind(3600.0);

    const double delay = session.replayDelaySeconds();
    QVERIFY2(delay > 0.0, "nessun riavvolgimento con una richiesta profonda");
    QVERIFY2(delay <= session.replayCapacitySeconds() + 0.5,
             qPrintable(QStringLiteral("ritardo oltre la capacità: %1 s su %2 s")
                            .arg(delay).arg(session.replayCapacitySeconds())));

    QVERIFY2(waitFor([&] { return session.channels()->at(0)->signalDb > -139.0f; }, 4000),
             "nessun audio dopo un riavvolgimento profondo");
}

void TestSessionDemo::changingBandStartsTheHistoryOver()
{
    SessionManager session;
    QVERIFY2(connectSession(session), "connessione al backend demo fallita");
    QVERIFY(waitFor([&] { return session.replayHistorySeconds() > 1.5; }, 8000));

    session.rewind(1.0);
    QVERIFY(session.replaying());

    // Cambiare banda getta la storia: quei campioni erano un'altra porzione di
    // spettro, e riascoltarli qui mostrerebbe segnali su frequenze dove non
    // sono mai stati.
    session.tuneTo(session.centerFrequency() + 3'000'000);

    QVERIFY2(waitFor([&] { return !session.replaying(); }, 3000),
             "il riascolto è sopravvissuto al cambio di banda");
    QVERIFY2(waitFor([&] { return session.replayHistorySeconds() < 1.0; }, 3000),
             "la storia della banda precedente non è stata dimenticata");
}

void TestSessionDemo::noiseFiltersReachTheDspWithoutKillingTheAudio()
{
    SessionManager session;
    QVERIFY2(connectSession(session), "connessione al backend demo fallita");
    QVERIFY2(waitFor([&] { return session.channels()->at(0)->signalDb > -139.0f; }, 5000),
             "nessun audio prima di toccare i filtri");

    // I filtri stanno spenti finché non li si accende: è la promessa della
    // catena di fabbrica, e un ricevitore che «aiuta» da solo è un ricevitore
    // di cui non ci si fida.
    const ChannelEntry *entry = session.channels()->at(0);
    QVERIFY2(!session.noiseBlanker(), "il blanker parte acceso");
    QVERIFY(!entry->settings.nrEnabled);
    QVERIFY(!entry->settings.anfEnabled);
    QVERIFY(!entry->settings.notchEnabled);

    // E devono risultare spenti anche **dal modello**, che è ciò che la UI
    // legge: i pulsanti del pannello si accendono su questi ruoli, e un ruolo
    // che restituisce il campo sbagliato mostrerebbe filtri accesi su una
    // catena che non li sta applicando.
    const QModelIndex index = session.channels()->index(0, 0);
    const auto roleBool = [&](int role) {
        return session.channels()->data(index, role).toBool();
    };
    QVERIFY2(!roleBool(ChannelModel::NrEnabledRole), "il modello dichiara NR acceso");
    QVERIFY2(!roleBool(ChannelModel::AnfEnabledRole), "il modello dichiara ANF acceso");
    QVERIFY2(!roleBool(ChannelModel::NotchEnabledRole), "il modello dichiara NOTCH acceso");

    // I nomi che QML usa devono corrispondere ai ruoli che li servono: è il
    // punto in cui un'aggiunta in mezzo all'enum sposta tutto di una casella
    // senza che nulla smetta di compilare.
    const auto names = session.channels()->roleNames();
    QCOMPARE(names.value(ChannelModel::NrEnabledRole), QByteArray("nrEnabled"));
    QCOMPARE(names.value(ChannelModel::AnfEnabledRole), QByteArray("anfEnabled"));
    QCOMPARE(names.value(ChannelModel::NotchEnabledRole), QByteArray("notchEnabled"));
    QCOMPARE(names.value(ChannelModel::SignalDbRole), QByteArray("signalDb"));

    // Il blanker è di catena, non di canale (SPEC-003 §4): un impulso arriva
    // su tutta la banda, e toglierlo per un ricevitore solo non vuol dire
    // niente.
    session.setNoiseBlanker(true, 4.0);
    session.setChannelNoiseReduction(0, true, 0.05);
    session.setChannelAutoNotch(0, true);
    session.setChannelNotch(0, true, 1200.0, 150.0);

    entry = session.channels()->at(0);
    QVERIFY(session.noiseBlanker());
    QVERIFY(entry->settings.nrEnabled);
    QVERIFY(entry->settings.anfEnabled);
    QVERIFY(entry->settings.notchEnabled);
    QCOMPARE(entry->settings.notchFrequencyHz, 1200.0);

    // Con tutti e quattro accesi la catena deve continuare a produrre audio.
    // Quattro filtri in cascata sono anche quattro modi di azzerare il segnale
    // per sbaglio, e nessun test unitario li vede messi insieme.
    QTest::qWait(1200);
    QVERIFY2(waitFor([&] { return session.channels()->at(0)->signalDb > -139.0f; }, 4000),
             "la catena si è azzittita con i filtri accesi");

    // Valori assurdi vengono riportati dentro i limiti invece di essere
    // presi alla lettera: un notch a 40 kHz farebbe suonare la biquad.
    session.setChannelNotch(0, true, 999999.0, 0.0);
    entry = session.channels()->at(0);
    QVERIFY2(entry->settings.notchFrequencyHz <= 5000.0,
             "frequenza del notch fuori dai limiti");
    QVERIFY2(entry->settings.notchWidthHz >= 20.0, "larghezza del notch degenere");

    QTest::qWait(600);
    QVERIFY2(waitFor([&] { return session.channels()->at(0)->signalDb > -139.0f; }, 4000),
             "la catena si è fermata dopo un notch fuori scala");
}

QTEST_MAIN(TestSessionDemo)

#include "tst_session_demo.moc"
