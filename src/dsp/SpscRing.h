// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — ring buffer lock-free a produttore singolo / consumatore singolo.
//
// È il solo canale ammesso per i campioni fra thread (§4.1 nota normativa,
// CONSTITUTION §5): i signal Qt notificano la disponibilità, i dati passano
// di qui. Capacità sempre potenza di due; nessuna allocazione dopo `reset()`.
#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <memory>
#include <type_traits>

namespace dsdr::dsp {

template <typename T>
class SpscRing
{
    static_assert(std::is_trivially_copyable<T>::value,
                  "SpscRing ammette solo tipi trivially copyable");

public:
    SpscRing() = default;
    explicit SpscRing(std::size_t minimumCapacity) { reset(minimumCapacity); }

    SpscRing(const SpscRing &) = delete;
    SpscRing &operator=(const SpscRing &) = delete;

    /// Alloca (o rialloca) il buffer. NON è thread-safe: va chiamata prima di
    /// avviare i thread, oppure quando entrambi sono fermi.
    void reset(std::size_t minimumCapacity)
    {
        std::size_t cap = 1;
        while (cap < minimumCapacity)
            cap <<= 1;
        m_capacity = cap;
        m_mask = cap - 1;
        m_buffer = std::make_unique<T[]>(cap);
        m_write.store(0, std::memory_order_relaxed);
        m_read.store(0, std::memory_order_relaxed);
    }

    void clear() noexcept
    {
        m_read.store(m_write.load(std::memory_order_acquire), std::memory_order_release);
    }

    std::size_t capacity() const noexcept { return m_capacity; }

    std::size_t available() const noexcept
    {
        return m_write.load(std::memory_order_acquire) - m_read.load(std::memory_order_acquire);
    }

    std::size_t space() const noexcept { return m_capacity - available(); }

    /// Produttore. Scrive fino a `count` elementi, restituisce quanti ne ha
    /// scritti (meno di `count` se il ring è pieno: il chiamante decide se è
    /// un overrun da segnalare).
    std::size_t write(const T *src, std::size_t count) noexcept
    {
        const std::uint64_t w = m_write.load(std::memory_order_relaxed);
        const std::uint64_t r = m_read.load(std::memory_order_acquire);
        const std::size_t free = m_capacity - static_cast<std::size_t>(w - r);
        count = std::min(count, free);
        if (count == 0)
            return 0;

        const std::size_t head = static_cast<std::size_t>(w) & m_mask;
        const std::size_t first = std::min(count, m_capacity - head);
        std::memcpy(m_buffer.get() + head, src, first * sizeof(T));
        if (count > first)
            std::memcpy(m_buffer.get(), src + first, (count - first) * sizeof(T));

        m_write.store(w + count, std::memory_order_release);
        return count;
    }

    /// Consumatore. Legge fino a `count` elementi e li rimuove.
    std::size_t read(T *dst, std::size_t count) noexcept
    {
        const std::size_t got = peek(dst, count);
        if (got)
            m_read.store(m_read.load(std::memory_order_relaxed) + got, std::memory_order_release);
        return got;
    }

    /// Consumatore. Copia senza consumare.
    std::size_t peek(T *dst, std::size_t count) const noexcept
    {
        const std::uint64_t r = m_read.load(std::memory_order_relaxed);
        const std::uint64_t w = m_write.load(std::memory_order_acquire);
        count = std::min(count, static_cast<std::size_t>(w - r));
        if (count == 0)
            return 0;

        const std::size_t tail = static_cast<std::size_t>(r) & m_mask;
        const std::size_t first = std::min(count, m_capacity - tail);
        std::memcpy(dst, m_buffer.get() + tail, first * sizeof(T));
        if (count > first)
            std::memcpy(dst + first, m_buffer.get(), (count - first) * sizeof(T));
        return count;
    }

    /// Consumatore. Scarta `count` elementi (usato per recuperare da un
    /// consumatore in ritardo, es. lo spettro che salta i frame vecchi).
    std::size_t discard(std::size_t count) noexcept
    {
        const std::uint64_t r = m_read.load(std::memory_order_relaxed);
        const std::uint64_t w = m_write.load(std::memory_order_acquire);
        count = std::min(count, static_cast<std::size_t>(w - r));
        m_read.store(r + count, std::memory_order_release);
        return count;
    }

    /// Numero totale di elementi scritti da sempre: utile per rilevare
    /// discontinuità senza aggiungere un contatore separato.
    std::uint64_t totalWritten() const noexcept { return m_write.load(std::memory_order_acquire); }

private:
    std::unique_ptr<T[]> m_buffer;
    std::size_t m_capacity = 0;
    std::size_t m_mask = 0;

    alignas(64) std::atomic<std::uint64_t> m_write{0};
    alignas(64) std::atomic<std::uint64_t> m_read{0};
};

} // namespace dsdr::dsp
