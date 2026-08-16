// SPDX-License-Identifier: GPL-3.0-or-later
// Il CAT e' la sola fonte che dice se l'IF e' sicura. Finche' non abbiamo una
// lettura valida, il percorso software resta chiuso: fail closed.
#pragma once

namespace dsdr::hal::rtlcat {

inline bool shouldBlockRtlInput(bool catStateKnown, bool transmitting) noexcept
{
    return !catStateKnown || transmitting;
}

} // namespace dsdr::hal::rtlcat
