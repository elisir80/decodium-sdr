// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/AprsController.h"
#include "core/AprsReceiver.h"
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QTimer>
#include <QVariantMap>
#include <cassert>
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    dsdr::core::AprsReceiver rx;
    dsdr::core::AprsController controller(&rx);
    controller.setChannels({QVariantMap{{"id",7},{"label","Test NFM"}}});
    QQmlApplicationEngine engine;
    engine.addImportPath(QStringLiteral("/Volumes/Projects/decodium-sdr-build-aprs/qml"));
    engine.rootContext()->setContextProperty("aprsController", &controller);
    engine.loadData(R"(import QtQuick
import QtQuick.Controls.Basic
import "file:///Volumes/Projects/decodium-sdr/src/app/qml" as App
ApplicationWindow {
    width: 800; height: 600; visible: true
    App.AprsDialog { id: dialog; objectName: "dialog"; controller: aprsController; Component.onCompleted: open() }
})", QUrl("file:///Volumes/Projects/decodium-sdr-build-aprs/harness.qml"));
    assert(!engine.rootObjects().isEmpty());
    auto *root=engine.rootObjects().first();
    QTimer::singleShot(500, [&] {
        auto *dialog=root->findChild<QObject *>("dialog"); assert(dialog && dialog->property("opened").toBool());
        auto *selector=root->findChild<QObject *>("aprsChannel"); assert(selector);
        assert(selector->property("currentIndex").toInt()==-1);
        controller.selectChannel(7);
        rx.setChannelState(7,true,true,48000); controller.setEnabled(true);
        QCoreApplication::processEvents();
        assert(selector->property("currentIndex").toInt()==0);
        assert(controller.status().contains("attiva"));
        auto *window=qobject_cast<QQuickWindow *>(root); assert(window && window->isVisible());
        app.quit();
    });
    int result=app.exec();
    puts("APRS QML dialog offscreen: OK");
    return result;
}
