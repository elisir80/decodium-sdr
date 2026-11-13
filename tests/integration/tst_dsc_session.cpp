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
#include <array>
#include <atomic>
#include <cmath>
#include <thread>
#include <QTimer>
#include <vector>
using namespace dsdr::core;
namespace {
void u32(QByteArray& b,quint32 n) { char p[4]; qToLittleEndian(n,p); b.append(p,4); }
void u16(QByteArray& b,quint16 n) { char p[2]; qToLittleEndian(n,p); b.append(p,2); }
bool makeWav(const QString& path,int format) {
    constexpr unsigned rate=192000, repeat=4; constexpr double pi=3.14159265358979323846;
    std::vector<int> body;
    if(format==112 || format==912) body={112,23,21,23,45,60,103,15,5,10,1,18,12,34,109};
    else if(format==116 || format==416) body={116,format==416?110:108,23,21,23,45,60,100,126,126,126,126,126,126,126};
    else if(format==120 || format==520 || format==620) body={120,12,34,56,78,90,100,23,21,23,45,60,100,126,126,126,126,126,126,126};
    else if(format==114 || format==123 || format==102)
        body={format,12,34,56,78,format==102?91:90,100,23,21,23,45,60,100,126,126,126,126,126,126,126};
    else if(format==514) body={114,12,34,56,78,90,106,23,21,23,45,60,100,126,126,126,126,126,126,126};
    else if(format==216 || format==316) body={116,112,23,21,23,45,60,format==216?110:112,12,34,56,78,90,103,99,99,99,99,99,88,88,109};
    else if(format==712 || format==812) body={format==712?120:102,12,34,56,78,format==712?90:91,112,23,21,23,45,60,format==712?110:112,12,34,56,78,90,103,99,99,99,99,99,88,88,109};
    else return false;
    std::vector<int> symbols(2*body.size()+22,126);
    for(int j=0;j<12;++j) symbols[j]=j%2?111-j/2:125;
    for(int j=13;j<16;j+=2) symbols[j]=111-j/2;
    std::vector<int> msg={body[0],body[0]}; msg.insert(msg.end(),body.begin()+1,body.end());
    const int eos=format==520?117:format==620?122:127;
    int parity=eos; for(int v:body) parity^=v;
    msg.push_back(eos); msg.push_back(parity);
    for(size_t j=0;j<msg.size();++j) { symbols[12+2*j]=msg[j]; symbols[17+2*j]=msg[j]; }
    symbols[symbols.size()-4]=eos; symbols[symbols.size()-2]=eos;
    std::vector<int> bits; for(int i=0;i<200;++i) bits.push_back((i+1)%2);
    for(int s:symbols) {
        int zeros=7; for(int k=0;k<7;++k) { int bit=(s>>k)&1; bits.push_back(bit); zeros-=bit; }
        for(int k=2;k>=0;--k) bits.push_back((zeros>>k)&1);
    }
    const size_t count=bits.size()*1920+rate/2;
    const quint32 bytes=quint32(count*8*repeat);
    QByteArray header; header.append("RIFF",4); u32(header,36+bytes); header.append("WAVEfmt ",8);
    u32(header,16); u16(header,3); u16(header,2); u32(header,rate); u32(header,rate*8); u16(header,8); u16(header,32);
    header.append("data",4); u32(header,bytes);
    QFile f(path); if(!f.open(QIODevice::WriteOnly) || f.write(header)!=header.size()) return false;
    QByteArray block; block.reserve(qsizetype(count*8)); double phase=0;
    for(size_t i=0;i<count;++i) {
        bool active=i<bits.size()*1920;
        if(active) phase+=(format==912?-1:1)*2*pi*(1700+(bits[i/1920]?-85:85))/rate;
        float pair[2]={active?float(.5*std::cos(phase)):0.f,active?float(.5*std::sin(phase)):0.f};
        block.append(reinterpret_cast<const char*>(pair),sizeof(pair));
    }
    for(unsigned n=0;n<repeat;++n) if(f.write(block)!=block.size()) return false;
    QFile meta(QFileInfo(path).absolutePath()+"/dsc-session.json"); if(!meta.open(QIODevice::WriteOnly)) return false;
    meta.write(QJsonDocument(QJsonObject{{"format","decodium-iq/1"},{"sampleRate",int(rate)},{"centerFrequencyHz",8414500}}).toJson());
    return true;
}
}
class TestDscSession : public QObject {
    Q_OBJECT
private slots:
    void recordedIqToDialog() {
        const QString path=qEnvironmentVariable("DSDR_TEST_DSC_RECORDED_IQ");
        const QString referencePath=qEnvironmentVariable("DSDR_TEST_DSC_REFERENCE_IDS");
        if(path.isEmpty() || referencePath.isEmpty()) QSKIP("optional external DSC recording not configured");
        QVERIFY(qEnvironmentVariableIntValue("DSDR_TEST_SILENT_AUDIO")==1);
        QFile ids(referencePath); QVERIFY(ids.open(QIODevice::ReadOnly));
        const auto expected=QString::fromUtf8(ids.readAll()).split('\n',Qt::SkipEmptyParts);
        QVERIFY(expected.size()==5);
        QTemporaryDir temp(QDir::tempPath()+"/dsdr-dsc-recorded-XXXXXX"); QVERIFY(temp.isValid());
        QCoreApplication::setOrganizationName("DECODIUM-dsc-test"); QCoreApplication::setApplicationName("recorded-session");
        QSettings::setDefaultFormat(QSettings::IniFormat); QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,temp.path());
        qputenv("DSDR_TEST_APPDATA_DIR",temp.path().toUtf8()); qputenv("DSDR_IQFILE_PATH",path.toUtf8());
        QVERIFY(QDir().mkpath(temp.path()+"/qml/DecodiumSdr"));
        QFile module(temp.path()+"/qml/DecodiumSdr/qmldir"); QVERIFY(module.open(QIODevice::WriteOnly));
        module.write("module DecodiumSdr\nsingleton Theme 1.0 Theme.qml\n"); module.close();
        QVERIFY(QFile::copy(QStringLiteral(DSDR_DSC_QML_DIR "/Theme.qml"),temp.path()+"/qml/DecodiumSdr/Theme.qml"));
        SessionManager session; QQmlApplicationEngine qml; qml.addImportPath(temp.path()+"/qml");
        qml.rootContext()->setContextProperty("dscController",session.dsc());
        const auto text=QStringLiteral(R"(import QtQuick
import QtQuick.Controls.Basic
import "%1" as App
ApplicationWindow { visible: true; width: 900; height: 700
    App.DscDialog { controller: dscController; Component.onCompleted: open() }
})").arg(QUrl::fromLocalFile(QStringLiteral(DSDR_DSC_QML_DIR)).toString());
        qml.loadData(text.toUtf8(),QUrl::fromLocalFile(temp.path()+"/app.qml")); QVERIFY(!qml.rootObjects().isEmpty());
        auto* root=qml.rootObjects().first(); auto* win=qobject_cast<QQuickWindow*>(root); QVERIFY(win);
        auto* toggle=root->findChild<QQuickItem*>("dscEnabled"); auto* label=root->findChild<QObject*>("dscMmsi"); QVERIFY(toggle && label);
        session.selectBackend("iqfile"); session.startDiscovery(); QTRY_VERIFY_WITH_TIMEOUT(session.devices()->rowCount()>0,5000);
        int row=-1; for(int i=0;i<session.devices()->rowCount();++i) if(session.devices()->at(i)->deviceId==path) row=i;
        QVERIFY(row>=0); session.connectToDevice(row); QVERIFY(session.isConnected());
        QTRY_VERIFY_WITH_TIMEOUT(session.channels()->rowCount()>0,5000);
        session.setChannelMode(0,int(dsdr::DemodMode::Usb)); session.setChannelFilter(0,100,3000); session.setChannelMuted(0,true);
        QTRY_VERIFY_WITH_TIMEOUT(!session.dsc()->channels().isEmpty(),3000);
        session.dsc()->selectChannel(session.dsc()->channels().first().toMap().value("id").toInt());
        const auto pos=toggle->mapToScene(QPointF(toggle->width()/2,toggle->height()/2));
        QTest::mouseClick(win,Qt::LeftButton,Qt::NoModifier,pos.toPoint()); QTRY_VERIFY(session.dsc()->enabled());
        const int configuredTimeout=qEnvironmentVariableIntValue("DSDR_TEST_DSC_TIMEOUT_MS");
        const int timeout=configuredTimeout>0 ? qBound(1000,configuredTimeout,90000) : 90000;
        QTRY_VERIFY_WITH_TIMEOUT(expected.contains(session.dsc()->lastMmsi()),timeout);
        QVERIFY(label->property("text").toString().contains(session.dsc()->lastMmsi()));
        session.requestShutdown(); QTRY_VERIFY_WITH_TIMEOUT(session.shutdownFencePassed(),10000);
        qunsetenv("DSDR_IQFILE_PATH");
    }
    void slowWorkerShutdown() {
        struct Gate {
            std::atomic<bool> entered{false},release{false};
            static void hold(void* p) noexcept {
                auto& g=*static_cast<Gate*>(p); g.entered=true;
                while(!g.release.load()) std::this_thread::yield();
            }
        } gate;
        QTemporaryDir temp(QDir::tempPath()+"/dsdr-dsc-slow-XXXXXX"); QVERIFY(temp.isValid());
        QCoreApplication::setOrganizationName("DECODIUM-dsc-test"); QCoreApplication::setApplicationName("slow-session");
        QSettings::setDefaultFormat(QSettings::IniFormat); QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,temp.path());
        qputenv("DSDR_TEST_APPDATA_DIR",temp.path().toUtf8());
        auto receiver=std::make_unique<DscReceiver>(64,4096,16,Gate::hold,&gate); auto* raw=receiver.get();
        SessionManager session(nullptr,{},{},{},{},{},std::move(receiver));
        struct Unblock { Gate& g; ~Unblock(){g.release=true;} } unblock{gate};
        session.selectBackend("demo"); session.startDiscovery(); QTRY_VERIFY_WITH_TIMEOUT(session.devices()->rowCount()>0,5000);
        session.connectToDevice(0); QVERIFY(session.isConnected());
        raw->setEnabled(true); const dsdr::dsp::Complex sample{0,0};
        QCOMPARE(raw->submitForGeneration(&sample,1,raw->generation()),dsdr::core::AsyncIqDecoder::SubmitResult::Accepted);
        QTRY_VERIFY_WITH_TIMEOUT(gate.entered.load(),5000);
        int ticks=0; QTimer heartbeat; heartbeat.setInterval(10); connect(&heartbeat,&QTimer::timeout,[&]{++ticks;}); heartbeat.start();
        QElapsedTimer elapsed; elapsed.start(); session.requestShutdown(); QVERIFY(elapsed.elapsed()<500);
        QTest::qWait(200); QVERIFY(session.shutdownPending()); QVERIFY(!session.shutdownFencePassed()); QVERIFY(ticks>=5);
        gate.release=true; QTRY_VERIFY_WITH_TIMEOUT(session.shutdownFencePassed(),10000);
        QVERIFY(!session.isConnected());
    }
    void iqFileToDialog_data() {
        QTest::addColumn<int>("callFormat"); QTest::addColumn<QString>("formatName");
        QTest::newRow("distress") << 112 << QStringLiteral("Soccorso");
        QTest::newRow("distress-LSB") << 912 << QStringLiteral("Soccorso");
        QTest::newRow("all-ships") << 116 << QStringLiteral("Tutte le navi");
        QTest::newRow("individual") << 120 << QStringLiteral("Individuale");
        QTest::newRow("group") << 114 << QStringLiteral("Gruppo");
        QTest::newRow("automatic") << 123 << QStringLiteral("Individuale automatico");
        QTest::newRow("area") << 102 << QStringLiteral("Area geografica");
        QTest::newRow("distress-ack") << 216 << QStringLiteral("Riscontro soccorso");
        QTest::newRow("distress-relay") << 316 << QStringLiteral("Rilancio soccorso");
        QTest::newRow("all-ships-urgency") << 416 << QStringLiteral("Tutte le navi");
        QTest::newRow("group-ships-business") << 514 << QStringLiteral("Gruppo");
        QTest::newRow("individual-request") << 520 << QStringLiteral("Individuale");
        QTest::newRow("individual-acknowledgement") << 620 << QStringLiteral("Individuale");
        QTest::newRow("addressed-distress-ack") << 712 << QStringLiteral("Riscontro rilancio soccorso");
        QTest::newRow("area-distress-relay") << 812 << QStringLiteral("Rilancio soccorso indirizzato");
    }
    void iqFileToDialog() {
        QFETCH(int,callFormat); QFETCH(QString,formatName);
        QVERIFY(qEnvironmentVariableIntValue("DSDR_TEST_SILENT_AUDIO")==1);
        QTemporaryDir temp(QDir::tempPath()+"/dsdr-dsc-iq-XXXXXX"); QVERIFY(temp.isValid());
        QCoreApplication::setOrganizationName("DECODIUM-dsc-test"); QCoreApplication::setApplicationName("iq-session");
        QSettings::setDefaultFormat(QSettings::IniFormat); QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,temp.path());
        qputenv("DSDR_TEST_APPDATA_DIR",temp.path().toUtf8());
        const auto path=temp.path()+"/dsc-session.wav"; QVERIFY(makeWav(path,callFormat)); qputenv("DSDR_IQFILE_PATH",path.toUtf8());
        QVERIFY(QDir().mkpath(temp.path()+"/qml/DecodiumSdr"));
        QFile module(temp.path()+"/qml/DecodiumSdr/qmldir"); QVERIFY(module.open(QIODevice::WriteOnly));
        module.write("module DecodiumSdr\nsingleton Theme 1.0 Theme.qml\n"); module.close();
        QVERIFY(QFile::copy(QStringLiteral(DSDR_DSC_QML_DIR "/Theme.qml"),temp.path()+"/qml/DecodiumSdr/Theme.qml"));
        SessionManager session; QQmlApplicationEngine qml; qml.addImportPath(temp.path()+"/qml");
        qml.rootContext()->setContextProperty("dscController",session.dsc());
        const auto text=QStringLiteral(R"(import QtQuick
import QtQuick.Controls.Basic
import "%1" as App
ApplicationWindow { visible: true; width: 900; height: 700
    App.DscDialog { objectName: "dialog"; controller: dscController; Component.onCompleted: open() }
})").arg(QUrl::fromLocalFile(QStringLiteral(DSDR_DSC_QML_DIR)).toString());
        qml.loadData(text.toUtf8(),QUrl::fromLocalFile(temp.path()+"/app.qml")); QVERIFY(!qml.rootObjects().isEmpty());
        auto* root=qml.rootObjects().first(); auto* win=qobject_cast<QQuickWindow*>(root); QVERIFY(win);
        auto* toggle=root->findChild<QQuickItem*>("dscEnabled"); auto* label=root->findChild<QObject*>("dscMmsi");
        auto* format=root->findChild<QObject*>("dscFormat"); QVERIFY(toggle && label && format);
        auto* category=root->findChild<QObject*>("dscCategory"); QVERIFY(category);
        auto* sequence=root->findChild<QObject*>("dscSequence"); QVERIFY(sequence);
        QVERIFY(category->property("text").toString().contains(QStringLiteral("—")));
        session.selectBackend("iqfile"); session.startDiscovery(); QTRY_VERIFY_WITH_TIMEOUT(session.devices()->rowCount()>0,5000);
        int row=-1; for(int i=0;i<session.devices()->rowCount();++i) if(session.devices()->at(i)->deviceId==path) row=i;
        QVERIFY(row>=0); session.connectToDevice(row); QVERIFY(session.isConnected());
        QTRY_VERIFY_WITH_TIMEOUT(session.channels()->rowCount()>0,5000);
        session.setChannelMode(0,int(callFormat==912 ? dsdr::DemodMode::Lsb : dsdr::DemodMode::Usb)); session.setChannelFilter(0,100,3000); session.setChannelMuted(0,true);
        QTRY_VERIFY_WITH_TIMEOUT(!session.dsc()->channels().isEmpty(),3000);
        session.dsc()->selectChannel(session.dsc()->channels().first().toMap().value("id").toInt());
        QTest::qWait(500); session.dsc()->poll(); QCOMPARE(session.dsc()->messageCount(),qulonglong(0));
        const auto p=toggle->mapToScene(QPointF(toggle->width()/2,toggle->height()/2));
        QTest::mouseClick(win,Qt::LeftButton,Qt::NoModifier,p.toPoint()); QTRY_VERIFY(session.dsc()->enabled());
        QTRY_COMPARE_WITH_TIMEOUT(session.dsc()->lastMmsi(),QStringLiteral("232123456"),25000);
        QTRY_VERIFY(label->property("text").toString().contains(QStringLiteral("232123456")));
        QTRY_VERIFY(format->property("text").toString().contains(formatName));
        const auto categoryName=(callFormat==112 || callFormat==912 || callFormat==216 || callFormat==316 || callFormat==712 || callFormat==812) ? QStringLiteral("Soccorso") :
                                callFormat==116 ? QStringLiteral("Sicurezza") : callFormat==416 ? QStringLiteral("Urgenza") :
                                callFormat==514 ? QStringLiteral("Servizio nave") : QStringLiteral("Routine");
        QTRY_VERIFY(category->property("text").toString().contains(categoryName));
        const auto sequenceName=callFormat==520 ? QStringLiteral("Riscontro richiesto") :
                                callFormat==620 ? QStringLiteral("Riscontro") : QStringLiteral("Fine chiamata");
        QTRY_COMPARE(session.dsc()->lastSequence(),sequenceName);
        QVERIFY(sequence->property("text").toString().contains(sequenceName));
        if(callFormat==112 || callFormat==912 || callFormat==520) {
            session.disconnectDevice();
            QTRY_VERIFY_WITH_TIMEOUT(!session.isConnected(),5000);
            QTRY_COMPARE_WITH_TIMEOUT(session.dsc()->status(),QStringLiteral("Seleziona un canale USB o LSB"),3000);
            QTRY_VERIFY_WITH_TIMEOUT(session.dsc()->lastMmsi().isEmpty(),3000);
            QVERIFY(session.dsc()->lastCategory().isEmpty());
            QVERIFY(session.dsc()->lastSequence().isEmpty());
            QVERIFY(session.dsc()->lastFormat().isEmpty());
            QTRY_VERIFY(label->property("text").toString().contains(QStringLiteral("—")));
            QTRY_VERIFY(format->property("text").toString().contains(QStringLiteral("—")));
            QTRY_VERIFY(category->property("text").toString().contains(QStringLiteral("—")));
            QTRY_VERIFY(sequence->property("text").toString().contains(QStringLiteral("—")));
            session.connectToDevice(row); QVERIFY(session.isConnected());
            QTRY_VERIFY_WITH_TIMEOUT(session.channels()->rowCount()>0,5000);
            session.setChannelMode(0,int(callFormat==912 ? dsdr::DemodMode::Lsb : dsdr::DemodMode::Usb)); session.setChannelFilter(0,100,3000); session.setChannelMuted(0,true);
            QTRY_VERIFY_WITH_TIMEOUT(!session.dsc()->channels().isEmpty(),3000);
            session.dsc()->selectChannel(session.dsc()->channels().first().toMap().value("id").toInt());
            QTRY_COMPARE_WITH_TIMEOUT(session.dsc()->lastMmsi(),QStringLiteral("232123456"),25000);
            QTRY_VERIFY(label->property("text").toString().contains(QStringLiteral("232123456")));
            QTRY_VERIFY(format->property("text").toString().contains(formatName));
            QTRY_COMPARE(session.dsc()->lastCategory(),categoryName);
            QTRY_COMPARE(session.dsc()->lastSequence(),sequenceName);
        }
        session.requestShutdown(); QTRY_VERIFY_WITH_TIMEOUT(session.shutdownFencePassed(),10000);
        qunsetenv("DSDR_IQFILE_PATH");
    }
    void dialogAndShutdown() {
        QVERIFY(qEnvironmentVariableIntValue("DSDR_TEST_SILENT_AUDIO")==1);
        QTemporaryDir temp(QDir::tempPath()+"/dsdr-dsc-session-XXXXXX"); QVERIFY(temp.isValid());
        QCoreApplication::setOrganizationName("DECODIUM-dsc-test"); QCoreApplication::setApplicationName("session");
        QSettings::setDefaultFormat(QSettings::IniFormat); QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,temp.path());
        qputenv("DSDR_TEST_APPDATA_DIR",temp.path().toUtf8());
        QVERIFY(QDir().mkpath(temp.path()+"/qml/DecodiumSdr"));
        QFile module(temp.path()+"/qml/DecodiumSdr/qmldir"); QVERIFY(module.open(QIODevice::WriteOnly));
        module.write("module DecodiumSdr\nsingleton Theme 1.0 Theme.qml\n"); module.close();
        QVERIFY(QFile::copy(QStringLiteral(DSDR_DSC_QML_DIR "/Theme.qml"),temp.path()+"/qml/DecodiumSdr/Theme.qml"));
        SessionManager session; QQmlApplicationEngine qml; qml.addImportPath(temp.path()+"/qml");
        qml.rootContext()->setContextProperty("dscController",session.dsc());
        const auto text=QStringLiteral(R"(import QtQuick
import QtQuick.Controls.Basic
import "%1" as App
ApplicationWindow { visible: true; width: 900; height: 700
    App.DscDialog { objectName: "dialog"; controller: dscController; Component.onCompleted: open() }
})").arg(QUrl::fromLocalFile(QStringLiteral(DSDR_DSC_QML_DIR)).toString());
        qml.loadData(text.toUtf8(),QUrl::fromLocalFile(temp.path()+"/app.qml")); QVERIFY(!qml.rootObjects().isEmpty());
        auto* root=qml.rootObjects().first(); auto* win=qobject_cast<QQuickWindow*>(root); QVERIFY(win);
        auto* toggle=root->findChild<QQuickItem*>("dscEnabled"); auto* label=root->findChild<QObject*>("dscStatus"); QVERIFY(toggle && label);
        QVERIFY(!session.dsc()->enabled());
        session.selectBackend("demo"); session.startDiscovery(); QTRY_VERIFY_WITH_TIMEOUT(session.devices()->rowCount()>0,5000);
        session.connectToDevice(0); QVERIFY(session.isConnected());
        QTRY_VERIFY_WITH_TIMEOUT(session.channels()->rowCount()>0,5000);
        session.setChannelMode(0,int(dsdr::DemodMode::Usb));
        QTRY_VERIFY_WITH_TIMEOUT(!session.dsc()->channels().isEmpty(),3000);
        session.dsc()->selectChannel(session.dsc()->channels().first().toMap().value("id").toInt());
        const auto p=toggle->mapToScene(QPointF(toggle->width()/2,toggle->height()/2));
        QTest::mouseClick(win,Qt::LeftButton,Qt::NoModifier,p.toPoint()); QTRY_VERIFY(session.dsc()->enabled());
        QTRY_VERIFY(label->property("text").toString().contains(QStringLiteral("MF/HF")));
        session.requestShutdown(); QTRY_VERIFY_WITH_TIMEOUT(session.shutdownFencePassed(),10000);
        QVERIFY(!session.dsc()->enabled());
    }
};
QTEST_MAIN(TestDscSession)
#include "tst_dsc_session.moc"
