// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/AprsPosition.h"
#include <cassert>
#include <cmath>
#include <cstdint>
#include <QString>

using dsdr::core::parseAprsPosition;
int main() {
    // Deterministic malformed and truncated packet corpus. Keep assertions
    // enabled even in Release builds so CI validates the invariants.
    const QString seeds[] = {QStringLiteral("@121234z3554.00N/01430.00E>"),
                             QStringLiteral("/235959h/5L!!<*e7>7P["),
                             QStringLiteral(";TEST     *121234z3554.00N/01430.00E>"),
                             QStringLiteral(")WX1234!/5L!!<*e7>7P[")};
    std::uint32_t state=0x673915u;
    auto random=[&] { state=state*1664525u+1013904223u; return state; };
    for (int trial=0; trial<100000; ++trial) {
        QString input=seeds[random()%4];
        const int edits=1+int(random()%5);
        for (int i=0; i<edits; ++i) {
            if (input.isEmpty()) break;
            if (random()%3==0) input.truncate(int(random()%unsigned(input.size()+1)));
            else input[int(random()%unsigned(input.size()))]=QChar(ushort(random()%256));
        }
        const auto position=parseAprsPosition(QStringView(input));
        if(position && (!std::isfinite(position->latitude) || !std::isfinite(position->longitude) ||
                        position->latitude < -90 || position->latitude > 90 ||
                        position->longitude < -180 || position->longitude > 180)) return 1;
    }
    for (const auto &seed:seeds) if(!parseAprsPosition(QStringView(seed))) return 2;
    return 0;
}
