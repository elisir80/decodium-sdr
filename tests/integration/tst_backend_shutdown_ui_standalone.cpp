// SPDX-License-Identifier: GPL-3.0-or-later
#define main backend_shutdown_core_test_main
#include "tst_backend_shutdown_standalone.cpp"
#undef main
#include "core/BackendShutdownViewModel.h"
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    auto trace = std::make_shared<Trace>(); trace->gui = QThread::currentThread();
    QObject owner;
    auto *radio = new FakeRadio(trace, &owner);
    BackendShutdownViewModel controller;
    assert(controller.beginForAuditedBackend(radio) == BackendShutdown::BeginResult::Accepted);
    waitFor([&]{ return controller.retryAllowed(); });
    assert(controller.statusText().contains(QStringLiteral("PTT")) && !controller.cancelAllowed());
    QQmlEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("shutdownController"), &controller);
    QQmlComponent component(&engine);
    component.setData(R"QML(
import QtQuick
import QtQuick.Controls.Basic
ApplicationWindow {
    visible: true; width: 400; height: 200
    contentItem.enabled: false
    BackendShutdownDialog { objectName: "prompt"; visible: true; controller: shutdownController }
}
)QML", QUrl::fromLocalFile(QStringLiteral("/Volumes/Projects/decodium-sdr/src/app/qml/ShutdownTest.qml")));
    if (component.isError()) { for (const auto &e : component.errors()) std::fprintf(stderr,"%s\n",qPrintable(e.toString())); return 1; }
    QObject *root=component.create();
    if (!root) { for (const auto &e : component.errors()) std::fprintf(stderr,"%s\n",qPrintable(e.toString())); return 1; }
    QTest::qWait(100);
    auto *prompt=root->findChild<QObject *>(QStringLiteral("prompt")); assert(prompt);
    auto *retry=prompt->findChild<QQuickItem *>(QStringLiteral("backendShutdownRetry"));
    auto *cancel=prompt->findChild<QQuickItem *>(QStringLiteral("backendShutdownCancel"));
    auto *status=prompt->findChild<QObject *>(QStringLiteral("backendShutdownStatus"));
    auto *window=qobject_cast<QQuickWindow *>(root);
    assert(retry && cancel && status && window && retry->isEnabled() && !cancel->isEnabled());
    assert(status->property("text").toString().contains(QStringLiteral("PTT")));
    trace->refusePtt.store(false); trace->holdClose.store(true);
    const auto pos=retry->mapToScene(QPointF(retry->width()/2,retry->height()/2));
    QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,pos.toPoint());
    waitFor([&]{return trace->closeEntered.load();});
    waitFor([&]{return !controller.retryAllowed();});
    assert(!controller.ready() && trace->open.load() && !trace->destroyed.load());
    trace->holdClose.store(false);
    waitFor([&]{return controller.ready();});
    assert(trace->destroyed.load() && !trace->wrongThread.load());
    assert(controller.statusText()==QStringLiteral("Radio chiusa."));
    delete root;
    std::puts("shutdown UI: PTT error visible, retry clicked with content disabled, backend closed and joined");
}
