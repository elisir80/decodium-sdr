// SPDX-License-Identifier: GPL-3.0-or-later
#include "dsp/AprsAfskDecoder.h"
#include <algorithm>
#include <cmath>

namespace dsdr::dsp {
namespace {
constexpr double pi = 3.14159265358979323846;
constexpr std::size_t maxBits = 4096; // bounded even on malformed input
}
AprsAfskDecoder::AprsAfskDecoder(Callback cb, void *context)
    : m_callback(cb), m_context(context) {}

bool AprsAfskDecoder::configure(double rate) {
    if (!std::isfinite(rate) || rate < 8000 || rate > 192000) {
        m_rate = 0; m_ring.clear(); reset(); return false;
    }
    m_rate = rate; m_period = rate / 1200.0;
    m_window = static_cast<std::size_t>(std::lround(m_period));
    m_ring.assign(m_window, {}); reset(); return true;
}
void AprsAfskDecoder::reset() {
    std::fill(m_ring.begin(), m_ring.end(), std::array<double,4>{});
    m_sum = {}; m_index = m_filled = 0; m_sample = m_lastDelivery = 0;
    m_phaseMark = m_phaseSpace = 0; m_lastFrame.clear();
    for (std::size_t i = 0; i < m_branches.size(); ++i) {
        m_branches[i] = Branch{};
        m_branches[i].next = m_window + m_period * i / m_branches.size();
    }
}
std::uint16_t AprsAfskDecoder::fcs(const std::uint8_t *data, std::size_t size) noexcept {
    std::uint16_t crc = 0xffff;
    for (std::size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b)
            crc = static_cast<std::uint16_t>((crc >> 1) ^ ((crc & 1) ? 0x8408 : 0));
    }
    return static_cast<std::uint16_t>(crc ^ 0xffff);
}
bool AprsAfskDecoder::validFrame(const Frame &frame) noexcept {
    // Destination and source addresses, control, PID, and FCS.
    if (frame.size() < 18 || frame.size() > 330) return false;
    const auto crc = fcs(frame.data(), frame.size() - 2);
    if (frame[frame.size()-2] != (crc & 0xff) || frame.back() != (crc >> 8)) return false;
    std::size_t pos = 0, addresses = 0;
    do {
        if (pos + 7 > frame.size() - 4 || addresses == 10) return false;
        for (int i = 0; i < 6; ++i) {
            const auto c = frame[pos+i];
            if ((c & 1) || (c >> 1) < 0x20 || (c >> 1) > 0x7e) return false;
        }
        ++addresses; pos += 7;
    } while (!(frame[pos-1] & 1));
    return addresses >= 2 && pos + 2 <= frame.size() - 2 &&
           (frame[pos] & 0xef) == 0x03 && frame[pos+1] == 0xf0;
}
void AprsAfskDecoder::process(const float *samples, std::size_t count) {
    if (!m_rate || !samples) return;
    const double markStep = 2*pi*1200/m_rate, spaceStep = 2*pi*2200/m_rate;
    for (std::size_t n = 0; n < count; ++n) {
        const double x = std::isfinite(samples[n]) ? samples[n] : 0;
        const std::array<double,4> v{x*std::cos(m_phaseMark), x*std::sin(m_phaseMark),
                                     x*std::cos(m_phaseSpace), x*std::sin(m_phaseSpace)};
        m_phaseMark += markStep; if (m_phaseMark >= 2*pi) m_phaseMark -= 2*pi;
        m_phaseSpace += spaceStep; if (m_phaseSpace >= 2*pi) m_phaseSpace -= 2*pi;
        for (int j = 0; j < 4; ++j) m_sum[j] += v[j] - m_ring[m_index][j];
        m_ring[m_index] = v; m_index = (m_index + 1) % m_window;
        if (m_filled < m_window) ++m_filled;
        ++m_sample;
        if (m_filled < m_window) continue;
        const bool tone = m_sum[0]*m_sum[0] + m_sum[1]*m_sum[1] >
                          m_sum[2]*m_sum[2] + m_sum[3]*m_sum[3];
        for (auto &branch : m_branches) {
            if (static_cast<double>(m_sample) < branch.next) continue;
            branch.next += m_period;
            if (branch.haveTone) bit(branch, tone == branch.tone);
            branch.tone = tone; branch.haveTone = true;
        }
    }
}
void AprsAfskDecoder::bit(Branch &b, bool value) {
    b.shift = static_cast<std::uint8_t>((b.shift >> 1) | (value ? 0x80 : 0));
    if (b.seen < 8) ++b.seen;
    if (b.inFrame) b.bits.push_back(value);
    if (b.seen == 8 && b.shift == 0x7e) {
        if (b.inFrame && b.bits.size() >= 8) {
            b.bits.resize(b.bits.size()-8);
            finish(b.bits);
        }
        b.bits.clear(); b.inFrame = true;
    } else if (b.bits.size() > maxBits) {
        b.bits.clear(); b.inFrame = false;
    }
}
void AprsAfskDecoder::finish(const std::vector<std::uint8_t> &bits) {
    Frame frame; unsigned ones = 0, bitIndex = 0; std::uint8_t byte = 0;
    for (const auto bit : bits) {
        if (bit) {
            if (++ones > 5) return;
        } else {
            if (ones == 5) { ones = 0; continue; }
            ones = 0;
        }
        byte |= static_cast<std::uint8_t>(bit << bitIndex);
        if (++bitIndex == 8) { frame.push_back(byte); byte = 0; bitIndex = 0; }
    }
    if (bitIndex || ones == 5 || !validFrame(frame)) return;
    frame.resize(frame.size()-2);
    // Several timing hypotheses can decode the same on-air frame.
    if (frame == m_lastFrame && m_sample - m_lastDelivery < m_rate * 0.02) return;
    m_lastFrame = frame; m_lastDelivery = m_sample;
    if (m_callback) m_callback(m_context, frame);
}
} // namespace dsdr::dsp
