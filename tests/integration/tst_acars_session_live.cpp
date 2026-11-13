// SPDX-License-Identifier: GPL-3.0-or-later
// Synthetic IQ file -> real session -> engine -> decoder -> actual QML dialog.
#include "core/SessionManager.h"
#include "../standalone/acars/acars_ui_synthetic.h"
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
QQuickItem *visualFind(QQuickItem *item,const QString &name) {
    if(!item) return nullptr;
    if(item->objectName()==name) return item;
    for(auto *child:item->childItems()) if(auto *found=visualFind(child,name)) return found;
    return nullptr;
}
void u32(QByteArray &b,quint32 n) { char p[4]; qToLittleEndian(n,p); b.append(p,4); }
void u16(QByteArray &b,quint16 n) { char p[2]; qToLittleEndian(n,p); b.append(p,2); }
bool makeWav(const QString &path) {
    const auto iq=generate(); constexpr int repetitions=20; const quint32 bytes=quint32(iq.size()*repetitions*8);
    QByteArray h; h.append("RIFF",4); u32(h,36+bytes); h.append("WAVEfmt ",8); u32(h,16);
    u16(h,3); u16(h,2); u32(h,unsigned(rate)); u32(h,unsigned(rate)*8); u16(h,8); u16(h,32); h.append("data",4); u32(h,bytes);
    QFile file(path); if(!file.open(QIODevice::WriteOnly) || file.write(h)!=h.size()) return false;
    QByteArray block; block.reserve(qsizetype(iq.size()*8));
    for(auto v:iq) { const float pair[2]={v.real(),v.imag()}; block.append(reinterpret_cast<const char *>(pair),sizeof(pair)); }
    for(int n=0;n<repetitions;++n) if(file.write(block)!=block.size()) return false;
    QFile meta(QFileInfo(path).absolutePath()+"/acars-session.json"); if(!meta.open(QIODevice::WriteOnly)) return false;
    meta.write(QJsonDocument(QJsonObject{{"format","decodium-iq/1"},{"sampleRate",int(rate)},{"centerFrequencyHz",131550000}}).toJson());
    return true;
}
}
class TestAcarsSessionLive : public QObject {
    Q_OBJECT
private slots:
    void recordedSourceToDialog() {
        const QString path=qEnvironmentVariable("DSDR_TEST_ACARS_RECORDED_IQ");
        if(path.isEmpty()) QSKIP("Optional external ACARS IQ fixture not provided");
        QVERIFY(qEnvironmentVariableIntValue("DSDR_TEST_SILENT_AUDIO")==1);
        QTemporaryDir temp(QDir::tempPath()+"/dsdr-acars-real-XXXXXX"); QVERIFY(temp.isValid());
        QCoreApplication::setOrganizationName("DECODIUM-acars-test"); QCoreApplication::setApplicationName("recorded-session");
        QSettings::setDefaultFormat(QSettings::IniFormat); QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,temp.path());
        qputenv("DSDR_TEST_APPDATA_DIR",temp.path().toUtf8()); qputenv("DSDR_IQFILE_PATH",path.toUtf8());
        QVERIFY(QDir().mkpath(temp.path()+"/qml/DecodiumSdr"));
        QFile module(temp.path()+"/qml/DecodiumSdr/qmldir"); QVERIFY(module.open(QIODevice::WriteOnly));
        module.write("module DecodiumSdr\nsingleton Theme 1.0 Theme.qml\n"); module.close();
        QVERIFY(QFile::copy(QStringLiteral(DSDR_ACARS_QML_DIR "/Theme.qml"),temp.path()+"/qml/DecodiumSdr/Theme.qml"));
        SessionManager session; QQmlApplicationEngine qml; qml.addImportPath(temp.path()+"/qml");
        qml.rootContext()->setContextProperty("acarsController",session.acars());
        const auto text=QStringLiteral(R"(import QtQuick
import QtQuick.Controls.Basic
import "%1" as App
ApplicationWindow { visible: true; width: 900; height: 700
    App.AcarsDialog { objectName: "dialog"; controller: acarsController; Component.onCompleted: open() }
})").arg(QUrl::fromLocalFile(QStringLiteral(DSDR_ACARS_QML_DIR)).toString());
        qml.loadData(text.toUtf8(),QUrl::fromLocalFile(temp.path()+"/app.qml")); QVERIFY(!qml.rootObjects().isEmpty());
        auto *window=qobject_cast<QQuickWindow *>(qml.rootObjects().first()); QVERIFY(window);
        session.selectBackend("iqfile"); session.startDiscovery(); QTRY_VERIFY_WITH_TIMEOUT(session.devices()->rowCount()>0,5000);
        int row=-1; for(int i=0;i<session.devices()->rowCount();++i) if(session.devices()->at(i)->deviceId==path) row=i;
        QVERIFY(row>=0); session.connectToDevice(row); QVERIFY(session.isConnected());
        QVERIFY(session.acars()->setFrequencyHz(131550000)); session.acars()->setEnabled(true);
        QTRY_VERIFY_WITH_TIMEOUT(session.acars()->messageCount()>0,12000);
        bool found=false;
        for(int i=0;i<session.acars()->messages()->rowCount();++i) {
            auto index=session.acars()->messages()->index(i,0);
            if(index.data(AcarsHistoryModel::AddressRole).toString()==QStringLiteral(".F-GTAE")) found=true;
        }
        QVERIFY(found);
        QQuickItem *rendered=nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((rendered=visualFind(window->contentItem(),QStringLiteral("acarsMessageText")))!=nullptr,3000);
        QVERIFY(rendered->property("text").toString().contains(QStringLiteral("#DFB00000")));
        auto *header=visualFind(window->contentItem(),QStringLiteral("acarsMessageHeader")); QVERIFY(header);
        QVERIFY(header->property("text").toString().contains(QStringLiteral(".F-GTAE")));
        session.requestShutdown(); QTRY_VERIFY_WITH_TIMEOUT(session.shutdownFencePassed(),10000);
    }
    void slowWorkerShutdown() {
        struct Gate {
            std::atomic<bool> entered{false},release{false};
            static void hold(void *p) noexcept {
                auto &g=*static_cast<Gate *>(p); g.entered=true;
                while(!g.release.load()) std::this_thread::yield();
            }
        } gate;
        QTemporaryDir temp(QDir::tempPath()+"/dsdr-acars-slow-XXXXXX"); QVERIFY(temp.isValid());
        QCoreApplication::setOrganizationName("DECODIUM-acars-test"); QCoreApplication::setApplicationName("slow-session");
        QSettings::setDefaultFormat(QSettings::IniFormat); QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,temp.path());
        qputenv("DSDR_TEST_APPDATA_DIR",temp.path().toUtf8());
        auto receiver=std::make_unique<AcarsReceiver>(64,4096,16,Gate::hold,&gate);
        auto *raw=receiver.get();
        SessionManager session(nullptr,{},{},std::move(receiver));
        struct Unblock { Gate &g; ~Unblock(){g.release=true;} } unblock{gate};
        session.selectBackend("demo"); session.startDiscovery(); QTRY_VERIFY_WITH_TIMEOUT(session.devices()->rowCount()>0,5000);
        session.connectToDevice(0); QVERIFY(session.isConnected());
        // The demo starts at 192 kHz; the ACARS RX accepts 1--4 MHz.
        // Supply synthetic samples to the held worker while keeping the real
        // session/backend shutdown path active.
        QVERIFY(session.acars()->setFrequencyHz(session.centerFrequency())); session.acars()->setEnabled(true);
        raw->setSource(true,1000000,0);
        const dsdr::dsp::Complex sample{0,0}; QCOMPARE(raw->submit(&sample,1),AcarsReceiver::SubmitResult::Accepted);
        QTRY_VERIFY_WITH_TIMEOUT(gate.entered.load(),5000);
        int ticks=0; QTimer heartbeat; heartbeat.setInterval(10); connect(&heartbeat,&QTimer::timeout,[&]{++ticks;}); heartbeat.start();
        QElapsedTimer elapsed; elapsed.start(); session.requestShutdown(); QVERIFY(elapsed.elapsed()<500);
        QTRY_VERIFY_WITH_TIMEOUT(ticks >= 5, 3000); QVERIFY(session.shutdownPending()); QVERIFY(!session.shutdownFencePassed());
        gate.release=true; QTRY_VERIFY_WITH_TIMEOUT(session.shutdownFencePassed(),10000);
        QVERIFY(!session.isConnected());
    }
    void sourceToDialogAB() {
        QVERIFY(qEnvironmentVariableIntValue("DSDR_TEST_SILENT_AUDIO")==1);
        QTemporaryDir temp(QDir::tempPath()+"/dsdr-acars-iq-XXXXXX"); QVERIFY(temp.isValid());
        QCoreApplication::setOrganizationName("DECODIUM-acars-test"); QCoreApplication::setApplicationName("iq-session");
        QSettings::setDefaultFormat(QSettings::IniFormat); QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,temp.path());
        qputenv("DSDR_TEST_APPDATA_DIR",temp.path().toUtf8());
        const auto path=temp.path()+"/acars-session.wav"; QVERIFY(makeWav(path)); qputenv("DSDR_IQFILE_PATH",path.toUtf8());
        QVERIFY(QDir().mkpath(temp.path()+"/qml/DecodiumSdr"));
        QFile module(temp.path()+"/qml/DecodiumSdr/qmldir"); QVERIFY(module.open(QIODevice::WriteOnly));
        module.write("module DecodiumSdr\nsingleton Theme 1.0 Theme.qml\n"); module.close();
        QVERIFY(QFile::copy(QStringLiteral(DSDR_ACARS_QML_DIR "/Theme.qml"),temp.path()+"/qml/DecodiumSdr/Theme.qml"));
        SessionManager session; QQmlApplicationEngine qml; qml.addImportPath(temp.path()+"/qml");
        qml.rootContext()->setContextProperty("acarsController",session.acars());
        const auto text=QStringLiteral(R"(import QtQuick
import QtQuick.Controls.Basic
import "%1" as App
ApplicationWindow { visible: true; width: 900; height: 700
    App.AcarsDialog { objectName: "dialog"; controller: acarsController; Component.onCompleted: open() }
})").arg(QUrl::fromLocalFile(QStringLiteral(DSDR_ACARS_QML_DIR)).toString());
        qml.loadData(text.toUtf8(),QUrl::fromLocalFile(temp.path()+"/app.qml")); QVERIFY(!qml.rootObjects().isEmpty());
        auto *root=qml.rootObjects().first(); auto *window=qobject_cast<QQuickWindow *>(root); QVERIFY(window);
        auto *dialog=root->findChild<QObject *>("dialog"); QVERIFY(dialog);
        QVERIFY(dialog->property("title").toString().contains("sperimentale"));
        auto *toggle=root->findChild<QQuickItem *>("acarsEnabled"); QVERIFY(toggle);
        session.selectBackend("iqfile"); session.startDiscovery(); QTRY_VERIFY_WITH_TIMEOUT(session.devices()->rowCount()>0,5000);
        int row=-1; for(int i=0;i<session.devices()->rowCount();++i) if(session.devices()->at(i)->deviceId==path) row=i;
        QVERIFY(row>=0); session.connectToDevice(row); QVERIFY(session.isConnected());
        QVERIFY(session.acars()->setFrequencyHz(131550000 + qint64(session.sampleRate())));
        QTest::qWait(700); session.acars()->poll(); QCOMPARE(session.acars()->messageCount(),qulonglong(0));
        const auto point=toggle->mapToScene(QPointF(toggle->width()/2,toggle->height()/2));
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,point.toPoint()); QTRY_VERIFY(session.acars()->enabled());
        QTRY_COMPARE_WITH_TIMEOUT(session.acars()->status(),QStringLiteral("Canale ACARS fuori dalla banda disponibile"),3000);
        auto *status=root->findChild<QObject *>("acarsStatus"); QVERIFY(status);
        QTRY_COMPARE(status->property("text").toString(),QStringLiteral("Canale ACARS fuori dalla banda disponibile"));
        QTest::qWait(700); session.acars()->poll(); QCOMPARE(session.acars()->messageCount(),qulonglong(0));
        QVERIFY(session.acars()->setFrequencyHz(131550000));
        QTRY_VERIFY_WITH_TIMEOUT(session.acars()->messageCount()>0,12000);
        const auto index=session.acars()->messages()->index(0,0);
        QCOMPARE(index.data(AcarsHistoryModel::AddressRole).toString(),QStringLiteral(".N824UA"));
        QCOMPARE(index.data(AcarsHistoryModel::TextRole).toString(),QStringLiteral("TEST123"));
        QQuickItem *rendered=nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((rendered=visualFind(window->contentItem(),QStringLiteral("acarsMessageText")))!=nullptr,3000);
        QCOMPARE(rendered->property("text").toString(),QStringLiteral("TEST123"));
        const auto beforeRetune=session.acars()->messageCount();
        QVERIFY(session.acars()->setFrequencyHz(131550000 + qint64(session.sampleRate())));
        QTRY_COMPARE_WITH_TIMEOUT(session.acars()->messages()->rowCount(),0,3000);
        QTRY_COMPARE(status->property("text").toString(),QStringLiteral("Canale ACARS fuori dalla banda disponibile"));
        QTest::qWait(700); session.acars()->poll(); QCOMPARE(session.acars()->messageCount(),beforeRetune);
        QVERIFY(session.acars()->setFrequencyHz(131550000));
        QTRY_VERIFY_WITH_TIMEOUT(session.acars()->messageCount()>beforeRetune,12000);
        QTRY_VERIFY_WITH_TIMEOUT(session.acars()->messages()->rowCount()>0,3000);
        const auto previousCount=session.acars()->messageCount();
        session.disconnectDevice();
        QTRY_VERIFY_WITH_TIMEOUT(!session.isConnected(),5000);
        QTRY_COMPARE_WITH_TIMEOUT(session.acars()->status(),QStringLiteral("Sorgente IQ non disponibile"),3000);
        QTRY_COMPARE_WITH_TIMEOUT(session.acars()->messages()->rowCount(),0,3000);
        session.connectToDevice(row); QVERIFY(session.isConnected());
        QTRY_VERIFY_WITH_TIMEOUT(session.acars()->messageCount()>previousCount,12000);
        QTRY_VERIFY_WITH_TIMEOUT(session.acars()->messages()->rowCount()>0,3000);
        QCOMPARE(session.acars()->messages()->index(0,0).data(AcarsHistoryModel::TextRole).toString(),QStringLiteral("TEST123"));
        QElapsedTimer timer; timer.start(); session.requestShutdown(); QVERIFY(timer.elapsed()<500);
        QTRY_VERIFY_WITH_TIMEOUT(session.shutdownFencePassed(),10000);
    }
};
QTEST_MAIN(TestAcarsSessionLive)
#include "tst_acars_session_live.moc"
