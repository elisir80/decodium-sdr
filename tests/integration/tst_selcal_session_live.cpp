// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/SessionManager.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
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
#include <cmath>
#include <atomic>
#include <thread>
#include <QTimer>
using namespace dsdr::core;
namespace {
void u32(QByteArray &b,quint32 n) { char p[4]; qToLittleEndian(n,p); b.append(p,4); }
void u16(QByteArray &b,quint16 n) { char p[2]; qToLittleEndian(n,p); b.append(p,2); }
bool makeWav(const QString &path) {
    constexpr unsigned rate=192000; constexpr int repeat=5; constexpr double pi=3.14159265358979323846;
    const quint32 bytes=rate*3*repeat*8;
    QByteArray h; h.append("RIFF",4); u32(h,36+bytes); h.append("WAVEfmt ",8); u32(h,16);
    u16(h,3); u16(h,2); u32(h,rate); u32(h,rate*8); u16(h,8); u16(h,32); h.append("data",4); u32(h,bytes);
    QFile file(path); if(!file.open(QIODevice::WriteOnly) || file.write(h)!=h.size()) return false;
    QByteArray block; block.reserve(rate*3*8);
    for(unsigned i=0;i<rate*3;++i) {
        const double t=double(i)/rate; double tone=0;
        if(t>=.2 && t<1.2) tone=.22*(std::sin(2*pi*312.6*t)+std::sin(2*pi*977.2*t));
        if(t>=1.4 && t<2.4) tone=.22*(std::sin(2*pi*384.6*t)+std::sin(2*pi*1083.9*t));
        const float pair[2]={float(.55*(1+tone)),0}; block.append(reinterpret_cast<const char *>(pair),sizeof(pair));
    }
    for(int n=0;n<repeat;++n) if(file.write(block)!=block.size()) return false;
    QFile meta(QFileInfo(path).absolutePath()+"/selcal-session.json"); if(!meta.open(QIODevice::WriteOnly)) return false;
    meta.write(QJsonDocument(QJsonObject{{"format","decodium-iq/1"},{"sampleRate",int(rate)},{"centerFrequencyHz",118000000}}).toJson());
    return true;
}
}
class TestSelcalSessionLive : public QObject {
    Q_OBJECT
private slots:
    void slowWorkerShutdown() {
        struct Gate {
            std::atomic<bool> entered{false},release{false};
            static void hold(void *p) noexcept {
                auto &g=*static_cast<Gate *>(p); g.entered=true;
                while(!g.release.load()) std::this_thread::yield();
            }
        } gate;
        QTemporaryDir temp(QDir::tempPath()+"/dsdr-selcal-slow-XXXXXX"); QVERIFY(temp.isValid());
        QCoreApplication::setOrganizationName("DECODIUM-selcal-test"); QCoreApplication::setApplicationName("slow-session");
        QSettings::setDefaultFormat(QSettings::IniFormat); QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,temp.path());
        qputenv("DSDR_TEST_APPDATA_DIR",temp.path().toUtf8());
        auto receiver=std::make_unique<SelcalReceiver>(64,4096,16,Gate::hold,&gate); auto *raw=receiver.get();
        SessionManager session(nullptr,{},{},{},{},std::move(receiver));
        struct Unblock { Gate &g; ~Unblock(){g.release=true;} } unblock{gate};
        session.selectBackend("demo"); session.startDiscovery(); QTRY_VERIFY_WITH_TIMEOUT(session.devices()->rowCount()>0,5000);
        session.connectToDevice(0); QVERIFY(session.isConnected());
        // No channel is selected, so the test owns the sole producer.
        raw->setEnabled(true); const dsdr::dsp::Complex sample{0,0};
        QCOMPARE(raw->submitForGeneration(&sample,1,raw->generation()),dsdr::core::AsyncIqDecoder::SubmitResult::Accepted);
        QTRY_VERIFY_WITH_TIMEOUT(gate.entered.load(),5000);
        int ticks=0; QTimer heartbeat; heartbeat.setInterval(10); connect(&heartbeat,&QTimer::timeout,[&]{++ticks;}); heartbeat.start();
        QElapsedTimer elapsed; elapsed.start(); session.requestShutdown(); QVERIFY(elapsed.elapsed()<500);
        QTRY_VERIFY_WITH_TIMEOUT(ticks >= 5, 3000); QVERIFY(session.shutdownPending()); QVERIFY(!session.shutdownFencePassed());
        gate.release=true; QTRY_VERIFY_WITH_TIMEOUT(session.shutdownFencePassed(),10000);
        QVERIFY(!session.isConnected());
    }
    void sourceToDialogAB() {
        QVERIFY(qEnvironmentVariableIntValue("DSDR_TEST_SILENT_AUDIO")==1);
        QTemporaryDir temp(QDir::tempPath()+"/dsdr-selcal-iq-XXXXXX"); QVERIFY(temp.isValid());
        QCoreApplication::setOrganizationName("DECODIUM-selcal-test"); QCoreApplication::setApplicationName("iq-session");
        QSettings::setDefaultFormat(QSettings::IniFormat); QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,temp.path());
        qputenv("DSDR_TEST_APPDATA_DIR",temp.path().toUtf8());
        const auto external=qEnvironmentVariable("DSDR_TEST_SELCAL_RECORDED_IQ");
        const auto path=external.isEmpty() ? temp.path()+"/selcal-session.wav" : external;
        if(external.isEmpty()) QVERIFY(makeWav(path)); else QVERIFY(QFileInfo::exists(path));
        const auto expected=external.isEmpty() ? QStringLiteral("AM-CP") : QStringLiteral("AB-CD");
        qputenv("DSDR_IQFILE_PATH",path.toUtf8());
        QVERIFY(QDir().mkpath(temp.path()+"/qml/DecodiumSdr"));
        QFile module(temp.path()+"/qml/DecodiumSdr/qmldir"); QVERIFY(module.open(QIODevice::WriteOnly));
        module.write("module DecodiumSdr\nsingleton Theme 1.0 Theme.qml\n"); module.close();
        QVERIFY(QFile::copy(QStringLiteral(DSDR_SELCAL_QML_DIR "/Theme.qml"),temp.path()+"/qml/DecodiumSdr/Theme.qml"));
        SessionManager session; QQmlApplicationEngine qml; qml.addImportPath(temp.path()+"/qml");
        qml.rootContext()->setContextProperty("selcalController",session.selcal());
        const auto text=QStringLiteral(R"(import QtQuick
import QtQuick.Controls.Basic
import "%1" as App
ApplicationWindow { visible: true; width: 900; height: 700
    App.SelcalDialog { objectName: "dialog"; controller: selcalController; Component.onCompleted: open() }
})").arg(QUrl::fromLocalFile(QStringLiteral(DSDR_SELCAL_QML_DIR)).toString());
        qml.loadData(text.toUtf8(),QUrl::fromLocalFile(temp.path()+"/app.qml")); QVERIFY(!qml.rootObjects().isEmpty());
        auto *root=qml.rootObjects().first(); auto *window=qobject_cast<QQuickWindow *>(root); QVERIFY(window);
        auto *toggle=root->findChild<QQuickItem *>("selcalEnabled"); auto *label=root->findChild<QObject *>("selcalCode"); QVERIFY(toggle && label);
        auto *limitations=root->findChild<QObject *>("selcalLimitations"); QVERIFY(limitations);
        QVERIFY(limitations->property("text").toString().contains(QStringLiteral("falsi allarmi")));
        session.selectBackend("iqfile"); session.startDiscovery(); QTRY_VERIFY_WITH_TIMEOUT(session.devices()->rowCount()>0,5000);
        int row=-1; for(int i=0;i<session.devices()->rowCount();++i) if(session.devices()->at(i)->deviceId==path) row=i;
        QVERIFY(row>=0); session.connectToDevice(row); QVERIFY(session.isConnected());
        QTRY_VERIFY_WITH_TIMEOUT(session.channels()->rowCount()>0,5000);
        session.setChannelMode(0,int(dsdr::DemodMode::Am)); session.setChannelFilter(0,-5000,5000); session.setChannelMuted(0,true);
        QTRY_VERIFY_WITH_TIMEOUT(!session.selcal()->channels().isEmpty(),3000);
        const int id=session.selcal()->channels().first().toMap().value("id").toInt(); session.selcal()->selectChannel(id);
        QTest::qWait(500); session.selcal()->poll(); QCOMPARE(session.selcal()->messageCount(),qulonglong(0));
        const auto point=toggle->mapToScene(QPointF(toggle->width()/2,toggle->height()/2));
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,point.toPoint()); QTRY_VERIFY(session.selcal()->enabled());
        QTRY_COMPARE_WITH_TIMEOUT(session.selcal()->lastCode(),expected,12000);
        QTRY_VERIFY(label->property("text").toString().contains(expected));
        session.disconnectDevice();
        QTRY_VERIFY_WITH_TIMEOUT(!session.isConnected(),5000);
        QTRY_COMPARE_WITH_TIMEOUT(session.selcal()->status(),QStringLiteral("Seleziona un canale AM o SSB"),3000);
        QTRY_VERIFY_WITH_TIMEOUT(session.selcal()->lastCode().isEmpty(),3000);
        QTRY_VERIFY_WITH_TIMEOUT(label->property("text").toString().contains(QStringLiteral("—")),3000);
        session.connectToDevice(row);
        QVERIFY(session.isConnected());
        QTRY_VERIFY_WITH_TIMEOUT(session.channels()->rowCount()>0,5000);
        session.setChannelMode(0,int(dsdr::DemodMode::Am)); session.setChannelFilter(0,-5000,5000); session.setChannelMuted(0,true);
        QTRY_VERIFY_WITH_TIMEOUT(!session.selcal()->channels().isEmpty(),3000);
        session.selcal()->selectChannel(session.selcal()->channels().first().toMap().value("id").toInt());
        QTRY_COMPARE_WITH_TIMEOUT(session.selcal()->lastCode(),expected,12000);
        QTRY_VERIFY(label->property("text").toString().contains(expected));
        session.requestShutdown(); QTRY_VERIFY_WITH_TIMEOUT(session.shutdownFencePassed(),10000);
    }
};
QTEST_MAIN(TestSelcalSessionLive)
#include "tst_selcal_session_live.moc"
