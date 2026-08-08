// SPDX-License-Identifier: GPL-3.0-or-later
// Il cerchio completo: si registra, si riapre, si riascolta.
//
// I due lati — `IqRecorder` che scrive e il backend `iqfile` che legge — sono
// scritti in momenti diversi e stanno in strati diversi. È esattamente il
// genere di coppia che smette di combaciare senza che nessuno se ne accorga,
// perché ciascuna metà continua a passare i propri test: il registratore
// scrive un file valido, il lettore legge file validi, e intanto il campo che
// li lega è cambiato di posto.

#include "hal/BackendRegistry.h"
#include "hal/IRadioBackend.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QtEndian>

#include <cmath>
#include <vector>

using namespace dsdr;
using namespace dsdr::hal;

namespace {

constexpr double kSampleRate = 192000.0;
constexpr qint64 kCenterHz = 7'100'000;

void appendTag(QByteArray &out, const char *tag) { out.append(tag, 4); }

void appendU32(QByteArray &out, quint32 v)
{
    char b[4];
    qToLittleEndian<quint32>(v, b);
    out.append(b, 4);
}

void appendU16(QByteArray &out, quint16 v)
{
    char b[2];
    qToLittleEndian<quint16>(v, b);
    out.append(b, 2);
}

/// Tono complesso a frequenza nota: riconoscibile dopo il giro su disco anche
/// se qualcosa ha scambiato I con Q o dimezzato il rate.
std::vector<float> makeTone(int frames, double toneHz)
{
    std::vector<float> data(static_cast<std::size_t>(frames) * 2);
    for (int i = 0; i < frames; ++i) {
        const double phase = 2.0 * M_PI * toneHz * i / kSampleRate;
        data[static_cast<std::size_t>(i) * 2] = static_cast<float>(std::cos(phase));
        data[static_cast<std::size_t>(i) * 2 + 1] = static_cast<float>(std::sin(phase));
    }
    return data;
}

/// Scrive un WAV float32 a due canali con lo stesso layout del registratore.
bool writeWav(const QString &path, const std::vector<float> &interleaved)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;

    const quint32 dataBytes = static_cast<quint32>(interleaved.size() * sizeof(float));

    QByteArray header;
    appendTag(header, "RIFF");
    appendU32(header, 36 + dataBytes);
    appendTag(header, "WAVE");
    appendTag(header, "fmt ");
    appendU32(header, 16);
    appendU16(header, 3);                      // IEEE float
    appendU16(header, 2);                      // I e Q
    appendU32(header, static_cast<quint32>(kSampleRate));
    appendU32(header, static_cast<quint32>(kSampleRate) * 2 * 4);
    appendU16(header, 8);
    appendU16(header, 32);
    appendTag(header, "data");
    appendU32(header, dataBytes);

    return file.write(header) == header.size()
        && file.write(reinterpret_cast<const char *>(interleaved.data()), dataBytes) == dataBytes;
}

bool writeSidecar(const QString &wavPath, qint64 centerHz)
{
    QJsonObject root;
    root.insert(QStringLiteral("format"), QStringLiteral("decodium-iq/1"));
    root.insert(QStringLiteral("centerFrequencyHz"), centerHz);
    root.insert(QStringLiteral("sampleRate"), kSampleRate);
    root.insert(QStringLiteral("backendId"), QStringLiteral("demo"));
    root.insert(QStringLiteral("deviceName"), QStringLiteral("DECODIUM Demo — HF 40 m"));

    QFileInfo info(wavPath);
    QFile sidecar(info.absolutePath() + QLatin1Char('/') + info.completeBaseName()
                  + QStringLiteral(".json"));
    if (!sidecar.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    sidecar.write(QJsonDocument(root).toJson());
    return true;
}

} // namespace

class TestIqFilePlayback : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void discoveryFindsTheRecording();
    void openReportsRecordedTuning();
    void playbackDeliversTheRecordedSamples();
    void tuningIsRefusedAndStaysTruthful();
    void transportControlsWork();
    void missingSidecarFallsBackToTheFileName();
    void unreadableFileIsRejectedNotCrashed();

private:
    QTemporaryDir m_dir;
    QString m_wavPath;

    static bool waitUntilStreaming(IRadioBackend *backend, int timeoutMs = 5000)
    {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < timeoutMs) {
            if (backend->state() == BackendState::Streaming)
                return true;
            if (backend->state() == BackendState::Error)
                return false;
            QTest::qWait(20);
        }
        return backend->state() == BackendState::Streaming;
    }

    std::unique_ptr<IRadioBackend> openRecording(const QString &path)
    {
        std::unique_ptr<IRadioBackend> backend(
            BackendRegistry::instance().create(QStringLiteral("iqfile")));
        if (!backend)
            return nullptr;

        DeviceDescriptor device;
        device.backendId = QStringLiteral("iqfile");
        device.deviceId = path;
        device.extra.insert(QStringLiteral("path"), path);
        backend->open(device);
        return backend;
    }
};

