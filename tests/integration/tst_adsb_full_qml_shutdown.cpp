// SPDX-License-Identifier: GPL-3.0-or-later
#define main fake_shutdown_reference_main
#include "tst_shutdown_fake_radio_standalone.cpp"
#undef main
#include "app/SessionSingleton.h"
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QQuickStyle>
#include <QTest>
#include <QtQml/qqmlextensionplugin.h>
Q_IMPORT_QML_PLUGIN(DecodiumSdrPlugin)
int main(int argc,char **argv) {
    QGuiApplication app(argc,argv);
    const auto root=qEnvironmentVariable("DSDR_TEST_BUILD_ROOT");
    const auto settings=qEnvironmentVariable("DSDR_TEST_SETTINGS_DIR");
    const auto data=qEnvironmentVariable("DSDR_TEST_APPDATA_DIR");
    if(!isolatedTestDirectory(settings,root) || !isolatedTestDirectory(data,root)) return 77;
    QCoreApplication::setOrganizationName("DSDR-shutdown-test");
    QCoreApplication::setApplicationName("adsb-full-qml");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings);
    QQuickStyle::setStyle("Basic");
    AdsbGate gate;
    const bool heldAprs=qEnvironmentVariableIntValue("DSDR_TEST_APRS_HELD")==1;
    std::unique_ptr<AdsbReceiver> adsb;
    std::unique_ptr<AprsReceiver> aprs;
    AprsReceiver *aprsRaw=nullptr;
    if (heldAprs) {
        aprs=std::make_unique<AprsReceiver>(64,4096,128,AdsbGate::hold,&gate);
        aprsRaw=aprs.get();
    } else {
        adsb=std::make_unique<AdsbReceiver>(AdsbGate::hold,&gate);
        adsb->setSource(true,false,2000000,1090000000); adsb->setEnabled(true);
        const dsp::Complex sample{}; adsb->process(&sample,1);
        assert(waitFor([&] { return gate.entered.load(); }));
    }
    auto trace=std::make_shared<Trace>(); FakeRadio *fake=nullptr;
    hal::BackendRegistry::instance().registerBackend({"adsb-qml-fake","ADS-B fake","test"},
        [&fake,trace](QObject *parent) { fake=new FakeRadio(trace,parent); return fake; });
    SessionManager session(nullptr, {}, {}, {}, {}, {}, {}, {}, std::move(adsb), std::move(aprs));
    if (heldAprs) {
        aprsRaw->selectChannel(1); aprsRaw->setChannelState(1,true,true,48000); aprsRaw->setEnabled(true);
        const dsp::Complex sample{};
        assert(aprsRaw->submit(1,&sample,1)==AsyncIqDecoder::SubmitResult::Accepted);
        assert(waitFor([&] { return gate.entered.load(); }));
    }
    dsdr::app::SessionSingleton::instance=&session;
    session.selectBackend("adsb-qml-fake"); session.startDiscovery();
    assert(waitFor([&] { return session.devices()->rowCount()>0; }));
    session.connectToDevice(0); assert(session.isConnected() && fake);
    fake->setPtt(true); assert(fake->ptt());
    const bool rejectPtt=qEnvironmentVariableIntValue("DSDR_TEST_REFUSE_PTT")==1;
    trace->refusePttOff=rejectPtt;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("vdl2Manager",static_cast<QObject *>(nullptr));
    engine.loadFromModule("DecodiumSdr","Main"); assert(!engine.rootObjects().isEmpty());
    auto *window=qobject_cast<QQuickWindow *>(engine.rootObjects().first()); assert(window);
    auto *menu=window->findChild<QObject *>("mainMenu"); assert(menu);
    // The default build supplies a null manager; optional VDL2 must not be
    // advertised when it is unavailable, and the rest of Main must still load.
    assert(!menu->property("vdl2Available").toBool());
    int ticks=0; QTimer heartbeat; heartbeat.setInterval(10);
    QObject::connect(&heartbeat,&QTimer::timeout,&app,[&] { ++ticks; }); heartbeat.start();
    if(qEnvironmentVariableIntValue("DSDR_TEST_QUIT_MENU")) {
        assert(QMetaObject::invokeMethod(menu,"quitRequested"));
    } else window->close();
    if(rejectPtt) {
        QTest::qWait(100);
        assert(!session.shutdownPending() && window->isVisible() && fake->ptt() && trace->active.load());
        assert(!gate.release.load());
        trace->refusePttOff=false; window->close();
    }
    assert(waitFor([&] { return trace->destroyed.load() && !trace->active.load(); },10000));
    assert(session.shutdownPending() && !session.shutdownFencePassed() && window->isVisible());
    const int before=ticks;
    assert(waitFor([&] { return ticks-before>=5; }, 3000));
    assert(!gate.release.load() && !session.shutdownFencePassed() && window->isVisible());
    gate.release=true;
    assert(waitFor([&] { return session.shutdownFencePassed(); },10000));
    assert(waitFor([&] { return !window->isVisible(); },10000));
    return 0;
}
