// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/BackendShutdown.h"
#include "hal/IRadioBackend.h"
#include <QMetaObject>
#include <QObject>

namespace dsdr::core {
struct BackendShutdown::State {
    std::atomic<Status> status{Status::Idle};
    hal::IRadioBackend *backend = nullptr; // ownership exclusively on worker after begin
};
BackendShutdown::BackendShutdown() : m_state(std::make_shared<State>()) {}
BackendShutdown::~BackendShutdown() {
    // Do not abandon a backend or terminate its thread. Integration retains
    // this handle until readyToExit(); this wait is only a safety fallback.
    if (m_thread.isRunning()) m_thread.wait();
}
BackendShutdown::BeginResult BackendShutdown::beginForAuditedBackend(hal::IRadioBackend *backend, ChildPolicy children) {
    if (m_state->status.load(std::memory_order_acquire) != Status::Idle) return BeginResult::AlreadyStarted;
    if (!backend) return BeginResult::NullBackend;
    if (QThread::currentThread() != m_thread.thread() || QThread::currentThread() != backend->thread())
        return BeginResult::WrongThread;
    if (!backend->children().isEmpty() && children == ChildPolicy::Reject) return BeginResult::HasChildren;
    QObject *parent = backend->parent();
    backend->setParent(nullptr);
    if (!backend->moveToThread(&m_thread)) {
        backend->setParent(parent);
        return BeginResult::MoveFailed;
    }
    m_state->backend = backend;
    m_state->status.store(Status::Running, std::memory_order_release);
    m_thread.start();
    enqueue();
    return BeginResult::Accepted;
}
bool BackendShutdown::enqueue() {
    auto state = m_state;
    auto *backend = state->backend;
    auto *thread = &m_thread;
    const bool queued = QMetaObject::invokeMethod(backend, [state, backend, thread] {
        try {
            if (backend->ptt()) backend->setPtt(false);
            if (backend->ptt()) { state->status.store(Status::PttActive, std::memory_order_release); return; }
            backend->close();
            if (backend->ptt()) { state->status.store(Status::PttActive, std::memory_order_release); return; }
            if (backend->isOpen()) { state->status.store(Status::BackendStillOpen, std::memory_order_release); return; }
            // The backend is the receiver of this event: never delete it
            // directly while its event handler is executing. Deferred deletion
            // runs on its affinity thread, and only then may the loop quit.
            QObject::connect(backend, &QObject::destroyed, thread, [state, thread] {
                state->backend = nullptr;
                state->status.store(Status::Closed, std::memory_order_release);
                thread->quit();
            }, Qt::DirectConnection);
            backend->deleteLater();
        } catch (...) {
            state->status.store(Status::Exception, std::memory_order_release);
        }
    }, Qt::QueuedConnection);
    if (!queued) state->status.store(Status::QueueFailed, std::memory_order_release);
    return queued;
}
bool BackendShutdown::retry() {
    if (QThread::currentThread() != m_thread.thread()) return false;
    const auto s = status();
    if (s != Status::PttActive && s != Status::BackendStillOpen && s != Status::Exception && s != Status::QueueFailed)
        return false;
    m_state->status.store(Status::Running, std::memory_order_release);
    return enqueue();
}
BackendShutdown::Status BackendShutdown::status() const noexcept { return m_state->status.load(std::memory_order_acquire); }
bool BackendShutdown::readyToExit() const noexcept {
    return status() == Status::Closed && const_cast<QThread &>(m_thread).wait(0);
}
} // namespace dsdr::core
