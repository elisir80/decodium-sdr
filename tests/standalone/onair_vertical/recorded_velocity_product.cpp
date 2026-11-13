// SPDX-License-Identifier: GPL-3.0-or-later
// Emits only velocity fields, never aircraft identifiers.
#include "core/AdsbMessage.h"
#include <cstdio>
#include <fstream>
#include <limits>
int main(int argc,char** argv) {
    if(argc!=2) return 2;
    std::ifstream in(argv[1],std::ios::binary); if(!in) return 2;
    dsdr::dsp::AdsbDecoder::Frame frame{};
    while(in.read(reinterpret_cast<char*>(frame.data()),frame.size())) {
        const auto message=dsdr::core::AdsbMessage::decode(frame);
        const double nan=std::numeric_limits<double>::quiet_NaN();
        std::printf("%d,%.8f,%.8f,%d,%.8f,%.8f,%d\n",message.typeCode,
                    message.groundSpeedKnots.value_or(nan),message.groundTrackDegrees.value_or(nan),
                    message.verticalRateFeetPerMinute.value_or(-999999),message.airspeedKnots.value_or(nan),
                    message.magneticHeadingDegrees.value_or(nan),message.trueAirspeed ? (*message.trueAirspeed?1:0) : -1);
    }
    return in.eof() && in.gcount()==0 ? 0 : 2;
}
