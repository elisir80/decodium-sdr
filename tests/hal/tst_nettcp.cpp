// SPDX-License-Identifier: GPL-3.0-or-later
// Test specifici del backend nettcp: il protocollo rtl_tcp e la sua
// traduzione in campioni utilizzabili dal DSP.
//
// La conformance suite verifica che il backend rispetti il contratto della
// HAL; qui si verifica che parli davvero rtl_tcp e che i byte diventino i
// numeri giusti.

#include "RtlTcpMockServer.h"
#include "SpyServerMockServer.h"
#include "NetworkIqMockServer.h"
#include "hal/BackendRegistry.h"
#include "hal/IRadioBackend.h"

#include <QElapsedTimer>
#include <QHostAddress>
#include <QSignalSpy>
#include <QTest>
#include <QUdpSocket>
#include <QtEndian>

#include <cmath>
#include <cstring>
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

    // ── SpyServer: secondo protocollo dietro la stessa facciata ──────────
    void spyServerIsRecognisedByItsSilence();
    void spyServerHandshakeStartsStreaming();
    void spyServerCapabilitiesComeFromTheDevice();
    void spyServerSamplesAreScaled();

    // ── IQ generico e SDR++ Server ───────────────────────────────────────
    void rawTcpIqUriStreamsDeclaredFormat();
    void rawUdpIqUriStreamsDeclaredFormat();
    void sdrppServerNegotiatesAndStreams();

private:
    std::unique_ptr<dsdr::test::RtlTcpMockServer> m_server;
    std::unique_ptr<dsdr::test::SpyServerMockServer> m_spyServer;
    std::unique_ptr<dsdr::test::RawIqTcpMockServer> m_rawIqServer;
    std::unique_ptr<dsdr::test::SdrppServerMockServer> m_sdrppServer;
    std::unique_ptr<IRadioBackend> m_backend;

    /// Quanto si è disposti ad aspettare.
    ///
    /// Venti secondi, e non è generosità: **questi test verificano che una
    /// cosa succeda, non che succeda in fretta**. Su una macchina scarica
    /// tornano tutti in qualche centinaio di millisecondi; su un runner di CI
    /// che compila altri tre progetti, una stretta di mano su localhost può
    /// aspettare qualche secondo. Con cinque secondi di limite cadeva, e un
    /// test che cade da solo insegna a ignorare i fallimenti veri — che è il
    /// danno peggiore che una suite possa fare.
    ///
    /// Se una di queste attese arrivasse davvero a scadenza, il difetto non
    /// sarebbe la lentezza: sarebbe che non succede.
    static constexpr int kPatience = 20'000;

    bool waitFor(std::function<bool()> predicate, int timeoutMs = kPatience)
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

    /// Sostituisce il mock rtl_tcp con uno SpyServer sullo stesso backend.
    bool useSpyServerMock()
    {
        m_server.reset();   // libera l'endpoint rtl_tcp
        m_spyServer = std::make_unique<dsdr::test::SpyServerMockServer>();
        if (!m_spyServer->listen())
            return false;
        qputenv("DSDR_NETTCP_HOSTS", m_spyServer->endpoint().toUtf8());

        m_backend.reset(BackendRegistry::instance().create(QStringLiteral("nettcp")));
        return m_backend != nullptr;
    }

    /// Un URI con schema è già una dichiarazione sufficiente: non deve
    /// attendere la sonda rtl_tcp/SpyServer per comparire nell'elenco.
    bool useDeclaredEndpoint(const QString &endpoint)
    {
        m_server.reset();
        m_spyServer.reset();
        m_backend.reset();
        qputenv("DSDR_NETTCP_HOSTS", endpoint.toUtf8());
        m_backend.reset(BackendRegistry::instance().create(QStringLiteral("nettcp")));
        return m_backend != nullptr;
    }

    bool openDeclaredEndpoint(DeviceDescriptor *declared = nullptr)
    {
        QSignalSpy found(m_backend.get(), &IRadioBackend::deviceFound);
        m_backend->startDiscovery();
        if (found.isEmpty() && !found.wait(kPatience))
            return false;
        if (found.isEmpty())
            return false;
        const auto device = found.first().first().value<DeviceDescriptor>();
        if (declared)
            *declared = device;
        m_backend->open(device);
        return waitFor([this] { return m_backend->state() == BackendState::Streaming; });
    }

    /// Avvia il mock, fa discovery e apre il device trovato.
    bool connectToMock()
    {
        QSignalSpy found(m_backend.get(), &IRadioBackend::deviceFound);
        m_backend->startDiscovery();
        if (!found.wait(kPatience) || found.isEmpty())
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
    m_spyServer.reset();
    m_rawIqServer.reset();
    m_sdrppServer.reset();
    qunsetenv("DSDR_NETTCP_HOSTS");
}

