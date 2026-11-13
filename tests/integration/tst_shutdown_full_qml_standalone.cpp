// SPDX-License-Identifier: GPL-3.0-or-later
#define main shutdown_fake_baseline_main
#include "tst_shutdown_fake_radio_standalone.cpp"
#undef main
#include "app/SessionSingleton.h"
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QQuickStyle>
#include <QTest>
int main(int argc, char **argv) {
    QGuiApplication app(argc,argv);
    const auto testRoot=qEnvironmentVariable("DSDR_TEST_BUILD_ROOT");
    const auto settings=qEnvironmentVariable("DSDR_TEST_SETTINGS_DIR");
    const auto data=qEnvironmentVariable("DSDR_TEST_APPDATA_DIR");
    if (!isolatedTestDirectory(settings,testRoot) || !isolatedTestDirectory(data,testRoot)) return 77;
    QCoreApplication::setOrganizationName("DSDR-shutdown-test");
    QCoreApplication::setApplicationName("full-qml-shutdown");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings);
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    auto trace=std::make_shared<Trace>(); trace->refuseClose.store(true);
    const bool rejectPtt = qEnvironmentVariableIntValue("DSDR_TEST_REFUSE_PTT") == 1;
    trace->refusePttOff.store(rejectPtt);
    FakeRadio *fake=nullptr;
    hal::BackendRegistry::instance().registerBackend({"shutdown-fake","Shutdown fake","test"},
        [&fake,trace](QObject *parent) { fake=new FakeRadio(trace,parent); return fake; });
    SessionManager session;
    const bool externalGate = qEnvironmentVariableIntValue("DSDR_TEST_EXTERNAL_GATE") == 1;
    if (externalGate) session.setExternalShutdownPending(true);
    dsdr::app::SessionSingleton::instance=&session;
    session.selectBackend("shutdown-fake"); session.startDiscovery();
    assert(waitFor([&]{return session.devices()->rowCount()>0;}));
    session.connectToDevice(0); assert(session.isConnected() && fake);
    fake->setPtt(true); assert(fake->ptt());
    const bool rigctlWasRunning = session.rigctlRunning();
    QQmlApplicationEngine engine;
    engine.loadFromModule("DecodiumSdr","Main");
    if (engine.rootObjects().isEmpty()) { std::fprintf(stderr,"Main.qml failed to load\n"); return 1; }
    auto *window=qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst()); assert(window);
    QTest::qWait(100);
    window->close();
    if (rejectPtt) {
        QTest::qWait(50);
        assert(!session.shutdownPending() && !session.shutdownFencePassed());
        assert(window->isVisible() && window->contentItem()->isEnabled());
        assert(session.isConnected() && trace->active.load() && fake->ptt());
        assert(session.rigctlRunning() == rigctlWasRunning);
        trace->refusePttOff.store(false);
        window->close();
    }
    assert(waitFor([&]{return session.shutdownBackendRetryAllowed();}));
    assert(session.shutdownPending() && !session.shutdownFencePassed());
    assert(trace->closeRefused.load() && trace->active.load() && !trace->pttState.load());
    QTest::qWait(100);
    auto *prompt=window->findChild<QObject *>("backendShutdownDialog"); assert(prompt);
    auto *retry=prompt->findChild<QQuickItem *>("backendShutdownRetry");
    auto *label=prompt->findChild<QObject *>("backendShutdownStatus");
    assert(retry && label && retry->isEnabled());
    assert(label->property("text").toString().contains(QStringLiteral("aperta")));
    trace->refuseClose.store(false);
    const auto point=retry->mapToScene(QPointF(retry->width()/2,retry->height()/2));
    QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,point.toPoint());
    if (externalGate) {
        assert(waitFor([&]{return session.coreShutdownFencePassed();}));
        assert(!session.shutdownFencePassed() && window->isVisible());
        QTest::qWait(50);
        assert(window->isVisible());
        session.setExternalShutdownPending(false);
    }
    assert(waitFor([&]{return session.shutdownFencePassed();}));
    assert(waitFor([&]{return !window->isVisible();}));
    assert(trace->destroyed.load() && !trace->active.load());
    std::puts(rejectPtt
        ? "full Main QML: PTT refusal restores UI, then close refusal retry joins backend"
        : "full Main QML: close refused, retry clicked in overlay, backend joined and window closed");
    return 0;
}
