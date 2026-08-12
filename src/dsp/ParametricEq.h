// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — equalizzatore parametrico sul percorso d'ascolto (SPEC-005 §4.1).
//
// Cinque celle a campana, in cascata. Ognuna ha frequenza, guadagno e Q: sono
// i tre numeri che descrivono una campana, e sono anche i tre gradi di libertà
// di un punto trascinabile su una curva — la frequenza è dove sta, il guadagno
// quanto è alto, il Q quanto è stretto. Non è un caso: l'editor a curve nasce
// da qui, non viceversa.
//
// Sta sull'**ascolto**, dopo lo stadio neurale e prima delle cuffie. Non sulla
// catena dei decoder, che resta lineare: un'equalizzazione prima di un
// decodificatore non migliora niente e rovina la stima del rapporto
// segnale-rumore (SPEC-005 §2.4).
//
// I parametri li scrive il thread della UI, i coefficienti li ricalcola il
// thread audio. Sono atomici uno per uno, e non un blocco pubblicato in modo
// coerente: un fotogramma in cui la frequenza è già quella nuova e il Q ancora
// quello vecchio dura un blocco — poche centinaia di microsecondi — e non si
// sente. Pubblicarli in modo coerente costerebbe un doppio buffer e un
// protocollo, per togliere un difetto che nessuno può percepire.
#pragma once

#include <array>
#include <atomic>
#include <cstddef>

namespace dsdr::dsp {

class ParametricEq
{
public:
    /// Quante celle. Cinque coprono una voce senza lasciare buchi fra una
    /// campana e l'altra, e restano contabili su una curva: dieci punti
    /// trascinabili in tre centimetri non sono un comando, sono un mosaico.
    static constexpr int kBands = 5;

    /// Il guadagno massimo di una campana. Oltre i dodici decibel non si
    /// equalizza più: si costruisce un altro segnale, e la cosa che serve è un
    /// filtro, non un equalizzatore.
    static constexpr double kMaxGainDb = 12.0;

    /// Prepara per la frequenza e il numero di canali dati. Alloca: da
    /// chiamare fuori dal percorso caldo (CONSTITUTION §5).
    void configure(double sampleRate, int channels);

    void reset() noexcept;

    /// Acceso o spento. Spento non azzera i parametri: si rientra dove si era.
    void setEnabled(bool enabled) noexcept
    {
        m_enabled.store(enabled, std::memory_order_relaxed);
    }
    bool isEnabled() const noexcept { return m_enabled.load(std::memory_order_relaxed); }

    /// Una campana. `gainDb` a zero la rende trasparente — la cella resta in
    /// cascata e non fa niente, che è più semplice e più prevedibile di
    /// toglierla dalla catena.
    void setBand(int index, double frequencyHz, double gainDb, double q) noexcept;

    double bandFrequency(int index) const noexcept;
    double bandGainDb(int index) const noexcept;
    double bandQ(int index) const noexcept;

    /// La risposta dell'insieme a una frequenza, in decibel.
    ///
    /// Serve a disegnare la curva, e la disegna dai *coefficienti*, non da una
    /// formula scritta due volte: una curva che mostra una campana diversa da
    /// quella che si sente è peggio di nessuna curva.
    double responseDb(double frequencyHz) const noexcept;

    /// Elabora audio interlacciato. Non alloca, non prende lock.
    void process(float *audio, std::size_t frames) noexcept;

private:
    struct Band
    {
        std::atomic<double> frequency{1000.0};
        std::atomic<double> gainDb{0.0};
        std::atomic<double> q{1.0};
    };

    /// I coefficienti di una campana, normalizzati su a0, più lo stato per
    /// canale. Forma diretta II trasposta, come il notch.
    struct Cell
    {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0;
        double a1 = 0.0, a2 = 0.0;
        std::array<double, 2> z1{};
        std::array<double, 2> z2{};
    };

    void redesign() noexcept;

    std::array<Band, kBands> m_bands;
    std::array<Cell, kBands> m_cells;

    double m_sampleRate = 0.0;
    int m_channels = 2;
    bool m_configured = false;

    std::atomic<bool> m_enabled{false};
    std::atomic<bool> m_dirty{true};
};

} // namespace dsdr::dsp
