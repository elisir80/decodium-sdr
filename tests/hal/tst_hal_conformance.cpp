// SPDX-License-Identifier: GPL-3.0-or-later
// Conformance suite della HAL (§8).
//
// Ogni test è data-driven sull'elenco dei backend registrati: aggiungere un
// backend nuovo significa automaticamente sottoporlo a questa batteria, senza
// scrivere una riga di test in più. Se un backend richiede hardware assente,
// il test si dichiara skipped — mai passato per finta.

#include "hal/BackendRegistry.h"
#include "hal/IRadioBackend.h"

#include <QSignalSpy>
#include <QTest>

#include <memory>

using namespace dsdr;
using namespace dsdr::hal;

Q_DECLARE_METATYPE(dsdr::BackendState)

class TestHalConformance : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void registryExposesAtLeastOneBackend();

    void discoveryIsAsynchronous_data();
    void discoveryIsAsynchronous();

    void capabilitiesAreSelfConsistent_data();
    void capabilitiesAreSelfConsistent();

    void openReportsStateAndDevice_data();
    void openReportsStateAndDevice();

    void closeIsIdempotent_data();
    void closeIsIdempotent();

    void streamDeliversSamples_data();
    void streamDeliversSamples();

    void frameSequenceIsMonotonic_data();
    void frameSequenceIsMonotonic();

    void channelLimitIsEnforced_data();
    void channelLimitIsEnforced();

    void transmitIsRefusedWhenUnsupported_data();
    void transmitIsRefusedWhenUnsupported();

    void teardownDuringStreamingIsClean_data();
    void teardownDuringStreamingIsClean();

    void unknownNativeCommandReturnsInvalid_data();
    void unknownNativeCommandReturnsInvalid();

private:
    static void addBackendRows();

    /// Crea il backend e apre il primo device trovato dalla discovery.
    /// Restituisce nullptr (con QSKIP già chiamato) se non c'è hardware.
    std::unique_ptr<IRadioBackend> openFirstDevice(const QString &backendId);
};

void TestHalConformance::initTestCase()
{
    qRegisterMetaType<dsdr::BackendState>("dsdr::BackendState");
    registerBuiltinBackends();
}

void TestHalConformance::addBackendRows()
{
    QTest::addColumn<QString>("backendId");
    const QStringList ids = BackendRegistry::instance().backendIds();
    for (const QString &id : ids)
        QTest::newRow(qPrintable(id)) << id;
}

std::unique_ptr<IRadioBackend> TestHalConformance::openFirstDevice(const QString &backendId)
{
    std::unique_ptr<IRadioBackend> backend(BackendRegistry::instance().create(backendId));
    if (!backend) {
        QTest::qFail("backend non istanziabile", __FILE__, __LINE__);
        return nullptr;
    }

    QSignalSpy found(backend.get(), &IRadioBackend::deviceFound);
    backend->startDiscovery();
    if (!found.wait(3000) || found.isEmpty())
        return nullptr; // hardware assente: il chiamante decide se skippare

    const auto device = found.first().first().value<DeviceDescriptor>();
    backend->open(device);
    return backend;
}

void TestHalConformance::registryExposesAtLeastOneBackend()
{
    const QStringList ids = BackendRegistry::instance().backendIds();
    QVERIFY2(!ids.isEmpty(), "nessun backend compilato: la build è inutilizzabile");

    // Un id inesistente non deve mai produrre un oggetto.
    QVERIFY(BackendRegistry::instance().create(QStringLiteral("non-esiste")) == nullptr);
}

void TestHalConformance::discoveryIsAsynchronous_data() { addBackendRows(); }

void TestHalConformance::discoveryIsAsynchronous()
{
    QFETCH(QString, backendId);
    std::unique_ptr<IRadioBackend> backend(BackendRegistry::instance().create(backendId));
    QVERIFY(backend);

    QSignalSpy found(backend.get(), &IRadioBackend::deviceFound);
    backend->startDiscovery();

    // Contratto: la discovery non consegna nulla in modo sincrono, altrimenti
    // il core svilupperebbe una dipendenza dal tempismo di un backend.
    QCOMPARE(found.count(), 0);

    if (!found.wait(3000))
        QSKIP("nessun device disponibile per questo backend");

    for (const QList<QVariant> &emission : found) {
        const auto device = emission.first().value<DeviceDescriptor>();
        QVERIFY2(device.isValid(), "descrittore incompleto");
        QCOMPARE(device.backendId, backendId);
        QVERIFY(!device.displayName.isEmpty());
    }

    backend->stopDiscovery();
}

