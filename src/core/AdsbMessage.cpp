// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/AdsbMessage.h"
#include <cstdint>
#include <cmath>

namespace dsdr::core {
AdsbMessage AdsbMessage::decode(const dsp::AdsbDecoder::Frame &frame)
{
    AdsbMessage result;
    if (!dsp::AdsbDecoder::validFrame(frame)) return result;
    const int df = frame[0] >> 3;
    // DF18 control fields other than zero may use non-ICAO addresses or
    // different payload formats. Do not label or interpret them as DF17.
    if (df == 18 && (frame[0] & 7) != 0) return result;
    const auto address = (std::uint32_t(frame[1]) << 16) |
                         (std::uint32_t(frame[2]) << 8) | frame[3];
    result.icao = QStringLiteral("%1").arg(address, 6, 16, QLatin1Char('0')).toUpper();
    result.typeCode = frame[4] >> 3;
    if (result.typeCode >= 1 && result.typeCode <= 4) {
        QString callsign;
        // The eight 6-bit characters begin at ME bit 9 (frame bit 40).
        for (int i = 0; i < 8; ++i) {
            unsigned value = 0;
            for (int j = 0; j < 6; ++j) {
                const int bit = 40 + i * 6 + j;
                value = (value << 1) | ((frame[bit / 8] >> (7 - bit % 8)) & 1);
            }
            if (value >= 1 && value <= 26) callsign.append(QChar('A' + value - 1));
            else if (value >= 48 && value <= 57) callsign.append(QChar('0' + value - 48));
            else if (value == 32) callsign.append(QLatin1Char(' '));
            else return result; // reserved character: don't invent an identification
        }
        result.callsign = callsign.trimmed();
    }
    // TC 9-18 carry barometric altitude; TC 20-22 carry GNSS height.
    if (result.typeCode >= 9 && result.typeCode <= 18) {
        const unsigned ac = (unsigned(frame[5]) << 4) | (frame[6] >> 4);
        // Zero is unavailable; Q=0 uses the 100-foot Gillham code.
        if (ac != 0 && (ac & 0x10)) {
            const unsigned n = ((ac & 0xfe0) >> 1) | (ac & 0x0f);
            result.barometricAltitudeFeet = int(n) * 25 - 1000;
        } else if (ac != 0) {
            // Insert the absent M bit to recover the Mode S 13-bit layout.
            const unsigned a = ((ac & 0xfc0U) << 1) | (ac & 0x3fU);
            auto bit = [a](int pos) { return (a >> (12 - pos)) & 1U; };
            const unsigned gray500 = (bit(10) << 7) | (bit(12) << 6) |
                (bit(1) << 5) | (bit(3) << 4) | (bit(5) << 3) |
                (bit(7) << 2) | (bit(9) << 1) | bit(11);
            const unsigned gray100 = (bit(0) << 2) | (bit(2) << 1) | bit(4);
            auto ungray = [](unsigned n) {
                n ^= n >> 4; n ^= n >> 2; n ^= n >> 1; return n;
            };
            const unsigned n500 = ungray(gray500);
            unsigned n100 = ungray(gray100);
            if (n100 != 0 && n100 != 5 && n100 != 6) {
                if (n100 == 7) n100 = 5;
                if (n500 & 1U) n100 = 6 - n100;
                result.barometricAltitudeFeet = int(n500) * 500 + int(n100) * 100 - 1300;
            }
        }
    }
    if (result.typeCode == 19) {
        auto bits = [&frame](int start, int count) {
            unsigned value=0;
            for(int i=0;i<count;++i) value=(value<<1)|((frame[(start+i)/8]>>(7-(start+i)%8))&1U);
            return value;
        };
        const auto subtype=bits(37,3);
        if(subtype==1 || subtype==2) {
            const auto eastCode=bits(46,10), northCode=bits(57,10);
            if(eastCode && northCode) {
                const int factor=subtype==2?4:1;
                const int east=int(eastCode-1)*factor*(bits(45,1)?-1:1);
                const int north=int(northCode-1)*factor*(bits(56,1)?-1:1);
                result.groundSpeedKnots=std::hypot(double(east),double(north));
                if(east || north) {
                    constexpr double radiansToDegrees=57.29577951308232;
                    double track=std::atan2(double(east),double(north))*radiansToDegrees;
                    if(track<0) track+=360;
                    result.groundTrackDegrees=track;
                }
            }
        } else if(subtype==3 || subtype==4) {
            if(bits(45,1)) result.magneticHeadingDegrees=double(bits(46,10))*360.0/1024.0;
            const auto speedCode=bits(57,10);
            if(speedCode) {
                result.airspeedKnots=double(speedCode-1)*(subtype==4?4:1);
                result.trueAirspeed=bool(bits(56,1));
            }
        }
        if(subtype>=1 && subtype<=4) {
            const auto code=bits(69,9);
            if(code) result.verticalRateFeetPerMinute=int(code-1)*64*(bits(68,1)?-1:1);
        }
    }
    return result;
}
}
