// SPDX-License-Identifier: GPL-3.0-or-later
#define main shutdown_fake_baseline_main
#include "tst_shutdown_fake_radio_standalone.cpp"
#undef main
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>
int main(int argc, char **argv) {
    QGuiApplication app(argc,argv);
    const auto rootDir=qEnvironmentVariable("DSDR_TEST_BUILD_ROOT");
    const auto settings=qEnvironmentVariable("DSDR_TEST_SETTINGS_DIR");
    const auto data=qEnvironmentVariable("DSDR_TEST_APPDATA_DIR");
    if (!isolatedTestDirectory(settings,rootDir) || !isolatedTestDirectory(data,rootDir)) return 77;
    QCoreApplication::setOrganizationName("DSDR-shutdown-test");
    QCoreApplication::setApplicationName("shutdown-qml");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings);
    auto trace=std::make_shared<Trace>(); trace->refuseClose.store(true);
    FakeRadio *fake=nullptr;
    hal::BackendRegistry::instance().registerBackend({"shutdown-fake","Shutdown fake","test"},
        [&fake,trace](QObject *parent) { fake=new FakeRadio(trace,parent); return fake; });
    SessionManager session;
    session.selectBackend("shutdown-fake"); session.startDiscovery();
    assert(waitFor([&]{return session.devices()->rowCount()>0;}));
    session.connectToDevice(0); assert(session.isConnected() && fake);
    fake->setPtt(true); assert(fake->ptt());
    QQmlEngine engine; engine.rootContext()->setContextProperty("Session",&session);
    QQmlComponent component(&engine);
    component.setData(R"QML(
import QtQuick
import QtQuick.Controls.Basic
ApplicationWindow {
    visible: true; width: 600; height: 300
    contentItem.enabled: !Session.shutdownPending
    menuBar: MenuBar {
        enabled: !Session.shutdownPending
        property BackendShutdownDialog prompt: BackendShutdownDialog {
            objectName: "prompt"
            visible: Session.shutdownPending && !Session.shutdownFencePassed
            statusText: Session.shutdownBackendMessage || "Arresto della radio in corso."
            retryAllowed: Session.shutdownBackendRetryAllowed
            onRetryRequested: Session.retryBackendShutdown()
        }
    }
}
)QML",QUrl::fromLocalFile(QStringLiteral(DSDR_SHUTDOWN_SOURCE_DIR "/src/app/qml/ShutdownTest.qml")));
    if (component.isError()) { for (const auto &e:component.errors()) std::fprintf(stderr,"%s\n",qPrintable(e.toString())); return 1; }
    QObject *root=component.create();
    if (!root) { for (const auto &e:component.errors()) std::fprintf(stderr,"%s\n",qPrintable(e.toString())); return 1; }
    session.requestShutdown();
    assert(waitFor([&]{return session.shutdownBackendRetryAllowed();}));
    assert(session.shutdownPending() && !session.shutdownFencePassed());
    assert(trace->closeRefused.load() && !trace->pttState.load() && trace->active.load());
    QTest::qWait(100);
    auto *prompt=root->findChild<QObject *>("prompt"); assert(prompt);
    auto *retry=prompt->findChild<QQuickItem *>("backendShutdownRetry");
    auto *label=prompt->findChild<QObject *>("backendShutdownStatus");
    auto *window=qobject_cast<QQuickWindow *>(root);
    assert(retry && label && window && retry->isEnabled());
    assert(label->property("text").toString().contains(QStringLiteral("aperta")));
    trace->refuseClose.store(false);
    const auto point=retry->mapToScene(QPointF(retry->width()/2,retry->height()/2));
    QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,point.toPoint());
    assert(waitFor([&]{return session.shutdownFencePassed();}));
    assert(!trace->active.load() && trace->destroyed.load());
    delete root;
    std::puts("QML/session: refused close reported, accessible retry clicked, backend stopped and joined");
    return 0;
}
