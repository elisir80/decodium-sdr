// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/SessionManager.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <QtEndian>
#include <array>
#include <cstring>
using namespace dsdr::core;
namespace {
void u32(QByteArray &b,quint32 n) { char p[4]; qToLittleEndian(n,p); b.append(p,4); }
void u16(QByteArray &b,quint16 n) { char p[2]; qToLittleEndian(n,p); b.append(p,2); }
bool makeWav(const QString &path, bool valid, bool identification, bool velocity, bool airspeed) {
    std::array<unsigned char,14> frame{0x8d,0x40,0x62,0x1d,0x58,0xc3,0x82,0xd6,0x90,0xc8,0xac,0x28,0x63,0xa7};
    std::array<unsigned char,14> odd{0x8d,0x40,0x62,0x1d,0x58,0xc3,0x86,0x43,0x5c,0xc4,0x12,0x69,0x2a,0xd6};
    if(velocity) frame={0x8d,0x48,0x50,0x20,0x99,0x44,0x09,0x94,0x08,0x38,0x17,0x5b,0x28,0x4f};
    if(airspeed) frame={0x8d,0xab,0xc1,0x23,0x9b,0x05,0x00,0x9f,0x68,0x2c,0x00,0x8e,0x5f,0x42};
    if(!valid) { frame[6]^=4; odd[6]^=4; }
    constexpr unsigned rate=2000000; constexpr int repeats=80, samples=200000;
    QByteArray block(samples*8,0);
    auto pulse=[&](int n) { const float value=.8f; std::memcpy(block.data()+n*8,&value,sizeof(value)); };
    auto addFrame=[&](const auto &msg,int start) {
        for(int i:{0,2,7,9}) pulse(start+i);
        for(int b=0;b<112;++b) pulse(start+16+2*b+((msg[b/8]>>(7-b%8))&1 ? 0 : 1));
    };
    addFrame(frame,0); if(!velocity && !airspeed) addFrame(odd,2500);
    if(identification) {
        std::array<unsigned char,14> ident{0x8d,0x40,0x62,0x1d,0x20,0x2c,0xc3,0x71,0xc3,0x2c,0xe0,0,0,0};
        unsigned crc=0;
        for(int i=0;i<11;++i) for(int bit=7;bit>=0;--bit) {
            const bool top=((crc>>23)^(ident[i]>>bit))&1;
            crc=(crc<<1)&0xffffff; if(top) crc^=0xfff409;
        }
        ident[11]=crc>>16; ident[12]=crc>>8; ident[13]=crc;
        addFrame(ident,5000);
    }
    const quint32 bytes=quint32(block.size()*repeats);
    QByteArray h; h.append("RIFF",4); u32(h,36+bytes); h.append("WAVEfmt ",8); u32(h,16);
    u16(h,3);u16(h,2);u32(h,rate);u32(h,rate*8);u16(h,8);u16(h,32);h.append("data",4);u32(h,bytes);
    QFile f(path); if(!f.open(QIODevice::WriteOnly) || f.write(h)!=h.size()) return false;
    for(int i=0;i<repeats;++i) if(f.write(block)!=block.size()) return false;
    QFile meta(QFileInfo(path).absolutePath()+"/adsb-session.json"); if(!meta.open(QIODevice::WriteOnly)) return false;
    meta.write(QJsonDocument(QJsonObject{{"format","decodium-iq/1"},{"sampleRate",int(rate)},{"centerFrequencyHz",1090000000}}).toJson()); return true;
}
}
class AdsbIqSession:public QObject {
    Q_OBJECT
private slots:
    void sourceToDialog_data() {
        QTest::addColumn<bool>("valid"); QTest::addColumn<bool>("identification"); QTest::addColumn<bool>("velocity"); QTest::addColumn<bool>("airspeed");
        QTest::newRow("valid") << true << false << false << false;
        QTest::newRow("bad-crc") << false << false << false << false;
        QTest::newRow("position-then-ident") << true << true << false << false;
        QTest::newRow("velocity") << true << false << true << false;
        QTest::newRow("velocity-bad-crc") << false << false << true << false;
        QTest::newRow("airspeed") << true << false << false << true;
        QTest::newRow("airspeed-bad-crc") << false << false << false << true;
    }
    void sourceToDialog() {
        QFETCH(bool,valid); QFETCH(bool,identification); QFETCH(bool,velocity); QFETCH(bool,airspeed); QVERIFY(qEnvironmentVariableIntValue("DSDR_TEST_SILENT_AUDIO")==1);
        QTemporaryDir temp(QDir::tempPath()+"/dsdr-adsb-iq-XXXXXX"); QVERIFY(temp.isValid());
        QCoreApplication::setOrganizationName("DSDR-adsb-test"); QCoreApplication::setApplicationName("iq-session");
        QSettings::setDefaultFormat(QSettings::IniFormat); QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,temp.path());
        qputenv("DSDR_TEST_APPDATA_DIR",temp.path().toUtf8());
        const auto path=temp.path()+"/adsb-session.wav"; QVERIFY(makeWav(path,valid,identification,velocity,airspeed)); qputenv("DSDR_IQFILE_PATH",path.toUtf8());
        QVERIFY(QDir().mkpath(temp.path()+"/qml/DecodiumSdr")); QFile module(temp.path()+"/qml/DecodiumSdr/qmldir");
        QVERIFY(module.open(QIODevice::WriteOnly)); module.write("module DecodiumSdr\nsingleton Theme 1.0 Theme.qml\n"); module.close();
        QVERIFY(QFile::copy(QStringLiteral(DSDR_ADSB_QML_DIR "/Theme.qml"),temp.path()+"/qml/DecodiumSdr/Theme.qml"));
        SessionManager session; QQmlApplicationEngine qml; qml.addImportPath(temp.path()+"/qml");
        qml.rootContext()->setContextProperty("adsbController",session.adsb());
        const auto text=QStringLiteral(R"(import QtQuick
import QtQuick.Controls.Basic
import "%1" as App
ApplicationWindow { visible:true; width:800; height:600
    App.AdsbDialog { objectName:"decoderDialog"; controller:adsbController; Component.onCompleted:open() }
})").arg(QUrl::fromLocalFile(QStringLiteral(DSDR_ADSB_QML_DIR)).toString());
        qml.loadData(text.toUtf8(),QUrl::fromLocalFile(temp.path()+"/app.qml")); QVERIFY(!qml.rootObjects().isEmpty());
        auto *label=qml.rootObjects().first()->findChild<QObject *>("adsbIcao"); QVERIFY(label);
        auto *dialog=qml.rootObjects().first()->findChild<QObject *>("decoderDialog"); QVERIFY(dialog);
        QVERIFY(dialog->property("title").toString().contains("sperimentale"));
        session.selectBackend("iqfile"); session.startDiscovery(); QTRY_VERIFY_WITH_TIMEOUT(session.devices()->rowCount()>0,5000);
        int row=-1; for(int i=0;i<session.devices()->rowCount();++i) if(session.devices()->at(i)->deviceId==path) row=i;
        QVERIFY(row>=0); session.connectToDevice(row); QVERIFY(session.isConnected());
        QTest::qWait(500); session.adsb()->poll(); QCOMPARE(session.adsb()->frameCount(),qulonglong(0));
        session.adsb()->setEnabled(true);
        if(airspeed && valid) {
            auto* speed=qml.rootObjects().first()->findChild<QObject*>("adsbAirspeed");
            auto* heading=qml.rootObjects().first()->findChild<QObject*>("adsbHeading");
            QVERIFY(speed && heading);
            QTRY_COMPARE_WITH_TIMEOUT(session.adsb()->icao(),QStringLiteral("ABC123"),10000);
            QTRY_VERIFY(session.adsb()->hasAirspeed() && session.adsb()->hasMagneticHeading());
            QTRY_VERIFY(speed->property("text").toString().contains(QStringLiteral("250.0 kt (TAS)")));
            QVERIFY(heading->property("text").toString().contains(QStringLiteral("90.0°")));
            session.disconnectDevice(); QTRY_VERIFY_WITH_TIMEOUT(!session.isConnected(),5000);
            QTRY_VERIFY_WITH_TIMEOUT(!session.adsb()->hasAirspeed() && !session.adsb()->hasMagneticHeading(),3000);
            QTRY_VERIFY(speed->property("text").toString().contains(QStringLiteral("non disponibile")));
        } else if(velocity && valid) {
            auto* speed=qml.rootObjects().first()->findChild<QObject*>("adsbSpeed");
            auto* track=qml.rootObjects().first()->findChild<QObject*>("adsbTrack");
            auto* vertical=qml.rootObjects().first()->findChild<QObject*>("adsbVerticalRate");
            QVERIFY(speed && track && vertical);
            QTRY_COMPARE_WITH_TIMEOUT(session.adsb()->icao(),QStringLiteral("485020"),10000);
            QTRY_VERIFY(session.adsb()->hasGroundSpeed() && session.adsb()->hasGroundTrack() && session.adsb()->hasVerticalRate());
            QTRY_VERIFY(speed->property("text").toString().contains(QStringLiteral("159.2 kt")));
            QVERIFY(track->property("text").toString().contains(QStringLiteral("182.9°")));
            QVERIFY(vertical->property("text").toString().contains(QStringLiteral("-832 ft/min")));
            session.disconnectDevice(); QTRY_VERIFY_WITH_TIMEOUT(!session.isConnected(),5000);
            QTRY_VERIFY_WITH_TIMEOUT(!session.adsb()->hasGroundSpeed(),3000);
            QVERIFY(!session.adsb()->hasGroundTrack() && !session.adsb()->hasVerticalRate());
            QTRY_VERIFY(speed->property("text").toString().contains(QStringLiteral("non disponibile")));
        } else if(valid) {
            QTRY_COMPARE_WITH_TIMEOUT(session.adsb()->icao(),QStringLiteral("40621D"),10000);
            QTRY_VERIFY(label->property("text").toString().contains(QStringLiteral("40621D")));
            QTRY_VERIFY_WITH_TIMEOUT(session.adsb()->hasPosition(),10000);
            auto *position=qml.rootObjects().first()->findChild<QObject *>("adsbPosition"); QVERIFY(position);
            auto *altitude=qml.rootObjects().first()->findChild<QObject *>("adsbAltitude"); QVERIFY(altitude);
            if(identification) {
                QTRY_COMPARE_WITH_TIMEOUT(session.adsb()->typeCode(),4,3000);
                QVERIFY(session.adsb()->hasPosition());
            } else {
                QTRY_VERIFY_WITH_TIMEOUT(session.adsb()->hasBarometricAltitude(),3000);
                QTRY_VERIFY(altitude->property("text").toString().contains(QStringLiteral("38000 ft")));
            }
            QVERIFY(!position->property("text").toString().contains(QStringLiteral("non disponibile")));
            session.disconnectDevice(); QTRY_VERIFY_WITH_TIMEOUT(!session.isConnected(),5000);
            QTRY_VERIFY_WITH_TIMEOUT(session.adsb()->icao().isEmpty(),3000);
            QVERIFY(!session.adsb()->hasPosition());
            QVERIFY(!session.adsb()->hasBarometricAltitude());
            QTRY_VERIFY(altitude->property("text").toString().contains(QStringLiteral("sconosciuta")));
            QTRY_VERIFY(label->property("text").toString().contains(QStringLiteral("sconosciuto")));
            QTRY_VERIFY(position->property("text").toString().contains(QStringLiteral("non disponibile")));
            session.connectToDevice(row); QVERIFY(session.isConnected());
            QTRY_COMPARE_WITH_TIMEOUT(session.adsb()->icao(),QStringLiteral("40621D"),10000);
            QTRY_VERIFY_WITH_TIMEOUT(session.adsb()->hasPosition(),10000);
            if(!identification) QTRY_VERIFY_WITH_TIMEOUT(session.adsb()->hasBarometricAltitude(),3000);
        } else {
            QTest::qWait(3000); session.adsb()->poll(); QCOMPARE(session.adsb()->frameCount(),qulonglong(0));
        }
        session.requestShutdown(); QTRY_VERIFY_WITH_TIMEOUT(session.shutdownFencePassed(),10000); qunsetenv("DSDR_IQFILE_PATH");
    }
};
QTEST_MAIN(AdsbIqSession)
#include "tst_adsb_iq_session.moc"
