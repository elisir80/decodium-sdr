// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QThread>
#include <atomic>
#include <memory>

namespace dsdr::hal { class IRadioBackend; }
namespace dsdr::core {

// Isolated prototype. Only use with a backend individually audited for moving
// to another Qt thread, including unparented QObjects and native resources.
// Before begin: fence every producer/consumer and queued/direct backend user,
// disconnect external callbacks, stop discovery, prohibit all new work and
// retain all dependencies. This class cannot validate those preconditions.
// All control calls except status() run on the creating/GUI thread.
class BackendShutdown final {
public:
    enum class Status { Idle, Running, PttActive, BackendStillOpen, Exception, QueueFailed, Closed };
    enum class BeginResult { Accepted, NullBackend, WrongThread, HasChildren, MoveFailed, AlreadyStarted };
    enum class ChildPolicy { Reject, IndividuallyAuditedTree };
    BackendShutdown();
    // A safety fallback may wait indefinitely. Keep the owner alive and poll
    // readyToExit() on the GUI; do not destroy while pending or failed.
    ~BackendShutdown();
    BackendShutdown(const BackendShutdown &) = delete;
    BackendShutdown &operator=(const BackendShutdown &) = delete;

    // Backend must be on the calling thread. On Accepted ownership transfers;
    // never touch the backend again. Otherwise ownership and parent are kept.
    // Children are rejected by default. The explicit opt-in asserts that the
    // entire child tree, live worker threads, unparented objects, connections,
    // callbacks and native handles have been audited for this transfer.
    BeginResult beginForAuditedBackend(hal::IRadioBackend *backend,
                                       ChildPolicy children = ChildPolicy::Reject);
    // Retry only after PttActive, BackendStillOpen, Exception or QueueFailed.
    bool retry();
    // Cancellation is deliberately unsupported: never drops a live backend.
    // UI must remain active and offer retry until safe ownership restoration
    // is implemented by the application.
    bool cancel() noexcept { return false; }
    Status status() const noexcept;
    // True only after backend destruction AND the cleanup QThread has exited.
    bool readyToExit() const noexcept;
private:
    struct State;
    bool enqueue();
    QThread m_thread;
    std::shared_ptr<State> m_state;
};
} // namespace dsdr::core
