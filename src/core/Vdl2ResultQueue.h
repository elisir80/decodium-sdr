// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "dsp/SpscRing.h"
#include "dsp/vdl2/Vdl2Offline.h"
#include <array>
#include <atomic>
namespace dsdr::core {
// Worker produces complete AVLC frames; one consumer reads. Results retain
// their source generation so a later retune can invalidate queued items.
class Vdl2ResultQueue final {
public:
    struct Result {
        std::array<std::uint8_t,4096> bytes{};
        std::size_t size=0;
        std::uint64_t generation=0;
    };
    explicit Vdl2ResultQueue(std::size_t capacity=64);
    void publish(const vdl2::OfflineBurst&) noexcept;
    bool read(Result&,std::uint64_t currentGeneration) noexcept;
    std::uint64_t dropped() const noexcept { return m_dropped.load(); }
private:
    dsp::SpscRing<Result> m_queue;
    std::atomic<std::uint64_t> m_dropped{0};
};
}
