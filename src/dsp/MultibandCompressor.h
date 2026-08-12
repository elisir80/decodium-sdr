// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — compressore multibanda della voce, il «CFC» (SPEC-005 §4.1).
//
// È il blocco che distingue una eSSB da una SSB alzata. Un compressore a banda
// intera guarda il segnale nel suo insieme: una esse sibilante o un colpo sul
// microfono abbassano tutto, e la voce si accartoccia a ogni consonante. Diviso
// in bande, ogni compressore vede solo la propria parte — il corpo non si
// abbassa perché è arrivata una sibilante, e la presenza non sparisce perché
// qualcuno ha bussato sul tavolo.
//
// Quattro bande, ai valori del disegno di riferimento: 50–250 Hz il corpo,
// 250–700 il calore, 700–1800 l'intelligibilità, 1800–4000 la presenza.
//
// **La ricostruzione è esatta**, e non è un dettaglio. Le tre separazioni sono
// Linkwitz-Riley del quarto ordine, che sommate danno un passa-tutto del
// secondo ordine: le bande già separate attraversano quello stesso passa-tutto
// per le separazioni che vengono dopo, e a guadagni unitari la somma torna il
// segnale di partenza. Senza questa compensazione, un multibanda con tutte le
// bande a riposo colora già la voce — e chi lo accende per la prima volta lo
// giudica da quel colore.
//
// I quattro compressori si comandano con **un numero solo**, «punch». Sedici
// manopole — soglia, rapporto, attacco, rilascio per banda — sono il motivo per
// cui un multibanda resta spento nella maggior parte delle stazioni che ce
// l'hanno: nessuno sa da dove cominciare. Il numero solo muove le soglie
// insieme, con la mano di chi lo ha già fatto mille volte.
#pragma once

#include <array>
#include <atomic>
#include <cstddef>

namespace dsdr::dsp {

class MultibandCompressor
{
public:
    static constexpr int kBands = 4;

    /// Le frequenze di separazione, in hertz. Sono tre perché le bande sono
    /// quattro, e sono quelle del disegno.
    static constexpr double kCrossovers[kBands - 1] = {250.0, 700.0, 1800.0};

    /// Prepara i filtri. Alloca: fuori dal percorso caldo (CONSTITUTION §5).
    void configure(double sampleRate);
    void reset() noexcept;

    void setEnabled(bool enabled) noexcept
    {
        m_enabled.store(enabled, std::memory_order_relaxed);
    }
    bool isEnabled() const noexcept { return m_enabled.load(std::memory_order_relaxed); }

    /// Quanto stringe, da 0 a 10.
    ///
    /// Zero è trasparente. Attorno a 5 la voce guadagna corpo senza sentirsi
    /// lavorata; a 10 è la compressione da pile-up, che si sente ed è quello
    /// che si vuole quando dall'altra parte c'è del rumore.
    void setPunch(double punch) noexcept;
    double punch() const noexcept { return m_punch.load(std::memory_order_relaxed); }

    /// Di quanto sta abbassando ogni banda, in decibel (valore positivo).
    /// È la misura che si guarda: dice quale parte della voce sta lavorando.
    double gainReductionDb(int band) const noexcept;

    /// Elabora audio mono, in luogo. Non alloca, non prende lock.
    void process(float *audio, std::size_t frames) noexcept;

private:
    /// Un biquad in forma diretta II trasposta. Serve a tutto: i passa-basso e
    /// passa-alto delle separazioni e i passa-tutto della compensazione.
    struct Biquad
    {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0;
        double a1 = 0.0, a2 = 0.0;
        double z1 = 0.0, z2 = 0.0;

        void reset() noexcept { z1 = z2 = 0.0; }

        double process(double in) noexcept
        {
            const double out = b0 * in + z1;
            z1 = b1 * in - a1 * out + z2;
            z2 = b2 * in - a2 * out;
            return out;
        }
    };

    /// Un compressore di banda: rivelatore di picco con attacco e rilascio,
    /// soglia e rapporto. Il guadagno si applica dolce, sul livello rivelato,
    /// non campione per campione: così non si sente «pompare».
    struct BandCompressor
    {
        double thresholdDb = -18.0;
        double ratio = 3.0;
        double attackCoeff = 0.0;
        double releaseCoeff = 0.0;

        double envelope = 0.0;
        double gainDb = 0.0;

        void reset() noexcept
        {
            envelope = 0.0;
            gainDb = 0.0;
        }

        double process(double in) noexcept;
    };

    void designCrossovers() noexcept;
    void applyPunch() noexcept;

    double m_sampleRate = 0.0;
    bool m_configured = false;

    // Le tre separazioni, ognuna Linkwitz-Riley del quarto ordine: due sezioni
    // di Butterworth in cascata.
    std::array<std::array<Biquad, 2>, kBands - 1> m_lowpass;
    std::array<std::array<Biquad, 2>, kBands - 1> m_highpass;

    // I passa-tutto della compensazione: la banda 0 attraversa quelli delle
    // separazioni 1 e 2, la banda 1 quello della separazione 2.
    std::array<Biquad, 2> m_allpassBand0;
    Biquad m_allpassBand1;

    std::array<BandCompressor, kBands> m_compressors;
    std::array<std::atomic<double>, kBands> m_reduction{};

    std::atomic<bool> m_enabled{false};
    std::atomic<double> m_punch{5.0};
    std::atomic<bool> m_dirty{true};
};

} // namespace dsdr::dsp
