// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace dsdr::core {

// A lifetime barrier for decoder objects, not a radio shutdown controller.
// Construct before shutdown; construction allocates and starts a cleanup thread.
// The cleanup thread owns resources until it has completed. The owner must
// retain this handle and poll readyToExit() before destruction on the UI.
// Request and acknowledgements are atomic; readyToExit uses QThread::wait(0).
//
// Integration MUST stop/disconnect every producer, consumer, queued call and
// QObject/raw-pointer user before acknowledgeUsersQuiescent(). When the
// backend fence is Required, it MUST turn
// PTT off, stop TX, close the backend and prohibit reopening before
// acknowledgeBackendStopped(). These acknowledgements are assertions by the
// caller, not checks performed by this helper. NotRequired is only valid for
// resources with no direct backend access after their users are quiescent.
// Keep the application event loop
// alive until readyToExit() AND all other application shutdown barriers pass.
// Never destroy dependencies of a resource before its destructor completes.
// Pass such dependencies in the same owned bundle, ordered so they outlive it.
// If an acknowledgement is missing or a decoder never returns, resources and
// the cleanup thread intentionally remain alive and readyToExit stays false.
class DecoderShutdown final {
public:
    enum class BackendFence { Required, NotRequired };
    class Resource {
    public:
        virtual ~Resource() = default;
    };
    template<class T> static std::unique_ptr<Resource> own(std::unique_ptr<T> value) {
        static_assert(std::is_nothrow_destructible<T>::value, "resource destructor must not throw");
        return std::unique_ptr<Resource>(new Holder<T>(std::move(value)));
    }
    using Resources = std::vector<std::unique_ptr<Resource>>;
    explicit DecoderShutdown(Resources resources, BackendFence backendFence = BackendFence::Required);
    // Safety fallback: waits if destroyed too early; never do that on the UI.
    ~DecoderShutdown();
    DecoderShutdown(const DecoderShutdown &) = delete;
    DecoderShutdown &operator=(const DecoderShutdown &) = delete;
    void request() noexcept;
    void acknowledgeUsersQuiescent() noexcept;
    void acknowledgeBackendStopped() noexcept;
    bool readyToExit() const noexcept;

private:
    template<class T> class Holder final : public Resource {
    public:
        explicit Holder(std::unique_ptr<T> value) : m_value(std::move(value)) {}
    private:
        std::unique_ptr<T> m_value;
    };
    struct State;
    class Worker;
    std::shared_ptr<State> m_state;
    std::unique_ptr<Worker> m_worker;
};
} // namespace dsdr::core
