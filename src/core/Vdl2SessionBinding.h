// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Vdl2Lifecycle.h"
#include <QObject>
#include <QPointer>

namespace dsdr::core {
class SessionManager;
// Owner must keep this object alive until the session's shutdown fence passes.
class Vdl2SessionBinding final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QObject *controller READ controller CONSTANT)
    Q_PROPERTY(QString state READ state NOTIFY changed)
public:
    Vdl2SessionBinding(SessionManager *session, unsigned rate, qint64 center,
                       double offset, double floor, QObject *parent = nullptr);
    Vdl2SessionBinding(SessionManager *session, std::unique_ptr<Vdl2Resources> resources,
                       QObject *parent = nullptr, bool gateAlreadyReserved = false);
    QObject *controller() noexcept { return m_lifecycle.controller(); }
    QString state() const { return m_lifecycle.state(); }
    void activate() { m_lifecycle.activate(); }
    void stop() { m_lifecycle.requestShutdown(); }
    bool ready() const noexcept { return m_lifecycle.ready(); }
signals:
    void changed();
private:
    void updateShutdown();
    QPointer<SessionManager> m_session;
    Vdl2Lifecycle m_lifecycle;
    bool m_backendAcknowledged = false;
};
}
