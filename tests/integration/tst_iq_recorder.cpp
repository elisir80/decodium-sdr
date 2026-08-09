// SPDX-License-Identifier: GPL-3.0-or-later
// Registrazione IQ (RF-17): il file prodotto deve essere leggibile da altri
// strumenti, non solo da noi.
//
// Non basta che la scrittura non fallisca: un WAV con un'intestazione
// sbagliata si apre lo stesso e mostra rumore, e ce ne si accorge settimane
// dopo quando la registrazione serviva davvero.

#include "core/IqRecorder.h"
#include "core/SessionManager.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>
#include <QtEndian>

using namespace dsdr;
using namespace dsdr::core;

namespace {

/// Legge un chunk RIFF individuandolo per tag.
QByteArray findChunk(const QByteArray &wav, const char *tag, qint64 *payloadOffset = nullptr)
{
    // I chunk iniziano dopo "RIFF"+size+"WAVE".
    qint64 offset = 12;
    while (offset + 8 <= wav.size()) {
        const QByteArray id = wav.mid(offset, 4);
        const quint32 size = qFromLittleEndian<quint32>(wav.constData() + offset + 4);
        if (id == tag) {
            if (payloadOffset)
                *payloadOffset = offset + 8;
            return wav.mid(offset + 8, static_cast<int>(std::min<quint64>(size, 1u << 20)));
        }
        offset += 8 + size + (size & 1);
    }
    return QByteArray();
}

} // namespace

class TestIqRecorder : public QObject
{
    Q_OBJECT

private slots:
    void suggestedNameIsSortableAndInformative();
    void suggestedAudioNameIsExplicit();
    void producesReadableWavWithCorrectHeader();
    void recordsAudioMixWithAudioSidecar();
    void writesSidecarWithTuningMetadata();
    void recordingStopsCleanlyOnDisconnect();

private:
    static bool waitFor(std::function<bool()> predicate, int timeoutMs = 6000)
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

    static bool connectDemo(SessionManager &session)
    {
        session.selectBackend(QStringLiteral("demo"));
        session.startDiscovery();
        if (!waitFor([&] { return session.devices()->rowCount() > 0; }, 3000))
            return false;
        session.connectToDevice(0);
        return session.isConnected();
    }
};

void TestIqRecorder::suggestedNameIsSortableAndInformative()
{
    IqRecordingInfo info;
    info.centerFrequencyHz = 7'100'000;
    info.sampleRate = 192000.0;
    info.startedAt = QDateTime(QDate(2026, 8, 7), QTime(14, 5, 9));

    const QString name = IqRecorder::suggestedFileName(info);

    // Data prima della frequenza: l'ordine alfabetico è anche cronologico.
    QVERIFY2(name.startsWith(QStringLiteral("20260807_140509")), qPrintable(name));
    QVERIFY2(name.contains(QStringLiteral("7.100MHz")), qPrintable(name));
    QVERIFY2(name.endsWith(QStringLiteral(".wav")), qPrintable(name));
}

void TestIqRecorder::suggestedAudioNameIsExplicit()
{
    IqRecordingInfo info;
    info.sampleRate = 48000.0;
    info.audio = true;
    info.startedAt = QDateTime(QDate(2026, 8, 7), QTime(14, 5, 9));

    const QString name = IqRecorder::suggestedFileName(info);
    QVERIFY2(name.contains(QStringLiteral("audio_48kSps")), qPrintable(name));
}

void TestIqRecorder::producesReadableWavWithCorrectHeader()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    SessionManager session;
    QVERIFY2(connectDemo(session), "connessione al backend demo fallita");

    const QString path = dir.filePath(QStringLiteral("prova.wav"));
    QVERIFY2(session.startRecording(path), "avvio registrazione fallito");
    QVERIFY(session.recorder()->isRecording());

    QVERIFY2(waitFor([&] { return session.recorder()->bytesWritten() > 100000; }),
             "nessun campione finito su disco");

    session.stopRecording();
    QVERIFY(!session.recorder()->isRecording());

    QFile file(path);
    QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(path));
    const QByteArray wav = file.readAll();
    QVERIFY(wav.size() > 1024);

    // ── Intestazione RIFF/WAVE ─────────────────────────────────────────
    QCOMPARE(wav.left(4), QByteArray("RIFF"));
    QCOMPARE(wav.mid(8, 4), QByteArray("WAVE"));

    const quint32 riffSize = qFromLittleEndian<quint32>(wav.constData() + 4);
    QCOMPARE(static_cast<qint64>(riffSize) + 8, static_cast<qint64>(wav.size()));

    // ── Chunk fmt ──────────────────────────────────────────────────────
    const QByteArray fmt = findChunk(wav, "fmt ");
    QCOMPARE(fmt.size(), 16);

    const quint16 format = qFromLittleEndian<quint16>(fmt.constData());
    const quint16 channels = qFromLittleEndian<quint16>(fmt.constData() + 2);
    const quint32 rate = qFromLittleEndian<quint32>(fmt.constData() + 4);
    const quint16 bits = qFromLittleEndian<quint16>(fmt.constData() + 14);

    QCOMPARE(format, quint16(3));      // IEEE float
    QCOMPARE(channels, quint16(2));    // I e Q
    QCOMPARE(bits, quint16(32));
    QCOMPARE(static_cast<double>(rate), session.sampleRate());

    // ── Chunk data ─────────────────────────────────────────────────────
    qint64 dataOffset = 0;
    findChunk(wav, "data", &dataOffset);
    QVERIFY2(dataOffset > 0, "chunk data assente");

    const quint32 dataSize =
        qFromLittleEndian<quint32>(wav.constData() + dataOffset - 4);
    QVERIFY2(dataSize > 0, "chunk data vuoto");
    QCOMPARE(dataOffset + static_cast<qint64>(dataSize), static_cast<qint64>(wav.size()));
    QCOMPARE(dataSize % (channels * (bits / 8)), 0u); // nessun campione troncato

    // ── I campioni ─────────────────────────────────────────────────────
    const auto *samples = reinterpret_cast<const float *>(wav.constData() + dataOffset);
    const int count = static_cast<int>(dataSize / sizeof(float));
    QVERIFY(count > 1000);

    bool anyNonZero = false;
    for (int i = 0; i < count; ++i) {
        QVERIFY2(std::isfinite(samples[i]), "campione non finito nel file");
        QVERIFY2(std::abs(samples[i]) <= 4.0f, "campione ben oltre il fondo scala");
        anyNonZero = anyNonZero || samples[i] != 0.0f;
    }
    QVERIFY2(anyNonZero, "il file contiene solo silenzio");
}

