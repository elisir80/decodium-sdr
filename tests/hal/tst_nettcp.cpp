// SPDX-License-Identifier: GPL-3.0-or-later
// Test specifici del backend nettcp: il protocollo rtl_tcp e la sua
// traduzione in campioni utilizzabili dal DSP.
//
// La conformance suite verifica che il backend rispetti il contratto della
// HAL; qui si verifica che parli davvero rtl_tcp e che i byte diventino i
// numeri giusti.

#include "RtlTcpMockServer.h"
#include "hal/BackendRegistry.h"
#include "hal/IRadioBackend.h"

#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTest>

#include <cmath>
#include <memory>

using namespace dsdr;
using namespace dsdr::hal;

class TestNetTcp : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    void discoveryFindsOnlyRealRtlTcpServers();
    void discoveryIgnoresServersWithoutGreeting();
    void handshakeAppliesPendingSettings();
    void samplesAreCentredAndScaled();
    void tunerTypeNarrowsFrequencyCoverage();
    void frequencyOutsideCoverageIsRefused();
    void transmitIsAlwaysRefused();
    void nativeCommandsReachTheServer();

private:
    std::unique_ptr<dsdr::test::RtlTcpMockServer> m_server;
    std::unique_ptr<IRadioBackend> m_backend;

    bool waitFor(std::function<bool()> predicate, int timeoutMs = 5000)
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

    /// Avvia il mock, fa discovery e apre il device trovato.
    bool connectToMock()
    {
        QSignalSpy found(m_backend.get(), &IRadioBackend::deviceFound);
        m_backend->startDiscovery();
        if (!found.wait(4000) || found.isEmpty())
            return false;

        const auto device = found.first().first().value<DeviceDescriptor>();
        m_backend->open(device);
        return waitFor([this] { return m_backend->state() == BackendState::Streaming; });
    }
};

void TestNetTcp::initTestCase()
{
    registerBuiltinBackends();
    if (!BackendRegistry::instance().contains(QStringLiteral("nettcp")))
        QSKIP("backend nettcp escluso da questa build");
}

void TestNetTcp::init()
{
    m_server = std::make_unique<dsdr::test::RtlTcpMockServer>();
    QVERIFY2(m_server->listen(), "impossibile aprire il mock rtl_tcp");
    qputenv("DSDR_NETTCP_HOSTS", m_server->endpoint().toUtf8());

    m_backend.reset(BackendRegistry::instance().create(QStringLiteral("nettcp")));
    QVERIFY(m_backend);
}

void TestNetTcp::cleanup()
{
    if (m_backend) {
        m_backend->close();
        m_backend.reset();
    }
    m_server.reset();
    qunsetenv("DSDR_NETTCP_HOSTS");
}

void TestNetTcp::discoveryFindsOnlyRealRtlTcpServers()
{
    QSignalSpy found(m_backend.get(), &IRadioBackend::deviceFound);
    QSignalSpy finished(m_backend.get(), &IRadioBackend::discoveryFinished);

    m_backend->startDiscovery();
    QVERIFY2(finished.wait(5000), "la discovery non è terminata");
    QCOMPARE(found.count(), 1);

    const auto device = found.first().first().value<DeviceDescriptor>();
    QCOMPARE(device.backendId, QStringLiteral("nettcp"));
    QCOMPARE(device.transport, QStringLiteral("tcp"));
    QVERIFY(device.deviceId.contains(QString::number(m_server->port())));
    // Il tuner dichiarato dal server deve comparire nel nome mostrato.
    QVERIFY2(device.displayName.contains(QStringLiteral("R820T")),
             qPrintable(device.displayName));
}

void TestNetTcp::discoveryIgnoresServersWithoutGreeting()
{
    // Un servizio qualsiasi in ascolto non è un rtl_tcp: accettare la
    // connessione non basta, serve il saluto.
    m_server->setSendGreeting(false);

    QSignalSpy found(m_backend.get(), &IRadioBackend::deviceFound);
    QSignalSpy finished(m_backend.get(), &IRadioBackend::discoveryFinished);

    m_backend->startDiscovery();
    QVERIFY(finished.wait(5000));
    QCOMPARE(found.count(), 0);
}

void TestNetTcp::handshakeAppliesPendingSettings()
{
    // rtl_tcp accetta comandi solo dopo l'handshake: le impostazioni scelte
    // prima devono essere riemesse, non perse.
    QVERIFY(connectToMock());

    QVERIFY2(waitFor([this] { return m_server->lastSampleRate() > 0; }),
             "nessun comando di frequenza di campionamento ricevuto");
    QVERIFY2(waitFor([this] { return m_server->lastFrequencyHz() > 0; }),
             "nessun comando di frequenza ricevuto");

    QCOMPARE(static_cast<double>(m_server->lastSampleRate()), m_backend->sampleRate());
    QCOMPARE(m_server->lastFrequencyHz(), m_backend->centerFrequency());
}

