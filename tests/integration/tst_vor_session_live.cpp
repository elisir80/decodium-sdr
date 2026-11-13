// SPDX-License-Identifier: GPL-3.0-or-later
#define main vor_fixture_main
#include "../standalone/vor_engine/test.cpp"
#undef main
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
#include <atomic>
#include <thread>
#include <QTimer>
using namespace dsdr::core;
namespace {
void u32(QByteArray &b,quint32 n) { char p[4]; qToLittleEndian(n,p); b.append(p,4); }
void u16(QByteArray &b,quint16 n) { char p[2]; qToLittleEndian(n,p); b.append(p,2); }
bool makeWav(const QString &path, unsigned sampleRate, double radial) {
    const auto iq=signal(sampleRate,radial,7000,true);
    constexpr int repetitions=4; const quint32 bytes=quint32(iq.size()*repetitions*8);
    QByteArray h; h.append("RIFF",4); u32(h,36+bytes); h.append("WAVEfmt ",8); u32(h,16);
    u16(h,3); u16(h,2); u32(h,sampleRate); u32(h,sampleRate*8); u16(h,8); u16(h,32); h.append("data",4); u32(h,bytes);
    QFile file(path); if(!file.open(QIODevice::WriteOnly) || file.write(h)!=h.size()) return false;
    std::vector<std::pair<double,double>> marks; double time=1.5;
    for(const char *letter : {"-..","-..-","-..."}) {
        for(const char *p=letter;*p;++p) { double length=*p=='-'?.3:.1; marks.push_back({time,time+length}); time+=length+.1; }
        time+=.2;
    }
    QByteArray block; block.reserve(qsizetype(iq.size()*8)); std::size_t mark=0;
    for(int n=0;n<repetitions;++n) {
        block.clear();
        for(std::size_t i=0;i<iq.size();++i) {
            const double t=(n*iq.size()+i)/double(sampleRate);
            while(mark<marks.size() && t>=marks[mark].second) ++mark;
            const bool keyed=mark<marks.size() && t>=marks[mark].first;
            const auto v=iq[i]*float(1+(keyed?.1*std::cos(2*pi*1020*t):0));
            const float pair[2]={v.real(),v.imag()}; block.append(reinterpret_cast<const char *>(pair),sizeof(pair));
        }
        if(file.write(block)!=block.size()) return false;
    }
    QFile meta(QFileInfo(path).absolutePath()+"/vor-session.json"); if(!meta.open(QIODevice::WriteOnly)) return false;
    meta.write(QJsonDocument(QJsonObject{{"format","decodium-iq/1"},{"sampleRate",int(sampleRate)},{"centerFrequencyHz",110000000}}).toJson());
    return true;
}
}
class TestVorSessionLive : public QObject {
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
        QTemporaryDir temp(QDir::tempPath()+"/dsdr-vor-slow-XXXXXX"); QVERIFY(temp.isValid());
        QCoreApplication::setOrganizationName("DECODIUM-vor-test"); QCoreApplication::setApplicationName("slow-session");
        QSettings::setDefaultFormat(QSettings::IniFormat); QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,temp.path());
        qputenv("DSDR_TEST_APPDATA_DIR",temp.path().toUtf8());
        auto receiver=std::make_unique<VorAsyncReceiver>(64,4096,Gate::hold,&gate); auto *raw=receiver.get();
        SessionManager session(nullptr,{},{},{},std::move(receiver));
        struct Unblock { Gate &g; ~Unblock(){g.release=true;} } unblock{gate};
        session.selectBackend("demo"); session.startDiscovery(); QTRY_VERIFY_WITH_TIMEOUT(session.devices()->rowCount()>0,5000);
        session.connectToDevice(0); QVERIFY(session.isConnected());
        // The VOR tap is disabled, so this test owns the sole IQ producer.
        const dsdr::dsp::Complex sample{0,0}; QCOMPARE(raw->submitIq(&sample,1),dsdr::core::AsyncIqDecoder::SubmitResult::Accepted);
        QTRY_VERIFY_WITH_TIMEOUT(gate.entered.load(),5000);
        int ticks=0; QTimer heartbeat; heartbeat.setInterval(10); connect(&heartbeat,&QTimer::timeout,[&]{++ticks;}); heartbeat.start();
        QElapsedTimer elapsed; elapsed.start(); session.requestShutdown(); QVERIFY(elapsed.elapsed()<500);
        QTRY_VERIFY_WITH_TIMEOUT(ticks >= 5, 3000); QVERIFY(session.shutdownPending()); QVERIFY(!session.shutdownFencePassed());
        gate.release=true; QTRY_VERIFY_WITH_TIMEOUT(session.shutdownFencePassed(),10000);
        QVERIFY(!session.isConnected());
    }
    void sourceToDialogAB_data() {
        QTest::addColumn<unsigned>("sampleRate");
        QTest::addColumn<double>("expectedRadial");
        QTest::newRow("192k") << 192000u << 90.;
        QTest::newRow("200k") << 200000u << 90.;
        QTest::newRow("960k") << 960000u << 90.;
        QTest::newRow("1300k") << 1300000u << 90.;
        QTest::newRow("2M") << 2000000u << 90.;
        QTest::newRow("2048k") << 2048000u << 90.;
        QTest::newRow("north-359") << 192000u << 359.;
        QTest::newRow("north-zero") << 192000u << 0.;
    }
    void sourceToDialogAB() {
        QFETCH(unsigned,sampleRate);
        QFETCH(double,expectedRadial);
        QVERIFY(qEnvironmentVariableIntValue("DSDR_TEST_SILENT_AUDIO")==1);
        QTemporaryDir temp(QDir::tempPath()+"/dsdr-vor-iq-XXXXXX"); QVERIFY(temp.isValid());
        QCoreApplication::setOrganizationName("DECODIUM-vor-test"); QCoreApplication::setApplicationName("iq-session");
        QSettings::setDefaultFormat(QSettings::IniFormat); QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,temp.path());
        qputenv("DSDR_TEST_APPDATA_DIR",temp.path().toUtf8());
        const auto path=temp.path()+"/vor-session.wav"; QVERIFY(makeWav(path,sampleRate,expectedRadial)); qputenv("DSDR_IQFILE_PATH",path.toUtf8());
        QVERIFY(QDir().mkpath(temp.path()+"/qml/DecodiumSdr"));
        QFile module(temp.path()+"/qml/DecodiumSdr/qmldir"); QVERIFY(module.open(QIODevice::WriteOnly));
        module.write("module DecodiumSdr\nsingleton Theme 1.0 Theme.qml\n"); module.close();
        QVERIFY(QFile::copy(QStringLiteral(DSDR_VOR_QML_DIR "/Theme.qml"),temp.path()+"/qml/DecodiumSdr/Theme.qml"));
        SessionManager session; QQmlApplicationEngine qml; qml.addImportPath(temp.path()+"/qml");
        QVERIFY(session.vor()->setFrequencyHz(110007000));
        qml.rootContext()->setContextProperty("vorController",session.vor());
        const auto text=QStringLiteral(R"(import QtQuick
import QtQuick.Controls.Basic
import "%1" as App
ApplicationWindow { visible: true; width: 900; height: 700
    App.VorDialog { objectName: "dialog"; controller: vorController; Component.onCompleted: open() }
})").arg(QUrl::fromLocalFile(QStringLiteral(DSDR_VOR_QML_DIR)).toString());
        qml.loadData(text.toUtf8(),QUrl::fromLocalFile(temp.path()+"/app.qml")); QVERIFY(!qml.rootObjects().isEmpty());
        auto *root=qml.rootObjects().first(); auto *window=qobject_cast<QQuickWindow *>(root); QVERIFY(window);
        auto *frequency=root->findChild<QObject *>("vorFrequency"); QVERIFY(frequency);
        QTRY_COMPARE(frequency->property("text").toString(),QStringLiteral("110007000"));
        auto *dialog=root->findChild<QObject *>("dialog"); QVERIFY(dialog);
        QVERIFY(QMetaObject::invokeMethod(dialog,"close"));
        QVERIFY(session.vor()->setFrequencyHz(110010000));
        QVERIFY(QMetaObject::invokeMethod(dialog,"open"));
        QTRY_COMPARE(frequency->property("text").toString(),QStringLiteral("110010000"));
        auto *toggle=root->findChild<QQuickItem *>("vorEnabled"); auto *radial=root->findChild<QObject *>("vorRadial"); QVERIFY(toggle && radial);
        session.selectBackend("iqfile"); session.startDiscovery(); QTRY_VERIFY_WITH_TIMEOUT(session.devices()->rowCount()>0,5000);
        int row=-1; for(int i=0;i<session.devices()->rowCount();++i) if(session.devices()->at(i)->deviceId==path) row=i;
        QVERIFY(row>=0); session.connectToDevice(row); QVERIFY(session.isConnected());
        QCOMPARE(session.sampleRate(),double(sampleRate));
        int ticks=0; QTimer heartbeat; heartbeat.setInterval(20);
        connect(&heartbeat,&QTimer::timeout,[&]{++ticks;}); heartbeat.start();
        auto *apply=root->findChild<QObject *>("vorApply"); QVERIFY(apply);
        QVERIFY(frequency->setProperty("text",QStringLiteral("0"))); QCoreApplication::processEvents();
        QVERIFY(!apply->property("enabled").toBool());
        QVERIFY(frequency->setProperty("text",QStringLiteral("110007000"))); QCoreApplication::processEvents();
        QVERIFY(apply->property("enabled").toBool()); QVERIFY(QMetaObject::invokeMethod(apply,"clicked"));
        QCOMPARE(session.vor()->frequencyHz(),qint64(110007000));
        QTest::qWait(500); session.vor()->poll(); QVERIFY(!session.vor()->valid());
        const auto point=toggle->mapToScene(QPointF(toggle->width()/2,toggle->height()/2));
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,point.toPoint()); QTRY_VERIFY(session.vor()->enabled());
        QTRY_VERIFY_WITH_TIMEOUT(session.vor()->valid(),12000);
        QVERIFY(ticks>10);
        QCOMPARE(session.vor()->droppedBlocks(),qulonglong(0));
        QVERIFY(std::abs(std::remainder(session.vor()->radial()-expectedRadial,360.))<3.);
        QTRY_VERIFY(radial->property("text").toString().contains(QString::number(session.vor()->radial(),'f',1)));
        auto *identifier=root->findChild<QObject *>("vorIdentifier"); QVERIFY(identifier);
        QTRY_COMPARE_WITH_TIMEOUT(session.vor()->identifier(),QStringLiteral("DXB"),12000);
        QTRY_VERIFY(identifier->property("text").toString().contains(QStringLiteral("DXB")));
        session.disconnectDevice();
        QTRY_VERIFY_WITH_TIMEOUT(!session.isConnected(),5000);
        QTRY_VERIFY_WITH_TIMEOUT(!session.vor()->valid(),3000);
        QTRY_VERIFY_WITH_TIMEOUT(session.vor()->identifier().isEmpty(),3000);
        QTRY_VERIFY(radial->property("text").toString().contains(QStringLiteral("non disponibile")));
        QTRY_VERIFY(identifier->property("text").toString().contains(QStringLiteral("non disponibile")));
        session.connectToDevice(row); QVERIFY(session.isConnected());
        QCOMPARE(session.sampleRate(),double(sampleRate));
        QTRY_VERIFY_WITH_TIMEOUT(session.vor()->valid(),12000);
        QCOMPARE(session.vor()->droppedBlocks(),qulonglong(0));
        QVERIFY(std::abs(std::remainder(session.vor()->radial()-expectedRadial,360.))<3.);
        QTRY_COMPARE_WITH_TIMEOUT(session.vor()->identifier(),QStringLiteral("DXB"),12000);
        QTRY_VERIFY(identifier->property("text").toString().contains(QStringLiteral("DXB")));
        QTRY_VERIFY(radial->property("text").toString().contains(QString::number(session.vor()->radial(),'f',1)));
        if(sampleRate==2000000) {
            QVERIFY(session.vor()->setFrequencyHz(110985001));
            QTRY_VERIFY(!session.vor()->valid());
            QVERIFY(session.vor()->identifier().isEmpty());
            QTRY_VERIFY(radial->property("text").toString().contains(QStringLiteral("non disponibile")));
            QVERIFY(session.vor()->status().contains(QStringLiteral("non supportati")));
            QVERIFY(session.vor()->setFrequencyHz(110007000));
            QTRY_VERIFY_WITH_TIMEOUT(session.vor()->valid(),12000);
            QVERIFY(std::abs(std::remainder(session.vor()->radial()-expectedRadial,360.))<3.);
            QTRY_COMPARE_WITH_TIMEOUT(session.vor()->identifier(),QStringLiteral("DXB"),12000);
        }
        if(sampleRate==2000000 && qEnvironmentVariableIntValue("DSDR_TEST_VOR_SUSTAINED")==1) {
            const int initialTicks=ticks;
            const auto blocks=session.vor()->droppedBlocks(), results=session.vor()->droppedResults();
            for(int i=0;i<12;++i) {
                QTest::qWait(5000);
                QTRY_VERIFY_WITH_TIMEOUT(session.vor()->valid(),3000);
                QVERIFY(std::abs(std::remainder(session.vor()->radial()-expectedRadial,360.))<3.);
            }
            QCOMPARE(session.vor()->droppedBlocks(),blocks);
            QCOMPARE(session.vor()->droppedResults(),results);
            QVERIFY(ticks-initialTicks>300);
            qInfo() << "VOR sustained 60 seconds" << "drops" << blocks << results
                    << "GUI ticks" << ticks-initialTicks;
        }
        session.vor()->setEnabled(false); QCOMPARE(session.vor()->identifier(),QString());
        session.requestShutdown(); QTRY_VERIFY_WITH_TIMEOUT(session.shutdownFencePassed(),10000);
    }
};
QTEST_MAIN(TestVorSessionLive)
#include "tst_vor_session_live.moc"
