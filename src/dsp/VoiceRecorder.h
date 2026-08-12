// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — gli ultimi secondi della propria voce, prima e dopo la catena.
//
// **Perché esiste.** Un trasmettitore non si regola ascoltandosi: mentre si
// parla si sente la propria voce per conduzione ossea, non quella che esce
// dall'antenna, e ogni giudizio dato in quel momento è dato sul suono
// sbagliato. La soluzione classica è un secondo ricevitore in stazione. Questo
// componente la sostituisce con l'unica cosa che funziona altrettanto bene:
// **riascoltarsi dopo**, subito, senza toccare niente.
//
// Registra in continuazione mentre si trasmette, e tiene gli ultimi dieci
// secondi. Non c'è un tasto «registra» da ricordarsi di premere prima: il
// momento in cui ci si accorge di voler riascoltare è sempre *dopo* aver
// parlato, e un registratore che va armato prima arriva sempre tardi.
//
// **Due tracce, non una.** «Prima» è il microfono così com'è; «dopo» è quello
// che parte verso la radio. Sentirle una dopo l'altra è la sola misura
// dell'effetto della catena che non passi dalla memoria di com'era: fra un
// riascolto e l'altro il ricordo del suono precedente è già sbiadito, e si
// finisce per giudicare l'ultimo che si è sentito.
//
// **Thread.** Scrive e legge il thread del motore TX, e nessun altro: qui non
// c'è un ring perché non c'è un confine da attraversare. Quello che la UI
// legge — quanto c'è registrato, dove si è arrivati, se sta suonando — passa da
// variabili atomiche.
#pragma once

#include <atomic>
#include <cstddef>
#include <vector>

namespace dsdr::dsp {

class VoiceRecorder
{
public:
    /// Quale delle due tracce.
    enum class Source {
        Dry,   ///< il microfono prima della catena
        Wet,   ///< quello che parte verso la radio
    };

    /// Alloca le due tracce. Da chiamare fuori dal percorso caldo.
    void configure(double sampleRate, double seconds = kDefaultSeconds);

    /// Butta via quello che c'è e ferma il riascolto.
    void reset() noexcept;

    // ── Registrazione (thread TX) ────────────────────────────────────────

    /// Aggiunge `count` campioni a entrambe le tracce.
    ///
    /// Se stava suonando, il riascolto si ferma qui: premere il PTT mentre ci
    /// si sta risentendo vuol dire tornare a parlare, e continuare a scrivere
    /// sotto la testina di lettura darebbe un riascolto a pezzi di due prese
    /// diverse.
    void record(const float *dry, const float *wet, std::size_t count) noexcept;

    // ── Riascolto (thread TX) ────────────────────────────────────────────

    /// Riparte dall'inizio di quello che c'è. Torna `false` se non c'è niente.
    bool startPlayback(Source source) noexcept;
    void stopPlayback() noexcept;

    /// Tira fuori i prossimi `count` campioni mono e dice quanti ne ha dati.
    /// Meno di quanti chiesti vuol dire che la registrazione è finita, e il
    /// riascolto si chiude da sé.
    std::size_t pull(float *out, std::size_t count) noexcept;

    /// Passa da una traccia all'altra **senza perdere il punto**.
    ///
    /// È il modo in cui il confronto prima/dopo diventa una misura invece di
    /// un ricordo: si commuta a metà di una parola e si sente la stessa
    /// sillaba nei due modi, di seguito.
    void setPlaybackSource(Source source) noexcept;

    // ── Letture, da qualunque thread ─────────────────────────────────────

    bool isPlaying() const noexcept
    {
        return m_playing.load(std::memory_order_acquire);
    }
    Source playbackSource() const noexcept
    {
        return static_cast<Source>(m_source.load(std::memory_order_relaxed));
    }
    double recordedSeconds() const noexcept
    {
        return m_rate > 0.0
            ? static_cast<double>(m_filled.load(std::memory_order_acquire)) / m_rate
            : 0.0;
    }
    double positionSeconds() const noexcept
    {
        return m_rate > 0.0
            ? static_cast<double>(m_position.load(std::memory_order_relaxed)) / m_rate
            : 0.0;
    }
    double capacitySeconds() const noexcept { return m_seconds; }
    bool hasContent() const noexcept
    {
        return m_filled.load(std::memory_order_acquire) > 0;
    }

    /// Dieci secondi: due o tre frasi. Meno non basta a giudicare una voce,
    /// più non si riascolta — e la memoria costa comunque poco (due tracce da
    /// dieci secondi a 48 kHz sono meno di quattro megabyte).
    static constexpr double kDefaultSeconds = 10.0;

private:
    /// L'indice del campione più vecchio ancora in memoria.
    std::size_t oldest() const noexcept;

    std::vector<float> m_dry;
    std::vector<float> m_wet;

    double m_rate = 0.0;
    double m_seconds = 0.0;
    std::size_t m_capacity = 0;
    std::size_t m_write = 0;

    /// Quanti campioni ci sono davvero: cresce fino alla capacità e poi si
    /// ferma, perché da lì in poi ogni campione nuovo ne scaccia uno vecchio.
    std::atomic<std::size_t> m_filled{0};

    std::atomic<std::size_t> m_position{0};
    std::atomic<bool> m_playing{false};
    std::atomic<int> m_source{static_cast<int>(Source::Wet)};
};

} // namespace dsdr::dsp
