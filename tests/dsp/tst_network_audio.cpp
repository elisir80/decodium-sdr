// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio/NetworkAudioSink.h"

#include <QElapsedTimer>
#include <QHostAddress>
#include <QTest>
#include <QUdpSocket>
#include <QtEndian>

#include <vector>

using dsdr::audio::NetworkAudioSink;

class TestNetworkAudio : public QObject
{
    Q_OBJECT

private slots:
    void udpIsRawPcm16At48k();
};

void TestNetworkAudio::udpIsRawPcm16At48k()
{
    QUdpSocket receiver;
    QVERIFY(receiver.bind(QHostAddress::LocalHost, 0));

    NetworkAudioSink sink;
    NetworkAudioSink::Config config;
    config.protocol = NetworkAudioSink::Protocol::Udp;
    config.host = QStringLiteral("127.0.0.1");
    config.port = receiver.localPort();
    config.stereo = true;
    QVERIFY(sink.start(config));

    QElapsedTimer started;
    started.start();
    while (!sink.isActive() && started.elapsed() < 5'000)
        QTest::qWait(10);
    QVERIFY2(sink.isActive(), qPrintable(sink.errorString()));

    // Ventidue millisecondi: il primo pacchetto emesso può essere silenzio
    // (il timer è già vivo); qui il successivo deve contenere questa coppia.
    std::vector<float> samples(960 * 2);
    for (int frame = 0; frame < 960; ++frame) {
        samples[static_cast<std::size_t>(frame) * 2] = 0.5f;
        samples[static_cast<std::size_t>(frame) * 2 + 1] = -0.25f;
    }
    sink.feed(samples.data(), 960);

    QByteArray payload;
    QElapsedTimer waited;
    waited.start();
    while (waited.elapsed() < 5'000) {
        if (!receiver.hasPendingDatagrams()) {
            QTest::qWait(10);
            continue;
        }
        QByteArray candidate;
        candidate.resize(static_cast<int>(receiver.pendingDatagramSize()));
        receiver.readDatagram(candidate.data(), candidate.size());
        if (candidate.size() != 960 * 2 * static_cast<int>(sizeof(qint16)))
            continue;
        if (qFromLittleEndian<qint16>(candidate.constData()) != 0) {
            payload = candidate;
            break;
        }
    }

    QVERIFY2(!payload.isEmpty(), "nessun pacchetto PCM non silenzioso ricevuto");
    QCOMPARE(payload.size(), 960 * 2 * static_cast<int>(sizeof(qint16)));
    QCOMPARE(qFromLittleEndian<qint16>(payload.constData()), static_cast<qint16>(16384));
    QCOMPARE(qFromLittleEndian<qint16>(payload.constData() + sizeof(qint16)),
             static_cast<qint16>(-8192));

    const QVariantMap status = sink.status();
    QCOMPARE(status.value(QStringLiteral("sampleRate")).toInt(), 48'000);
    // Il timer può aver già consegnato anche il pacchetto seguente: la
    // proprietà che conta è che almeno quello con il nostro blocco sia uscito.
    QVERIFY(status.value(QStringLiteral("framesSent")).toULongLong() >= quint64(960));
    sink.stop();
    QVERIFY(!sink.isActive());
}

QTEST_MAIN(TestNetworkAudio)
#include "tst_network_audio.moc"
