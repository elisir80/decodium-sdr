// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Vdl2MessageModel.h"
#include <QObject>
#include <QTimer>
namespace dsdr::core {
class Vdl2Session;
class Vdl2IqIngress;
// GUI-thread only. Session must outlive this object or be detached first.
class Vdl2Controller final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QAbstractItemModel* messages READ messages CONSTANT)
    Q_PROPERTY(qulonglong droppedResults READ droppedResults NOTIFY changed)
    Q_PROPERTY(qulonglong droppedBlocks READ droppedBlocks NOTIFY changed)
    Q_PROPERTY(qulonglong workerErrors READ workerErrors NOTIFY changed)
    Q_PROPERTY(QString sourceStatus READ sourceStatus NOTIFY changed)
public:
    static constexpr int PollBatch=64;
    explicit Vdl2Controller(Vdl2Session* session,QObject* parent=nullptr,Vdl2IqIngress* ingress=nullptr);
    QAbstractItemModel* messages() noexcept { return &m_model; }
    qulonglong droppedResults() const noexcept;
    qulonglong droppedBlocks() const noexcept;
    qulonglong workerErrors() const noexcept;
    QString sourceStatus() const;
    void detach() noexcept;
public slots:
    void poll();
signals:
    void changed();
private:
    Vdl2Session* m_session;
    Vdl2IqIngress* m_ingress;
    Vdl2MessageModel m_model;
    QTimer m_timer;
    qulonglong m_lastResults=0,m_lastBlocks=0,m_lastErrors=0;
    QString m_lastStatus;
};
}
