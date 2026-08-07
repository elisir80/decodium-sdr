// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — ponte fra il DSP Engine e il panadattatore.
//
// Il produttore è il thread DSP, il consumatore è il render thread della scena
// QML. Il passaggio avviene su un ring SPSC di righe complete: il thread DSP
// non prende mai un lock (CONSTITUTION §5) e il render thread consuma tutte le
// righe accumulate, così il waterfall non perde storia se un frame salta.
#pragma once

#include "dsp/SpscRing.h"

#include <QObject>

#include <atomic>
#include <memory>
#include <vector>

namespace dsdr::core {

class SpectrumFeed : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int binCount READ binCount NOTIFY geometryChanged)
    Q_PROPERTY(qint64 centerFrequency READ centerFrequency NOTIFY geometryChanged)
    Q_PROPERTY(double spanHz READ spanHz NOTIFY geometryChanged)

public:
    explicit SpectrumFeed(QObject *parent = nullptr);

    // ── Lato produttore (thread DSP) ─────────────────────────────────────

    /// Riconfigura la geometria. Chiamata dal thread DSP quando cambia device
    /// o frequenza di campionamento; il consumatore se ne accorge al fetch.
    void configure(int binCount, double spanHz, qint64 centerFrequencyHz);

    /// Pubblica una riga di spettro (dBFS, già in ordine di frequenza).
    /// Non blocca: se il consumatore è in ritardo la riga più vecchia viene
    /// scartata, perché per un waterfall la riga fresca vale più di quella persa.
    void publish(const float *magnitudesDb);

    // ── Lato consumatore (render thread) ─────────────────────────────────

    /// Estrae fino a `maxRows` righe complete in `out` (righe consecutive di
    /// `binCount` valori). Restituisce il numero di righe estratte.
    int fetchRows(std::vector<float> &out, int maxRows);

    int binCount() const noexcept { return m_binCount.load(std::memory_order_acquire); }
    qint64 centerFrequency() const noexcept { return m_centerHz.load(std::memory_order_acquire); }
    double spanHz() const noexcept { return m_spanHz.load(std::memory_order_acquire); }

    /// Numero di generazione della geometria: se cambia, il consumatore deve
    /// ributtare via le texture e ricostruirle.
    quint32 generation() const noexcept { return m_generation.load(std::memory_order_acquire); }

    /// Livelli di riferimento del waterfall, in dBFS.
    float floorDb() const noexcept { return m_floorDb.load(std::memory_order_relaxed); }
    float ceilingDb() const noexcept { return m_ceilingDb.load(std::memory_order_relaxed); }
    void setLevelRange(float floorDb, float ceilingDb);

signals:
    void geometryChanged();
    void levelRangeChanged();
    /// Emesso (dal thread DSP) quando c'è almeno una riga nuova da consumare.
    void framesAvailable();

private:
    std::unique_ptr<dsp::SpscRing<float>> m_ring;
    std::atomic<int> m_binCount{0};
    std::atomic<qint64> m_centerHz{0};
    std::atomic<double> m_spanHz{0.0};
    std::atomic<quint32> m_generation{0};
    std::atomic<float> m_floorDb{-130.0f};
    std::atomic<float> m_ceilingDb{-20.0f};
};

} // namespace dsdr::core
