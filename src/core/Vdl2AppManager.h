// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QObject>
#include <QPointer>
#include <memory>
namespace dsdr::core {
class SessionManager;
class Vdl2ResourcePreparation;
class Vdl2SessionBinding;
// Keep alive until shutdownFencePassed; the preparation thread and the
// activated binding are never destroyed while they can still be in use.
class Vdl2AppManager final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QObject *controller READ controller NOTIFY changed)
    Q_PROPERTY(QString state READ state NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
public:
    explicit Vdl2AppManager(SessionManager *session, QObject *parent = nullptr);
    ~Vdl2AppManager() override;
    QObject *controller() const;
    QString state() const { return m_state; }
    bool busy() const { return m_preparation || m_binding; }
    Q_INVOKABLE bool start(unsigned rate, qint64 center, double offset);
    Q_INVOKABLE bool startForFrequency(qint64 frequency);
    Q_INVOKABLE void stop();
signals:
    void changed();
private:
    void prepared();
    void setState(const QString &state);
    QPointer<SessionManager> m_session;
    Vdl2ResourcePreparation *m_preparation = nullptr;
    std::unique_ptr<Vdl2SessionBinding> m_binding;
    QString m_state;
    bool m_cancelled = false;
    bool m_checkSource = false;
    unsigned m_expectedRate = 0;
    qint64 m_expectedCenter = 0;
};
}