void TestNetTcp::samplesAreCentredAndScaled()
{
    QVERIFY(connectToMock());

    SampleRing *ring = m_backend->iqStream();
    QVERIFY(ring);
    QVERIFY2(waitFor([ring] { return ring->available() >= 4096; }, 5000),
             "nessun campione ricevuto dal server");

    std::vector<float> samples(4096);
    const std::size_t got = ring->read(samples.data(), samples.size());
    QVERIFY(got >= 2048);

    // Il mock genera un tono al 40% circa del fondo scala. I campioni devono
    // essere centrati su zero (rtl_tcp li manda non segnati attorno a 127,5)
    // e restare dentro il fondo scala.
    double sum = 0.0;
    double peak = 0.0;
    for (std::size_t i = 0; i < got; ++i) {
        QVERIFY2(std::isfinite(samples[i]), "campione non finito");
        sum += samples[i];
        peak = std::max(peak, std::abs(static_cast<double>(samples[i])));
    }

    const double mean = sum / static_cast<double>(got);
    QVERIFY2(std::abs(mean) < 0.05,
             qPrintable(QStringLiteral("componente continua residua: %1 — la conversione "
                                       "da uint8 non è centrata").arg(mean)));
    QVERIFY2(peak > 0.5 && peak <= 1.0,
             qPrintable(QStringLiteral("ampiezza fuori scala: %1").arg(peak)));
}

void TestNetTcp::tunerTypeNarrowsFrequencyCoverage()
{
    // Un FC0012 arriva a ~948 MHz, un R820T a ~1766: la copertura dichiarata
    // deve seguire il tuner vero, altrimenti la UI promette bande inesistenti.
    m_server->setTunerType(2); // FC0012
    QVERIFY(connectToMock());

    const BackendCapabilities caps = m_backend->capabilities();
    QVERIFY2(caps.maxFrequencyHz < 1'000'000'000,
             qPrintable(QStringLiteral("copertura non ristretta: %1 Hz").arg(caps.maxFrequencyHz)));
    QVERIFY(caps.coversFrequency(144'000'000));
    QVERIFY(!caps.coversFrequency(1'500'000'000));
}

void TestNetTcp::frequencyOutsideCoverageIsRefused()
{
    QVERIFY(connectToMock());

    QSignalSpy errors(m_backend.get(), &IRadioBackend::errorOccurred);
    const qint64 before = m_backend->centerFrequency();

    m_backend->setCenterFrequency(9'000'000'000); // ben oltre ogni tuner RTL

    QCOMPARE(m_backend->centerFrequency(), before);
    QVERIFY2(!errors.isEmpty(), "frequenza impossibile accettata in silenzio");
    QCOMPARE(errors.last().first().value<BackendError>().code, BackendError::Unsupported);
}

void TestNetTcp::transmitIsAlwaysRefused()
{
    QVERIFY(connectToMock());
    QVERIFY(!m_backend->capabilities().canTransmit());

    QSignalSpy errors(m_backend.get(), &IRadioBackend::errorOccurred);
    m_backend->setPtt(true);

    QVERIFY2(!m_backend->ptt(), "una chiavetta RTL è andata in trasmissione");
    QVERIFY2(!errors.isEmpty(), "la richiesta di TX è stata ignorata in silenzio");
}

void TestNetTcp::nativeCommandsReachTheServer()
{
    QVERIFY(connectToMock());
    QVERIFY(waitFor([this] { return m_server->commandCount() > 0; }));

    const int before = m_server->commandCount();

    // Guadagno manuale: il client deve mandare "modo manuale" e poi il valore.
    const QVariant result = m_backend->nativeCommand(
        QStringLiteral("nettcp.setGain"), {{QStringLiteral("tenthsDb"), 288}});
    QCOMPARE(result.toInt(), 288);

    QVERIFY2(waitFor([this, before] { return m_server->commandCount() > before; }),
             "il comando nativo non ha raggiunto il server");
    QVERIFY2(waitFor([this] { return m_server->lastGainMode() == 1; }),
             "il guadagno non è passato in modalità manuale");

    // Un comando sconosciuto degrada senza lanciare (§4.1).
    QVERIFY(!m_backend->nativeCommand(QStringLiteral("nettcp.inesistente"), {}).isValid());
}

QTEST_MAIN(TestNetTcp)

#include "tst_nettcp.moc"
