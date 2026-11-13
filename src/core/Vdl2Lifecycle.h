// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "DecoderShutdown.h"
#include "Vdl2Controller.h"
#include "Vdl2Resources.h"
#include <QPointer>
#include <QTimer>
#include <atomic>
namespace dsdr::core {
class DspEngine;
// Experimental owner, controlled exclusively from its QObject thread. The
// engine and this object must outlive the asynchronous shutdown handshake.
// VDL2 has no direct backend references: queued DSP tap detachment fences its
// producer, so a standalone decoder stop does not require stopping the radio.
class Vdl2Lifecycle final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString state READ state NOTIFY changed)
    Q_PROPERTY(QObject* controller READ controller CONSTANT)
public:
    Vdl2Lifecycle(DspEngine* engine,unsigned rate,std::int64_t center,double offset,double floor,QObject* parent=nullptr);
    Vdl2Lifecycle(DspEngine* engine,std::unique_ptr<Vdl2Resources> resources,QObject* parent=nullptr);
    ~Vdl2Lifecycle() override;
    QString state() const { return m_state; }
    QObject* controller() noexcept { return &m_controller; }
    bool ready() const noexcept { return m_shutdown && m_shutdown->readyToExit(); }
    void activate();
    void requestShutdown();
    void acknowledgeBackendStopped() noexcept;
signals:
    void changed();
private:
    void setState(const QString& state);
    QPointer<DspEngine> m_engine;
    Vdl2Resources* m_resources=nullptr;
    std::unique_ptr<DecoderShutdown> m_shutdown;
    Vdl2Controller m_controller;
    QTimer m_timer;
    QString m_state;
    bool m_requested=false,m_activated=false,m_usersFenced=false;
    std::atomic<bool> m_stopping{false};
};
}
