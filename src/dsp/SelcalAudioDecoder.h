// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace dsdr::dsp {
// Isolated 8 kHz mono-audio decoder. The owner must serialize calls and run
// processing outside the UI/audio callback. This does not tune or demodulate IQ.
class SelcalAudioDecoder final {
public:
    using Callback = void (*)(void *, const std::array<char, 5> &);
    SelcalAudioDecoder(Callback callback, void *context) noexcept;
    void reset() noexcept;
    void process(const float *samples, std::size_t count) noexcept;
private:
    void analyze() noexcept;
    Callback m_callback;
    void *m_context;
    std::array<float, 800> m_samples{};
    std::size_t m_written=0, m_filled=0, m_hop=0;
    int m_current=-1, m_pending=-2, m_pendingRuns=0, m_run=0, m_first=-1;
    std::uint64_t m_hops=0, m_firstEnd=0;
};
}
