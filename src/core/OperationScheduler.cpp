// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/OperationScheduler.h"

#include <QLoggingCategory>
#include <QSettings>
#include <QUuid>

#include <algorithm>

Q_LOGGING_CATEGORY(dsdrScheduler, "dsdr.scheduler")

namespace dsdr::core {

namespace {
constexpr auto kSettingsKey = "scheduler/jobsV1";
constexpr auto kPending = "pending";
constexpr auto kRunning = "running";
constexpr auto kCompleted = "completed";
constexpr auto kFailed = "failed";
constexpr auto kMissed = "missed";
constexpr auto kCancelled = "cancelled";

QString isoUtc(const QDateTime &dateTime)
{
    return dateTime.toUTC().toString(Qt::ISODateWithMs);
}

qint64 epochMs(const QDateTime &dateTime)
{
    return dateTime.toUTC().toMSecsSinceEpoch();
}

bool isTerminal(const QString &status)
{
    return status == QLatin1String(kCompleted) || status == QLatin1String(kFailed)
        || status == QLatin1String(kMissed) || status == QLatin1String(kCancelled);
}
} // namespace

OperationScheduler::OperationScheduler(QObject *parent)
    : QObject(parent)
    , m_timer(this)
{
    load();
    m_timer.setInterval(250);
    connect(&m_timer, &QTimer::timeout, this,
            [this] { evaluateAt(QDateTime::currentDateTimeUtc()); });
    m_timer.start();
}

QStringList OperationScheduler::supportedActions()
{
    // Questo è deliberatamente l'intero contratto: non esistono azioni TX,
    // PTT o tune TX programmabili. Uno scheduler che può trasmettere da solo
    // non è una comodità, è un rischio operativo.
    return {QStringLiteral("tune"), QStringLiteral("scan"),
            QStringLiteral("record-iq-start"), QStringLiteral("record-iq-stop"),
            QStringLiteral("record-audio-start"), QStringLiteral("record-audio-stop")};
}

QString OperationScheduler::normaliseAction(const QString &action)
{
    return action.trimmed().toLower();
}

bool OperationScheduler::isSupportedAction(const QString &action)
{
    return supportedActions().contains(normaliseAction(action));
}

QDateTime OperationScheduler::parseUtc(const QString &whenUtc)
{
    const QString value = whenUtc.trimmed();
    if (value.isEmpty())
        return {};

    QDateTime parsed = QDateTime::fromString(value, Qt::ISODateWithMs);
    if (!parsed.isValid())
        parsed = QDateTime::fromString(value, Qt::ISODate);
    if (!parsed.isValid())
        return {};

    // L'interfaccia dichiara UTC. Un orario senza fuso sarebbe ambiguo e
    // cambierebbe significato passando a un Mac con impostazioni diverse.
    if (!value.endsWith(QLatin1Char('Z')) && !value.contains(QLatin1Char('+'))
        && value.lastIndexOf(QLatin1Char('-')) <= 9)
        return {};
    return parsed.toUTC();
}

int OperationScheduler::indexOf(const QString &id) const
{
    for (int index = 0; index < m_jobs.size(); ++index) {
        if (m_jobs.at(index).toMap().value(QStringLiteral("id")).toString() == id)
            return index;
    }
    return -1;
}

QString OperationScheduler::schedule(const QString &action, const QString &whenUtc,
                                     const QVariantMap &arguments)
{
    const QString normalised = normaliseAction(action);
    const QDateTime runAt = parseUtc(whenUtc);
    if (!isSupportedAction(normalised) || !runAt.isValid()) {
        qCWarning(dsdrScheduler) << "scheduler: rifiutata azione o data non valida"
                                 << action << whenUtc;
        return {};
    }

    const QDateTime now = QDateTime::currentDateTimeUtc();
    if (runAt <= now) {
        qCWarning(dsdrScheduler) << "scheduler: rifiutata scadenza non futura" << whenUtc;
        return {};
    }

    QVariantMap job;
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    job.insert(QStringLiteral("id"), id);
    job.insert(QStringLiteral("action"), normalised);
    job.insert(QStringLiteral("arguments"), arguments);
    job.insert(QStringLiteral("atUtc"), isoUtc(runAt));
    job.insert(QStringLiteral("atEpochMs"), epochMs(runAt));
    job.insert(QStringLiteral("createdAtUtc"), isoUtc(now));
    job.insert(QStringLiteral("enabled"), true);
    job.insert(QStringLiteral("status"), QString::fromLatin1(kPending));
    job.insert(QStringLiteral("message"), tr("In attesa"));
    m_jobs.append(job);
    std::sort(m_jobs.begin(), m_jobs.end(), [](const QVariant &left, const QVariant &right) {
        return left.toMap().value(QStringLiteral("atEpochMs")).toLongLong()
             < right.toMap().value(QStringLiteral("atEpochMs")).toLongLong();
    });
    save();
    publish();
    qCInfo(dsdrScheduler) << "scheduler: azione pianificata" << normalised << id
                           << isoUtc(runAt);
    return id;
}

bool OperationScheduler::cancel(const QString &id)
{
    const int index = indexOf(id);
    if (index < 0)
        return false;
    QVariantMap job = m_jobs.at(index).toMap();
    if (isTerminal(job.value(QStringLiteral("status")).toString()))
        return false;
    job.insert(QStringLiteral("enabled"), false);
    job.insert(QStringLiteral("status"), QString::fromLatin1(kCancelled));
    job.insert(QStringLiteral("message"), tr("Annullata dall'operatore"));
    job.insert(QStringLiteral("completedAtUtc"), isoUtc(QDateTime::currentDateTimeUtc()));
    m_jobs[index] = job;
    save();
    publish();
    qCInfo(dsdrScheduler) << "scheduler: azione annullata" << id;
    return true;
}

bool OperationScheduler::setEnabled(const QString &id, bool enabled)
{
    const int index = indexOf(id);
    if (index < 0)
        return false;
    QVariantMap job = m_jobs.at(index).toMap();
    if (job.value(QStringLiteral("status")).toString() != QLatin1String(kPending))
        return false;
    if (job.value(QStringLiteral("enabled")).toBool() == enabled)
        return true;
    if (enabled && job.value(QStringLiteral("atEpochMs")).toLongLong()
                       <= QDateTime::currentDateTimeUtc().toMSecsSinceEpoch()) {
        // Anche una richiesta disabilitata può essere rimasta nella coda oltre
        // la sua ora. Riaccenderla non deve trasformarla in un'operazione RX
        // tardiva e potenzialmente inattesa.
        job.insert(QStringLiteral("enabled"), false);
        job.insert(QStringLiteral("status"), QString::fromLatin1(kMissed));
        job.insert(QStringLiteral("message"), tr("Scadenza ormai trascorsa"));
        job.insert(QStringLiteral("completedAtUtc"), isoUtc(QDateTime::currentDateTimeUtc()));
        m_jobs[index] = job;
        save();
        publish();
        return false;
    }
    job.insert(QStringLiteral("enabled"), enabled);
    job.insert(QStringLiteral("message"), enabled ? tr("In attesa") : tr("Disattivata"));
    m_jobs[index] = job;
    save();
    publish();
    return true;
}

bool OperationScheduler::remove(const QString &id)
{
    const int index = indexOf(id);
    if (index < 0)
        return false;
    m_jobs.removeAt(index);
    save();
    publish();
    return true;
}

void OperationScheduler::clearHistory()
{
    const auto firstHistory = std::remove_if(m_jobs.begin(), m_jobs.end(), [](const QVariant &item) {
        return isTerminal(item.toMap().value(QStringLiteral("status")).toString());
    });
    if (firstHistory == m_jobs.end())
        return;
    m_jobs.erase(firstHistory, m_jobs.end());
    save();
    publish();
}

void OperationScheduler::evaluateAt(const QDateTime &nowUtc)
{
    if (!nowUtc.isValid())
        return;

    const qint64 nowMs = epochMs(nowUtc);
    struct DueJob {
        QString id;
        QString action;
        QVariantMap arguments;
    };
    QList<DueJob> due;
    bool changed = false;

    for (int index = 0; index < m_jobs.size(); ++index) {
        QVariantMap job = m_jobs.at(index).toMap();
        if (!job.value(QStringLiteral("enabled")).toBool()
            || job.value(QStringLiteral("status")).toString() != QLatin1String(kPending)
            || job.value(QStringLiteral("atEpochMs")).toLongLong() > nowMs) {
            continue;
        }

        job.insert(QStringLiteral("enabled"), false);
        job.insert(QStringLiteral("status"), QString::fromLatin1(kRunning));
        job.insert(QStringLiteral("message"), tr("In esecuzione"));
        m_jobs[index] = job;
        due.append({job.value(QStringLiteral("id")).toString(),
                    job.value(QStringLiteral("action")).toString(),
                    job.value(QStringLiteral("arguments")).toMap()});
        changed = true;
    }

    if (!changed)
        return;
    save();
    publish();
    for (const DueJob &job : due) {
        qCInfo(dsdrScheduler) << "scheduler: azione dovuta" << job.action << job.id;
        emit jobDue(job.id, job.action, job.arguments);
    }
}

void OperationScheduler::complete(const QString &id, bool succeeded, const QString &message)
{
    const int index = indexOf(id);
    if (index < 0)
        return;
    QVariantMap job = m_jobs.at(index).toMap();
    if (job.value(QStringLiteral("status")).toString() != QLatin1String(kRunning))
        return;
    job.insert(QStringLiteral("enabled"), false);
    job.insert(QStringLiteral("status"), succeeded ? QString::fromLatin1(kCompleted)
                                                    : QString::fromLatin1(kFailed));
    job.insert(QStringLiteral("message"), message);
    job.insert(QStringLiteral("completedAtUtc"), isoUtc(QDateTime::currentDateTimeUtc()));
    m_jobs[index] = job;
    save();
    publish();
    qCInfo(dsdrScheduler) << "scheduler: azione" << (succeeded ? "completata" : "fallita")
                           << id << message;
}

void OperationScheduler::load()
{
    QSettings settings;
    const QVariantList stored = settings.value(QString::fromLatin1(kSettingsKey)).toList();
    const QDateTime now = QDateTime::currentDateTimeUtc();
    bool repaired = false;
    for (const QVariant &value : stored) {
        QVariantMap job = value.toMap();
        const QString id = job.value(QStringLiteral("id")).toString();
        const QString action = normaliseAction(job.value(QStringLiteral("action")).toString());
        const QDateTime runAt = parseUtc(job.value(QStringLiteral("atUtc")).toString());
        if (id.isEmpty() || !isSupportedAction(action) || !runAt.isValid()) {
            repaired = true;
            continue;
        }
        job.insert(QStringLiteral("action"), action);
        job.insert(QStringLiteral("atUtc"), isoUtc(runAt));
        job.insert(QStringLiteral("atEpochMs"), epochMs(runAt));
        if (job.value(QStringLiteral("status")).toString() == QLatin1String(kRunning)) {
            job.insert(QStringLiteral("enabled"), false);
            job.insert(QStringLiteral("status"), QString::fromLatin1(kMissed));
            job.insert(QStringLiteral("message"), tr("Interrotta alla chiusura precedente"));
            job.insert(QStringLiteral("completedAtUtc"), isoUtc(now));
            repaired = true;
        } else if (job.value(QStringLiteral("status")).toString() == QLatin1String(kPending)
                   && job.value(QStringLiteral("enabled")).toBool() && runAt < now) {
            // Non si avvia una registrazione in ritardo al prossimo avvio:
            // un compito scaduto viene reso visibile, non eseguito a sorpresa.
            job.insert(QStringLiteral("enabled"), false);
            job.insert(QStringLiteral("status"), QString::fromLatin1(kMissed));
            job.insert(QStringLiteral("message"), tr("Scaduta mentre l'applicazione era chiusa"));
            job.insert(QStringLiteral("completedAtUtc"), isoUtc(now));
            repaired = true;
        }
        m_jobs.append(job);
    }
    std::sort(m_jobs.begin(), m_jobs.end(), [](const QVariant &left, const QVariant &right) {
        return left.toMap().value(QStringLiteral("atEpochMs")).toLongLong()
             < right.toMap().value(QStringLiteral("atEpochMs")).toLongLong();
    });
    if (repaired)
        save();
}

void OperationScheduler::save() const
{
    QSettings settings;
    settings.setValue(QString::fromLatin1(kSettingsKey), m_jobs);
    settings.sync();
}

void OperationScheduler::publish()
{
    emit jobsChanged();
}

} // namespace dsdr::core
