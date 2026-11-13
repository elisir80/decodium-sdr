// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "core/AsyncIqDecoder.h"
#include "dsp/AisDecoder.h"
#include "dsp/SpscRing.h"
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace dsdr::core {
// One IQ producer, one result consumer. Control setters must be serialized
// with each other. Lifecycle methods require producer/consumer quiescence.
// stop/destruction join the worker: run them off GUI and DSP threads and keep
// the owner alive until completion. The hook is for tests, on the worker only.
class AisReceiver final {
public:
    using WorkerHook = void (*)(void *) noexcept;
    enum class Status { Disabled, Stopped, NoSource, WrongRate, WrongOffset, Active };
    struct Message {
        std::array<std::uint8_t, 128> payload{};
        std::size_t size = 0;
        std::uint32_t mmsi = 0;
        unsigned type = 0;
        bool hasPosition = false;
        double latitude = 0, longitude = 0;
        std::uint64_t generation = 0;
    };
    explicit AisReceiver(std::size_t slots = 64, std::size_t maxFrames = 4096,
                         std::size_t messageCapacity = 128,
                         WorkerHook hook = nullptr, void *hookContext = nullptr);
    ~AisReceiver();
    AisReceiver(const AisReceiver &) = delete;
    AisReceiver &operator=(const AisReceiver &) = delete;
    void start();
    void stop() noexcept; // discard queued blocks; joins
    void setEnabled(bool enabled) noexcept;
    bool enabled() const noexcept { return m_enabled.load(); }
    // IQ must already be filtered to one AIS channel. A different absolute
    // tune with identical rate/offset must also call discontinuity().
    void setSource(bool present, double rate, double offsetHz, bool inBand = true) noexcept;
    Status status() const noexcept;
    void discontinuity() noexcept;
    // One producer; take generation before acquiring source samples. The
    // explicit variant prevents stale samples being relabelled after a retune.
    AsyncIqDecoder::SubmitResult submit(const dsp::Complex *samples, std::size_t count) noexcept;
    AsyncIqDecoder::SubmitResult submitForGeneration(const dsp::Complex *samples, std::size_t count,
                                                      std::uint64_t generation) noexcept;
    enum class ReadResult { Empty, Stale, Message };
    ReadResult readOne(Message &message) noexcept; // consumes at most one slot
    bool read(Message &message) noexcept; // one consumer, drops stale results
    std::uint64_t generation() const noexcept { return m_generation.load(); }
    std::uint64_t droppedMessages() const noexcept { return m_dropped.load(); }
    std::uint64_t workerErrors() const noexcept { return m_errors.load(); }
    AsyncIqDecoder::Stats stats() const noexcept { return m_async.stats(); }
private:
    static void resetWorker(void *) noexcept;
    static void processWorker(void *, const dsp::Complex *, std::size_t, std::uint64_t,
                              std::chrono::steady_clock::time_point) noexcept;
    static void receive(void *, const dsp::AisDecoder::Message &);
    const WorkerHook m_hook;
    void *const m_hookContext;
    std::atomic<bool> m_enabled{false}, m_running{false}, m_present{false}, m_updating{false}, m_inBand{true};
    std::atomic<double> m_rate{0}, m_offset{0};
    std::atomic<std::uint64_t> m_generation{1}, m_dropped{0}, m_errors{0};
    std::uint64_t m_workerGeneration = 0;
    dsp::SpscRing<Message> m_messages;
    dsp::AisDecoder m_decoder;
    AsyncIqDecoder m_async;
};
}
