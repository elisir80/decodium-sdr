// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/IqAnalysisLab.h"
#include <QDataStream>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>
#include <cmath>

using dsdr::core::IqAnalysisLab;

static bool writeRecording(const QString &path, qint16 i, qint16 q)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    QDataStream s(&f); s.setByteOrder(QDataStream::LittleEndian);
    s.writeRawData("RIFF", 4); s << quint32(36 + 4096*4);
    s.writeRawData("WAVEfmt ", 8); s << quint32(16) << quint16(1) << quint16(2)
       << quint32(48000) << quint32(192000) << quint16(4) << quint16(16);
    s.writeRawData("data", 4); s << quint32(4096*4);
    for (int n=0; n<4096; ++n) s << i << q;
    return s.status() == QDataStream::Ok;
}
class TestIqAnalysis : public QObject {
    Q_OBJECT
private slots:
    void compareAndHandleErrors();
    void cancellation();
    void spectralSegments();
};
void TestIqAnalysis::compareAndHandleErrors()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    const QString a = dir.filePath("a.wav"), b = dir.filePath("b.wav");
    QVERIFY(writeRecording(a, 8192, 0));
    QVERIFY(writeRecording(b, 16384, 0));
    IqAnalysisLab lab;
    QVERIFY(lab.start(QUrl::fromLocalFile(a).toString(), 0));
    QVERIFY(lab.running());
    QVERIFY(!lab.start(b, 1));
    QTRY_VERIFY_WITH_TIMEOUT(!lab.running(), 10000);
    QVERIFY2(lab.error().isEmpty(), qPrintable(lab.error()));
    QVERIFY(lab.start(b, 1));
    QTRY_VERIFY_WITH_TIMEOUT(!lab.running(), 10000);
    QVERIFY2(lab.error().isEmpty(), qPrintable(lab.error()));
    QCOMPARE(lab.resultA().value("frames").toLongLong(), 4096);
    QCOMPARE(lab.resultA().value("sampleRate").toDouble(), 48000.0);
    QVERIFY(std::abs(lab.resultA().value("rms").toDouble() - 0.25) < 1e-6);
    QVERIFY(std::abs(lab.resultB().value("rms").toDouble() - 0.5) < 1e-6);
    QVERIFY(std::abs(lab.resultB().value("meanI").toDouble() - 0.5) < 1e-6);
    QCOMPARE(lab.resultB().value("clippedPercent").toDouble(), 0.0);
    QVERIFY(writeRecording(b, 32767, 0));
    QVERIFY(lab.start(b, 1));
    QTRY_VERIFY_WITH_TIMEOUT(!lab.running(), 10000);
    QCOMPARE(lab.resultB().value("clippedPercent").toDouble(), 100.0);
    QVERIFY(lab.start(dir.filePath("missing.wav"), 1));
    QTRY_VERIFY_WITH_TIMEOUT(!lab.running(), 10000);
    QVERIFY(!lab.error().isEmpty());
    QVERIFY(lab.resultB().isEmpty());
    QVERIFY(!lab.resultA().isEmpty());
}
void TestIqAnalysis::cancellation()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    const QString a = dir.filePath("a.wav"); QVERIFY(writeRecording(a, 1, 0));
    IqAnalysisLab lab;
    QVERIFY(lab.start(a, 0)); lab.cancel();
    QTRY_VERIFY_WITH_TIMEOUT(!lab.running(), 10000);
    QVERIFY(lab.resultA().isEmpty());
    QVERIFY(!lab.error().isEmpty());
}
void TestIqAnalysis::spectralSegments()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    const QString path = dir.filePath("tones.wav");
    QFile f(path); QVERIFY(f.open(QIODevice::WriteOnly));
    QDataStream s(&f); s.setByteOrder(QDataStream::LittleEndian);
    constexpr int frames = 8192;
    s.writeRawData("RIFF", 4); s << quint32(36 + frames*4);
    s.writeRawData("WAVEfmt ", 8); s << quint32(16) << quint16(1) << quint16(2)
       << quint32(48000) << quint32(192000) << quint16(4) << quint16(16);
    s.writeRawData("data", 4); s << quint32(frames*4);
    for (int n = 0; n < frames; ++n) {
        const double hz = n < 4096 ? 3000.0 : -6000.0;
        const double phase = 2 * 3.14159265358979323846 * hz * n / 48000.0;
        s << qint16(std::lround(16384 * std::cos(phase)))
          << qint16(std::lround(16384 * std::sin(phase)));
    }
    f.close();
    IqAnalysisLab lab;
    QVERIFY(lab.analyze(path, 0, 0, 4096.0/48000, 1024, 0));
    QTRY_VERIFY_WITH_TIMEOUT(!lab.running(), 10000);
    QVERIFY2(lab.error().isEmpty(), qPrintable(lab.error()));
    QVERIFY(lab.analyze(path, 1, 4096.0/48000, 4096.0/48000, 1024, 1));
    QTRY_VERIFY_WITH_TIMEOUT(!lab.running(), 10000);
    QVERIFY2(lab.error().isEmpty(), qPrintable(lab.error()));
    const auto a = lab.resultA(), b = lab.resultB();
    QCOMPARE(a.value("peakOffsetHz").toDouble(), 3000.0);
    QCOMPARE(b.value("peakOffsetHz").toDouble(), -6000.0);
    QCOMPARE(a.value("bandwidth3dbHz").toDouble(), 48000.0/1024);
    QVERIFY(std::abs(a.value("peakDbfs").toDouble() + 6.0206) < 0.02);
    QVERIFY(std::abs(b.value("peakDbfs").toDouble() + 6.0206) < 0.02);
    QCOMPARE(a.value("spectrum").toList().size(), 1024);
    QCOMPARE(a.value("frames").toLongLong(), 4096);
    QCOMPARE(b.value("frames").toLongLong(), 4096);
    QVERIFY(lab.analyze(path, 1, 0, 0.001, 1024, 1));
    QTRY_VERIFY_WITH_TIMEOUT(!lab.running(), 10000);
    QVERIFY(!lab.error().isEmpty());
    QVERIFY(lab.resultB().isEmpty());
    QVERIFY(!lab.resultA().isEmpty());
    QVERIFY(!lab.analyze(path, 0, 0, 0, 1000, 1));
}
QTEST_MAIN(TestIqAnalysis)
#include "tst_iq_analysis.moc"
