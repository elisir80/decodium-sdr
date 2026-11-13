// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Vdl2Offline.h"
#include <functional>
#include <vector>
namespace dsdr::vdl2 {
// Experimental worker-thread receiver for channel-filtered IQ. Fixed-size
// overlapping windows bound memory; configure/reset/feed must be serialized.
// Not suitable for bursts longer than the overlap or unattended RF yet.
class WindowStream {
public:
    using Callback=std::function<void(const OfflineBurst&)>;
    WindowStream(double rate,double minimumMeanPower,Callback callback);
    void reset() noexcept;
    void feed(const std::complex<float>* iq,std::size_t count);
private:
    static constexpr std::size_t windowSize=32768, hopSize=16384;
    double m_rate,m_floor;
    Callback m_callback;
    std::vector<std::complex<float>> m_samples;
    std::uint64_t m_base=0,m_lastStart=0;
    bool m_haveLast=false;
};
}
