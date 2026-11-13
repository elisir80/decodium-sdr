// SPDX-License-Identifier: GPL-3.0-or-later
#define main fake_shutdown_reference_main
#include "tst_shutdown_fake_radio_standalone.cpp"
#undef main
#include "core/Vdl2AppManager.h"

class NoClientIqRadio final : public hal::demo::DemoBackend {
public:
    explicit NoClientIqRadio(QObject *parent) : DemoBackend(parent) {}
    hal::BackendCapabilities capabilities() const override {
        auto caps = DemoBackend::capabilities();
        caps.demod = DspLocation::Device;
        return caps;
    }
};

int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    const auto root = qEnvironmentVariable("DSDR_TEST_BUILD_ROOT");
    const auto settings = qEnvironmentVariable("DSDR_TEST_SETTINGS_DIR");
    const auto data = qEnvironmentVariable("DSDR_TEST_APPDATA_DIR");
    if (!isolatedTestDirectory(settings, root) || !isolatedTestDirectory(data, root)) return 77;
    QCoreApplication::setOrganizationName("DSDR-shutdown-test");
    QCoreApplication::setApplicationName("vdl2-app-manager");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings);
    auto trace = std::make_shared<Trace>(); FakeRadio *fake = nullptr;
    hal::BackendRegistry::instance().registerBackend({"vdl2-app-fake", "VDL2 fake", "test"},
        [&fake, trace](QObject *parent) { fake = new FakeRadio(trace, parent); return fake; });
    SessionManager session;
    Vdl2AppManager manager(&session);
    assert(!manager.busy());
    assert(!manager.startForFrequency(136975000));
    assert(!manager.busy() && !manager.controller() && !session.externalShutdownPending());
    assert(manager.state().contains(QStringLiteral("sorgente IQ")));
    if (qEnvironmentVariableIntValue("DSDR_TEST_NO_CLIENT_IQ")) {
        hal::BackendRegistry::instance().registerBackend({"vdl2-no-iq", "VDL2 no IQ", "test"},
            [](QObject *parent) { return new NoClientIqRadio(parent); });
        session.selectBackend("vdl2-no-iq"); session.startDiscovery();
        assert(waitFor([&] { return session.devices()->rowCount() > 0; }));
        session.connectToDevice(0); assert(session.isConnected());
        assert(!session.capabilities()->clientDemod());
        assert(!manager.startForFrequency(session.centerFrequency()));
        assert(!manager.busy() && !manager.controller() && !session.externalShutdownPending());
        assert(manager.state().contains(QStringLiteral("sorgente IQ")));
        session.disconnectDevice(); assert(waitFor([&] { return !session.isConnected(); }));
    }
    session.selectBackend("vdl2-app-fake"); session.startDiscovery();
    assert(waitFor([&] { return session.devices()->rowCount() > 0; }));
    session.connectToDevice(0); assert(session.isConnected() && fake);
    if (qEnvironmentVariableIntValue("DSDR_TEST_NO_CLIENT_IQ")) {
        assert(session.capabilities()->clientDemod());
        assert(manager.startForFrequency(session.centerFrequency()));
        assert(waitFor([&] { return manager.controller() && manager.state() == QStringLiteral("Attivo"); },10000));
        session.requestShutdown();
        assert(waitFor([&] { return session.shutdownFencePassed(); },10000));
        return 0;
    }
    if (qEnvironmentVariableIntValue("DSDR_TEST_CHANGE_SOURCE")) {
        const double originalRate=session.sampleRate();
        emit fake->sampleRateChanged(originalRate+0.5);
        assert(session.sampleRate()==originalRate+0.5);
        assert(!manager.startForFrequency(session.centerFrequency()));
        assert(manager.state().contains(QStringLiteral("campionamento IQ non supportata")));
        assert(!manager.busy() && !session.externalShutdownPending());
        emit fake->sampleRateChanged(originalRate);
        assert(session.sampleRate()==originalRate);
        assert(!manager.startForFrequency(session.centerFrequency() + qint64(session.sampleRate())));
        assert(!session.externalShutdownPending() && !manager.busy());
        assert(manager.state().contains(QStringLiteral("fuori dalla banda IQ")));
        assert(!manager.startForFrequency(0));
        assert(manager.state().contains(QStringLiteral("fuori dalla banda IQ")));
        assert(manager.startForFrequency(session.centerFrequency()));
        session.setCenterFrequency(session.centerFrequency() + 1000);
        assert(waitFor([&] { return !session.externalShutdownPending(); }));
        assert(!manager.controller() && manager.state().contains("cambiata"));
        session.requestShutdown();
        assert(waitFor([&] { return session.shutdownFencePassed(); }, 10000));
        return 0;
    }
    assert(manager.start(1000, 136975000, 0));
    assert(manager.busy());
    assert(waitFor([&] { return !session.externalShutdownPending(); }));
    assert(!manager.controller() && !manager.busy() && manager.state().contains("Impossibile"));
    assert(manager.start(105000, 136975000, 0));
    assert(!manager.start(105000, 136975000, 0));
    if (qEnvironmentVariableIntValue("DSDR_TEST_STOP_RESTART")) {
        assert(waitFor([&] { return manager.controller() && manager.state() == QStringLiteral("Attivo"); }));
        int beats=0; QTimer heartbeat; heartbeat.setInterval(5);
        QObject::connect(&heartbeat, &QTimer::timeout, &app, [&] { ++beats; }); heartbeat.start();
        manager.stop();
        assert(waitFor([&] { return !manager.controller() && !session.externalShutdownPending(); },10000));
        assert(session.isConnected() && !session.shutdownPending());
        assert(manager.start(105000, 136975000, 0));
        assert(waitFor([&] { return manager.controller() && manager.state() == QStringLiteral("Attivo"); }));
        assert(beats>0);
        session.requestShutdown();
        assert(waitFor([&] { return session.shutdownFencePassed(); },10000));
    } else if (qEnvironmentVariableIntValue("DSDR_TEST_STOP_PREPARATION")) {
        manager.stop();
        assert(waitFor([&] { return !session.externalShutdownPending(); },10000));
        assert(!manager.controller() && manager.state() == QStringLiteral("Arrestato") && session.isConnected());
        session.requestShutdown();
        assert(waitFor([&] { return session.shutdownFencePassed(); },10000));
    } else if (qEnvironmentVariableIntValue("DSDR_TEST_CANCEL_PREPARATION")) {
        session.requestShutdown();
        assert(!session.shutdownFencePassed());
        assert(waitFor([&] { return session.shutdownFencePassed(); }, 10000));
        assert(!manager.controller() && manager.state() == QStringLiteral("Arrestato"));
    } else {
        assert(waitFor([&] { return manager.controller() && manager.state() == QStringLiteral("Attivo"); }));
        session.requestShutdown();
        assert(waitFor([&] { return session.shutdownFencePassed(); }, 10000));
        assert(manager.state() == QStringLiteral("Arrestato"));
    }
    return 0;
}
