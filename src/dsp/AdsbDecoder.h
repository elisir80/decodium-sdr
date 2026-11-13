// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "dsp/DspTypes.h"
#include <array>
#include <cstdint>

namespace dsdr::dsp {

// Receive-only DF17/DF18 extended squitters. Input must be contiguous complex
// baseband at exactly 2 MHz, tuned to 1090 MHz. No resampling is performed.
// Call reset() on a discontinuity or retune. Callback runs on the caller's thread.
class AdsbDecoder {
public:
    using Frame = std::array<std::uint8_t, 14>;
    using Callback = void (*)(void *, const Frame &);

    explicit AdsbDecoder(Callback callback, void *context = nullptr) noexcept;
    void reset() noexcept;
    void process(const Complex *samples, std::size_t count) noexcept;
    static bool validFrame(const Frame &frame) noexcept;

private:
    static constexpr std::size_t kFrameSamples = 240;
    std::array<float, kFrameSamples> m_power{};
    std::size_t m_next = 0;
    std::size_t m_count = 0;
    std::size_t m_skip = 0;
    Callback m_callback;
    void *m_context;
    void inspect() noexcept;
};
} // namespace dsdr::dsp
