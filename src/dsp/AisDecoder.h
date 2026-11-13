// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace dsdr::dsp {
// One narrowband AIS GMSK channel, complex baseband IQ at 9600 symbols/s.
// A/B channels require independent instances/tuning. Callback runs on the
// caller's thread; caller owns asynchronous dispatch, buffering and lifecycle.
class AisDecoder final {
public:
    struct Message {
        // AIS message bytes as specified in ITU tables, MSB-first within bytes;
        // HDLC flags, stuffing and FCS are removed.
        std::vector<std::uint8_t> payload;
        unsigned type = 0;
        std::uint32_t mmsi = 0;
        bool hasPosition = false;
        double latitude = 0, longitude = 0;
    };
    using Callback = void (*)(void *, const Message &);
    explicit AisDecoder(Callback callback, void *context = nullptr);
    // sampleRate 48k..384k. channelOffsetHz is relative to IQ centre.
    // Input must contain a filtered single AIS channel; no channel filter here.
    bool configure(double sampleRate, double channelOffsetHz = 0);
    void reset();
    void process(const std::complex<float> *iq, std::size_t count);
    static std::uint16_t fcs(const std::uint8_t *data, std::size_t size) noexcept;
private:
    struct Branch {
        double next = 0;
        bool previous = false, havePrevious = false, inFrame = false;
        unsigned seen = 0;
        std::uint8_t shift = 0;
        std::vector<std::uint8_t> bits;
    };
    void bit(Branch &branch, bool value);
    void finish(const std::vector<std::uint8_t> &bits);
    Callback m_callback;
    void *m_context;
    double m_rate = 0, m_period = 0, m_offsetStep = 0;
    std::complex<float> m_previous{};
    bool m_havePrevious = false;
    std::uint64_t m_sample = 0, m_lastDelivery = 0;
    std::vector<double> m_ring;
    std::size_t m_index = 0, m_filled = 0;
    double m_sum = 0;
    std::array<Branch, 16> m_branches;
    std::vector<std::uint8_t> m_lastPayload;
};
}
