// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/OperationScheduler.h"

#include <QCoreApplication>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>

using namespace dsdr::core;

namespace {
void clearSchedulerSettings()
{
    QSettings settings;
    settings.remove(QStringLiteral("scheduler"));
    settings.sync();
}
} // namespace

class TestOperationScheduler : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanup();
    void runsSafeActionOnce();
    void rejectsUnsafeAndExpiredActions();
    void persistsPendingJobs();
};

void TestOperationScheduler::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("DecodiumSdrTests"));
    QCoreApplication::setApplicationName(QStringLiteral("operation_scheduler"));
    clearSchedulerSettings();
}

void TestOperationScheduler::cleanup()
{
    clearSchedulerSettings();
}

void TestOperationScheduler::runsSafeActionOnce()
{
    OperationScheduler scheduler;
    QSignalSpy due(&scheduler, &OperationScheduler::jobDue);
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const QString id = scheduler.schedule(QStringLiteral("tune"),
                                          now.addSecs(10).toString(Qt::ISODateWithMs),
                                          {{QStringLiteral("frequencyHz"), 14'200'000}});
    QVERIFY(!id.isEmpty());

    scheduler.evaluateAt(now.addSecs(9));
    QCOMPARE(due.size(), 0);
    scheduler.evaluateAt(now.addSecs(11));
    QCOMPARE(due.size(), 1);
    QCOMPARE(due.takeFirst().at(0).toString(), id);

    scheduler.complete(id, true, QStringLiteral("Sintonizzata"));
    QCOMPARE(scheduler.jobs().size(), 1);
    QCOMPARE(scheduler.jobs().first().toMap().value(QStringLiteral("status")).toString(),
             QStringLiteral("completed"));
    scheduler.evaluateAt(now.addSecs(60));
    QCOMPARE(due.size(), 0);
}

void TestOperationScheduler::rejectsUnsafeAndExpiredActions()
{
    OperationScheduler scheduler;
    const QDateTime now = QDateTime::currentDateTimeUtc();
    QVERIFY(scheduler.schedule(QStringLiteral("ptt"),
                                now.addSecs(10).toString(Qt::ISODateWithMs)).isEmpty());
    QVERIFY(scheduler.schedule(QStringLiteral("tune"),
                                now.addSecs(-10).toString(Qt::ISODateWithMs)).isEmpty());
    QVERIFY(scheduler.schedule(QStringLiteral("tune"), QStringLiteral("2030-01-01T12:00:00"))
                 .isEmpty());

    const QString pending = scheduler.schedule(QStringLiteral("tune"),
                                               QDateTime::currentDateTimeUtc()
                                                   .addMSecs(150).toString(Qt::ISODateWithMs));
    QVERIFY(!pending.isEmpty());
    QVERIFY(scheduler.setEnabled(pending, false));
    QTest::qWait(250);
    QVERIFY(!scheduler.setEnabled(pending, true));
    QCOMPARE(scheduler.jobs().last().toMap().value(QStringLiteral("status")).toString(),
             QStringLiteral("missed"));
}

void TestOperationScheduler::persistsPendingJobs()
{
    const QDateTime later = QDateTime::currentDateTimeUtc().addSecs(3600);
    QString id;
    {
        OperationScheduler scheduler;
        id = scheduler.schedule(QStringLiteral("record-audio-start"),
                                later.toString(Qt::ISODateWithMs));
        QVERIFY(!id.isEmpty());
    }
    OperationScheduler restored;
    QCOMPARE(restored.jobs().size(), 1);
    QCOMPARE(restored.jobs().first().toMap().value(QStringLiteral("id")).toString(), id);
    QCOMPARE(restored.jobs().first().toMap().value(QStringLiteral("status")).toString(),
             QStringLiteral("pending"));
}

QTEST_MAIN(TestOperationScheduler)
#include "tst_operation_scheduler.moc"