void TestIqFilePlayback::initTestCase()
{
    QVERIFY(m_dir.isValid());
    registerBuiltinBackends();

    if (!BackendRegistry::instance().backendIds().contains(QStringLiteral("iqfile")))
        QSKIP("backend iqfile non compilato in questa build");

    // Cinque secondi di tono: abbastanza perché la riproduzione a velocità
    // reale abbia di che scorrere senza far durare il test.
    m_wavPath = m_dir.filePath(QStringLiteral("20260101_120000_7.100MHz_192kSps.wav"));
    QVERIFY(writeWav(m_wavPath, makeTone(static_cast<int>(kSampleRate) * 5, 1000.0)));
    QVERIFY(writeSidecar(m_wavPath, kCenterHz));

    qputenv("DSDR_IQFILE_PATH", m_dir.path().toLocal8Bit());
}

void TestIqFilePlayback::discoveryFindsTheRecording()
{
    std::unique_ptr<IRadioBackend> backend(
        BackendRegistry::instance().create(QStringLiteral("iqfile")));
    QVERIFY(backend);

    QSignalSpy found(backend.get(), &IRadioBackend::deviceFound);
    backend->startDiscovery();

    // Contratto della HAL: mai nulla di sincrono.
    QCOMPARE(found.count(), 0);
    QVERIFY2(found.wait(3000), "la registrazione non è stata trovata");

    bool seen = false;
    for (const QList<QVariant> &emission : found) {
        const auto device = emission.first().value<DeviceDescriptor>();
        QCOMPARE(device.backendId, QStringLiteral("iqfile"));
        QVERIFY(device.isValid());
        if (device.extra.value(QStringLiteral("path")).toString() == m_wavPath)
            seen = true;
    }
    QVERIFY2(seen, "la registrazione creata dal test non compare fra i device");
}

void TestIqFilePlayback::openReportsRecordedTuning()
{
    auto backend = openRecording(m_wavPath);
    QVERIFY(backend);
    QVERIFY(waitUntilStreaming(backend.get()));

    // I due numeri che rendono utile una registrazione: dove eravamo
    // sintonizzati e quanto larga era la finestra.
    QCOMPARE(backend->centerFrequency(), kCenterHz);
    QCOMPARE(backend->sampleRate(), kSampleRate);

    // Aperta la registrazione, il rate dichiarato è solo il suo: offrirne
    // altri sarebbe una promessa che il backend non può mantenere.
    const auto caps = backend->capabilities();
    QCOMPARE(caps.sampleRates.size(), 1);
    QCOMPARE(caps.sampleRates.first(), kSampleRate);
    QVERIFY(!caps.canTransmit());
}

void TestIqFilePlayback::playbackDeliversTheRecordedSamples()
{
    auto backend = openRecording(m_wavPath);
    QVERIFY(backend);
    QVERIFY(waitUntilStreaming(backend.get()));

    QSignalSpy frames(backend.get(), &IRadioBackend::iqFrameReady);
    QVERIFY2(frames.wait(3000), "nessun frame IQ dalla registrazione");

    SampleRing *ring = backend->iqStream();
    QVERIFY(ring);
    QVERIFY(ring->available() > 0);

    std::vector<float> samples(4096);
    const std::size_t got = ring->read(samples.data(), samples.size());
    QVERIFY(got >= 2);

    // Il tono registrato ha modulo unitario: se il lettore avesse sbagliato
    // formato, allineamento o scala, il modulo non sarebbe più uno.
    for (std::size_t i = 0; i + 1 < got; i += 2) {
        const double magnitude = std::hypot(samples[i], samples[i + 1]);
        QVERIFY2(std::isfinite(magnitude), "campione non finito dalla registrazione");
        QVERIFY2(std::abs(magnitude - 1.0) < 0.02,
                 qPrintable(QStringLiteral("modulo %1 invece di 1").arg(magnitude)));
    }

    // La riproduzione va al ritmo della registrazione, non a quello del disco:
    // in un secondo di attesa non può uscire un file da cinque.
    const qint64 durationMs = 5000;
    QTest::qWait(300);
    const qint64 position = backend->nativeCommand(QStringLiteral("iqfile.status"), {})
                                .toMap()
                                .value(QStringLiteral("positionMs"))
                                .toLongLong();
    QVERIFY2(position < durationMs,
             qPrintable(QStringLiteral("riprodotti %1 ms in 300 ms: troppo veloce")
                            .arg(position)));
}

void TestIqFilePlayback::tuningIsRefusedAndStaysTruthful()
{
    auto backend = openRecording(m_wavPath);
    QVERIFY(backend);
    QVERIFY(waitUntilStreaming(backend.get()));

    QSignalSpy errors(backend.get(), &IRadioBackend::errorOccurred);

    // Spostare la frequenza di una registrazione significherebbe mentire su
    // cosa contengono i campioni: il rifiuto dev'essere esplicito e la
    // frequenza deve restare quella vera.
    backend->setCenterFrequency(kCenterHz + 1'000'000);
    QVERIFY2(!errors.isEmpty(), "sintonia accettata in silenzio su una registrazione");
    QCOMPARE(backend->centerFrequency(), kCenterHz);

    errors.clear();
    backend->setSampleRate(48000.0);
    QVERIFY(!errors.isEmpty());
    QCOMPARE(backend->sampleRate(), kSampleRate);

    errors.clear();
    backend->setPtt(true);
    QVERIFY2(!errors.isEmpty(), "PTT accettato su una registrazione");
    QVERIFY(!backend->ptt());
}

