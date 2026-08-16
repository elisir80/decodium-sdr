// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/SessionManager.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QSettings>
#include <QStandardPaths>
#include <QTest>

#include <functional>

using namespace dsdr::core;

namespace {
bool waitFor(const std::function<bool()> &predicate, int timeoutMs = 5000)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        if (predicate())
            return true;
        QTest::qWait(25);
    }
    return predicate();
}

QVariantMap jobById(const QVariantList &jobs, const QString &id)
{
    for (const QVariant &value : jobs) {
        const QVariantMap job = value.toMap();
        if (job.value(QStringLiteral("id")).toString() == id)
            return job;
    }
    return {};
}
} // namespace

class TestSessionScheduler : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanup();
    void scheduledTuneReachesTheSession();
};

void TestSessionScheduler::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("DecodiumSdrTests"));
    QCoreApplication::setApplicationName(QStringLiteral("session_scheduler"));
}

void TestSessionScheduler::cleanup()
{
    QSettings settings;
    settings.remove(QStringLiteral("scheduler"));
    settings.sync();
}

void TestSessionScheduler::scheduledTuneReachesTheSession()
{
    SessionManager session;
    session.selectBackend(QStringLiteral("demo"));
    session.startDiscovery();
    QVERIFY2(waitFor([&] { return session.devices()->rowCount() > 0; }, 3000),
             "backend demo non trovato");
    session.connectToDevice(0);
    QVERIFY2(waitFor([&] { return session.isConnected(); }, 3000),
             "connessione demo fallita");

    constexpr qint64 targetFrequency = 7'220'000;
    const QString id = session.scheduleAction(
        QStringLiteral("tune"),
        QDateTime::currentDateTimeUtc().addMSecs(700).toString(Qt::ISODateWithMs),
        {{QStringLiteral("frequencyHz"), targetFrequency}});
    QVERIFY(!id.isEmpty());

    QVERIFY2(waitFor([&] {
                 return jobById(session.scheduledJobs(), id)
                            .value(QStringLiteral("status")).toString()
                        == QStringLiteral("completed");
             }, 4000),
             "la sintonia pianificata non ha raggiunto la sessione");
    QCOMPARE(session.channels()->at(session.channels()->currentIndex())->frequencyHz,
             targetFrequency);
}

QTEST_MAIN(TestSessionScheduler)
#include "tst_session_scheduler.moc"
