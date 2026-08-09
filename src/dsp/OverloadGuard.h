// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — guardia contro la saturazione dell'ADC (SPEC-003 §3).
//
// È il difetto numero uno dell'esperienza SDR di fascia bassa, e il più
// difficile da riconoscere: quando l'ingresso satura, il convertitore genera
// prodotti di intermodulazione su tutta la banda. La sensibilità crolla, il
// fondo si alza, i segnali deboli spariscono — e tutto questo somiglia a una
// brutta serata di propagazione. Chi non lo sa cerca il guasto nell'antenna.
//
// La guardia guarda una cosa sola: il picco assoluto del flusso, su finestre
// da cento millisecondi. Nel percorso caldo costa un confronto per campione.
//
// Due soglie con isteresi lunga, perché una guardia nervosa è peggio del
// problema che risolve: si interviene al ribasso solo dopo tre finestre
// consecutive in saturazione, e si torna a salire solo dopo mezzo minuto di
// margine abbondante. E non si sale mai oltre il livello che ha impostato
// l'operatore: la guardia toglie guadagno quando serve e lo restituisce, non
// decide per lui.
#pragma once

#include "dsp/DspTypes.h"

#include <cstddef>

namespace dsdr::dsp {

class OverloadGuard
{
public:
    /// Cosa fa la guardia quando riconosce la saturazione.
    enum class Mode
    {
        Auto,       ///< chiede la correzione di guadagno, dove il device la offre
        WarnOnly,   ///< accende la spia e basta
        Off,        ///< nemmeno misura
    };

    void configure(double sampleRate);
    void reset() noexcept;

    void setMode(Mode mode) noexcept { m_mode = mode; }
    Mode mode() const noexcept { return m_mode; }

    /// Percorso caldo: aggiorna il picco della finestra corrente e, quando la
    /// finestra si chiude, fa avanzare la macchina a stati.
    void feed(const Complex *iq, std::size_t n) noexcept;

    /// Correzione maturata, in dB, e azzeramento della richiesta. Negativa per
    /// togliere guadagno, positiva per restituirlo; zero se non c'è nulla da
    /// fare. Il chiamante la gira al device se ne è capace.
    double takeRequestDb() noexcept;

    /// L'ingresso è in saturazione adesso. È la spia da accendere, e resta
    /// accesa finché il picco non scende: un lampo di un decimo di secondo non
    /// si vedrebbe.
    bool overloaded() const noexcept { return m_overloaded; }

    /// Picco dell'ultima finestra chiusa, in dBFS.
    float peakDbfs() const noexcept { return m_lastPeakDb; }

    /// Quanto guadagno la guardia ha tolto finora, in dB (mai negativo): è il
    /// debito che restituirà quando la banda si calma.
    double appliedReductionDb() const noexcept { return m_reductionDb; }

    /// Interventi da quando è stata azzerata: se cresce senza fermarsi, il
    /// problema non è il guadagno ma l'antenna o un segnale fuori banda.
    int interventions() const noexcept { return m_interventions; }

private:
    void closeWindow() noexcept;

    Mode m_mode = Mode::Auto;

    std::size_t m_windowSamples = 0;
    std::size_t m_windowCount = 0;
    float m_windowPeak = 0.0f;

    float m_lastPeakDb = -160.0f;
    bool m_overloaded = false;

    int m_hotWindows = 0;        ///< finestre consecutive sopra la soglia alta
    int m_coolWindows = 0;       ///< finestre consecutive sotto la soglia bassa
    int m_coolWindowsNeeded = 1; ///< quante ne servono per restituire guadagno

    double m_reductionDb = 0.0;
    double m_pendingDb = 0.0;
    int m_interventions = 0;
};

} // namespace dsdr::dsp
