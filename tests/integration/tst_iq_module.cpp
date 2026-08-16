// SPDX-License-Identifier: GPL-3.0-or-later
// Verifica il caricamento del modulo IQ C ABI e il callback sul baseband.

#include "core/SessionManager.h"

#include <QElapsedTimer>
#include <QLibrary>
#include <QCoreApplication>
#include <QSettings>
#include <QStandardPaths>
#include <QTest>

#include <functional>
#include <cstdint>

using namespace dsdr;
using namespace dsdr::core;

namespace {
bool waitFor(const std::function<bool()> &predicate, int timeoutMs = 5000)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        if (predicate())
            return true;
        QTest::qWait(20);
    }
    return predicate();
}
} // namespace

class TestIqModule : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanup();
    void loadsAndReceivesBaseband();
    void enablesAndDisablesIndividualModule();
};

void TestIqModule::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("DecodiumSdrTests"));
    QCoreApplication::setApplicationName(QStringLiteral("iq_module"));
    QSettings settings;
    settings.remove(QStringLiteral("iqModules"));
    settings.sync();
}

void TestIqModule::cleanup()
{
    QSettings settings;
    settings.remove(QStringLiteral("iqModules"));
    settings.sync();
}

void TestIqModule::loadsAndReceivesBaseband()
{
    const QString path = qEnvironmentVariable("DSDR_TEST_IQ_MODULE");
    QVERIFY2(!path.isEmpty(), "percorso del modulo IQ di test assente");

    QLibrary probe(path);
    QVERIFY2(probe.load(), qPrintable(probe.errorString()));
    using Counter = std::uint64_t (*)();
    const auto calls = reinterpret_cast<Counter>(probe.resolve("dsdr_test_iq_module_calls"));
    const auto frames = reinterpret_cast<Counter>(probe.resolve("dsdr_test_iq_module_frames"));
    QVERIFY(calls);
    QVERIFY(frames);

    SessionManager session;
    QVERIFY(session.loadIqModule(path));

    session.selectBackend(QStringLiteral("demo"));
    session.startDiscovery();
    QVERIFY2(waitFor([&] { return session.devices()->rowCount() > 0; }, 3000),
             "backend demo non trovato");
    session.connectToDevice(0);

    QVERIFY2(waitFor([&] { return session.isConnected(); }, 3000),
             "connessione demo fallita");
    QVERIFY2(waitFor([&] { return calls() > 0 && frames() > 0; }, 3000),
             "il modulo IQ non ha ricevuto il baseband del canale");
}

void TestIqModule::enablesAndDisablesIndividualModule()
{
    const QString path = qEnvironmentVariable("DSDR_TEST_IQ_MODULE");
    QVERIFY(!path.isEmpty());

    SessionManager session;
    QVERIFY(session.loadIqModule(path));
    QCOMPARE(session.iqModuleCatalog().size(), 1);
    QCOMPARE(session.iqModuleCatalog().first().toMap().value(QStringLiteral("state")).toString(),
             QStringLiteral("active"));

    QVERIFY(session.setIqModuleEnabled(path, false));
    QCOMPARE(session.iqModuleCatalog().first().toMap().value(QStringLiteral("enabled")).toBool(),
             false);
    QCOMPARE(session.iqModuleCatalog().first().toMap().value(QStringLiteral("loaded")).toBool(),
             false);
    QCOMPARE(session.iqModuleCatalog().first().toMap().value(QStringLiteral("state")).toString(),
             QStringLiteral("disabled"));
    QVERIFY(session.iqModuleNames().isEmpty());
}

QTEST_MAIN(TestIqModule)
#include "tst_iq_module.moc"
