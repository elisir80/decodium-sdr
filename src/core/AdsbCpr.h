// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "core/AdsbMessage.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <map>
#include <optional>

namespace dsdr::core {
// Global airborne CPR only. Timestamps are monotonic reception times.
class AdsbCpr {
public:
    using Clock = std::chrono::steady_clock;
    struct Position { double latitude, longitude; Clock::time_point timestamp; };
    void clear() { m_aircraft.clear(); }
    std::optional<Position> update(const dsp::AdsbDecoder::Frame &f, Clock::time_point t) {
        const auto msg = AdsbMessage::decode(f);
        if (msg.icao.isEmpty() || !((msg.typeCode >= 9 && msg.typeCode <= 18) ||
                                    (msg.typeCode >= 20 && msg.typeCode <= 22))) return {};
        const unsigned address = (unsigned(f[1]) << 16) | (unsigned(f[2]) << 8) | f[3];
        for (auto it = m_aircraft.begin(); it != m_aircraft.end();) {
            if (t >= it->second.last && t - it->second.last > std::chrono::seconds(10)) it = m_aircraft.erase(it);
            else ++it;
        }
        if (m_aircraft.size() >= 1024 && !m_aircraft.count(address)) {
            auto oldest = std::min_element(m_aircraft.begin(), m_aircraft.end(), [](const auto &a, const auto &b) { return a.second.last < b.second.last; });
            m_aircraft.erase(oldest);
        }
        auto &pair = m_aircraft[address];
        if (pair.present && t < pair.last) return {}; // never replace newer data with delayed data
        pair.present = true; pair.last = t;
        const int odd = (f[6] >> 2) & 1;
        Sample s{unsigned((f[6] & 3) << 15 | f[7] << 7 | f[8] >> 1),
                 unsigned((f[8] & 1) << 16 | f[9] << 8 | f[10]), t};
        pair.samples[odd] = s;
        if (!pair.samples[0] || !pair.samples[1]) return {};
        const auto &e = *pair.samples[0], &o = *pair.samples[1];
        if (t - e.time > std::chrono::seconds(10) || t - o.time > std::chrono::seconds(10)) return {};
        constexpr double scale = 131072.0;
        const double ye=e.lat/scale, yo=o.lat/scale, xe=e.lon/scale, xo=o.lon/scale;
        const int j=int(std::floor(59*ye-60*yo+0.5));
        double le=6.0*(mod(j,60)+ye), lo=(360.0/59)*(mod(j,59)+yo);
        if (le>=270) le-=360;
        if (lo>=270) lo-=360;
        if (std::abs(le)>90 || std::abs(lo)>90 || nl(le)!=nl(lo)) return {};
        const bool useOdd = o.time > e.time;
        const double lat=useOdd ? lo : le;
        const int zones=nl(lat), n=std::max(zones-int(useOdd),1);
        const int m=int(std::floor(xe*(zones-1)-xo*zones+0.5));
        double lon=(360.0/n)*(mod(m,n)+(useOdd ? xo : xe));
        if (lon>180) lon-=360;
        return Position{lat,lon,useOdd ? o.time : e.time};
    }
private:
    struct Sample { unsigned lat, lon; Clock::time_point time; };
    struct Pair { std::array<std::optional<Sample>,2> samples; Clock::time_point last{}; bool present=false; };
    std::map<unsigned,Pair> m_aircraft;
    static int mod(int a,int b) { return (a%b+b)%b; }
    static int nl(double lat) {
        constexpr double pi=3.14159265358979323846;
        lat=std::abs(lat);
        if (lat>=87) return lat==87 ? 2 : 1;
        const double c=std::cos(lat*pi/180);
        return int(std::floor(2*pi/std::acos(1-(1-std::cos(pi/30))/(c*c))));
    }
};
}
