// SPDX-License-Identifier: GPL-3.0-or-later
#define main fake_shutdown_reference_main
#include "tst_shutdown_fake_radio_standalone.cpp"
#undef main
#include "core/Vdl2SessionBinding.h"
#include "core/Vdl2ResourcePreparation.h"
#include <stdexcept>

int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    const auto root = qEnvironmentVariable("DSDR_TEST_BUILD_ROOT");
    const auto settings = qEnvironmentVariable("DSDR_TEST_SETTINGS_DIR");
    const auto data = qEnvironmentVariable("DSDR_TEST_APPDATA_DIR");
    if (!isolatedTestDirectory(settings, root) || !isolatedTestDirectory(data, root)) return 77;
    QCoreApplication::setOrganizationName("DSDR-shutdown-test");
    QCoreApplication::setApplicationName("vdl2-binding");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings);
    auto trace = std::make_shared<Trace>();
    const bool refuseClose = qEnvironmentVariableIntValue("DSDR_TEST_REFUSE_BACKEND") == 1;
    trace->refuseClose = refuseClose;
    FakeRadio *fake = nullptr;
    hal::BackendRegistry::instance().registerBackend({"vdl2-fake", "VDL2 fake", "test"},
        [&fake, trace](QObject *parent) { fake = new FakeRadio(trace, parent); return fake; });
    SessionManager session;
    session.selectBackend("vdl2-fake");
    session.startDiscovery();
    assert(waitFor([&] { return session.devices()->rowCount() > 0; }));
    session.connectToDevice(0);
    assert(session.isConnected() && fake);
    Vdl2ResourcePreparation prep(105000, 136975000, 0, 0.001);
    prep.start();
    assert(waitFor([&] { return prep.isFinished(); }));
    assert(prep.error().isEmpty());
    Vdl2SessionBinding binding(&session, prep.takeResources());
    bool duplicateRejected = false;
    try { Vdl2SessionBinding duplicate(&session, 105000, 136975000, 0, 0.001); }
    catch (const std::invalid_argument &) { duplicateRejected = true; }
    assert(duplicateRejected);
    binding.activate();
    assert(waitFor([&] { return binding.state() == QStringLiteral("Attivo"); }));
    fake->setPtt(true);
    trace->refusePttOff = true;
    session.requestShutdown();
    assert(!session.shutdownPending() && binding.state() == QStringLiteral("Attivo"));
    trace->refusePttOff = false;
    session.requestShutdown();
    assert(session.shutdownPending() && !session.shutdownFencePassed());
    if (refuseClose) {
        assert(waitFor([&] { return session.shutdownBackendRetryAllowed(); }));
        assert(trace->active && !trace->pttState && !session.shutdownFencePassed());
        int heartbeats = 0;
        QTimer heartbeat;
        heartbeat.setInterval(5);
        QObject::connect(&heartbeat, &QTimer::timeout, &app, [&] { ++heartbeats; });
        heartbeat.start();
        assert(waitFor([&] { return heartbeats >= 3; }));
        assert(!session.shutdownFencePassed());
        trace->refuseClose = false;
        session.retryBackendShutdown();
    }
    assert(waitFor([&] { return session.shutdownFencePassed(); }, 10000));
    assert(binding.state() == QStringLiteral("Arrestato"));
    assert(trace->destroyed && !trace->active);
    return 0;
}
