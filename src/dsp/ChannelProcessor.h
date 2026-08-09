// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — catena completa di un canale RX (§5.1).
//
//   IQ device → DDC/NCO → decimazione multistadio → passa-banda complesso
//   → demodulatore → AGC (con AGC-T) → volume → audio 48 kHz
//
// Un'istanza per canale. Non è thread-safe: vive interamente nel thread del
// DspEngine, che è anche l'unico a chiamare `applySettings()`.
#pragma once

#include "common/Types.h"
#include "dsp/Agc.h"
#include "dsp/ComplexFir.h"
#include "dsp/DecimatorChain.h"
#include "dsp/Demodulator.h"
#include "dsp/Nco.h"

#include <vector>

namespace dsdr::dsp {

struct ChannelSettings
{
    double offsetHz = 0.0;   ///< scostamento dal centro del device
    DemodMode mode = DemodMode::Usb;
    int filterLowHz = 300;   ///< bordi del passa-banda, riferiti alla portante
    int filterHighHz = 2700;
    AgcMode agcMode = AgcMode::Medium;
    double agcThresholdDb = -100.0;
    double agcMaxGainDb = 90.0;
    double cwPitchHz = 600.0;
    float volume = 0.7f;
    bool muted = false;

    /// Soglia dello squelch, in dB sul livello del canale. Sotto, l'audio
    /// tace. `squelchEnabled` a falso lo spegne del tutto: una soglia
    /// bassissima non è la stessa cosa, perché il rumore impulsivo la
    /// supererebbe comunque e l'audio si aprirebbe a scatti.
    bool squelchEnabled = false;
    double squelchThresholdDb = -95.0;

    bool operator==(const ChannelSettings &o) const noexcept;
    bool operator!=(const ChannelSettings &o) const noexcept { return !(*this == o); }
};

class ChannelProcessor
{
public:
    ChannelProcessor();

    /// Prepara la catena per la frequenza di campionamento del device.
    /// Alloca tutti i buffer di lavoro: dopo di questa il percorso caldo è
    /// allocation-free (RNF-05).
    bool configure(double deviceSampleRate, double audioSampleRate = 48000.0);

    /// Applica nuove impostazioni. Ridisegna i coefficienti solo se il filtro
    /// è effettivamente cambiato; la capacità dei vettori è già prenotata,
    /// quindi non c'è allocazione.
    void applySettings(const ChannelSettings &settings);
    const ChannelSettings &settings() const noexcept { return m_settings; }

    void reset() noexcept;

    /// Elabora `n` campioni IQ del device; scrive l'audio in `out`.
    /// `out` deve avere spazio per `maxAudioFrames(n)` campioni.
    std::size_t process(const Complex *iq, std::size_t n, float *out) noexcept;

    std::size_t maxAudioFrames(std::size_t inputFrames) const noexcept
    {
        return m_chain.maxOutput(inputFrames) + 8;
    }

    double channelRate() const noexcept { return m_channelRate; }
    int decimation() const noexcept { return m_chain.totalDecimation(); }

    /// Livello del segnale filtrato, pre-AGC, in dBFS (S-meter).
    float signalLevelDb() const noexcept { return m_signalLevelDb; }

    /// Vero quando lo squelch sta tenendo chiuso l'audio. Serve alla UI per
    /// dirlo: uno squelch chiuso e una radio guasta suonano identici, e senza
    /// una spia si finisce a cercare il problema nel cavo dell'antenna.
    bool squelchClosed() const noexcept { return m_squelchClosed; }
    float agcGainDb() const noexcept { return m_agc.gainDb(); }

    /// Banda base del canale dopo il filtro: è il tap da cui deriva il flusso
    /// IQ verso DECODIUM 4 (post-decimazione, pre-demodulazione, §5.1).
    const Complex *lastBaseband() const noexcept { return m_filtered.data(); }
    std::size_t lastBasebandFrames() const noexcept { return m_lastBasebandFrames; }

private:
    void redesignFilter();
    void computeFilterEdges(double &loHz, double &hiHz) const;
    double tuningOffsetHz() const;

    ChannelSettings m_settings;

    Nco m_nco;
    DecimatorChain m_chain;
    ComplexFir m_filter;
    Demodulator m_demod;
    Agc m_agc;

    std::vector<Complex> m_mixed;
    std::vector<Complex> m_decimated;
    std::vector<Complex> m_filtered;
    std::vector<Complex> m_filterTaps;

    double m_deviceRate = 0.0;
    double m_channelRate = 0.0;
    double m_audioRate = 48000.0;
    float m_signalLevelDb = -160.0f;
    bool m_squelchClosed = false;
    float m_squelchGain = 0.0f;   ///< apertura corrente, 0..1, per non scattare
    std::size_t m_lastBasebandFrames = 0;
    bool m_configured = false;
};

} // namespace dsdr::dsp