void TestIqRecorder::recordsAudioMixWithAudioSidecar()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    SessionManager session;
    QVERIFY2(connectDemo(session), "connessione al backend demo fallita");

    const QString path = dir.filePath(QStringLiteral("audio.wav"));
    QVERIFY2(session.startAudioRecording(path), "avvio registrazione audio fallito");
    QVERIFY(session.audioRecorder()->isRecording());
    QVERIFY2(waitFor([&] { return session.audioRecorder()->bytesWritten() > 10000; }),
             "nessun audio finito su disco");
    session.stopAudioRecording();

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray wav = file.readAll();
    QCOMPARE(wav.left(4), QByteArray("RIFF"));
    QCOMPARE(wav.mid(8, 4), QByteArray("WAVE"));
    const QByteArray fmt = findChunk(wav, "fmt ");
    QCOMPARE(qFromLittleEndian<quint16>(fmt.constData() + 2), quint16(2));
    QCOMPARE(qFromLittleEndian<quint32>(fmt.constData() + 4), quint32(48000));

    QFile sidecar(dir.filePath(QStringLiteral("audio.json")));
    QVERIFY(sidecar.open(QIODevice::ReadOnly));
    const QJsonObject root = QJsonDocument::fromJson(sidecar.readAll()).object();
    QCOMPARE(root.value(QStringLiteral("format")).toString(),
             QStringLiteral("decodium-audio/1"));
    QCOMPARE(root.value(QStringLiteral("channels")).toInt(), 2);
}

void TestIqRecorder::writesSidecarWithTuningMetadata()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    SessionManager session;
    QVERIFY(connectDemo(session));

    const QString path = dir.filePath(QStringLiteral("meta.wav"));
    QVERIFY(session.startRecording(path));
    QVERIFY(waitFor([&] { return session.recorder()->bytesWritten() > 50000; }));
    session.stopRecording();

    // Il WAV sa dire quanti campioni al secondo, non su quale frequenza:
    // senza sidecar la registrazione è una sequenza di numeri senza senso.
    QFile sidecar(dir.filePath(QStringLiteral("meta.json")));
    QVERIFY2(sidecar.open(QIODevice::ReadOnly), "sidecar dei metadati assente");

    const QJsonObject root = QJsonDocument::fromJson(sidecar.readAll()).object();
    QCOMPARE(root.value(QStringLiteral("centerFrequencyHz")).toInteger(),
             session.centerFrequency());
    QCOMPARE(root.value(QStringLiteral("sampleRate")).toDouble(), session.sampleRate());
    QCOMPARE(root.value(QStringLiteral("sampleFormat")).toString(), QStringLiteral("float32"));
    QCOMPARE(root.value(QStringLiteral("channels")).toInt(), 2);
    QCOMPARE(root.value(QStringLiteral("backendId")).toString(), QStringLiteral("demo"));
    QVERIFY(!root.value(QStringLiteral("startedAt")).toString().isEmpty());
    QVERIFY(root.value(QStringLiteral("durationMs")).toInteger() > 0);
}

void TestIqRecorder::recordingStopsCleanlyOnDisconnect()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    SessionManager session;
    QVERIFY(connectDemo(session));

    const QString path = dir.filePath(QStringLiteral("interrotta.wav"));
    QVERIFY(session.startRecording(path));
    QVERIFY(waitFor([&] { return session.recorder()->bytesWritten() > 20000; }));

    // Disconnettersi con la registrazione aperta è il caso che lascia i file
    // con l'intestazione incompleta se non lo si gestisce.
    session.disconnectDevice();
    QVERIFY(!session.recorder()->isRecording());

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray wav = file.readAll();

    const quint32 riffSize = qFromLittleEndian<quint32>(wav.constData() + 4);
    QCOMPARE(static_cast<qint64>(riffSize) + 8, static_cast<qint64>(wav.size()));
    QVERIFY2(QFile::exists(dir.filePath(QStringLiteral("interrotta.json"))),
             "sidecar non scritto sulla chiusura forzata");
}

QTEST_MAIN(TestIqRecorder)

#include "tst_iq_recorder.moc"
