// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/SessionManager.h"
#include "dsp/navtex/NavtexProtocol.h"
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
#include <atomic>
#include <cmath>
#include <thread>
#include <QTimer>
#include <vector>
using namespace dsdr::core;
namespace {
void u32(QByteArray& b,quint32 n) { char p[4]; qToLittleEndian(n,p); b.append(p,4); }
void u16(QByteArray& b,quint16 n) { char p[2]; qToLittleEndian(n,p); b.append(p,2); }
bool makeWav(const QString& path, bool lower) {
    constexpr unsigned rate=192000,repeat=4; constexpr double pi=3.14159265358979323846;
    std::vector<int> tokens(25,-1);
    for(auto c:dsdr::navtex::navtexEncode("ZCZC EA01\nTEST 123\nNNNN\n")) tokens.push_back(c);
    tokens.insert(tokens.end(),25,-1);
    const auto bits=dsdr::navtex::codesToBits(dsdr::navtex::sitorBSlots(tokens));
    const size_t count=bits.size()*1920+rate/2; const quint32 bytes=quint32(count*8*repeat);
    QByteArray header; header.append("RIFF",4); u32(header,36+bytes); header.append("WAVEfmt ",8);
    u32(header,16); u16(header,3); u16(header,2); u32(header,rate); u32(header,rate*8); u16(header,8); u16(header,32);
    header.append("data",4); u32(header,bytes);
    QFile f(path); if(!f.open(QIODevice::WriteOnly) || f.write(header)!=header.size()) return false;
    QByteArray block; block.reserve(qsizetype(count*8)); double phase=0;
    for(size_t i=0;i<count;++i) {
        bool active=i<bits.size()*1920;
        if(active) phase+=(lower?-1:1)*2*pi*(1000+(bits[i/1920]?85:-85))/rate;
        float pair[2]={active?float(.5*std::cos(phase)):0.f,active?float(.5*std::sin(phase)):0.f};
        block.append(reinterpret_cast<const char*>(pair),sizeof(pair));
    }
    for(unsigned n=0;n<repeat;++n) if(f.write(block)!=block.size()) return false;
    QFile meta(QFileInfo(path).absolutePath()+"/navtex-session.json"); if(!meta.open(QIODevice::WriteOnly)) return false;
    meta.write(QJsonDocument(QJsonObject{{"format","decodium-iq/1"},{"sampleRate",int(rate)},{"centerFrequencyHz",518000}}).toJson());
    return true;
}
}
class TestNavtexSession : public QObject {
    Q_OBJECT
private slots:
    void recordedIqToDialog() {
        const QString path=qEnvironmentVariable("DSDR_TEST_NAVTEX_RECORDED_IQ");
        if(path.isEmpty()) QSKIP("Optional external NAVTEX IQ fixture not provided");
        QVERIFY(qEnvironmentVariableIntValue("DSDR_TEST_SILENT_AUDIO")==1);
        QTemporaryDir temp(QDir::tempPath()+"/dsdr-navtex-real-XXXXXX"); QVERIFY(temp.isValid());
        QCoreApplication::setOrganizationName("DECODIUM-navtex-test"); QCoreApplication::setApplicationName("recorded-session");
        QSettings::setDefaultFormat(QSettings::IniFormat); QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,temp.path());
        qputenv("DSDR_TEST_APPDATA_DIR",temp.path().toUtf8());qputenv("DSDR_IQFILE_PATH",path.toUtf8());
        QVERIFY(QDir().mkpath(temp.path()+"/qml/DecodiumSdr"));
        QFile module(temp.path()+"/qml/DecodiumSdr/qmldir"); QVERIFY(module.open(QIODevice::WriteOnly));
        module.write("module DecodiumSdr\nsingleton Theme 1.0 Theme.qml\n");module.close();
        QVERIFY(QFile::copy(QStringLiteral(DSDR_NAVTEX_QML_DIR "/Theme.qml"),temp.path()+"/qml/DecodiumSdr/Theme.qml"));
        SessionManager session;QQmlApplicationEngine qml;qml.addImportPath(temp.path()+"/qml");
        qml.rootContext()->setContextProperty("navtexController",session.navtex());
        auto text=QStringLiteral(R"(import QtQuick
import QtQuick.Controls.Basic
import "%1" as App
ApplicationWindow { visible: true; width: 900; height: 700
    App.NavtexDialog { objectName: "dialog"; controller: navtexController; Component.onCompleted: open() }
})").arg(QUrl::fromLocalFile(QStringLiteral(DSDR_NAVTEX_QML_DIR)).toString());
        qml.loadData(text.toUtf8(),QUrl::fromLocalFile(temp.path()+"/app.qml"));QVERIFY(!qml.rootObjects().isEmpty());
        auto *root=qml.rootObjects().first();auto *body=root->findChild<QObject*>("navtexText");auto *header=root->findChild<QObject*>("navtexHeader");QVERIFY(body && header);
        session.selectBackend("iqfile");session.startDiscovery();QTRY_VERIFY_WITH_TIMEOUT(session.devices()->rowCount()>0,5000);
        int row=-1;for(int i=0;i<session.devices()->rowCount();++i)if(session.devices()->at(i)->deviceId==path)row=i;
        QVERIFY(row>=0);session.connectToDevice(row);QVERIFY(session.isConnected());
        QTRY_VERIFY_WITH_TIMEOUT(session.channels()->rowCount()>0,5000);
        session.setChannelMode(0,int(dsdr::DemodMode::Usb));session.setChannelFilter(0,100,2500);session.setChannelMuted(0,true);
        QTRY_VERIFY_WITH_TIMEOUT(!session.navtex()->channels().isEmpty(),3000);
        session.navtex()->selectChannel(session.navtex()->channels().first().toMap().value("id").toInt());session.navtex()->setEnabled(true);
        QTRY_COMPARE_WITH_TIMEOUT(session.navtex()->header(),QStringLiteral("SA97"),80000);
        QTRY_VERIFY(body->property("text").toString().contains(QStringLiteral("NCC-HAMBURG")));
        QVERIFY(header->property("text").toString().contains(QStringLiteral("SA97")));
        session.requestShutdown();QTRY_VERIFY_WITH_TIMEOUT(session.shutdownFencePassed(),10000);qunsetenv("DSDR_IQFILE_PATH");
    }
    void slowWorkerShutdown() {
        struct Gate { std::atomic<bool> entered{false},release{false}; static void hold(void* p) noexcept {
            auto& g=*static_cast<Gate*>(p); g.entered=true; while(!g.release.load()) std::this_thread::yield(); } } gate;
        QTemporaryDir temp(QDir::tempPath()+"/dsdr-navtex-slow-XXXXXX"); QVERIFY(temp.isValid());
        QCoreApplication::setOrganizationName("DECODIUM-navtex-test"); QCoreApplication::setApplicationName("slow-session");
        QSettings::setDefaultFormat(QSettings::IniFormat); QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,temp.path());
        qputenv("DSDR_TEST_APPDATA_DIR",temp.path().toUtf8());
        auto rx=std::make_unique<NavtexReceiver>(64,4096,8,Gate::hold,&gate); auto* raw=rx.get();
        SessionManager session(nullptr,{},{},{},{},{},{},std::move(rx));
        struct Unblock { Gate& g; ~Unblock(){g.release=true;} } unblock{gate};
        session.selectBackend("demo"); session.startDiscovery(); QTRY_VERIFY_WITH_TIMEOUT(session.devices()->rowCount()>0,5000);
        session.connectToDevice(0); QVERIFY(session.isConnected()); raw->setEnabled(true);
        const dsdr::dsp::Complex sample{0,0};
        QCOMPARE(raw->submitForGeneration(&sample,1,raw->generation()),dsdr::core::AsyncIqDecoder::SubmitResult::Accepted);
        QTRY_VERIFY_WITH_TIMEOUT(gate.entered.load(),5000);
        int ticks=0; QTimer heartbeat; heartbeat.setInterval(10); connect(&heartbeat,&QTimer::timeout,[&]{++ticks;}); heartbeat.start();
        QElapsedTimer elapsed; elapsed.start(); session.requestShutdown(); QVERIFY(elapsed.elapsed()<500);
        QTest::qWait(200); QVERIFY(session.shutdownPending()); QVERIFY(!session.shutdownFencePassed()); QVERIFY(ticks>=5);
        gate.release=true; QTRY_VERIFY_WITH_TIMEOUT(session.shutdownFencePassed(),10000);
    }
    void iqFileToDialog_data() {
        QTest::addColumn<bool>("lower");
        QTest::newRow("USB") << false;
        QTest::newRow("LSB") << true;
    }
    void iqFileToDialog() {
        QFETCH(bool,lower);
        QVERIFY(qEnvironmentVariableIntValue("DSDR_TEST_SILENT_AUDIO")==1);
        QTemporaryDir temp(QDir::tempPath()+"/dsdr-navtex-iq-XXXXXX"); QVERIFY(temp.isValid());
        QCoreApplication::setOrganizationName("DECODIUM-navtex-test"); QCoreApplication::setApplicationName("iq-session");
        QSettings::setDefaultFormat(QSettings::IniFormat); QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,temp.path());
        qputenv("DSDR_TEST_APPDATA_DIR",temp.path().toUtf8());
        const auto path=temp.path()+"/navtex-session.wav"; QVERIFY(makeWav(path,lower)); qputenv("DSDR_IQFILE_PATH",path.toUtf8());
        QVERIFY(QDir().mkpath(temp.path()+"/qml/DecodiumSdr"));
        QFile module(temp.path()+"/qml/DecodiumSdr/qmldir"); QVERIFY(module.open(QIODevice::WriteOnly));
        module.write("module DecodiumSdr\nsingleton Theme 1.0 Theme.qml\n"); module.close();
        QVERIFY(QFile::copy(QStringLiteral(DSDR_NAVTEX_QML_DIR "/Theme.qml"),temp.path()+"/qml/DecodiumSdr/Theme.qml"));
        SessionManager session; QQmlApplicationEngine qml; qml.addImportPath(temp.path()+"/qml");
        qml.rootContext()->setContextProperty("navtexController",session.navtex());
        const auto text=QStringLiteral(R"(import QtQuick
import QtQuick.Controls.Basic
import "%1" as App
ApplicationWindow { visible: true; width: 900; height: 700
    App.NavtexDialog { objectName: "dialog"; controller: navtexController; Component.onCompleted: open() }
})").arg(QUrl::fromLocalFile(QStringLiteral(DSDR_NAVTEX_QML_DIR)).toString());
        qml.loadData(text.toUtf8(),QUrl::fromLocalFile(temp.path()+"/app.qml")); QVERIFY(!qml.rootObjects().isEmpty());
        auto* root=qml.rootObjects().first(); auto* win=qobject_cast<QQuickWindow*>(root); QVERIFY(win);
        auto* toggle=root->findChild<QQuickItem*>("navtexEnabled"); auto* label=root->findChild<QObject*>("navtexText"); QVERIFY(toggle && label);
        auto* header=root->findChild<QObject*>("navtexHeader"); QVERIFY(header);
        QCOMPARE(header->property("textFormat").toInt(), 0);
        QCOMPARE(label->property("textFormat").toInt(), 0);
        session.selectBackend("iqfile"); session.startDiscovery(); QTRY_VERIFY_WITH_TIMEOUT(session.devices()->rowCount()>0,5000);
        int row=-1; for(int i=0;i<session.devices()->rowCount();++i) if(session.devices()->at(i)->deviceId==path) row=i;
        QVERIFY(row>=0); session.connectToDevice(row); QVERIFY(session.isConnected());
        QTRY_VERIFY_WITH_TIMEOUT(session.channels()->rowCount()>0,5000);
        session.setChannelMode(0,int(lower ? dsdr::DemodMode::Lsb : dsdr::DemodMode::Usb)); session.setChannelFilter(0,100,2500); session.setChannelMuted(0,true);
        QTRY_VERIFY_WITH_TIMEOUT(!session.navtex()->channels().isEmpty(),3000);
        session.navtex()->selectChannel(session.navtex()->channels().first().toMap().value("id").toInt());
        QTest::qWait(500); session.navtex()->poll(); QCOMPARE(session.navtex()->messageCount(),qulonglong(0));
        const auto p=toggle->mapToScene(QPointF(toggle->width()/2,toggle->height()/2));
        QTest::mouseClick(win,Qt::LeftButton,Qt::NoModifier,p.toPoint()); QTRY_VERIFY(session.navtex()->enabled());
        QTRY_COMPARE_WITH_TIMEOUT(session.navtex()->header(),QStringLiteral("EA01"),40000);
        QTRY_VERIFY(label->property("text").toString().contains(QStringLiteral("TEST 123")));
        session.disconnectDevice();
        QTRY_VERIFY_WITH_TIMEOUT(!session.isConnected(),5000);
        QTRY_COMPARE_WITH_TIMEOUT(session.navtex()->status(),QStringLiteral("Seleziona un canale USB o LSB"),3000);
        QTRY_VERIFY_WITH_TIMEOUT(session.navtex()->header().isEmpty(),3000);
        QTRY_VERIFY_WITH_TIMEOUT(label->property("text").toString().isEmpty(),3000);
        QTRY_VERIFY_WITH_TIMEOUT(header->property("text").toString().contains(QStringLiteral("—")),3000);
        session.connectToDevice(row); QVERIFY(session.isConnected());
        QTRY_VERIFY_WITH_TIMEOUT(session.channels()->rowCount()>0,5000);
        session.setChannelMode(0,int(lower ? dsdr::DemodMode::Lsb : dsdr::DemodMode::Usb)); session.setChannelFilter(0,100,2500); session.setChannelMuted(0,true);
        QTRY_VERIFY_WITH_TIMEOUT(!session.navtex()->channels().isEmpty(),3000);
        session.navtex()->selectChannel(session.navtex()->channels().first().toMap().value("id").toInt());
        QTRY_COMPARE_WITH_TIMEOUT(session.navtex()->header(),QStringLiteral("EA01"),40000);
        QTRY_VERIFY(label->property("text").toString().contains(QStringLiteral("TEST 123")));
        session.requestShutdown(); QTRY_VERIFY_WITH_TIMEOUT(session.shutdownFencePassed(),10000);
        qunsetenv("DSDR_IQFILE_PATH");
    }
};
QTEST_MAIN(TestNavtexSession)
#include "tst_navtex_session.moc"
