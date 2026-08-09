// SPDX-License-Identifier: GPL-3.0-or-later
#include "dsp/TimeShiftBuffer.h"

#include <algorithm>
#include <cstring>

namespace dsdr::dsp {

void TimeShiftBuffer::configure(std::size_t frames)
{
    m_capacity = frames;
    m_data.assign(frames * 2, 0.0f);
    m_head = 0;
    m_filled = 0;
}

void TimeShiftBuffer::clear()
{
    m_head = 0;
    m_filled = 0;
}

void TimeShiftBuffer::write(const float *interleaved, std::size_t frames)
{
    if (m_capacity == 0 || interleaved == nullptr || frames == 0)
        return;

    // Un blocco più grande dell'anello riempirebbe due volte le stesse celle:
    // si tiene la coda, cioè la parte più recente, che è l'unica ancora utile.
    if (frames > m_capacity) {
        interleaved += (frames - m_capacity) * 2;
        frames = m_capacity;
    }

    const std::size_t untilEnd = std::min(frames, m_capacity - m_head);
    std::memcpy(m_data.data() + m_head * 2, interleaved, untilEnd * 2 * sizeof(float));
    if (untilEnd < frames) {
        std::memcpy(m_data.data(), interleaved + untilEnd * 2,
                    (frames - untilEnd) * 2 * sizeof(float));
    }

    m_head = (m_head + frames) % m_capacity;
    m_filled = std::min(m_capacity, m_filled + frames);
}

std::size_t TimeShiftBuffer::clampDelay(std::size_t delayFrames,
                                        std::size_t frames) const noexcept
{
    if (m_capacity == 0)
        return 0;

    // Non si legge più indietro di quanto sia stato scritto, e la finestra che
    // si sta per leggere deve stare tutta dentro la storia: chiedere gli
    // ultimi mille campioni a partire da un punto che ne ha solo cento
    // davanti significherebbe leggere celle mai scritte.
    const std::size_t deepest = m_filled > frames ? m_filled - frames : 0;
    return std::min(delayFrames, deepest);
}

std::size_t TimeShiftBuffer::readDelayed(std::size_t delayFrames, float *out,
                                         std::size_t frames) const
{
    if (m_capacity == 0 || out == nullptr || frames == 0)
        return 0;

    const std::size_t wanted = std::min(frames, m_filled);
    if (wanted == 0)
        return 0;

    const std::size_t delay = clampDelay(delayFrames, wanted);

    // La finestra comincia `wanted + delay` campioni prima della testa: la
    // testa è la prossima cella da scrivere, non l'ultima scritta.
    const std::size_t back = wanted + delay;
    const std::size_t start = (m_head + m_capacity - (back % m_capacity)) % m_capacity;

    const std::size_t untilEnd = std::min(wanted, m_capacity - start);
    std::memcpy(out, m_data.data() + start * 2, untilEnd * 2 * sizeof(float));
    if (untilEnd < wanted) {
        std::memcpy(out + untilEnd * 2, m_data.data(),
                    (wanted - untilEnd) * 2 * sizeof(float));
    }

    return wanted;
}

} // namespace dsdr::dsp
