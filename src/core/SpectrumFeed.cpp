// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/SpectrumFeed.h"

#include <algorithm>

namespace dsdr::core {

namespace {
/// Profondità del ring in righe: copre ~2 s di waterfall a 25 righe/s, più che
/// sufficiente perché un frame lungo della UI non buchi la storia.
constexpr int kRingRows = 64;
} // namespace

// ── Perché la media si fa in decibel ─────────────────────────────────────────
//
// Le trasformate arrivano già in dBFS, e mediarle così com'sono è ciò che sugli
// analizzatori di spettro si chiama media video. Costa una somma e una
// divisione per bin — niente esponenziali, niente logaritmi, niente buffer in
// più — e ha una proprietà che qui torna comoda: sul rumore la media
// logaritmica converge circa 2,5 dB sotto la media di potenza, mentre su una
// portante, la cui potenza non fluttua, le due coincidono. Il fondo scende
// rispetto ai segnali invece di salire con loro, ed è esattamente il verso in
// cui vogliamo sbagliare: i segnali deboli emergono.
//
// Il prezzo, dichiarato: con N trasformate per riga il waterfall produce N
// volte meno righe al secondo. La storia visibile si allunga della stessa
// proporzione, e siccome `PanadapterView` il ritmo lo *misura* invece di
// supporlo, l'asse dei tempi si riadegua da solo.

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
        // L'accumulatore della media nasce qui, con il ring: è l'unico punto in
        // cui è lecito allocare, e da qui in poi `publish()` si limita a
        // sommarci dentro.
        m_accumulator.assign(static_cast<std::size_t>(std::max(binCount, 0)), 0.0f);
        m_accumulated = 0;
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
    const int wanted = std::clamp(m_averaging.load(std::memory_order_relaxed), 1, kMaxAveraging);

    // Senza media, o se per qualche motivo l'accumulatore non corrisponde alla
    // geometria corrente, la trasformata passa dritta: meglio una riga che
    // sfarfalla di una riga che non esce.
    if (wanted <= 1 || m_accumulator.size() != rowFloats) {
        m_accumulated = 0;
        pushRow(magnitudesDb, rowFloats);
        return;
    }

    if (m_accumulated == 0)
        std::copy_n(magnitudesDb, bins, m_accumulator.begin());
    else
        for (int bin = 0; bin < bins; ++bin)
            m_accumulator[static_cast<std::size_t>(bin)] += magnitudesDb[bin];
    ++m_accumulated;

    // `wanted` può essere sceso mentre accumulavamo: il confronto è "abbastanza
    // trasformate", non "esattamente quelle previste all'inizio".
    if (m_accumulated < wanted)
        return;

    const float scale = 1.0f / static_cast<float>(m_accumulated);
    for (float &value : m_accumulator)
        value *= scale;

    pushRow(m_accumulator.data(), rowFloats);
    m_accumulated = 0;
}

void SpectrumFeed::pushRow(const float *row, std::size_t rowFloats)
{
    // Se non c'è spazio scartiamo la riga più vecchia: in un waterfall la
    // continuità visiva vale meno della freschezza.
    if (m_ring->space() < rowFloats)
        m_ring->discard(rowFloats);

    m_ring->write(row, rowFloats);
    emit framesAvailable();
}

void SpectrumFeed::setAveraging(int frames)
{
    frames = std::clamp(frames, 1, kMaxAveraging);
    if (m_averaging.exchange(frames, std::memory_order_relaxed) == frames)
        return;

    // Niente da azzerare da qui: l'accumulo appartiene al thread produttore, e
    // toccarlo di là sarebbe la corsa che questo ring esiste per evitare. Alla
    // prossima trasformata `publish()` legge il valore nuovo e si adegua.
    emit averagingChanged();
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
