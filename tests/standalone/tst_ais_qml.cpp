// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/AisReceiver.h"
#include "core/AisController.h"
#include "ais_synthetic.h"
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QDir>
#include <QFile>
#include <QElapsedTimer>
#include <QThread>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdio>
static void writeFile(const QString &path,const QByteArray &text) {
    QFile f(path); assert(f.open(QIODevice::WriteOnly|QIODevice::Truncate)); assert(f.write(text)==text.size());
}
template<class F> void waitFor(F f) {
    QElapsedTimer timer; timer.start();
    while(!f()) { assert(timer.elapsed()<5000); QCoreApplication::processEvents(); QThread::msleep(1); }
    QCoreApplication::processEvents();
}
int main(int argc,char **argv) {
    QGuiApplication app(argc,argv);
    assert(argc==3);
    const QString fixtures=QString::fromLocal8Bit(argv[1]);
    const QString appQml=QString::fromLocal8Bit(argv[2]);
    assert(QDir().mkpath(fixtures+"/DecodiumSdr"));
    writeFile(fixtures+"/DecodiumSdr/qmldir","module DecodiumSdr\nsingleton Theme 1.0 Theme.qml\n");
    writeFile(fixtures+"/DecodiumSdr/Theme.qml",R"(pragma Singleton
import QtQuick
QtObject { readonly property color surfaceRaised: "#222"; readonly property color border: "#888"; readonly property color textPrimary: "white"; readonly property color textSecondary: "silver"; readonly property int radiusSmall: 4 }
)");
    dsdr::core::AisReceiver rx;
    dsdr::core::AisController controller(&rx);
    QQmlApplicationEngine engine; engine.addImportPath(fixtures);
    engine.rootContext()->setContextProperty("aisController",&controller);
    const QString source=QStringLiteral(R"(import QtQuick
import QtQuick.Controls.Basic
import "%1" as App
ApplicationWindow { width: 800; height: 600; visible: true
    App.AisDialog { objectName: "dialog"; controller: aisController; Component.onCompleted: open() }
})").arg(QUrl::fromLocalFile(appQml).toString());
    engine.loadData(source.toUtf8(),QUrl::fromLocalFile(fixtures+"/harness.qml"));
    assert(!engine.rootObjects().isEmpty());
    auto *root=engine.rootObjects().first();
    auto *dialog=root->findChild<QObject *>("dialog"); assert(dialog);
    waitFor([&]{return dialog->property("opened").toBool();});
    auto *type=root->findChild<QObject *>("aisType");
    auto *mmsi=root->findChild<QObject *>("aisMmsi");
    auto *position=root->findChild<QObject *>("aisPosition");
    auto *enabled=root->findChild<QObject *>("aisEnabled");
    auto *status=root->findChild<QObject *>("aisStatus");
    assert(type && mmsi && position && enabled && status && !enabled->property("checked").toBool());
    rx.start(); rx.setSource(true,48000,0); controller.setEnabled(true);
    auto iq=ais_test::transmit(ais_test::position(),48000,0,0,2,false);
    assert(rx.submit(iq.data(),iq.size())==dsdr::core::AsyncIqDecoder::SubmitResult::Accepted);
    waitFor([&]{controller.poll();return controller.frameCount()==1;});
    assert(type->property("text").toString().contains("1"));
    assert(mmsi->property("text").toString().contains("123456789"));
    assert(position->property("text").toString().contains("35.90000"));
    assert(position->property("text").toString().contains("ultima trama"));
    // A static report for the same vessel has no position: the visible
    // coordinates must be cleared until a new position frame arrives.
    std::vector<std::uint8_t> staticData(53,0);
    ais_test::setBits(staticData,0,6,5);
    ais_test::setBits(staticData,8,30,123456789);
    auto staticIq=ais_test::transmit(staticData,48000,0,0,2,false);
    assert(rx.submit(staticIq.data(),staticIq.size())==dsdr::core::AsyncIqDecoder::SubmitResult::Accepted);
    waitFor([&]{controller.poll();return controller.frameCount()==2;});
    assert(controller.messageType()==5 && controller.mmsi()==QStringLiteral("123456789"));
    assert(position->property("text").toString().contains("non disponibile"));
    assert(rx.submit(iq.data(),iq.size())==dsdr::core::AsyncIqDecoder::SubmitResult::Accepted);
    waitFor([&]{controller.poll();return controller.frameCount()==3;});
    assert(position->property("text").toString().contains("35.90000"));
    rx.setSource(true,48000,0,false); controller.poll(); QCoreApplication::processEvents();
    assert(status->property("text").toString().contains("fuori dalla banda"));
    assert(rx.submit(iq.data(),iq.size())==dsdr::core::AsyncIqDecoder::SubmitResult::Stopped);
    assert(position->property("text").toString().contains("non disponibile"));
    rx.setSource(true,32000,0); controller.poll(); QCoreApplication::processEvents();
    assert(status->property("text").toString().contains("campionamento non supportata"));
    assert(rx.submit(iq.data(),iq.size())==dsdr::core::AsyncIqDecoder::SubmitResult::Stopped);
    rx.setSource(true,48000,0); controller.poll(); QCoreApplication::processEvents();
    assert(status->property("text").toString().contains("attiva"));
    assert(!controller.hasPosition() && controller.mmsi().isEmpty() && controller.processedUtc().isEmpty());
    assert(position->property("text").toString().contains("non disponibile"));
    assert(rx.submit(iq.data(),iq.size())==dsdr::core::AsyncIqDecoder::SubmitResult::Accepted);
    waitFor([&]{controller.poll();return controller.hasPosition();});
    assert(controller.mmsi()==QStringLiteral("123456789") && !controller.processedUtc().isEmpty());
    rx.setSource(false,48000,0); controller.poll(); QCoreApplication::processEvents();
    assert(position->property("text").toString().contains("non disponibile"));
    assert(qobject_cast<QQuickWindow *>(root)->isVisible());
    rx.stop();
    std::puts("AIS dialog offscreen with real controller: OK");
}
