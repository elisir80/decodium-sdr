// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QVariantMap>
#include <atomic>

class QThread;

namespace dsdr::core {

// One independent file analysis at a time. All properties belong to the GUI
// thread; the worker only communicates through queued callbacks.
class IqAnalysisLab : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool running READ running NOTIFY changed)
    Q_PROPERTY(double progress READ progress NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QVariantMap resultA READ resultA NOTIFY changed)
    Q_PROPERTY(QVariantMap resultB READ resultB NOTIFY changed)
public:
    explicit IqAnalysisLab(QObject *parent = nullptr);
    ~IqAnalysisLab() override;
    bool running() const { return m_running; }
    double progress() const { return m_progress; }
    QString error() const { return m_error; }
    QVariantMap resultA() const { return m_a; }
    QVariantMap resultB() const { return m_b; }
    Q_INVOKABLE bool start(const QString &path, int slot);
    Q_INVOKABLE bool analyze(const QString &path, int slot, double startSeconds,
                             double durationSeconds, int fftSize, int window);
    Q_INVOKABLE void cancel();

signals:
    void changed();

private:
    QThread *m_thread = nullptr;
    std::atomic_bool m_cancel{false};
    bool m_running = false;
    double m_progress = 0;
    QString m_error;
    QVariantMap m_a, m_b;
};
}
