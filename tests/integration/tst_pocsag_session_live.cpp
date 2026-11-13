// SPDX-License-Identifier: GPL-3.0-or-later
// Synthetic IQ file -> real session -> engine -> decoder -> actual QML dialog.
#include "core/SessionManager.h"
#include "../standalone/pocsag/async_fixture.h"
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
#include <algorithm>
#include <atomic>
#include <thread>
#include <QTimer>
using namespace dsdr::core;
namespace {
QQuickItem *visualFind(QQuickItem *item,const QString &name) {
    if(!item) return nullptr;
    if(item->objectName()==name) return item;
    for(auto *child:item->childItems()) if(auto *found=visualFind(child,name)) return found;
    return nullptr;
}
void u32(QByteArray &b,quint32 n) { char p[4]; qToLittleEndian(n,p); b.append(p,4); }
void u16(QByteArray &b,quint16 n) { char p[2]; qToLittleEndian(n,p); b.append(p,2); }
bool makeWav(const QString &path, unsigned rate, unsigned baud) {
    const auto iq=generate(rate,baud,2000,0.01,77);
    const int repetitions=baud==2400 ? (rate==2000000 ? 10 : 40) : 8;
    const quint32 bytes=quint32(iq.size()*repetitions*8);
    QByteArray h; h.append("RIFF",4); u32(h,36+bytes); h.append("WAVEfmt ",8); u32(h,16);
    u16(h,3); u16(h,2); u32(h,rate); u32(h,rate*8); u16(h,8); u16(h,32); h.append("data",4); u32(h,bytes);
    QFile file(path); if(!file.open(QIODevice::WriteOnly) || file.write(h)!=h.size()) return false;
    QByteArray block; block.reserve(int(iq.size()*8));
    for(auto v:iq) { const float pair[2]={v.real(),v.imag()}; block.append(reinterpret_cast<const char *>(pair),sizeof(pair)); }
    for(int n=0;n<repetitions;++n) if(file.write(block)!=block.size()) return false;
    QFile meta(QFileInfo(path).absolutePath()+"/pocsag-session.json"); if(!meta.open(QIODevice::WriteOnly)) return false;
    meta.write(QJsonDocument(QJsonObject{{"format","decodium-iq/1"},{"sampleRate",int(rate)},{"centerFrequencyHz",100000000}}).toJson());
    return true;
}
}
class TestPocsagSessionLive : public QObject {
    Q_OBJECT
private slots:
    void recordedIqToDialog() {
        const QString path=qEnvironmentVariable("DSDR_TEST_POCSAG_RECORDED_IQ");
        if(path.isEmpty()) QSKIP("Optional external POCSAG IQ fixture not provided");
        QVERIFY(qEnvironmentVariableIntValue("DSDR_TEST_SILENT_AUDIO")==1);
        QTemporaryDir temp(QDir::tempPath()+"/dsdr-pocsag-real-XXXXXX"); QVERIFY(temp.isValid());
        QCoreApplication::setOrganizationName("DECODIUM-pocsag-test");QCoreApplication::setApplicationName("recorded-session");
        QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,temp.path());
        qputenv("DSDR_TEST_APPDATA_DIR",temp.path().toUtf8());qputenv("DSDR_IQFILE_PATH",path.toUtf8());
        QVERIFY(QDir().mkpath(temp.path()+"/qml/DecodiumSdr"));
        QFile module(temp.path()+"/qml/DecodiumSdr/qmldir");QVERIFY(module.open(QIODevice::WriteOnly));
        module.write("module DecodiumSdr\nsingleton Theme 1.0 Theme.qml\n");module.close();
        QVERIFY(QFile::copy(QStringLiteral(DSDR_POCSAG_QML_DIR "/Theme.qml"),temp.path()+"/qml/DecodiumSdr/Theme.qml"));
        SessionManager session;QQmlApplicationEngine qml;qml.addImportPath(temp.path()+"/qml");qml.rootContext()->setContextProperty("pocsagController",session.pocsag());
        auto text=QStringLiteral(R"(import QtQuick
import QtQuick.Controls.Basic
import "%1" as App
ApplicationWindow { visible: true; width: 800; height: 600
    App.PocsagDialog { objectName: "dialog"; controller: pocsagController; Component.onCompleted: open() }
})").arg(QUrl::fromLocalFile(QStringLiteral(DSDR_POCSAG_QML_DIR)).toString());
        qml.loadData(text.toUtf8(),QUrl::fromLocalFile(temp.path()+"/app.qml"));QVERIFY(!qml.rootObjects().isEmpty());
        auto *window=qobject_cast<QQuickWindow*>(qml.rootObjects().first());QVERIFY(window);
        session.selectBackend("iqfile");session.startDiscovery();QTRY_VERIFY_WITH_TIMEOUT(session.devices()->rowCount()>0,5000);
        int row=-1;for(int i=0;i<session.devices()->rowCount();++i)if(session.devices()->at(i)->deviceId==path)row=i;
        QVERIFY(row>=0);session.connectToDevice(row);QVERIFY(session.isConnected());
        QVERIFY(session.pocsag()->configureRequested(1200,100000000));session.pocsag()->setEnabled(true);
        QTRY_VERIFY_WITH_TIMEOUT(session.pocsag()->messageCount()>0,15000);
        auto index=session.pocsag()->messages()->index(0,0);QCOMPARE(index.data(PocsagHistoryModel::AddressRole).toUInt(),1100004u);
        QVERIFY(index.data(PocsagHistoryModel::TextRole).toString().contains(QStringLiteral("This is a test of the pager")));
        QQuickItem *rendered=nullptr;QTRY_VERIFY_WITH_TIMEOUT((rendered=visualFind(window->contentItem(),QStringLiteral("pocsagMessageText")))!=nullptr,3000);
        QVERIFY(rendered->property("text").toString().contains(QStringLiteral("This is a test of the pager")));
        session.requestShutdown();QTRY_VERIFY_WITH_TIMEOUT(session.shutdownFencePassed(),10000);qunsetenv("DSDR_IQFILE_PATH");
    }
    void slowWorkerShutdown() {
        struct Gate {
            std::atomic<bool> entered{false},release{false};
            static void hold(void *p) noexcept {
                auto &g=*static_cast<Gate *>(p); g.entered=true;
                while(!g.release.load()) std::this_thread::yield();
            }
        } gate;
        QTemporaryDir temp(QDir::tempPath()+"/dsdr-pocsag-slow-XXXXXX"); QVERIFY(temp.isValid());
        QCoreApplication::setOrganizationName("DECODIUM-pocsag-test"); QCoreApplication::setApplicationName("slow-session");
        QSettings::setDefaultFormat(QSettings::IniFormat); QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,temp.path());
        qputenv("DSDR_TEST_APPDATA_DIR",temp.path().toUtf8());
        auto receiver=std::make_unique<PocsagReceiver>(64,4096,16,Gate::hold,&gate);
        SessionManager session(nullptr,{},std::move(receiver));
        struct Unblock { Gate &g; ~Unblock(){g.release=true;} } unblock{gate};
        session.selectBackend("demo"); session.startDiscovery(); QTRY_VERIFY_WITH_TIMEOUT(session.devices()->rowCount()>0,5000);
        session.connectToDevice(0); QVERIFY(session.isConnected());
        QVERIFY(session.pocsag()->configureRequested(1200,session.centerFrequency())); session.pocsag()->setEnabled(true);
        QTRY_VERIFY_WITH_TIMEOUT(gate.entered.load(),5000);
        int ticks=0; QTimer heartbeat; heartbeat.setInterval(10); connect(&heartbeat,&QTimer::timeout,[&]{++ticks;}); heartbeat.start();
        QElapsedTimer elapsed; elapsed.start(); session.requestShutdown(); QVERIFY(elapsed.elapsed()<500);
        QTest::qWait(200); QVERIFY(session.shutdownPending()); QVERIFY(!session.shutdownFencePassed()); QVERIFY(ticks>=5);
        gate.release=true; QTRY_VERIFY_WITH_TIMEOUT(session.shutdownFencePassed(),10000);
        QVERIFY(!session.isConnected());
    }
    void sourceToDialogAB_data() {
        QTest::addColumn<unsigned>("sampleRate");
        QTest::addColumn<unsigned>("baud");
        QTest::newRow("192k") << 192000u << 2400u;
        QTest::newRow("200k") << 200000u << 2400u;
        QTest::newRow("1300k") << 1300000u << 2400u;
        QTest::newRow("2M") << 2000000u << 2400u;
        QTest::newRow("192k-512baud") << 192000u << 512u;
        QTest::newRow("192k-1200baud") << 192000u << 1200u;
        QTest::newRow("200k-512baud") << 200000u << 512u;
        QTest::newRow("200k-1200baud") << 200000u << 1200u;
    }
    void sourceToDialogAB() {
        QFETCH(unsigned,sampleRate);
        QFETCH(unsigned,baud);
        QVERIFY(qEnvironmentVariableIntValue("DSDR_TEST_SILENT_AUDIO")==1);
        QTemporaryDir temp(QDir::tempPath()+"/dsdr-pocsag-iq-XXXXXX"); QVERIFY(temp.isValid());
        QCoreApplication::setOrganizationName("DECODIUM-pocsag-test"); QCoreApplication::setApplicationName("iq-session");
        QSettings::setDefaultFormat(QSettings::IniFormat); QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,temp.path());
        qputenv("DSDR_TEST_APPDATA_DIR",temp.path().toUtf8());
        const auto path=temp.path()+"/pocsag-session.wav"; QVERIFY(makeWav(path,sampleRate,baud)); qputenv("DSDR_IQFILE_PATH",path.toUtf8());
        QVERIFY(QDir().mkpath(temp.path()+"/qml/DecodiumSdr"));
        QFile module(temp.path()+"/qml/DecodiumSdr/qmldir"); QVERIFY(module.open(QIODevice::WriteOnly));
        module.write("module DecodiumSdr\nsingleton Theme 1.0 Theme.qml\n"); module.close();
        QVERIFY(QFile::copy(QStringLiteral(DSDR_POCSAG_QML_DIR "/Theme.qml"),temp.path()+"/qml/DecodiumSdr/Theme.qml"));
        SessionManager session; QQmlApplicationEngine qml; qml.addImportPath(temp.path()+"/qml");
        qml.rootContext()->setContextProperty("pocsagController",session.pocsag());
        const auto text=QStringLiteral(R"(import QtQuick
import QtQuick.Controls.Basic
import "%1" as App
ApplicationWindow { visible: true; width: 800; height: 600
    App.PocsagDialog { objectName: "dialog"; controller: pocsagController; Component.onCompleted: open() }
})").arg(QUrl::fromLocalFile(QStringLiteral(DSDR_POCSAG_QML_DIR)).toString());
        qml.loadData(text.toUtf8(),QUrl::fromLocalFile(temp.path()+"/app.qml")); QVERIFY(!qml.rootObjects().isEmpty());
        auto *root=qml.rootObjects().first(); auto *window=qobject_cast<QQuickWindow *>(root); QVERIFY(window);
        auto *dialog=root->findChild<QObject *>("dialog"); QVERIFY(dialog);
        QVERIFY(dialog->property("title").toString().contains("sperimentale"));
        auto *toggle=root->findChild<QQuickItem *>("pocsagEnabled"); QVERIFY(toggle);
        session.selectBackend("iqfile"); session.startDiscovery(); QTRY_VERIFY_WITH_TIMEOUT(session.devices()->rowCount()>0,5000);
        int row=-1; for(int i=0;i<session.devices()->rowCount();++i) if(session.devices()->at(i)->deviceId==path) row=i;
        QVERIFY(row>=0); session.connectToDevice(row); QVERIFY(session.isConnected());
        const bool testOutOfBand = sampleRate == 200000 && baud == 2400;
        QVERIFY(session.pocsag()->configureRequested(baud, testOutOfBand ? 100002000 + sampleRate : 100002000));
        QTest::qWait(700); session.pocsag()->poll(); QCOMPARE(session.pocsag()->messageCount(),qulonglong(0));
        const auto point=toggle->mapToScene(QPointF(toggle->width()/2,toggle->height()/2));
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,point.toPoint()); QTRY_VERIFY(session.pocsag()->enabled());
        if(sampleRate==192000 && (baud==512 || baud==1200)) {
            QVERIFY(session.pocsag()->configureRequested(2400,100002000));
            QTest::qWait(4000); session.pocsag()->poll();
            QCOMPARE(session.pocsag()->messageCount(),qulonglong(0));
            QVERIFY(session.pocsag()->configureRequested(baud,100002000));
        }
        if (testOutOfBand) {
            auto *status = root->findChild<QObject *>("pocsagStatus"); QVERIFY(status);
            QTRY_VERIFY_WITH_TIMEOUT(status->property("text").toString().contains("non supportati"),3000);
            QTest::qWait(700); session.pocsag()->poll(); QCOMPARE(session.pocsag()->messageCount(),qulonglong(0));
            QVERIFY(session.pocsag()->configureRequested(baud,100002000));
        }
        QTest::qWait(2500);
        qInfo() << "POCSAG diagnostic" << sampleRate << session.pocsag()->status()
                << "dropped blocks" << session.pocsag()->droppedBlocks()
                << "messages" << session.pocsag()->messageCount();
        QCOMPARE(session.pocsag()->droppedBlocks(),qulonglong(0));
        QTRY_VERIFY_WITH_TIMEOUT(session.pocsag()->messageCount()>0,12000);
        const auto index=session.pocsag()->messages()->index(0,0);
        QCOMPARE(index.data(PocsagHistoryModel::AddressRole).toUInt(),1234563u);
        QCOMPARE(index.data(PocsagHistoryModel::TextRole).toString(),QStringLiteral("HELLO PAGER"));
        QQuickItem *rendered=nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((rendered=visualFind(window->contentItem(),QStringLiteral("pocsagMessageText")))!=nullptr,3000);
        QCOMPARE(rendered->property("text").toString(),QStringLiteral("HELLO PAGER"));
        if (testOutOfBand) {
            QVERIFY(session.pocsag()->configureRequested(baud,100002000 + sampleRate));
            QTRY_COMPARE_WITH_TIMEOUT(session.pocsag()->messages()->rowCount(),0,3000);
            QCOMPARE(session.pocsag()->messageCount(),qulonglong(0));
            QVERIFY(session.pocsag()->configureRequested(baud,100002000));
            QTRY_VERIFY_WITH_TIMEOUT(session.pocsag()->messages()->rowCount()>0,12000);
            QCOMPARE(session.pocsag()->messages()->index(0,0).data(PocsagHistoryModel::TextRole).toString(),QStringLiteral("HELLO PAGER"));
        }
        if(sampleRate==192000) {
            session.disconnectDevice();
            QTRY_VERIFY_WITH_TIMEOUT(!session.isConnected(),5000);
            QTRY_VERIFY_WITH_TIMEOUT(!session.pocsag()->active(),3000);
            QTRY_COMPARE_WITH_TIMEOUT(session.pocsag()->messages()->rowCount(),0,3000);
            session.connectToDevice(row); QVERIFY(session.isConnected());
            QTRY_VERIFY_WITH_TIMEOUT(session.pocsag()->messages()->rowCount()>0,12000);
            QCOMPARE(session.pocsag()->messages()->index(0,0).data(PocsagHistoryModel::TextRole).toString(),QStringLiteral("HELLO PAGER"));
        }
        if(sampleRate==2000000 && qEnvironmentVariableIntValue("DSDR_TEST_POCSAG_SUSTAINED")==1) {
            const auto initialCount=session.pocsag()->messageCount();
            const auto initialDrops=session.pocsag()->droppedBlocks();
            int ticks=0; QTimer heartbeat; heartbeat.setInterval(20);
            connect(&heartbeat,&QTimer::timeout,[&]{++ticks;}); heartbeat.start();
            const int requested=qEnvironmentVariableIntValue("DSDR_TEST_POCSAG_SUSTAINED_MS");
            const int duration=std::clamp(requested>0 ? requested : 15000,1000,120000);
            QTest::qWait(duration);
            session.pocsag()->poll();
            qInfo() << "POCSAG sustained" << "messages" << initialCount << "->"
                    << session.pocsag()->messageCount() << "drops" << initialDrops << "->"
                    << session.pocsag()->droppedBlocks();
            QVERIFY(session.pocsag()->messageCount()>initialCount);
            QCOMPARE(session.pocsag()->droppedBlocks(),initialDrops);
            QVERIFY(ticks>duration/100);
        }
        QElapsedTimer timer; timer.start(); session.requestShutdown(); QVERIFY(timer.elapsed()<500);
        QTRY_VERIFY_WITH_TIMEOUT(session.shutdownFencePassed(),10000);
    }
};
QTEST_MAIN(TestPocsagSessionLive)
#include "tst_pocsag_session_live.moc"
