// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "core/AsyncIqDecoder.h"
#include "dsp/PocsagIqReceiver.h"
#include "dsp/SpscRing.h"
#include <array>
#include <atomic>
#include <cstdint>

namespace dsdr::core {
// Single IQ producer, single result consumer. Configure/start/stop/destruction
// require producer and result-consumer quiescence and serialized control; joining can block and
// must be performed off the UI/real-time threads. The hook is for tests only.
class PocsagReceiver final {
public:
    using WorkerHook = void (*)(void *) noexcept;
    struct Message {
        std::uint32_t address = 0;
        unsigned function = 0, correctedBits = 0;
        std::uint64_t generation = 0;
        std::size_t size = 0;
        std::array<char, 1024> text{};
    };
    explicit PocsagReceiver(std::size_t slots = 128, std::size_t maxFrames = 4096,
                            std::size_t messageCapacity = 32,
                            WorkerHook hook = nullptr, void *hookContext = nullptr);
    ~PocsagReceiver();
    PocsagReceiver(const PocsagReceiver &) = delete;
    PocsagReceiver &operator=(const PocsagReceiver &) = delete;
    bool configure(unsigned sampleRate, unsigned baud, double channelHz);
    bool start();
    void stop(bool drain = false) noexcept;
    // Managed mode: start/stop still require producer/consumer quiescence and
    // must run outside GUI/DSP threads. Control setters are nonblocking and
    // must be serialized with each other; filter configuration runs on worker.
    bool startManaged();
    bool configureManaged(unsigned sampleRate, unsigned baud, double channelHz) noexcept;
    void setManagedEnabled(bool enabled) noexcept;
    enum class ManagedStatus { Stopped, Disabled, InvalidConfiguration, Configuring, Active, Error };
    ManagedStatus managedStatus() const noexcept;
    AsyncIqDecoder::SubmitResult submit(const dsp::Complex *samples, std::size_t count) noexcept;
    // Producer only. A caller may snapshot generation before acquiring IQ;
    // an intervening invalidation makes that block stale, even if queued later.
    AsyncIqDecoder::SubmitResult submitForGeneration(const dsp::Complex *samples, std::size_t count,
                                                      std::uint64_t generation) noexcept;
    // Thread-safe result and queued-block invalidation; does not touch the
    // producer-owned gap flag. Lifecycle still requires quiescence.
    void invalidateResults() noexcept { ++m_generation; }
    void discontinuity() noexcept; // producer only
    enum class ReadResult { Empty, Stale, Message };
    ReadResult readOne(Message &message) noexcept; // consumes at most one slot
    bool read(Message &message) noexcept; // sole consumer; discards stale generations
    AsyncIqDecoder::Stats stats() const noexcept { return m_async.stats(); }
    std::uint64_t droppedMessages() const noexcept { return m_droppedMessages.load(); }
    std::uint64_t resetCount() const noexcept { return m_resetCount.load(); }
    std::uint64_t workerErrors() const noexcept { return m_workerErrors.load(); }
    std::uint64_t generation() const noexcept { return m_generation.load(); }
private:
    static void resetWorker(void *) noexcept;
    static void processWorker(void *, const dsp::Complex *, std::size_t, std::uint64_t, std::chrono::steady_clock::time_point) noexcept;
    static void receive(void *, const dsp::PocsagDecoder::Message &);
    WorkerHook m_hook;
    void *m_hookContext;
    std::atomic<std::uint64_t> m_generation{1}, m_droppedMessages{0}, m_workerErrors{0}, m_resetCount{0};
    std::uint64_t m_workerGeneration = 0;
    bool m_configured = false, m_started = false;
    std::atomic<bool> m_managed{false}, m_managedEnabled{false}, m_managedUpdating{false}, m_managedValid{false};
    std::atomic<unsigned> m_managedRate{0}, m_managedBaud{0};
    std::atomic<double> m_managedOffset{0};
    std::atomic<std::uint64_t> m_managedApplied{0}, m_managedFailed{0};
    dsp::SpscRing<Message> m_messages;
    dsp::PocsagIqReceiver m_decoder;
    AsyncIqDecoder m_async;
};
}