void TestNetTcp::discoveryFindsOnlyRealRtlTcpServers()
{
    QSignalSpy found(m_backend.get(), &IRadioBackend::deviceFound);
    QSignalSpy finished(m_backend.get(), &IRadioBackend::discoveryFinished);

    m_backend->startDiscovery();
    QVERIFY2(finished.wait(kPatience), "la discovery non è terminata");
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
    QVERIFY(finished.wait(kPatience));
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
    QVERIFY2(waitFor([ring] { return ring->available() >= 4096; }),
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

// ─────────────────────────────────────────────────────────────────────────────
// SpyServer
// ─────────────────────────────────────────────────────────────────────────────

void TestNetTcp::spyServerIsRecognisedByItsSilence()
{
    QVERIFY(useSpyServerMock());

    QSignalSpy found(m_backend.get(), &IRadioBackend::deviceFound);
    QSignalSpy finished(m_backend.get(), &IRadioBackend::discoveryFinished);

    m_backend->startDiscovery();
    QVERIFY2(finished.wait(kPatience), "la discovery non è terminata");
    QCOMPARE(found.count(), 1);

    // Il riconoscimento si regge su una differenza di comportamento: rtl_tcp
    // saluta per primo, SpyServer aspetta di essere salutato.
    const auto device = found.first().first().value<DeviceDescriptor>();
    QCOMPARE(device.extra.value(QStringLiteral("protocol")).toString(),
             QStringLiteral("spyserver"));
    QVERIFY2(device.displayName.contains(QStringLiteral("SpyServer")),
             qPrintable(device.displayName));
    QVERIFY2(m_spyServer->helloReceived(),
             "il client non si è presentato: senza handshake un SpyServer resta muto");
}

void TestNetTcp::spyServerHandshakeStartsStreaming()
{
    QVERIFY(useSpyServerMock());
    QVERIFY(connectToMock());

    // Lo streaming non parte da solo: va chiesto, dopo aver detto che cosa si
    // vuole ricevere.
    QVERIFY2(waitFor([this] { return m_spyServer->streamingEnabled(); }),
             "lo streaming non è stato abilitato");
    QVERIFY2(m_spyServer->settingCount() >= 4,
             "troppe poche impostazioni inviate prima di accendere il flusso");
    QVERIFY2(waitFor([this] { return m_spyServer->lastFrequencyHz() > 0; }),
             "nessuna frequenza comunicata al server");
}

void TestNetTcp::spyServerCapabilitiesComeFromTheDevice()
{
    QVERIFY(useSpyServerMock());
    QVERIFY(connectToMock());
    QVERIFY(waitFor([this] { return m_backend->capabilities().maxFrequencyHz > 0; }));

    const BackendCapabilities caps = m_backend->capabilities();

    // Copertura e risoluzione arrivano dal messaggio DeviceInfo.
    QCOMPARE(caps.minFrequencyHz, 24'000'000);
    QCOMPARE(caps.maxFrequencyHz, 1'800'000'000);
    QCOMPARE(caps.adcBits, 12);
    QVERIFY2(!caps.canTransmit(), "SpyServer è un servizio di sola ricezione");
    QVERIFY2(caps.multiClient, "un SpyServer serve più ascoltatori insieme");

    // I rate non sono liberi: sono il massimo diviso per potenze di due.
    QVERIFY2(!caps.sampleRates.isEmpty(), "nessuna frequenza di campionamento offerta");
    for (double rate : caps.sampleRates) {
        const double ratio = 10'000'000.0 / rate;
        const double stage = std::log2(ratio);
        QVERIFY2(std::abs(stage - std::round(stage)) < 1e-6,
                 qPrintable(QStringLiteral("rate non ottenibile per decimazione: %1").arg(rate)));
    }
}

void TestNetTcp::spyServerSamplesAreScaled()
{
    QVERIFY(useSpyServerMock());
    QVERIFY(connectToMock());

    SampleRing *ring = m_backend->iqStream();
    QVERIFY(ring);
    QVERIFY2(waitFor([ring] { return ring->available() >= 4096; }),
             "nessun campione ricevuto dal SpyServer");

    std::vector<float> samples(4096);
    const std::size_t got = ring->read(samples.data(), samples.size());
    QVERIFY(got > 1000);

    // Il mock manda int16 al ~37% del fondo scala: dopo la conversione i
    // campioni devono restare in [-1, 1] e non essere silenzio.
    double peak = 0.0;
    for (std::size_t i = 0; i < got; ++i) {
        QVERIFY2(std::isfinite(samples[i]), "campione non finito");
        peak = std::max(peak, std::abs(static_cast<double>(samples[i])));
    }
    QVERIFY2(peak > 0.2 && peak <= 1.0,
             qPrintable(QStringLiteral("ampiezza fuori scala: %1").arg(peak)));
}

// ─────────────────────────────────────────────────────────────────────────────
// IQ generico e SDR++ Server
// ─────────────────────────────────────────────────────────────────────────────

void TestNetTcp::rawTcpIqUriStreamsDeclaredFormat()
{
    m_rawIqServer = std::make_unique<dsdr::test::RawIqTcpMockServer>();
    QVERIFY(m_rawIqServer->listen());
    QVERIFY(useDeclaredEndpoint(m_rawIqServer->endpoint()));

    DeviceDescriptor device;
    QVERIFY(openDeclaredEndpoint(&device));
    QCOMPARE(device.extra.value(QStringLiteral("protocol")).toString(),
             QStringLiteral("raw_tcp"));
    QCOMPARE(m_backend->sampleRate(), 96'000.0);

    SampleRing *ring = m_backend->iqStream();
    QVERIFY(ring);
    QVERIFY2(waitFor([ring] { return ring->available() >= 8; }),
             "nessun IQ raw TCP ricevuto");

    std::vector<float> samples(8);
    QCOMPARE(ring->read(samples.data(), samples.size()), samples.size());
    QCOMPARE(samples[0], 32767.0f / 32768.0f);
    QCOMPARE(samples[1], 0.0f);
    QCOMPARE(samples[2], 0.0f);
    QCOMPARE(samples[3], -0.5f);
    QCOMPARE(samples[4], -1.0f);
    QCOMPARE(samples[5], 0.5f);
    QCOMPARE(samples[6], 0.25f);
    QCOMPARE(samples[7], -0.25f);
}

void TestNetTcp::rawUdpIqUriStreamsDeclaredFormat()
{
    QUdpSocket probe;
    QVERIFY(probe.bind(QHostAddress::LocalHost, 0));
    const quint16 port = probe.localPort();
    probe.close();

    const QString endpoint = QStringLiteral("udp://127.0.0.1:%1?rate=48000&format=float32")
                                 .arg(port);
    QVERIFY(useDeclaredEndpoint(endpoint));
    QVERIFY(openDeclaredEndpoint());
    QCOMPARE(m_backend->sampleRate(), 48'000.0);

    auto appendFloat = [](QByteArray &bytes, float value) {
        quint32 bits = 0;
        static_assert(sizeof(bits) == sizeof(value));
        std::memcpy(&bits, &value, sizeof(bits));
        const int offset = bytes.size();
        bytes.resize(offset + static_cast<int>(sizeof(bits)));
        qToLittleEndian(bits, bytes.data() + offset);
    };

    QByteArray datagram;
    appendFloat(datagram, 0.25f);
    appendFloat(datagram, -0.5f);
    appendFloat(datagram, 1.0f);
    appendFloat(datagram, -1.0f);
    QUdpSocket sender;
    QCOMPARE(sender.writeDatagram(datagram, QHostAddress::LocalHost, port),
             static_cast<qint64>(datagram.size()));

    SampleRing *ring = m_backend->iqStream();
    QVERIFY(ring);
    QVERIFY2(waitFor([ring] { return ring->available() >= 4; }),
             "nessun IQ raw UDP ricevuto");
    std::vector<float> samples(4);
    QCOMPARE(ring->read(samples.data(), samples.size()), samples.size());
    QCOMPARE(samples[0], 0.25f);
    QCOMPARE(samples[1], -0.5f);
    QCOMPARE(samples[2], 1.0f);
    QCOMPARE(samples[3], -1.0f);
}

void TestNetTcp::sdrppServerNegotiatesAndStreams()
{
    m_sdrppServer = std::make_unique<dsdr::test::SdrppServerMockServer>();
    QVERIFY(m_sdrppServer->listen());
    QVERIFY(useDeclaredEndpoint(m_sdrppServer->endpoint()));

    DeviceDescriptor device;
    QVERIFY(openDeclaredEndpoint(&device));
    QCOMPARE(device.extra.value(QStringLiteral("protocol")).toString(),
             QStringLiteral("sdrpp"));
    QCOMPARE(m_backend->sampleRate(), 1'024'000.0);
    QVERIFY2(waitFor([this] { return m_sdrppServer->receivedStart(); }),
             "il server SDR++ non ha ricevuto Start");
    QVERIFY2(waitFor([this] { return m_sdrppServer->requestedFrequencyHz() > 0; }),
             "il server SDR++ non ha ricevuto la frequenza iniziale");

    SampleRing *ring = m_backend->iqStream();
    QVERIFY(ring);
    QVERIFY2(waitFor([ring] { return ring->available() >= 8; }),
             "il server SDR++ non ha inviato IQ");
    std::vector<float> samples(8);
    QCOMPARE(ring->read(samples.data(), samples.size()), samples.size());
    QCOMPARE(samples[0], 0.5f);
    QCOMPARE(samples[1], -0.5f);
    QCOMPARE(samples[2], -0.25f);
    QCOMPARE(samples[3], 0.25f);
    QCOMPARE(samples[4], 0.0f);
    QCOMPARE(samples[5], 32767.0f / 32768.0f);
    QCOMPARE(samples[6], -1.0f);
    QCOMPARE(samples[7], 0.0f);
}

QTEST_MAIN(TestNetTcp)

#include "tst_nettcp.moc"