void TestHalConformance::capabilitiesAreSelfConsistent_data() { addBackendRows(); }

void TestHalConformance::capabilitiesAreSelfConsistent()
{
    QFETCH(QString, backendId);
    std::unique_ptr<IRadioBackend> backend(BackendRegistry::instance().create(backendId));
    QVERIFY(backend);

    const BackendCapabilities caps = backend->capabilities();

    QVERIFY2(caps.maxRxChannels >= 1, "un backend deve offrire almeno un canale RX");
    QVERIFY2(caps.maxPanadapters >= 0, "numero di panadattatori negativo");

    if (caps.minFrequencyHz != 0 || caps.maxFrequencyHz != 0)
        QVERIFY2(caps.maxFrequencyHz > caps.minFrequencyHz, "copertura in frequenza invertita");

    // Un backend raw-IQ deve dichiarare le frequenze di campionamento: senza
    // di esse il DSP client non saprebbe come configurarsi.
    if (caps.isRawIq())
        QVERIFY2(!caps.sampleRates.isEmpty(), "backend raw-IQ senza sampleRates dichiarati");

    // La coerenza fra canali ha senso solo se i canali sono più d'uno.
    if (caps.coherentRx)
        QVERIFY2(caps.maxRxChannels > 1, "coherentRx con un solo canale non ha significato");

    QVERIFY(caps.coversFrequency(caps.minFrequencyHz));
    QVERIFY(caps.coversFrequency(caps.maxFrequencyHz));
}

void TestHalConformance::openReportsStateAndDevice_data() { addBackendRows(); }

void TestHalConformance::openReportsStateAndDevice()
{
    QFETCH(QString, backendId);
    auto backend = openFirstDevice(backendId);
    if (!backend)
        QSKIP("nessun device disponibile per questo backend");

    QVERIFY(backend->isOpen());
    QVERIFY(backend->currentDevice().isValid());
    QCOMPARE(backend->currentDevice().backendId, backendId);
    QVERIFY(backend->state() == BackendState::Ready || backend->state() == BackendState::Streaming);
    QVERIFY(backend->sampleRate() > 0.0);
}

void TestHalConformance::closeIsIdempotent_data() { addBackendRows(); }

void TestHalConformance::closeIsIdempotent()
{
    QFETCH(QString, backendId);
    auto backend = openFirstDevice(backendId);
    if (!backend)
        QSKIP("nessun device disponibile per questo backend");

    backend->close();
    QVERIFY(!backend->isOpen());
    QCOMPARE(backend->state(), BackendState::Idle);

    // Una seconda close non deve esplodere né cambiare stato.
    backend->close();
    QVERIFY(!backend->isOpen());
    QCOMPARE(backend->state(), BackendState::Idle);
}

void TestHalConformance::streamDeliversSamples_data() { addBackendRows(); }

void TestHalConformance::streamDeliversSamples()
{
    QFETCH(QString, backendId);
    auto backend = openFirstDevice(backendId);
    if (!backend)
        QSKIP("nessun device disponibile per questo backend");

    const BackendCapabilities caps = backend->capabilities();
    if (!caps.isRawIq())
        QSKIP("backend server-DSP: verificato dal test sull'audio, non sull'IQ");

    QSignalSpy frames(backend.get(), &IRadioBackend::iqFrameReady);
    QVERIFY2(frames.wait(3000), "nessun frame IQ entro 3 secondi");

    SampleRing *ring = backend->iqStream();
    QVERIFY2(ring != nullptr, "un backend raw-IQ deve esporre il ring device-wide");
    QVERIFY2(ring->available() > 0, "frame annunciato ma ring vuoto");

    // I campioni devono essere finiti e in un intervallo sensato: un backend
    // che consegna NaN farebbe esplodere il DSP molto più a valle.
    std::vector<float> samples(1024);
    const std::size_t got = ring->read(samples.data(), samples.size());
    QVERIFY(got > 0);
    for (std::size_t i = 0; i < got; ++i) {
        QVERIFY2(std::isfinite(samples[i]), "campione non finito nel flusso IQ");
        QVERIFY2(std::abs(samples[i]) <= 4.0f, "campione ben oltre il fondo scala");
    }
}

void TestHalConformance::frameSequenceIsMonotonic_data() { addBackendRows(); }

