// SPDX-License-Identifier: GPL-3.0-or-later
// Real SessionManager + IQ-file backend + DspEngine + AIS + actual QML dialog.
#include "core/SessionManager.h"
#include "../standalone/ais_synthetic.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QtEndian>
#include <vector>
#include <atomic>
#include <thread>
#include <QTimer>
using namespace dsdr::core;
namespace {
void u32(QByteArray &b,quint32 n) { char t[4]; qToLittleEndian(n,t); b.append(t,4); }
void u16(QByteArray &b,quint16 n) { char t[2]; qToLittleEndian(n,t); b.append(t,2); }
bool makeWav(const QString &path, unsigned type, unsigned rate) {
    std::vector<std::uint8_t> classB(type==17 ? 10 : (type==27 ? 12 : (type==19 ? 39 : (type==21 ? 34 : 21))),0);
    ais_test::setBits(classB,0,6,type);
    ais_test::setBits(classB,8,30,123456789);
    if(type==27 || type==17) {
        ais_test::setBits(classB,type==17 ? 40 : 44,18,std::uint32_t(-7500)&0x3ffff);
        ais_test::setBits(classB,type==17 ? 58 : 62,17,30000);
    } else if(type==4 || type==11) {
        ais_test::setBits(classB,79,28,std::uint32_t(-7500000)&0x0fffffff);
        ais_test::setBits(classB,107,27,31000000);
    } else if(type==21) {
        ais_test::setBits(classB,164,28,std::uint32_t(-7500000)&0x0fffffff);
        ais_test::setBits(classB,192,27,31000000);
    } else if(type==9 || (type>=1 && type<=3)) {
        ais_test::setBits(classB,61,28,std::uint32_t(-7500000)&0x0fffffff);
        ais_test::setBits(classB,89,27,31000000);
    } else {
        ais_test::setBits(classB,57,28,std::uint32_t(-7500000)&0x0fffffff);
        ais_test::setBits(classB,85,27,31000000);
    }
    const auto a=ais_test::transmit(classB,192000,-25000,0.01,3,false);
    const auto bad=ais_test::transmit(classB,192000,-25000,0.01,3,true);
    const auto b=ais_test::transmit(ais_test::position(987654321),192000,25000,0.01,3,false);
    std::vector<std::complex<float>> iq(192000/4,{0,0});
    auto append=[&](const auto &first) {
        for(std::size_t i=0;i<std::max(first.size(),b.size());++i) {
            const auto x=i<first.size() ? first[i] : std::complex<float>{};
            const auto y=i<b.size() ? b[i] : std::complex<float>{};
            iq.push_back(x+y);
        }
        iq.insert(iq.end(),192000/20,{0,0});
    };
    for(int i=0;i<10;++i) append(bad);
    const int validFrames=qEnvironmentVariableIntValue("DSDR_TEST_AIS_SUSTAINED")==1 ? 220 : 55;
    for(int i=0;i<validFrames;++i) append(a);
    if(rate!=192000) {
        std::vector<std::complex<float>> resampled;
        const auto count=std::size_t(double(iq.size()-1)*rate/192000.0);
        resampled.reserve(count);
        for(std::size_t i=0;i<count;++i) {
            const double at=double(i)*192000.0/rate;
            const auto index=std::size_t(at);
            const float fraction=float(at-index);
            resampled.push_back(iq[index]+(iq[index+1]-iq[index])*fraction);
        }
        iq=std::move(resampled);
    }
    const quint32 bytes=quint32(iq.size()*2*sizeof(float));
    QByteArray h; h.append("RIFF",4); u32(h,36+bytes); h.append("WAVEfmt ",8); u32(h,16);
    u16(h,3);u16(h,2);u32(h,rate);u32(h,rate*8);u16(h,8);u16(h,32);h.append("data",4);u32(h,bytes);
    QFile f(path); if(!f.open(QIODevice::WriteOnly))return false;
    if(f.write(h)!=h.size())return false;
    for(auto z:iq) { float pair[2]={z.real(),z.imag()}; if(f.write(reinterpret_cast<char *>(pair),sizeof(pair))!=sizeof(pair))return false; }
    QFile side(QFileInfo(path).absolutePath()+"/ais-session.json");
    if(!side.open(QIODevice::WriteOnly))return false;
    side.write(QJsonDocument(QJsonObject{{"format","decodium-iq/1"},{"sampleRate",int(rate)},{"centerFrequencyHz",162000000}}).toJson());
    return true;
}
}
class TestAisSessionLive : public QObject {
    Q_OBJECT
private slots:
    void recordedIqToDialog() {
        const QString path=qEnvironmentVariable("DSDR_TEST_AIS_RECORDED_IQ");
        if(path.isEmpty()) QSKIP("Optional external AIS IQ fixture not provided");
        QVERIFY(qEnvironmentVariableIntValue("DSDR_TEST_SILENT_AUDIO")==1);
        QTemporaryDir temp(QDir::tempPath()+"/dsdr-ais-real-XXXXXX");QVERIFY(temp.isValid());
        QCoreApplication::setOrganizationName("DECODIUM-ais-test");QCoreApplication::setApplicationName("recorded-session");
        QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,temp.path());
        qputenv("DSDR_TEST_APPDATA_DIR",temp.path().toUtf8());qputenv("DSDR_IQFILE_PATH",path.toUtf8());
        QVERIFY(QDir().mkpath(temp.path()+"/qml/DecodiumSdr"));QFile module(temp.path()+"/qml/DecodiumSdr/qmldir");
        QVERIFY(module.open(QIODevice::WriteOnly));module.write("module DecodiumSdr\nsingleton Theme 1.0 Theme.qml\n");module.close();
        QVERIFY(QFile::copy(QStringLiteral(DSDR_AIS_QML_DIR "/Theme.qml"),temp.path()+"/qml/DecodiumSdr/Theme.qml"));
        SessionManager session;QQmlApplicationEngine qml;qml.addImportPath(temp.path()+"/qml");qml.rootContext()->setContextProperty("aisController",session.ais());
        auto text=QStringLiteral(R"(import QtQuick
import QtQuick.Controls.Basic
import "%1" as App
ApplicationWindow { visible:true; width:800; height:600
 App.AisDialog { controller:aisController; Component.onCompleted:open() }
})").arg(QUrl::fromLocalFile(QStringLiteral(DSDR_AIS_QML_DIR)).toString());
        qml.loadData(text.toUtf8(),QUrl::fromLocalFile(temp.path()+"/app.qml"));QVERIFY(!qml.rootObjects().isEmpty());
        auto *label=qml.rootObjects().first()->findChild<QObject*>("aisMmsi");QVERIFY(label);
        session.selectBackend("iqfile");session.startDiscovery();QTRY_VERIFY_WITH_TIMEOUT(session.devices()->rowCount()>0,5000);
        int row=-1;for(int i=0;i<session.devices()->rowCount();++i)if(session.devices()->at(i)->deviceId==path)row=i;
        QVERIFY(row>=0);session.connectToDevice(row);QVERIFY(session.isConnected());session.ais()->setEnabled(true);
        QTRY_VERIFY_WITH_TIMEOUT(session.ais()->frameCount()>0,15000);QVERIFY(!session.ais()->mmsi().isEmpty());
        const QString referencePath=qEnvironmentVariable("DSDR_TEST_AIS_REFERENCE_IDS");
        if(!referencePath.isEmpty()) {
            QFile reference(referencePath);QVERIFY(reference.open(QIODevice::ReadOnly|QIODevice::Text));
            const auto lines=reference.readAll().split('\n');QVERIFY(lines.size()>1);
            bool matched=false;QElapsedTimer deadline;deadline.start();
            while(!matched && deadline.elapsed()<15000) {
                const QByteArray current=session.ais()->mmsi().toUtf8();
                matched=!current.isEmpty() && lines.contains(current);
                if(!matched) QTest::qWait(20);
            }
            QVERIFY(matched);
        }
        QTRY_VERIFY(label->property("text").toString().contains(session.ais()->mmsi()));
        session.requestShutdown();QTRY_VERIFY_WITH_TIMEOUT(session.shutdownFencePassed(),10000);qunsetenv("DSDR_IQFILE_PATH");
    }
    void vorSessionControlsAndShutdown() {
        QVERIFY(qEnvironmentVariableIntValue("DSDR_TEST_SILENT_AUDIO")==1);
        QTemporaryDir temp(QDir::tempPath()+"/dsdr-vor-session-XXXXXX"); QVERIFY(temp.isValid());
        QCoreApplication::setOrganizationName("DECODIUM-vor-test"); QCoreApplication::setApplicationName("vor-session");
        QSettings::setDefaultFormat(QSettings::IniFormat); QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,temp.path());
        qputenv("DSDR_TEST_APPDATA_DIR",temp.path().toUtf8());
        SessionManager session; QVERIFY(session.vor()); QVERIFY(!session.vor()->enabled());
        session.vor()->setEnabled(true); QVERIFY(!session.vor()->active());
        QVERIFY(!session.vor()->setFrequencyHz(0));
        session.selectBackend("demo"); session.startDiscovery(); QTRY_VERIFY_WITH_TIMEOUT(session.devices()->rowCount()>0,5000);
        session.connectToDevice(0); QVERIFY(session.isConnected());
        QVERIFY(session.vor()->setFrequencyHz(session.centerFrequency()));
        QTRY_VERIFY_WITH_TIMEOUT(session.vor()->active(),5000);
        session.vor()->setEnabled(false); QTRY_VERIFY_WITH_TIMEOUT(!session.vor()->active(),5000);
        session.requestShutdown(); QTRY_VERIFY_WITH_TIMEOUT(session.shutdownFencePassed(),10000);
        QVERIFY(!session.isConnected());
    }
    void pocsagSessionControlsAndShutdown() {
        QVERIFY(qEnvironmentVariableIntValue("DSDR_TEST_SILENT_AUDIO")==1);
        QTemporaryDir temp(QDir::tempPath()+"/dsdr-pocsag-session-XXXXXX"); QVERIFY(temp.isValid());
        QCoreApplication::setOrganizationName("DECODIUM-pocsag-test"); QCoreApplication::setApplicationName("pocsag-session");
        QSettings::setDefaultFormat(QSettings::IniFormat); QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,temp.path());
        qputenv("DSDR_TEST_APPDATA_DIR",temp.path().toUtf8());
        SessionManager session; QVERIFY(session.pocsag()); QVERIFY(!session.pocsag()->enabled());
        session.pocsag()->setEnabled(true); QVERIFY(!session.pocsag()->active());
        QVERIFY(session.pocsag()->status().contains(QStringLiteral("non supportati")));
        QVERIFY(!session.pocsag()->configureRequested(900,7000000));
        QVERIFY(session.pocsag()->configureRequested(1200,7000000));
        session.selectBackend("demo"); session.startDiscovery();
        QTRY_VERIFY_WITH_TIMEOUT(session.devices()->rowCount()>0,5000);
        session.connectToDevice(0); QVERIFY(session.isConnected());
        QVERIFY(session.pocsag()->configureRequested(1200,session.centerFrequency()));
        QTRY_VERIFY_WITH_TIMEOUT(session.pocsag()->active(),5000);
        session.pocsag()->setEnabled(false);
        QTRY_VERIFY_WITH_TIMEOUT(!session.pocsag()->active(),5000);
        session.requestShutdown(); QTRY_VERIFY_WITH_TIMEOUT(session.shutdownFencePassed(),10000);
        QVERIFY(!session.isConnected());
    }
    void slowWorkerShutdownKeepsGuiResponsive() {
        struct Gate {
            std::atomic<bool> entered{false},release{false};
            static void hook(void *p) noexcept {
                auto &g=*static_cast<Gate *>(p); g.entered=true;
                while(!g.release.load()) std::this_thread::yield();
            }
        } gate;
        QTemporaryDir temp(QDir::tempPath()+"/dsdr-ais-shutdown-XXXXXX"); QVERIFY(temp.isValid());
        QCoreApplication::setOrganizationName("DECODIUM-ais-test"); QCoreApplication::setApplicationName("ais-shutdown");
        QSettings::setDefaultFormat(QSettings::IniFormat); QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,temp.path());
        qputenv("DSDR_TEST_APPDATA_DIR",temp.path().toUtf8());
        auto receiver=std::make_unique<AisReceiver>(64,4096,16,Gate::hook,&gate);
        auto *raw=receiver.get(); SessionManager session(nullptr,std::move(receiver));
        struct Unblock { Gate &g; ~Unblock(){g.release=true;} } unblock{gate};
        session.selectBackend("demo"); session.startDiscovery();
        QTRY_VERIFY_WITH_TIMEOUT(session.devices()->rowCount()>0,5000);
        session.connectToDevice(0); QVERIFY(session.isConnected());
        // Inject a synthetic filtered source only to hold this decoder worker;
        // the real demo backend still exercises shutdown and backend closure.
        raw->setSource(true,48000,0); session.ais()->setEnabled(true);
        auto iq=ais_test::transmit(ais_test::position(),48000,0,0,3,false);
        QCOMPARE(raw->submit(iq.data(),iq.size()),AsyncIqDecoder::SubmitResult::Accepted);
        QTRY_VERIFY_WITH_TIMEOUT(gate.entered.load(),3000);
        int ticks=0; QTimer timer; timer.setInterval(10); connect(&timer,&QTimer::timeout,[&]{++ticks;}); timer.start();
        QElapsedTimer elapsed; elapsed.start(); session.requestShutdown();
        QVERIFY(elapsed.elapsed()<500); QTRY_VERIFY_WITH_TIMEOUT(ticks >= 5, 3000);
        QVERIFY(session.shutdownPending()); QVERIFY(!session.shutdownFencePassed());
        gate.release=true;
        QTRY_VERIFY_WITH_TIMEOUT(session.shutdownFencePassed(),10000);
        QVERIFY(!session.isConnected());
    }
    void sourceUiAndShutdown_data() {
        QTest::addColumn<unsigned>("messageType");
        QTest::addColumn<unsigned>("sampleRate");
        QTest::newRow("class-A-type-1") << 1u << 192000u;
        QTest::newRow("class-A-type-2") << 2u << 192000u;
        QTest::newRow("class-A-type-3") << 3u << 192000u;
        QTest::newRow("class-B-type-18") << 18u << 192000u;
        QTest::newRow("class-B-type-19") << 19u << 192000u;
        QTest::newRow("long-range-type-27") << 27u << 192000u;
        QTest::newRow("base-station-type-4") << 4u << 192000u;
        QTest::newRow("sar-aircraft-type-9") << 9u << 192000u;
        QTest::newRow("utc-response-type-11") << 11u << 192000u;
        QTest::newRow("aid-to-navigation-type-21") << 21u << 192000u;
        QTest::newRow("dgnss-station-type-17") << 17u << 192000u;
        QTest::newRow("class-B-type-18-200k") << 18u << 200000u;
        QTest::newRow("class-B-type-18-960k") << 18u << 960000u;
    }
    void sourceUiAndShutdown() {
        QFETCH(unsigned,messageType);
        QFETCH(unsigned,sampleRate);
        QVERIFY(qEnvironmentVariableIntValue("DSDR_TEST_SILENT_AUDIO")==1);
        QTemporaryDir temp(QDir::tempPath()+"/dsdr-ais-session-XXXXXX"); QVERIFY(temp.isValid());
        QCoreApplication::setOrganizationName("DECODIUM-ais-test"); QCoreApplication::setApplicationName("ais-session");
        QSettings::setDefaultFormat(QSettings::IniFormat); QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,temp.path());
        qputenv("DSDR_TEST_APPDATA_DIR",temp.path().toUtf8());
        const auto path=temp.path()+"/ais-session.wav"; QVERIFY(makeWav(path,messageType,sampleRate)); qputenv("DSDR_IQFILE_PATH",path.toUtf8());
        QVERIFY(QDir().mkpath(temp.path()+"/qml/DecodiumSdr"));
        QFile module(temp.path()+"/qml/DecodiumSdr/qmldir"); QVERIFY(module.open(QIODevice::WriteOnly));
        module.write("module DecodiumSdr\nsingleton Theme 1.0 Theme.qml\n"); module.close();
        QVERIFY(QFile::copy(QStringLiteral(DSDR_AIS_QML_DIR "/Theme.qml"),temp.path()+"/qml/DecodiumSdr/Theme.qml"));
        SessionManager session; QVERIFY(session.ais()); QVERIFY(!session.ais()->enabled());
        QQmlApplicationEngine qml; qml.addImportPath(temp.path()+"/qml");
        qml.rootContext()->setContextProperty("aisController",session.ais());
        const auto qmlText=QStringLiteral(R"(import QtQuick
import QtQuick.Controls.Basic
import "%1" as App
ApplicationWindow { visible: true; width: 800; height: 600
    App.AisDialog { objectName: "dialog"; controller: aisController; Component.onCompleted: open() }
})").arg(QUrl::fromLocalFile(QStringLiteral(DSDR_AIS_QML_DIR)).toString());
        qml.loadData(qmlText.toUtf8(),QUrl::fromLocalFile(temp.path()+"/app.qml"));
        QVERIFY(!qml.rootObjects().isEmpty()); auto *root=qml.rootObjects().first();
        auto *window=qobject_cast<QQuickWindow *>(root); QVERIFY(window);
        auto *dialog=root->findChild<QObject *>("dialog"); QVERIFY(dialog);
        QVERIFY(dialog->property("title").toString().contains("sperimentale"));
        auto *toggle=root->findChild<QQuickItem *>("aisEnabled"); QVERIFY(toggle);
        auto *combo=root->findChild<QQuickItem *>("aisChannel"); QVERIFY(combo);
        auto *label=root->findChild<QObject *>("aisMmsi"); QVERIFY(label);
        auto *position=root->findChild<QObject *>("aisPosition"); QVERIFY(position);
        auto *processed=root->findChild<QObject *>("aisProcessedUtc"); QVERIFY(processed);
        QVERIFY(processed->property("text").toString().contains(QStringLiteral("—")));
        auto *capabilities=root->findChild<QObject *>("aisCapabilities"); QVERIFY(capabilities);
        QVERIFY(capabilities->property("text").toString().contains(QStringLiteral("18, 19, 21 e 27")));
        QTRY_VERIFY_WITH_TIMEOUT(dialog->property("opened").toBool(),3000);
        session.selectBackend("iqfile"); session.startDiscovery();
        QTRY_VERIFY_WITH_TIMEOUT(session.devices()->rowCount()>0,5000);
        int row=-1;
        for(int i=0;i<session.devices()->rowCount();++i) if(session.devices()->at(i)->deviceId==path) row=i;
        QVERIFY(row>=0); session.connectToDevice(row); QVERIFY(session.isConnected());
        auto point=toggle->mapToScene(QPointF(toggle->width()/2,toggle->height()/2));
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,point.toPoint());
        QTRY_VERIFY_WITH_TIMEOUT(session.ais()->enabled(),3000);
        QTRY_COMPARE_WITH_TIMEOUT(session.ais()->status(),QStringLiteral("Ricezione AIS attiva"),3000);
        QTest::qWait(350); session.ais()->poll(); QCOMPARE(session.ais()->frameCount(),qulonglong(0));
        QTRY_COMPARE_WITH_TIMEOUT(session.ais()->mmsi(),QStringLiteral("123456789"),10000);
        QCOMPARE(session.ais()->droppedBlocks(),qulonglong(0));
        QVERIFY(session.ais()->hasPosition());
        QCOMPARE(session.ais()->messageType(),int(messageType));
        const double expectedLatitude=(messageType==27 || messageType==17) ? 50.0 : 31000000.0/600000.0;
        QVERIFY(qAbs(session.ais()->latitude()-expectedLatitude)<1e-6);
        QVERIFY(qAbs(session.ais()->longitude()-(-12.5))<1e-6);
        QTRY_VERIFY(label->property("text").toString().contains("123456789"));
        QTRY_VERIFY(position->property("text").toString().contains(QStringLiteral("-12.50000")));
        QVERIFY(position->property("text").toString().contains(QStringLiteral("ultima trama")));
        QTRY_VERIFY(!session.ais()->processedUtc().isEmpty());
        QVERIFY(processed->property("text").toString().contains(session.ais()->processedUtc()));
        if(messageType==17) QVERIFY(position->property("text").toString().contains(QStringLiteral("DGNSS")));
        if(sampleRate==960000 && qEnvironmentVariableIntValue("DSDR_TEST_AIS_SUSTAINED")==1) {
            const auto initial=session.ais()->frameCount();
            const auto initialDrops=session.ais()->droppedBlocks();
            int ticks=0; QTimer heartbeat; heartbeat.setInterval(20);
            connect(&heartbeat,&QTimer::timeout,[&]{++ticks;}); heartbeat.start();
            QTest::qWait(15000); session.ais()->poll();
            qInfo() << "AIS sustained frames" << initial << "->" << session.ais()->frameCount()
                    << "dropped blocks" << initialDrops << "->" << session.ais()->droppedBlocks();
            QVERIFY(session.ais()->frameCount()>initial+20);
            QCOMPARE(session.ais()->droppedBlocks(),initialDrops);
            QVERIFY(ticks>200);
        }
        // Exercise the real combo's activation handler to select AIS 2.
        combo->setProperty("currentIndex",1);
        QVERIFY(QMetaObject::invokeMethod(combo,"activated",Q_ARG(int,1)));
        QCOMPARE(session.ais()->channel(),1);
        QVERIFY(session.ais()->mmsi().isEmpty());
        QVERIFY(!session.ais()->hasPosition());
        QVERIFY(session.ais()->processedUtc().isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(session.ais()->channel(),1,3000);
        QTRY_COMPARE_WITH_TIMEOUT(session.ais()->mmsi(),QStringLiteral("987654321"),5000);
        if(messageType==18) {
            session.disconnectDevice();
            QTRY_VERIFY_WITH_TIMEOUT(!session.isConnected(),5000);
            QTRY_COMPARE_WITH_TIMEOUT(session.ais()->status(),QStringLiteral("Sorgente IQ non disponibile"),3000);
            QTRY_VERIFY_WITH_TIMEOUT(session.ais()->mmsi().isEmpty(),3000);
            QVERIFY(!session.ais()->hasPosition());
            session.connectToDevice(row); QVERIFY(session.isConnected());
            QTRY_COMPARE_WITH_TIMEOUT(session.ais()->mmsi(),QStringLiteral("987654321"),10000);
            QTRY_VERIFY(label->property("text").toString().contains(QStringLiteral("987654321")));
        }
        QElapsedTimer timer; timer.start(); session.requestShutdown();
        QVERIFY2(timer.elapsed()<500,"shutdown request blocked the GUI");
        QTRY_VERIFY_WITH_TIMEOUT(session.shutdownFencePassed(),10000);
        QVERIFY(!session.isConnected());
        QCOMPARE(session.ais()->status(),QStringLiteral("Ricevitore AIS in arresto"));
    }
};
QTEST_MAIN(TestAisSessionLive)
#include "tst_ais_session_live.moc"
