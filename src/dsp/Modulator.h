// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — modulatore: audio in, banda base complessa fuori.
//
// È il Demodulator letto al contrario, e usa gli stessi attrezzi. In
// particolare la banda laterale singola nasce come in ricezione: un
// passa-banda a coefficienti **complessi**, che non essendo simmetrico attorno
// a DC lascia passare una sola metà dello spettro. Nessuna trasformata di
// Hilbert, nessun secondo percorso in quadratura da tenere allineato — che è
// il punto in cui la modulazione SSB «di manuale» perde la reiezione.
//
// L'uscita è alla stessa frequenza dell'audio d'ingresso: la salita alla
// frequenza del convertitore è mestiere di InterpolatorChain, e la traslazione
// nella banda del device è mestiere dell'NCO. Ogni pezzo fa una cosa sola,
// come in ricezione.
#pragma once

#include "common/Types.h"
#include "dsp/ComplexFir.h"
#include "dsp/DspTypes.h"

#include <vector>

namespace dsdr::dsp {

/// Ciò che l'operatore decide della trasmissione. I valori di default sono
/// quelli di una SSB fonia in HF.
struct TxSettings
{
    DemodMode mode = DemodMode::Usb;

    /// Banda del filtro di trasmissione, in hertz dalla portante. Per la SSB
    /// 300–2700 è il compromesso classico fra intelligibilità e occupazione;
    /// i modi digitali vogliono invece tutta la banda che il canale concede.
    int lowHz = 300;
    int highHz = 2700;

    /// Profondità di modulazione AM (0…1). Sopra 1 la portante si annulla e
    /// il segnale si sovramodula: il modulatore lo impedisce.
    double amDepth = 0.8;

    /// Deviazione di picco della FM. 5 kHz è lo standard NBFM dei ripetitori,
    /// 75 kHz quello della radiodiffusione.
    double fmDeviationHz = 5000.0;
};

class Modulator
{
public:
    /// Prepara i buffer per la frequenza audio data. Alloca: da chiamare fuori
    /// dal percorso caldo.
    bool configure(double audioRate);

    /// Cambia modo e filtro. Riprogetta i coefficienti senza riallocare
    /// (RNF-05), quindi è chiamabile mentre si trasmette.
    void setSettings(const TxSettings &settings);
    const TxSettings &settings() const noexcept { return m_settings; }

    void reset() noexcept;

    /// `audio` è mono, normalizzato in ±1. Scrive `n` campioni complessi in
    /// `out`, alla stessa frequenza dell'ingresso.
    void process(const float *audio, std::size_t n, Complex *out) noexcept;

    /// Picco del modulo dell'ultimo blocco: è ciò che l'ALC guarda, e ciò che
    /// la UI mostra come livello di uscita.
    float lastPeak() const noexcept { return m_lastPeak; }

    double audioRate() const noexcept { return m_audioRate; }

private:
    void designFilter();

    TxSettings m_settings;
    ComplexFir m_filter;
    std::vector<Complex> m_taps;
    std::vector<Complex> m_scratch;
    double m_audioRate = 0.0;
    double m_fmPhase = 0.0;
    float m_lastPeak = 0.0f;
    bool m_filterActive = false;
};

} // namespace dsdr::dsp
