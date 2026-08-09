// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — riduzione di rumore spettrale, famiglia MMSE-STSA
// (DSDR-SPEC-003 §6).
//
// È lo stato dell'arte non neurale, e la differenza con un predittore
// adattivo si sente: il predittore decide *nel tempo* che cosa è segnale —
// e quindi conserva ciò che è periodico, colorando la voce — mentre qui la
// decisione è *per frequenza*. Ogni riga dello spettro riceve il guadagno che
// merita in base a quanto emerge dal rumore stimato in quella riga.
//
// Implementazione originale dalla letteratura: Ephraim & Malah (1984/85) per
// il guadagno log-spettrale e il *decision-directed*, Cohen (2002) per la
// stima del rumore a minima statistica ricorsiva. Nessun codice derivato.
//
// Il compromesso di ogni NR spettrale sono gli **artefatti musicali**: se il
// guadagno può scendere a zero, i bin isolati che sopravvivono al rumore
// diventano campanellini. Per questo il fondo del guadagno è limitato — ed è
// l'unico parametro esposto, perché è l'unico che l'operatore giudica a
// orecchio: sotto, si sente meno rumore e più campanelli.
//
// Latenza: una finestra, circa 11 ms a 48 kHz (RNF-03).
#pragma once

#include <cstddef>
#include <vector>

struct fftwf_plan_s;

namespace dsdr::dsp {

class SpectralDenoiser
{
public:
    SpectralDenoiser() = default;
    ~SpectralDenoiser();

    SpectralDenoiser(const SpectralDenoiser &) = delete;
    SpectralDenoiser &operator=(const SpectralDenoiser &) = delete;

    /// Prepara le trasformate e i buffer. `frameSize` è la finestra di
    /// analisi: 512 per la voce, 1024 per la CW, che non ha la struttura del
    /// parlato e tollera una risoluzione più fine in cambio di più ritardo.
    bool configure(double sampleRate, int frameSize = 512);

    void reset() noexcept;

    /// Quanto togliere, da 0 a 10 — l'unico comando esposto.
    ///
    /// Mappa il fondo del guadagno da 0 dB (inerte) a −25 dB. Non si espone la
    /// costante di adattamento né la soglia: sono numeri che nessuno regola a
    /// orecchio, e ognuno di essi è un modo di rovinare l'audio senza capire
    /// perché.
    void setStrength(double zeroToTen) noexcept;
    double strength() const noexcept { return m_strength; }

    /// Elabora in place. Il primo blocco esce attenuato: la catena si riempie,
    /// e prima che ci sia una finestra intera non c'è niente da ricostruire.
    void process(float *audio, std::size_t count) noexcept;

    /// Ritardo introdotto, in campioni: una finestra.
    int latencySamples() const noexcept { return m_frameSize; }

    bool isConfigured() const noexcept { return m_frameSize > 0; }

private:
    void destroyPlans();
    void processFrame() noexcept;

    fftwf_plan_s *m_forward = nullptr;
    fftwf_plan_s *m_inverse = nullptr;
    float *m_timeBuffer = nullptr;      ///< fftwf_malloc, frameSize reali
    void *m_freqBuffer = nullptr;       ///< fftwf_malloc, frameSize/2+1 complessi

    int m_frameSize = 0;
    int m_hop = 0;
    int m_bins = 0;
    double m_sampleRate = 48000.0;
    double m_strength = 5.0;
    float m_gainFloor = 0.1f;           ///< fondo del guadagno, lineare

    std::vector<float> m_window;        ///< Hann, radice per analisi e sintesi
    std::vector<float> m_input;         ///< finestra corrente, scorre di un hop
    std::vector<float> m_overlap;       ///< coda della finestra precedente
    std::vector<float> m_pending;       ///< ingresso non ancora completo
    std::vector<float> m_ready;         ///< uscita ricostruita, in attesa
    std::size_t m_pendingCount = 0;
    std::size_t m_readyCount = 0;

    std::vector<float> m_noise;         ///< potenza del rumore per bin
    std::vector<float> m_minimum;       ///< minimo corrente, per la statistica
    std::vector<float> m_smoothed;      ///< potenza levigata per bin
    std::vector<float> m_priorSnr;      ///< SNR a priori del blocco precedente
    std::vector<float> m_lastGain;

    int m_minimumAge = 0;               ///< blocchi dall'ultimo azzeramento
    bool m_primed = false;
};

} // namespace dsdr::dsp
