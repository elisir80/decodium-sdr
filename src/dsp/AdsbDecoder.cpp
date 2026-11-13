// SPDX-License-Identifier: GPL-3.0-or-later
#include "dsp/AdsbDecoder.h"
#include <algorithm>
#include <cmath>

namespace dsdr::dsp {
AdsbDecoder::AdsbDecoder(Callback callback, void *context) noexcept
    : m_callback(callback), m_context(context) {}

void AdsbDecoder::reset() noexcept
{
    m_next = m_count = m_skip = 0;
}

bool AdsbDecoder::validFrame(const Frame &frame) noexcept
{
    const auto df = frame[0] >> 3;
    if (df != 17 && df != 18)
        return false;
    // Polynomial x^24 + x^23 + x^22 + x^21 + x^20 + x^19 +
    // x^18 + x^17 + x^16 + x^15 + x^14 + x^13 + x^12 +
    // x^10 + x^3 + 1; a complete unoverlaid frame has zero remainder.
    std::uint32_t crc = 0;
    for (auto byte : frame) {
        for (int bit = 7; bit >= 0; --bit) {
            const bool top = ((crc >> 23) ^ (byte >> bit)) & 1U;
            crc = (crc << 1) & 0xffffffU;
            if (top) crc ^= 0xfff409U;
        }
    }
    return crc == 0;
}

void AdsbDecoder::process(const Complex *samples, std::size_t count) noexcept
{
    if (!samples) return;
    for (std::size_t i = 0; i < count; ++i) {
        const float p = magnitudeSquared(samples[i]);
        m_power[m_next] = std::isfinite(p) ? p : 0.0f;
        m_next = (m_next + 1) % kFrameSamples;
        if (m_count < kFrameSamples) ++m_count;
        if (m_count != kFrameSamples) continue;
        if (m_skip) { --m_skip; continue; }
        inspect();
    }
}

void AdsbDecoder::inspect() noexcept
{
    auto p = [this](std::size_t i) { return m_power[(m_next + i) % kFrameSamples]; };
    // Four half-microsecond preamble pulses, at 0, 1, 3.5 and 4.5 us.
    const float low = std::max({p(1), p(3), p(4), p(5), p(6), p(8),
                                p(10), p(11), p(12), p(13), p(14), p(15)});
    const float high = std::min({p(0), p(2), p(7), p(9)});
    if (!(high > 1.0e-12f && high > low * 1.5f)) return;
    Frame frame{};
    for (std::size_t bit = 0; bit < 112; ++bit) {
        const float a = p(16 + bit * 2), b = p(17 + bit * 2);
        // Reject ambiguous symbols instead of manufacturing bits from noise.
        if (std::max(a, b) < high * 0.15f || std::max(a, b) <= std::min(a, b) * 1.1f)
            return;
        frame[bit / 8] |= static_cast<std::uint8_t>((a > b ? 1U : 0U) << (7 - bit % 8));
    }
    if (!validFrame(frame)) return;
    if (m_callback) m_callback(m_context, frame);
    // Avoid reporting the same waveform at nearby sample offsets.
    m_skip = kFrameSamples - 1;
}
} // namespace dsdr::dsp
