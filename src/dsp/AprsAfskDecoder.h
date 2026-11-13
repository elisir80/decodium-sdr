// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace dsdr::dsp {

// Bell 202 receive-only decoder. Input is contiguous, demodulated mono audio;
// callback receives an AX.25 UI frame without FCS, on the processing thread.
// Call reset on retune, audio discontinuity, or channel change. Not thread safe.
class AprsAfskDecoder {
public:
    using Frame = std::vector<std::uint8_t>;
    using Callback = void (*)(void *, const Frame &);
    explicit AprsAfskDecoder(Callback callback, void *context = nullptr);
    bool configure(double sampleRate);
    void reset();
    void process(const float *samples, std::size_t count);
    // AX.25 CRC-16/X-25 over the frame excluding its little-endian FCS.
    static std::uint16_t fcs(const std::uint8_t *data, std::size_t size) noexcept;
    static bool validFrame(const Frame &frameWithFcs) noexcept;
private:
    struct Branch {
        double next = 0;
        bool haveTone = false;
        bool tone = false;
        std::uint8_t shift = 0;
        unsigned seen = 0;
        bool inFrame = false;
        std::vector<std::uint8_t> bits;
    };
    void bit(Branch &branch, bool value);
    void finish(const std::vector<std::uint8_t> &bits);
    Callback m_callback;
    void *m_context;
    double m_rate = 0, m_period = 0;
    std::size_t m_window = 0, m_index = 0, m_filled = 0;
    std::uint64_t m_sample = 0, m_lastDelivery = 0;
    std::vector<std::array<double, 4>> m_ring;
    std::array<double, 4> m_sum{};
    double m_phaseMark = 0, m_phaseSpace = 0;
    std::array<Branch, 12> m_branches;
    Frame m_lastFrame;
};
} // namespace dsdr::dsp
