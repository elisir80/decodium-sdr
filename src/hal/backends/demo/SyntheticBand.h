// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — generatore di banda sintetica (RF-09).
//
// Non è un giocattolo: è la sorgente contro cui girano gli integration test
// headless in CI (RNF-07) ed è ciò che rende l'applicazione dimostrabile senza
// hardware. Ogni stazione è un segnale corretto nel dominio complesso — una
// SSB sintetica è davvero a banda laterale unica, non un tono mascherato —
// così il DSP viene esercitato per quello che dovrà fare sul serio.
#pragma once

#include "dsp/DspTypes.h"
#include "hal/backends/demo/MorseKeyer.h"

#include <QString>

#include <random>
#include <vector>

namespace dsdr::hal::demo {

enum class SignalKind {
    Cw,       ///< portante manipolata in morse
    SsbUpper, ///< voce sintetica a banda laterale superiore
    SsbLower,
    Am,       ///< portante + modulazione d'ampiezza
    Fm,       ///< FM stretta, per la banda VHF
    Carrier,  ///< portante pura (beacon / marcatore)
    Digital,  ///< salto di toni tipo FT8, 15 s di ciclo
};

struct StationSpec
{
    qint64 frequencyHz = 0;
    SignalKind kind = SignalKind::Cw;
    double amplitudeDb = -50.0; ///< dBFS di picco
    QString text;               ///< testo CW o "voce"
    double wpm = 22.0;
    double qsbPeriodSeconds = 0.0; ///< 0 = nessun fading
    double qsbDepthDb = 0.0;
    double toneHz = 600.0;      ///< tono base per SSB/AM/FM
};

class SyntheticBand
{
public:
    /// `centerHz` è la frequenza centrale del ricevitore: le stazioni fuori
    /// dalla banda campionata semplicemente non compaiono, come nella realtà.
    void configure(double sampleRate, qint64 centerHz, const std::vector<StationSpec> &stations);

    void setCenterFrequency(qint64 centerHz);
    void setNoiseFloorDb(double db) { m_noiseFloorDb = db; }

    void reset();

    /// Riempie `out` con `n` campioni IQ. Non alloca.
    void generate(dsp::Complex *out, std::size_t n) noexcept;

    /// Banda plan predefiniti, usati dai due device della discovery.
    static std::vector<StationSpec> hfBandPlan();
    static std::vector<StationSpec> vhfBandPlan();

private:
    struct Station
    {
        StationSpec spec;
        double offsetHz = 0.0;
        float amplitude = 0.0f;
        bool inBand = false;

        dsp::Complex phasor{1.0f, 0.0f};
        dsp::Complex step{1.0f, 0.0f};
        unsigned normalizeCounter = 0;

        MorseKeyer keyer;

        // Fase degli oscillatori di modulazione (voce, QSB, salto toni).
        double modPhase[3] = {0.0, 0.0, 0.0};
        double syllablePhase = 0.0;
        double qsbPhase = 0.0;
        double digitalClock = 0.0;
        int digitalTone = 0;
    };

    void prepareStation(Station &station);

    /// Banda base della stazione (complessa: le SSB sono davvero a banda
    /// laterale unica, non toni reali mascherati).
    dsp::Complex modulationSampleImpl(Station &station) noexcept;

    double m_sampleRate = 0.0;
    qint64 m_centerHz = 0;
    double m_noiseFloorDb = -95.0;
    float m_noiseAmplitude = 0.0f;

    std::vector<Station> m_stations;
    std::mt19937 m_rng{0x5EED};
    std::normal_distribution<float> m_gauss{0.0f, 1.0f};
};

} // namespace dsdr::hal::demo
