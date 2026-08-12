// SPDX-License-Identifier: GPL-3.0-or-later
#include "dsp/VoiceRecorder.h"

#include <algorithm>
#include <cmath>

namespace dsdr::dsp {

void VoiceRecorder::configure(double sampleRate, double seconds)
{
    if (sampleRate <= 0.0 || seconds <= 0.0) {
        m_dry.clear();
        m_wet.clear();
        m_capacity = 0;
        m_rate = 0.0;
        m_seconds = 0.0;
        reset();
        return;
    }

    m_rate = sampleRate;
    m_seconds = seconds;
    m_capacity = static_cast<std::size_t>(std::llround(sampleRate * seconds));

    // Le due tracce nascono qui e non crescono più: da qui in poi si scrive
    // dentro un thread che non può permettersi di allocare (CONSTITUTION §5).
    m_dry.assign(m_capacity, 0.0f);
    m_wet.assign(m_capacity, 0.0f);
    reset();
}

void VoiceRecorder::reset() noexcept
{
    m_write = 0;
    m_filled.store(0, std::memory_order_release);
    m_position.store(0, std::memory_order_relaxed);
    m_playing.store(false, std::memory_order_release);
}

std::size_t VoiceRecorder::oldest() const noexcept
{
    const std::size_t filled = m_filled.load(std::memory_order_acquire);
    if (m_capacity == 0)
        return 0;
    return (m_write + m_capacity - filled) % m_capacity;
}

void VoiceRecorder::record(const float *dry, const float *wet, std::size_t count) noexcept
{
    if (m_capacity == 0 || count == 0)
        return;

    // Chi torna a parlare smette di riascoltarsi. Scrivere sotto la testina di
    // lettura non darebbe un errore: darebbe un riascolto cucito con due prese
    // diverse, che è il modo peggiore di sbagliare perché sembra funzionare.
    m_playing.store(false, std::memory_order_release);

    // Un blocco più lungo della memoria non ha senso di essere copiato per
    // intero: di quello che entra resterebbe solo la coda.
    if (count > m_capacity) {
        const std::size_t skip = count - m_capacity;
        if (dry)
            dry += skip;
        if (wet)
            wet += skip;
        count = m_capacity;
    }

    std::size_t written = 0;
    while (written < count) {
        const std::size_t chunk = std::min(count - written, m_capacity - m_write);
        if (dry)
            std::copy_n(dry + written, chunk, m_dry.begin()
                            + static_cast<std::ptrdiff_t>(m_write));
        else
            std::fill_n(m_dry.begin() + static_cast<std::ptrdiff_t>(m_write), chunk, 0.0f);

        if (wet)
            std::copy_n(wet + written, chunk, m_wet.begin()
                            + static_cast<std::ptrdiff_t>(m_write));
        else
            std::fill_n(m_wet.begin() + static_cast<std::ptrdiff_t>(m_write), chunk, 0.0f);

        m_write = (m_write + chunk) % m_capacity;
        written += chunk;
    }

    const std::size_t filled = m_filled.load(std::memory_order_relaxed);
    m_filled.store(std::min(m_capacity, filled + count), std::memory_order_release);
}

bool VoiceRecorder::startPlayback(Source source) noexcept
{
    if (m_filled.load(std::memory_order_acquire) == 0)
        return false;

    m_source.store(static_cast<int>(source), std::memory_order_relaxed);
    m_position.store(0, std::memory_order_relaxed);
    m_playing.store(true, std::memory_order_release);
    return true;
}

void VoiceRecorder::stopPlayback() noexcept
{
    m_playing.store(false, std::memory_order_release);
}

void VoiceRecorder::setPlaybackSource(Source source) noexcept
{
    // Il punto non si tocca: è tutto il senso di questo metodo. Commutare e
    // ripartire da capo costringerebbe a ricordarsi com'era la sillaba di
    // prima, e il ricordo di un suono dura meno di un secondo.
    m_source.store(static_cast<int>(source), std::memory_order_relaxed);
}

std::size_t VoiceRecorder::pull(float *out, std::size_t count) noexcept
{
    if (!out || count == 0 || !m_playing.load(std::memory_order_acquire))
        return 0;

    const std::size_t filled = m_filled.load(std::memory_order_acquire);
    std::size_t position = m_position.load(std::memory_order_relaxed);
    if (position >= filled) {
        m_playing.store(false, std::memory_order_release);
        return 0;
    }

    const auto source = static_cast<Source>(m_source.load(std::memory_order_relaxed));
    const std::vector<float> &track = source == Source::Dry ? m_dry : m_wet;

    const std::size_t wanted = std::min(count, filled - position);
    const std::size_t start = oldest();

    std::size_t done = 0;
    while (done < wanted) {
        const std::size_t index = (start + position + done) % m_capacity;
        const std::size_t chunk = std::min(wanted - done, m_capacity - index);
        std::copy_n(track.begin() + static_cast<std::ptrdiff_t>(index), chunk,
                    out + done);
        done += chunk;
    }

    position += done;
    m_position.store(position, std::memory_order_relaxed);
    if (position >= filled)
        m_playing.store(false, std::memory_order_release);

    return done;
}

} // namespace dsdr::dsp
