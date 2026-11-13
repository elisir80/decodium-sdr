// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "NavtexProtocol.h"
#include <array>
#include <cstddef>
#include <functional>
namespace dsdr::navtex {
// 24 kHz mono audio; 100 baud, 170 Hz shift, fixed 1000 Hz centre.
// Called by one worker; it never opens an audio device or changes radio tuning.
class NavtexAudioDecoder {
public:
    using Callback=std::function<void(const NavtexMessage&)>;
    explicit NavtexAudioDecoder(Callback);
    void process(const float*,std::size_t);
    void reset();
private:
    struct IQ { double i=0,q=0; };
    Callback m_callback;
    NavtexDecoder m_normal{false},m_inverted{true};
    std::array<IQ,120> m_high{},m_low{}; IQ m_highSum{},m_lowSum{};
    std::size_t m_index=0,m_count=0;
    double m_highPhase=0,m_lowPhase=0,m_clock=0,m_smooth=0;
    int m_lastSign=0;
};
}
