// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — la storia recente della banda, per riascoltarla.
//
// Un ricevitore in diretta butta via il passato: quando ci si accorge di aver
// perso un nominativo, quel segnale non esiste più. Qui il flusso IQ scorre
// dentro un anello che ne conserva gli ultimi secondi, così il DSP può
// smettere di leggere la testa e mettersi a leggere qualche secondo indietro.
//
// Il ritardo è *costante per costruzione*: a ogni giro si scrive quanto si
// legge, quindi la distanza fra scrittura e lettura non si muove da sola. È
// ciò che distingue questo da un normale ring di trasporto — non è un canale
// fra due thread, è una memoria a scorrimento con un solo padrone.
//
// Vincoli (CONSTITUTION §5): un solo thread lo usa, quello del DSP. Alloca
// soltanto in `configure()`; `write` e `readDelayed` sono copie di memoria e
// nient'altro.
#pragma once

#include <cstddef>
#include <memory>
#include <vector>

namespace dsdr::dsp {

class TimeShiftBuffer
{
public:
    TimeShiftBuffer() = default;

    TimeShiftBuffer(const TimeShiftBuffer &) = delete;
    TimeShiftBuffer &operator=(const TimeShiftBuffer &) = delete;

    /// Alloca la memoria per `frames` campioni complessi (due float ciascuno).
    /// Va chiamata fuori dallo streaming: rialloca e azzera la storia.
    void configure(std::size_t frames);

    /// Dimentica ciò che è stato scritto senza toccare l'allocazione. Serve
    /// quando la sorgente cambia: la storia di un'altra radio non è storia.
    void clear();

    std::size_t capacityFrames() const noexcept { return m_capacity; }

    /// Quanti campioni di passato sono davvero disponibili adesso. Cresce col
    /// tempo fino alla capacità: subito dopo la connessione non si può tornare
    /// indietro di trenta secondi perché quei trenta secondi non ci sono stati.
    std::size_t availableFrames() const noexcept { return m_filled; }

    /// Accoda campioni interleaved I/Q. Se ne arrivano più della capacità si
    /// tiene la coda: è la parte recente quella che interessa.
    void write(const float *interleaved, std::size_t frames);

    /// Copia `frames` campioni presi `delayFrames` indietro rispetto all'ultimo
    /// scritto, e restituisce quanti ne ha prodotti davvero.
    ///
    /// Un ritardo più profondo della storia disponibile viene accorciato a
    /// quello che c'è: si torna indietro fin dove si può, e il chiamante lo
    /// scopre da `clampDelay()` invece che da un silenzio inspiegabile.
    std::size_t readDelayed(std::size_t delayFrames, float *out, std::size_t frames) const;

    /// Il ritardo davvero ottenibile per una lettura di `frames` campioni:
    /// mai oltre la storia disponibile, e mai tale da mordere la coda che si
    /// sta per leggere.
    std::size_t clampDelay(std::size_t delayFrames, std::size_t frames) const noexcept;

private:
    std::vector<float> m_data;      ///< interleaved, 2 float per campione
    std::size_t m_capacity = 0;     ///< in campioni complessi
    std::size_t m_head = 0;         ///< prossima posizione di scrittura, in campioni
    std::size_t m_filled = 0;       ///< storia valida, in campioni
};

} // namespace dsdr::dsp
