// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/DecoderShutdown.h"
#include <chrono>
#include <QThread>
#include <thread>
#include <utility>

namespace dsdr::core {
static_assert(std::atomic<unsigned>::is_always_lock_free, "shutdown control requires lock-free atomics");
namespace {
constexpr unsigned requested = 1, quiescent = 2, backendStopped = 4, completed = 8;
constexpr unsigned allBarriers = requested | quiescent | backendStopped;
}
struct DecoderShutdown::State {
    explicit State(Resources value) : resources(std::move(value)) {}
    Resources resources;
    std::atomic<unsigned> flags{0};
};
class DecoderShutdown::Worker final : public QThread {
public:
    explicit Worker(std::shared_ptr<State> state) : m_state(std::move(state)) {}
    void run() override {
        while ((m_state->flags.load(std::memory_order_acquire) & allBarriers) != allBarriers)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        m_state->resources.clear();
        m_state->flags.fetch_or(completed, std::memory_order_release);
    }
private:
    std::shared_ptr<State> m_state;
};
DecoderShutdown::DecoderShutdown(Resources resources, BackendFence backendFence)
    : m_state(std::make_shared<State>(std::move(resources))),
      m_worker(std::make_unique<Worker>(m_state))
{
    if (backendFence == BackendFence::NotRequired)
        m_state->flags.fetch_or(backendStopped, std::memory_order_release);
    m_worker->start();
}
DecoderShutdown::~DecoderShutdown()
{
    // Retain resources and wait rather than destroy a running worker. The UI
    // must keep the handle alive until readyToExit() is true.
    if (m_worker) m_worker->wait();
}
void DecoderShutdown::request() noexcept { m_state->flags.fetch_or(requested, std::memory_order_release); }
void DecoderShutdown::acknowledgeUsersQuiescent() noexcept { m_state->flags.fetch_or(quiescent, std::memory_order_release); }
void DecoderShutdown::acknowledgeBackendStopped() noexcept { m_state->flags.fetch_or(backendStopped, std::memory_order_release); }
bool DecoderShutdown::readyToExit() const noexcept {
    return (m_state->flags.load(std::memory_order_acquire) & (allBarriers | completed)) == (allBarriers | completed)
        && m_worker->wait(0);
}
} // namespace dsdr::core
