// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <string>
#include <array>

namespace dsdr::dsp {
// Experimental fixed-speed VOR identifier detector, 48 kHz composite audio.
// Not suitable for navigation; a decoded label is not an authenticated ID.
class VorIdentDecoder {
public:
    void reset();
    void process(const float *samples, std::size_t count);
    const std::string &identifier() const noexcept { return m_identifier; }
private:
    void block();
    void finishMark();
    void finishCharacter();
    double m_i=0,m_q=0,m_energy=0,m_phase=0,m_dc=0;
    std::array<double,1440> m_iHistory{},m_qHistory{};
    std::size_t m_historyIndex=0,m_historyCount=0;
    unsigned m_samples=0,m_onBlocks=0,m_offBlocks=0;
    bool m_keyed=false,m_characterEmitted=false;
    std::string m_code,m_identifier;
};
}
