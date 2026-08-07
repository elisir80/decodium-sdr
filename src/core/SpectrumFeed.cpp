// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/SpectrumFeed.h"

#include <algorithm>

namespace dsdr::core {

namespace {
/// Profondità del ring in righe: copre ~2 s di waterfall a 25 righe/s, più che
/// sufficiente perché un frame lungo della UI non buchi la storia.
constexpr int kRingRows = 64;
} // namespace

SpectrumFeed::SpectrumFeed(QObject *parent)
    : QObject(parent)
{
}

void SpectrumFeed::configure(int binCount, double spanHz, qint64 centerFrequencyHz)
{
    const bool geometryDiffers = binCount != m_binCount.load(std::memory_order_acquire);

    if (geometryDiffers) {
        m_ring = std::make_unique<dsp::SpscRing<float>>(
            static_cast<std::size_t>(binCount) * kRingRows);
        m_binCount.store(binCount, std::memory_order_release);
    }

    m_spanHz.store(spanHz, std::memory_order_release);
    m_centerHz.store(centerFrequencyHz, std::memory_order_release);
    m_generation.fetch_add(1, std::memory_order_acq_rel);

    emit geometryChanged();
}

void SpectrumFeed::publish(const float *magnitudesDb)
{
    const int bins = m_binCount.load(std::memory_order_acquire);
    if (bins <= 0 || !m_ring)
        return;

    const std::size_t rowFloats = static_cast<std::size_t>(bins);

    // Se non c'è spazio scartiamo la riga più vecchia: in un waterfall la
    // continuità visiva vale meno della freschezza.
    if (m_ring->space() < rowFloats)
        m_ring->discard(rowFloats);

    m_ring->write(magnitudesDb, rowFloats);
    emit framesAvailable();
}

int SpectrumFeed::fetchRows(std::vector<float> &out, int maxRows)
{
    const int bins = m_binCount.load(std::memory_order_acquire);
    if (bins <= 0 || !m_ring || maxRows <= 0)
        return 0;

    const std::size_t rowFloats = static_cast<std::size_t>(bins);
    const int pending = static_cast<int>(m_ring->available() / rowFloats);
    const int rows = std::min(pending, maxRows);
    if (rows <= 0)
        return 0;

    out.resize(rowFloats * static_cast<std::size_t>(rows));
    const std::size_t got = m_ring->read(out.data(), out.size());
    return static_cast<int>(got / rowFloats);
}

void SpectrumFeed::setLevelRange(float floorDb, float ceilingDb)
{
    if (ceilingDb <= floorDb + 5.0f)
        ceilingDb = floorDb + 5.0f;

    const bool changed = m_floorDb.load(std::memory_order_relaxed) != floorDb
        || m_ceilingDb.load(std::memory_order_relaxed) != ceilingDb;

    m_floorDb.store(floorDb, std::memory_order_relaxed);
    m_ceilingDb.store(ceilingDb, std::memory_order_relaxed);

    if (changed)
        emit levelRangeChanged();
}

} // namespace dsdr::core
