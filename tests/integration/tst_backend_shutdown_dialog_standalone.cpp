// SPDX-License-Identifier: GPL-3.0-or-later
#include <QGuiApplication>
#include <QQuickWindow>
#include <QQuickItem>
#include <QTest>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QCoreApplication>
#include <QObject>
#include <QUrl>
#include <cassert>
#include <cstdio>
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    QQmlEngine engine;
    QQmlComponent component(&engine);
    const QByteArray qml = R"QML(
import QtQuick
import QtQuick.Controls.Basic
ApplicationWindow {
    id: parentWindow
    visible: true
    width: 400; height: 200
    contentItem.enabled: false
    property int retries: 0
    menuBar: MenuBar {
        enabled: false
        property BackendShutdownDialog shutdownPrompt: BackendShutdownDialog {
            objectName: "prompt"
            visible: true
            statusText: "PTT ancora attivo"
            retryAllowed: true
            cancelAllowed: false
            onRetryRequested: parentWindow.retries++
        }
    }
}
)QML";
    component.setData(qml, QUrl::fromLocalFile(QStringLiteral("/Volumes/Projects/decodium-sdr/src/app/qml/ShutdownTest.qml")));
    if (component.isError()) { for (const auto &e : component.errors()) std::fprintf(stderr,"%s\n",qPrintable(e.toString())); return 1; }
    QObject *root = component.create();
    if (!root) { for (const auto &e : component.errors()) std::fprintf(stderr,"%s\n",qPrintable(e.toString())); return 1; }
    QTest::qWait(100);
    auto *prompt = root->findChild<QObject *>(QStringLiteral("prompt"));
    assert(prompt);
    auto *retry = prompt->findChild<QObject *>(QStringLiteral("backendShutdownRetry"));
    auto *cancel = prompt->findChild<QObject *>(QStringLiteral("backendShutdownCancel"));
    auto *status = prompt->findChild<QObject *>(QStringLiteral("backendShutdownStatus"));
    assert(retry && cancel && status);
    assert(retry->property("enabled").toBool());
    assert(!cancel->property("enabled").toBool());
    assert(status->property("text").toString() == QStringLiteral("PTT ancora attivo"));
    auto *window = qobject_cast<QQuickWindow *>(root);
    auto *button = qobject_cast<QQuickItem *>(retry);
    assert(window && button);
    const auto point = button->mapToScene(QPointF(button->width()/2, button->height()/2));
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, point.toPoint());
    QCoreApplication::processEvents();
    assert(root->property("retries").toInt() == 1);
    prompt->setProperty("retryAllowed", false);
    QCoreApplication::processEvents();
    assert(!retry->property("enabled").toBool());
    delete root;
    std::puts("shutdown dialog: status visible, retry reachable while content disabled, cancel safely disabled");
}