void TestHalConformance::frameSequenceIsMonotonic()
{
    QFETCH(QString, backendId);
    auto backend = openFirstDevice(backendId);
    if (!backend)
        QSKIP("nessun device disponibile per questo backend");
    if (!backend->capabilities().isRawIq())
        QSKIP("backend server-DSP");

    QSignalSpy frames(backend.get(), &IRadioBackend::iqFrameReady);
    QVERIFY(frames.wait(3000));
    while (frames.count() < 5 && frames.wait(2000)) { }
    QVERIFY2(frames.count() >= 2, "troppo pochi frame per verificare l'ordinamento");

    quint64 previous = 0;
    for (const QList<QVariant> &emission : frames) {
        const auto frame = emission.first().value<IqFrame>();
        QVERIFY2(frame.sequence > previous, "sequenza non monotona: i frame arrivano fuori ordine");
        previous = frame.sequence;
        QVERIFY(frame.sampleRate > 0.0);
    }
}

void TestHalConformance::channelLimitIsEnforced_data() { addBackendRows(); }

void TestHalConformance::channelLimitIsEnforced()
{
    QFETCH(QString, backendId);
    auto backend = openFirstDevice(backendId);
    if (!backend)
        QSKIP("nessun device disponibile per questo backend");

    const int limit = backend->capabilities().maxRxChannels;
    QSignalSpy errors(backend.get(), &IRadioBackend::errorOccurred);

    RxChannelConfig config;
    config.frequencyHz = backend->centerFrequency();

    QList<ChannelId> created;
    for (int i = 0; i < limit; ++i) {
        const ChannelId id = backend->createRxChannel(config);
        QVERIFY2(id != kInvalidChannel, "rifiutato un canale entro il limite dichiarato");
        QVERIFY2(!created.contains(id), "handle di canale duplicato");
        created.append(id);
    }
    QCOMPARE(backend->channels().size(), limit);

    // Oltre il limite: rifiuto esplicito, mai un handle silenziosamente valido.
    const ChannelId overflow = backend->createRxChannel(config);
    QCOMPARE(overflow, kInvalidChannel);
    QVERIFY2(!errors.isEmpty(), "limite superato senza segnalare l'errore");
    const auto error = errors.last().first().value<BackendError>();
    QCOMPARE(error.code, BackendError::ResourceExhausted);

    for (ChannelId id : created)
        backend->destroyRxChannel(id);
    QVERIFY(backend->channels().isEmpty());
}

void TestHalConformance::transmitIsRefusedWhenUnsupported_data() { addBackendRows(); }

void TestHalConformance::transmitIsRefusedWhenUnsupported()
{
    QFETCH(QString, backendId);
    auto backend = openFirstDevice(backendId);
    if (!backend)
        QSKIP("nessun device disponibile per questo backend");

    if (backend->capabilities().canTransmit()) {
        QSignalSpy ptt(backend.get(), &IRadioBackend::pttChanged);
        backend->setPtt(true);
        QCOMPARE(ptt.count(), 1);
        QVERIFY(backend->ptt());
        backend->setPtt(false);
        QVERIFY(!backend->ptt());
    } else {
        // Receive-only: il PTT non deve mai attivarsi, nemmeno se richiesto.
        backend->setPtt(true);
        QVERIFY2(!backend->ptt(), "backend receive-only è andato in trasmissione");
    }
}

void TestHalConformance::teardownDuringStreamingIsClean_data() { addBackendRows(); }

void TestHalConformance::teardownDuringStreamingIsClean()
{
    QFETCH(QString, backendId);
    auto backend = openFirstDevice(backendId);
    if (!backend)
        QSKIP("nessun device disponibile per questo backend");

    RxChannelConfig config;
    config.frequencyHz = backend->centerFrequency();
    backend->createRxChannel(config);

    QSignalSpy frames(backend.get(), &IRadioBackend::iqFrameReady);
    frames.wait(1500);

    // Chiusura a flusso attivo: è il caso che fa cadere i backend scritti male.
    backend->close();
    QVERIFY(!backend->isOpen());
    QVERIFY(backend->channels().isEmpty());

    // Nessun frame deve arrivare dopo la chiusura.
    frames.clear();
    QTest::qWait(300);
    QCOMPARE(frames.count(), 0);
}

void TestHalConformance::unknownNativeCommandReturnsInvalid_data() { addBackendRows(); }

void TestHalConformance::unknownNativeCommandReturnsInvalid()
{
    QFETCH(QString, backendId);
    std::unique_ptr<IRadioBackend> backend(BackendRegistry::instance().create(backendId));
    QVERIFY(backend);

    // La valvola di sfogo deve degradare, non lanciare: nessuna eccezione
    // attraversa il seam (§4.1).
    const QVariant result =
        backend->nativeCommand(QStringLiteral("comando.inesistente"), QVariantMap());
    QVERIFY(!result.isValid());
}

QTEST_MAIN(TestHalConformance)

#include "tst_hal_conformance.moc"
