// SPDX-License-Identifier: GPL-3.0-or-later
#define main aprs_fixture_main
#include "../standalone/tst_aprs_afsk.cpp"
#undef main
#include "core/SessionManager.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <QtEndian>
using namespace dsdr;
using namespace dsdr::core;
namespace {
void u32(QByteArray &b,quint32 n) { char p[4]; qToLittleEndian(n,p); b.append(p,4); }
void u16(QByteArray &b,quint16 n) { char p[2]; qToLittleEndian(n,p); b.append(p,2); }
bool makeWav(const QString &path, bool valid, bool ambiguous, bool object, bool item, bool compressed, bool truncated) {
    constexpr unsigned rate=192000;
    auto packet=frame();
    if(object || item || compressed || truncated) {
        packet.resize(23);
        const std::string info=truncated ? "/" : compressed ? "=d5L!!<*e7>7P[" : item ? ")WX1234!3554.00N/01430.00E-Test" : ";TEST     *121234z3554.00N/01430.00E-Test";
        for(char c:info) packet.push_back(c);
        const auto crc=Decoder::fcs(packet.data(),packet.size());
        packet.push_back(crc&255); packet.push_back(crc>>8);
    }
    if(ambiguous) {
        packet[30]=' '; // ambiguous latitude minute, with valid AX.25 FCS
        const auto crc=Decoder::fcs(packet.data(),packet.size()-2);
        packet[packet.size()-2]=crc&255; packet.back()=crc>>8;
    }
    if(!valid) packet.back()^=1;
    const auto audio=waveform(packet,48000,0.3,5);
    QByteArray block; double phase=0;
    for(std::size_t i=0;i<audio.size()*4;++i) {
        phase+=2*pi*3000.0*audio[i/4]/rate;
        const float pair[2]={float(.7*std::cos(phase)),float(.7*std::sin(phase))};
        block.append(reinterpret_cast<const char *>(pair),sizeof(pair));
    }
    block.append(QByteArray(int(rate/10*8),0));
    constexpr int repeats=20; const quint32 bytes=quint32(block.size()*repeats);
    QByteArray h; h.append("RIFF",4); u32(h,36+bytes); h.append("WAVEfmt ",8); u32(h,16);
    u16(h,3); u16(h,2); u32(h,rate); u32(h,rate*8); u16(h,8); u16(h,32); h.append("data",4); u32(h,bytes);
    QFile f(path); if(!f.open(QIODevice::WriteOnly) || f.write(h)!=h.size()) return false;
    for(int i=0;i<repeats;++i) if(f.write(block)!=block.size()) return false;
    QFile meta(QFileInfo(path).absolutePath()+"/aprs-session.json"); if(!meta.open(QIODevice::WriteOnly)) return false;
    meta.write(QJsonDocument(QJsonObject{{"format","decodium-iq/1"},{"sampleRate",int(rate)},{"centerFrequencyHz",144800000}}).toJson());
    return true;
}
}
class AprsIqSession : public QObject {
    Q_OBJECT
private slots:
    void recordedIqToDialog() {
        const QString path=qEnvironmentVariable("DSDR_TEST_APRS_RECORDED_IQ");
        if(path.isEmpty()) QSKIP("Optional external APRS IQ fixture not provided");
        QVERIFY(qEnvironmentVariableIntValue("DSDR_TEST_SILENT_AUDIO")==1);
        QTemporaryDir temp(QDir::tempPath()+"/dsdr-aprs-real-XXXXXX");QVERIFY(temp.isValid());
        QCoreApplication::setOrganizationName("DSDR-aprs-test");QCoreApplication::setApplicationName("recorded-session");
        QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,temp.path());
        qputenv("DSDR_TEST_APPDATA_DIR",temp.path().toUtf8());qputenv("DSDR_IQFILE_PATH",path.toUtf8());
        QVERIFY(QDir().mkpath(temp.path()+"/qml/DecodiumSdr"));QFile module(temp.path()+"/qml/DecodiumSdr/qmldir");
        QVERIFY(module.open(QIODevice::WriteOnly));module.write("module DecodiumSdr\nsingleton Theme 1.0 Theme.qml\n");module.close();
        QVERIFY(QFile::copy(QStringLiteral(DSDR_APRS_QML_DIR "/Theme.qml"),temp.path()+"/qml/DecodiumSdr/Theme.qml"));
        SessionManager session;QQmlApplicationEngine qml;qml.addImportPath(temp.path()+"/qml");qml.rootContext()->setContextProperty("aprsController",session.aprs());
        auto text=QStringLiteral(R"(import QtQuick
import QtQuick.Controls.Basic
import "%1" as App
ApplicationWindow { visible:true; width:800; height:600
 App.AprsDialog { objectName:"dialog"; controller:aprsController; Component.onCompleted:open() }
})").arg(QUrl::fromLocalFile(QStringLiteral(DSDR_APRS_QML_DIR)).toString());
        qml.loadData(text.toUtf8(),QUrl::fromLocalFile(temp.path()+"/app.qml"));QVERIFY(!qml.rootObjects().isEmpty());
        auto *root=qml.rootObjects().first();auto *label=root->findChild<QObject*>("aprsSource");auto *info=root->findChild<QObject*>("aprsInformation");QVERIFY(label&&info);
        session.selectBackend("iqfile");session.startDiscovery();QTRY_VERIFY_WITH_TIMEOUT(session.devices()->rowCount()>0,5000);
        int row=-1;for(int i=0;i<session.devices()->rowCount();++i)if(session.devices()->at(i)->deviceId==path)row=i;
        QVERIFY(row>=0);session.connectToDevice(row);QVERIFY(session.isConnected());QTRY_VERIFY_WITH_TIMEOUT(session.channels()->rowCount()>0,5000);
        session.setChannelMode(0,int(DemodMode::Nfm));session.setChannelFilter(0,-6000,6000);session.setChannelMuted(0,true);
        QTRY_VERIFY_WITH_TIMEOUT(!session.aprs()->channels().isEmpty(),3000);
        session.aprs()->selectChannel(session.aprs()->channels().first().toMap().value("id").toInt());session.aprs()->setEnabled(true);
        QTRY_COMPARE_WITH_TIMEOUT(session.aprs()->source(),QStringLiteral("S57LN"),12000);
        QTRY_VERIFY(label->property("text").toString().contains(QStringLiteral("S57LN")));
        QTRY_VERIFY(info->property("text").toString().contains(QStringLiteral("Op. Andrej")));
        QVERIFY(session.aprs()->hasPosition());QVERIFY(std::abs(session.aprs()->latitude()-46.0605)<0.00001);QVERIFY(std::abs(session.aprs()->longitude()-14.521)<0.00001);
        session.requestShutdown();QTRY_VERIFY_WITH_TIMEOUT(session.shutdownFencePassed(),10000);qunsetenv("DSDR_IQFILE_PATH");
    }
    void iqToDialogAndReconnect_data() {
        QTest::addColumn<bool>("valid");
        QTest::addColumn<bool>("ambiguous");
        QTest::addColumn<bool>("object");
        QTest::addColumn<bool>("item");
        QTest::addColumn<bool>("compressed");
        QTest::addColumn<bool>("truncated");
        QTest::newRow("valid-fcs") << true << false << false << false << false << false;
        QTest::newRow("invalid-fcs") << false << false << false << false << false << false;
        QTest::newRow("ambiguous-position") << true << true << false << false << false << false;
        QTest::newRow("active-object") << true << false << true << false << false << false;
        QTest::newRow("active-item") << true << false << false << true << false << false;
        QTest::newRow("compressed") << true << false << false << false << true << false;
        QTest::newRow("truncated-timestamp") << true << false << false << false << false << true;
    }
    void iqToDialogAndReconnect() {
        QFETCH(bool,valid); QFETCH(bool,ambiguous); QFETCH(bool,object); QFETCH(bool,item); QFETCH(bool,compressed); QFETCH(bool,truncated);
        QVERIFY(qEnvironmentVariableIntValue("DSDR_TEST_SILENT_AUDIO")==1);
        QTemporaryDir temp(QDir::tempPath()+"/dsdr-aprs-iq-XXXXXX"); QVERIFY(temp.isValid());
        QCoreApplication::setOrganizationName("DSDR-aprs-test"); QCoreApplication::setApplicationName("iq-session");
        QSettings::setDefaultFormat(QSettings::IniFormat); QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,temp.path());
        qputenv("DSDR_TEST_APPDATA_DIR",temp.path().toUtf8());
        const auto path=temp.path()+"/aprs-session.wav"; QVERIFY(makeWav(path,valid,ambiguous,object,item,compressed,truncated)); qputenv("DSDR_IQFILE_PATH",path.toUtf8());
        QVERIFY(QDir().mkpath(temp.path()+"/qml/DecodiumSdr"));
        QFile module(temp.path()+"/qml/DecodiumSdr/qmldir"); QVERIFY(module.open(QIODevice::WriteOnly));
        module.write("module DecodiumSdr\nsingleton Theme 1.0 Theme.qml\n"); module.close();
        QVERIFY(QFile::copy(QStringLiteral(DSDR_APRS_QML_DIR "/Theme.qml"),temp.path()+"/qml/DecodiumSdr/Theme.qml"));
        SessionManager session; QQmlApplicationEngine qml; qml.addImportPath(temp.path()+"/qml");
        qml.rootContext()->setContextProperty("aprsController",session.aprs());
        const auto qmlText=QStringLiteral(R"(import QtQuick
import QtQuick.Controls.Basic
import "%1" as App
ApplicationWindow { visible:true; width:800; height:600
    App.AprsDialog { objectName:"dialog"; controller:aprsController; Component.onCompleted:open() }
})").arg(QUrl::fromLocalFile(QStringLiteral(DSDR_APRS_QML_DIR)).toString());
        qml.loadData(qmlText.toUtf8(),QUrl::fromLocalFile(temp.path()+"/app.qml")); QVERIFY(!qml.rootObjects().isEmpty());
        auto *root=qml.rootObjects().first(); auto *label=root->findChild<QObject *>("aprsSource"); QVERIFY(label);
        auto *dialog=root->findChild<QObject *>("dialog"); QVERIFY(dialog);
        QVERIFY(dialog->property("title").toString().contains("sperimentale"));
        auto *information=root->findChild<QObject *>("aprsInformation"); QVERIFY(information);
        auto *position=root->findChild<QObject *>("aprsPosition"); QVERIFY(position);
        auto *objectLabel=root->findChild<QObject *>("aprsPositionObject"); QVERIFY(objectLabel);
        QCOMPARE(label->property("textFormat").toInt(), 0);
        QCOMPARE(information->property("textFormat").toInt(), 0);
        session.selectBackend("iqfile"); session.startDiscovery(); QTRY_VERIFY_WITH_TIMEOUT(session.devices()->rowCount()>0,5000);
        int row=-1; for(int i=0;i<session.devices()->rowCount();++i) if(session.devices()->at(i)->deviceId==path) row=i;
        QVERIFY(row>=0); session.connectToDevice(row); QVERIFY(session.isConnected());
        QTRY_VERIFY_WITH_TIMEOUT(session.channels()->rowCount()>0,5000);
        session.setChannelMode(0,int(DemodMode::Nfm)); session.setChannelFilter(0,-6000,6000); session.setChannelMuted(0,true);
        QTRY_VERIFY_WITH_TIMEOUT(!session.aprs()->channels().isEmpty(),3000);
        session.aprs()->selectChannel(session.aprs()->channels().first().toMap().value("id").toInt());
        QTest::qWait(500); session.aprs()->poll(); QCOMPARE(session.aprs()->frameCount(),qulonglong(0));
        session.aprs()->setEnabled(true);
        if(!valid) {
            QTest::qWait(3000); session.aprs()->poll();
            QCOMPARE(session.aprs()->frameCount(),qulonglong(0));
            QVERIFY(session.aprs()->source().isEmpty());
            session.requestShutdown(); QTRY_VERIFY_WITH_TIMEOUT(session.shutdownFencePassed(),10000);
            qunsetenv("DSDR_IQFILE_PATH"); return;
        }
        QTRY_COMPARE_WITH_TIMEOUT(session.aprs()->source(),QStringLiteral("IU8LMC-7"),10000);
        QTRY_VERIFY(label->property("text").toString().contains(QStringLiteral("IU8LMC-7")));
        QCOMPARE(session.aprs()->destination(),QStringLiteral("APRS"));
        if(ambiguous || truncated) {
            QVERIFY(!session.aprs()->hasPosition());
            QTRY_VERIFY(position->property("text").toString().contains(QStringLiteral("non disponibile")));
        } else if(compressed) {
            QVERIFY(session.aprs()->hasPosition());
            QVERIFY(std::abs(session.aprs()->latitude()-49.5)<0.00001);
            QVERIFY(std::abs(session.aprs()->longitude()+72.75)<0.00001);
            QTRY_VERIFY(position->property("text").toString().contains(QStringLiteral("49.50000")));
        } else {
            QVERIFY(session.aprs()->information().contains(QStringLiteral("3554.00N")));
            QTRY_VERIFY(information->property("text").toString().contains(QStringLiteral("3554.00N")));
            QVERIFY(session.aprs()->hasPosition());
            QTRY_VERIFY(position->property("text").toString().contains(QStringLiteral("35.90000")));
            QVERIFY(position->property("text").toString().contains(QStringLiteral("14.50000")));
            if(object || item) {
                const auto name=item ? QStringLiteral("WX1234") : QStringLiteral("TEST");
                QCOMPARE(session.aprs()->positionObject(),name);
                QTRY_VERIFY(objectLabel->property("text").toString().contains(name));
                QVERIFY(objectLabel->property("visible").toBool());
            } else QVERIFY(session.aprs()->positionObject().isEmpty());
        }
        session.disconnectDevice(); QTRY_VERIFY_WITH_TIMEOUT(!session.isConnected(),5000);
        QTRY_VERIFY_WITH_TIMEOUT(session.aprs()->source().isEmpty(),3000);
        QVERIFY(session.aprs()->destination().isEmpty());
        QVERIFY(session.aprs()->information().isEmpty());
        QVERIFY(!session.aprs()->hasPosition());
        QVERIFY(session.aprs()->positionObject().isEmpty());
        QTRY_VERIFY(position->property("text").toString().contains(QStringLiteral("non disponibile")));
        QVERIFY(session.aprs()->lastFrame().isEmpty());
        QTRY_VERIFY(information->property("text").toString().contains(QStringLiteral("—")));
        session.connectToDevice(row); QVERIFY(session.isConnected());
        QTRY_VERIFY_WITH_TIMEOUT(session.channels()->rowCount()>0,5000);
        session.setChannelMode(0,int(DemodMode::Nfm)); session.setChannelFilter(0,-6000,6000); session.setChannelMuted(0,true);
        QTRY_VERIFY_WITH_TIMEOUT(!session.aprs()->channels().isEmpty(),3000);
        session.aprs()->selectChannel(session.aprs()->channels().first().toMap().value("id").toInt());
        QTRY_COMPARE_WITH_TIMEOUT(session.aprs()->source(),QStringLiteral("IU8LMC-7"),10000);
        QTRY_VERIFY(label->property("text").toString().contains(QStringLiteral("IU8LMC-7")));
        if(ambiguous || truncated) {
            QVERIFY(!session.aprs()->hasPosition());
            QTRY_VERIFY(position->property("text").toString().contains(QStringLiteral("non disponibile")));
        } else if(compressed) {
            QTRY_VERIFY(session.aprs()->hasPosition());
            QTRY_VERIFY(position->property("text").toString().contains(QStringLiteral("49.50000")));
        } else {
            QTRY_VERIFY(information->property("text").toString().contains(QStringLiteral("3554.00N")));
            if(item) {
                QTRY_VERIFY(session.aprs()->hasPosition());
                QCOMPARE(session.aprs()->positionObject(),QStringLiteral("WX1234"));
                QTRY_VERIFY(position->property("text").toString().contains(QStringLiteral("35.90000")));
            }
        }
        session.requestShutdown(); QTRY_VERIFY_WITH_TIMEOUT(session.shutdownFencePassed(),10000);
        qunsetenv("DSDR_IQFILE_PATH");
    }
};
QTEST_MAIN(AprsIqSession)
#include "tst_aprs_iq_session.moc"
