// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "core/BackendShutdown.h"
#include <QObject>
#include <QTimer>

namespace dsdr::core {
// UI adapter. Owner and backend dependencies must outlive the whole shutdown.
// Destruction before ready can block; retain it and keep the event loop alive.
class BackendShutdownViewModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString statusText READ statusText NOTIFY changed)
    Q_PROPERTY(bool retryAllowed READ retryAllowed NOTIFY changed)
    Q_PROPERTY(bool cancelAllowed READ cancelAllowed CONSTANT)
    Q_PROPERTY(bool ready READ ready NOTIFY changed)
public:
    explicit BackendShutdownViewModel(QObject *parent = nullptr);
    BackendShutdown::BeginResult beginForAuditedBackend(hal::IRadioBackend *,
        BackendShutdown::ChildPolicy = BackendShutdown::ChildPolicy::Reject);
    QString statusText() const;
    bool retryAllowed() const noexcept;
    bool cancelAllowed() const noexcept { return false; }
    bool ready() const noexcept { return m_ready; }
    Q_INVOKABLE void retry();
signals:
    void changed();
private:
    void poll();
    BackendShutdown m_shutdown;
    QTimer m_timer;
    BackendShutdown::Status m_status = BackendShutdown::Status::Idle;
    bool m_ready = false;
};
} // namespace dsdr::core