void TestIqFilePlayback::transportControlsWork()
{
    auto backend = openRecording(m_wavPath);
    QVERIFY(backend);
    QVERIFY(waitUntilStreaming(backend.get()));

    // Pausa: la posizione smette di avanzare.
    backend->nativeCommand(QStringLiteral("iqfile.setPaused"),
                           {{QStringLiteral("paused"), true}});
    QTest::qWait(150);
    const auto atPause = backend->nativeCommand(QStringLiteral("iqfile.status"), {})
                             .toMap()
                             .value(QStringLiteral("positionMs"))
                             .toLongLong();
    QTest::qWait(300);
    const auto stillThere = backend->nativeCommand(QStringLiteral("iqfile.status"), {})
                                .toMap()
                                .value(QStringLiteral("positionMs"))
                                .toLongLong();
    QCOMPARE(stillThere, atPause);

    // Riavvolgimento e ripresa.
    backend->nativeCommand(QStringLiteral("iqfile.seek"), {{QStringLiteral("ms"), 2000}});
    backend->nativeCommand(QStringLiteral("iqfile.setPaused"),
                           {{QStringLiteral("paused"), false}});
    QTest::qWait(250);

    const auto status = backend->nativeCommand(QStringLiteral("iqfile.status"), {}).toMap();
    QVERIFY(status.value(QStringLiteral("positionMs")).toLongLong() >= 2000);
    QCOMPARE(status.value(QStringLiteral("durationMs")).toLongLong(), 5000);
    QCOMPARE(status.value(QStringLiteral("paused")).toBool(), false);

    const auto speed = backend->nativeCommand(QStringLiteral("iqfile.setSpeed"),
                                              {{QStringLiteral("speed"), 99.0}});
    QVERIFY2(speed.toDouble() <= 8.0, "velocità non limitata");
}

void TestIqFilePlayback::missingSidecarFallsBackToTheFileName()
{
    // Un file senza sidecar capita: copiato a mano, scaricato, scompattato da
    // un archivio. Meglio una frequenza dedotta dal nome che una radio
    // sintonizzata su zero.
    const QString orphan = m_dir.filePath(QStringLiteral("dump_14.074MHz_192kSps.wav"));
    QVERIFY(writeWav(orphan, makeTone(48000, 1000.0)));

    auto backend = openRecording(orphan);
    QVERIFY(backend);
    QVERIFY(waitUntilStreaming(backend.get()));
    QCOMPARE(backend->centerFrequency(), 14'074'000);

    const auto status = backend->nativeCommand(QStringLiteral("iqfile.status"), {}).toMap();
    QCOMPARE(status.value(QStringLiteral("hasSidecar")).toBool(), false);
}

void TestIqFilePlayback::unreadableFileIsRejectedNotCrashed()
{
    const QString junk = m_dir.filePath(QStringLiteral("non-un-wav.wav"));
    QFile file(junk);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("questo non e' un RIFF, e' solo testo");
    file.close();

    std::unique_ptr<IRadioBackend> backend(
        BackendRegistry::instance().create(QStringLiteral("iqfile")));
    QVERIFY(backend);

    QSignalSpy errors(backend.get(), &IRadioBackend::errorOccurred);

    DeviceDescriptor device;
    device.backendId = QStringLiteral("iqfile");
    device.deviceId = junk;
    device.extra.insert(QStringLiteral("path"), junk);
    backend->open(device);

    QVERIFY2(!errors.isEmpty(), "file illeggibile aperto senza protestare");
    QVERIFY(!backend->isOpen());
    QCOMPARE(backend->state(), BackendState::Error);

    // E la discovery non deve inciamparci: la cartella delle registrazioni può
    // contenere qualunque WAV.
    std::unique_ptr<IRadioBackend> scanner(
        BackendRegistry::instance().create(QStringLiteral("iqfile")));
    QSignalSpy found(scanner.get(), &IRadioBackend::deviceFound);
    QSignalSpy done(scanner.get(), &IRadioBackend::discoveryFinished);
    scanner->startDiscovery();
    QVERIFY(done.wait(3000));
    for (const QList<QVariant> &emission : found) {
        const auto seen = emission.first().value<DeviceDescriptor>();
        QVERIFY2(seen.extra.value(QStringLiteral("path")).toString() != junk,
                 "un file illeggibile è stato annunciato come device");
    }
}

QTEST_MAIN(TestIqFilePlayback)
#include "tst_iqfile_playback.moc"
