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
#include "dsp/AudioHighPass.h"
#include "dsp/BroadcastFmStereo.h"
#include "dsp/ComplexFir.h"
#include "dsp/CtcssDetector.h"
#include "dsp/DecimatorChain.h"
#include "dsp/Demodulator.h"
#include "dsp/LmsFilter.h"
#include "dsp/Nco.h"
#include "dsp/NotchFilter.h"
#include "dsp/PeakFilter.h"
#include "dsp/SpectralDenoiser.h"
#include "dsp/FmIfNoiseReducer.h"
#include "dsp/RdsDecoder.h"

#include <cmath>
#include <array>
#include <vector>

namespace dsdr::dsp {

struct ChannelSettings
{
    double offsetHz = 0.0;   ///< scostamento dal centro del device
    DemodMode mode = DemodMode::Usb;
    int filterLowHz = 300;   ///< bordi del passa-banda, riferiti alla portante
    int filterHighHz = 2700;
    /// Spostamento del passa-banda rispetto alla portante, in hertz.
    ///
    /// È l'IF shift dei ricevitori di una volta: la larghezza resta quella, si
    /// sposta la finestra. Serve quando l'interferenza sta da un lato solo —
    /// si scivola dall'altra parte e si continua a copiare, invece di
    /// stringere il filtro e perdere anche il timbro di chi parla.
    double passbandShiftHz = 0.0;

    AgcMode agcMode = AgcMode::Medium;
    double agcThresholdDb = -100.0;
    double agcMaxGainDb = 90.0;
    double agcAttackMs = 2.0;
    double agcDecayMs = 0.0;
    bool amCarrierAgc = false;
    double cwPitchHz = 600.0;
    float volume = 0.7f;
    bool muted = false;
    bool audioHighPassEnabled = false;
    double audioHighPassHz = 300.0;
    bool fmStereo = true;
    bool fmAudioLowPass = true;
    double fmDeemphasisUs = 50.0;
    bool fmRds = true;
    bool rdsAutomaticAf = false;
    RdsRegion rdsRegion = RdsRegion::Europe;
    bool squelchEnabled = false;
    double squelchThresholdDb = -80.0;
    bool ctcssEnabled = false;
    bool ctcssDecodeOnly = false;
    double ctcssToneHz = 100.0;
    bool fmIfNoiseReductionEnabled = false;
    int fmIfNoiseReductionPreset = 0;

    // ── Filtri di disturbo ──────────────────────────────────────────────
    //
    // Tutti spenti di fabbrica, e ognuno col suo interruttore. Nessuno di
    // questi è gratis: la riduzione di rumore colora la voce, il notch
    // automatico si mangia anche le note CW. Un ricevitore che li tenesse
    // sempre accesi suonerebbe meglio sulle scale di misura e peggio
    // all'orecchio.
    //
    // Il noise blanker non è qui: sta nel motore, perché un impulso è
    // dell'ambiente e non del canale, e va tolto a banda piena prima della
    // decimazione (SPEC-003 §4).
    bool nrEnabled = false;
    /// Quanto togliere, da 0 a 10 (DSDR-SPEC-003 §6): un solo comando, che
    /// mappa il fondo del guadagno spettrale da 0 a −25 dB. Gli altri
    /// parametri della stima non si regolano a orecchio, e per questo non si
    /// espongono.
    double nrStrength = 5.0;

    bool anfEnabled = false;       ///< notch automatico sulle righe fisse

    /// Notch manuali, fino a otto (DSDR-SPEC-003 §5).
    ///
    /// La frequenza è uno **scostamento dalla portante del canale**, non un
    /// tono audio: chi tiene la sessione la ricalcola quando ci si sintonizza
    /// altrove, così il notch resta sul disturbo invece di seguire la
    /// sintonia. È la differenza fra togliere un fischio e portarselo dietro.
    struct Notch
    {
        double offsetHz = 0.0;    ///< rispetto alla portante del canale
        double widthHz = 120.0;
        bool enabled = false;

        bool operator==(const Notch &o) const noexcept
        {
            return offsetHz == o.offsetHz && widthHz == o.widthHz
                && enabled == o.enabled;
        }
    };

    /// Filtro di picco sulla nota CW (SPEC-003 §7): esalta il tono che si sta
    /// copiando e allontana tutto il resto. Solo in CW — su una voce una
    /// campana così stretta suona come un telefono.
    bool apfEnabled = false;
    double apfQ = 12.0;

    /// Ascolto binaurale in CW (SPEC-003 §7).
    ///
    /// I due canali portano le due componenti in quadratura del segnale di
    /// banda base: toni a frequenze diverse arrivano con differenze di fase
    /// diverse, e l'orecchio li separa nello spazio. In un pile-up è il modo
    /// più economico di distinguere due stazioni che si accavallano.
    bool binauralCw = false;

    /// AM sincrona: quale banda laterale ascoltare, e quanto largo è il
    /// campo in cui il PLL può agganciare la portante.
    ///
    /// Scegliere una sola banda laterale è l'arma contro l'interferenza
    /// adiacente: la stazione che disturba di solito sta da un lato solo, e
    /// buttarla via non costa nulla in fedeltà perché in AM le due bande
    /// portano la stessa informazione.
    enum class SamSideband
    {
        Both,      ///< entrambe, com'è l'AM
        Lower,
        Upper,
    };
    SamSideband samSideband = SamSideband::Both;
    double samCaptureRangeHz = 500.0;

    static constexpr int kMaxNotches = 8;
    std::array<Notch, kMaxNotches> notches{};

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

    /// Come `process()`, ma restituisce L/R interleaved. I modi non stereo
    /// vengono duplicati sui due canali; Wide-FM usa il decoder MPX.
    std::size_t processStereo(const Complex *iq, std::size_t n,
                              float *outInterleaved) noexcept;

    std::size_t maxAudioFrames(std::size_t inputFrames) const noexcept
    {
        const std::size_t channelFrames = m_chain.maxOutput(inputFrames);
        if (!m_resampleAudio || m_channelRate <= 0.0)
            return channelFrames + 8;
        return static_cast<std::size_t>(std::ceil(
                   static_cast<double>(channelFrames) * m_audioRate / m_channelRate))
            + 8;
    }

    double channelRate() const noexcept { return m_channelRate; }
    int decimation() const noexcept { return m_chain.totalDecimation(); }

    /// Livello del segnale filtrato, pre-AGC, in dBFS (S-meter).
    float signalLevelDb() const noexcept { return m_signalLevelDb; }

    /// Stima adattiva del fondo rumore del canale, in dBFS (SPEC-003 §9).
    float noiseFloorDb() const noexcept { return m_noiseFloorDb; }
    /// Differenza fra livello RF e fondo rumore, in dB. È il numero che
    /// trasforma le impressioni in confronti: «si sente meglio» non si può
    /// discutere, «dodici dB invece di sei» sì.
    float snrDb() const noexcept { return m_snrDb; }
    /// Livello RMS dell'audio effettivamente emesso dal canale, dopo volume.
    float audioLevelDb() const noexcept { return m_audioLevelDb; }

    /// Vero quando lo squelch sta tenendo chiuso l'audio. Serve alla UI per
    /// dirlo: uno squelch chiuso e una radio guasta suonano identici, e senza
    /// una spia si finisce a cercare il problema nel cavo dell'antenna.
    bool squelchClosed() const noexcept { return m_squelchClosed; }
    float agcGainDb() const noexcept { return m_agc.gainDb(); }
    float agcAttackMs() const noexcept { return static_cast<float>(m_agc.attackMs()); }
    float agcDecayMs() const noexcept { return static_cast<float>(m_agc.decayMs()); }
    float ctcssLevelDb() const noexcept { return m_ctcss.levelDb(); }
    bool ctcssDetected() const noexcept { return m_ctcss.detected(); }
    int fmIfNoiseReductionPreset() const noexcept { return m_fmIfNoiseReducer.preset(); }
    bool rdsSynced() const noexcept { return m_rds.synced(); }
    std::uint16_t rdsPiCode() const noexcept { return m_rds.piCode(); }
    std::uint8_t rdsCountryCode() const noexcept { return m_rds.countryCode(); }
    std::uint8_t rdsProgramCoverage() const noexcept { return m_rds.programCoverage(); }
    std::uint8_t rdsProgramReferenceNumber() const noexcept
    { return m_rds.programReferenceNumber(); }
    std::string rdsCallsign() const { return m_rds.callsign(); }
    std::string rdsProgramType() const { return m_rds.programTypeName(); }
    RdsRegion rdsRegion() const noexcept { return m_rds.region(); }
    bool rdsTrafficProgram() const noexcept { return m_rds.trafficProgram(); }
    bool rdsTrafficAnnouncement() const noexcept { return m_rds.trafficAnnouncement(); }
    std::string rdsAlternateFrequencies() const { return m_rds.alternateFrequencies(); }
    std::string rdsProgramService() const { return m_rds.programService(); }
    std::string rdsRadioText() const { return m_rds.radioText(); }

    /// Il notch automatico sta davvero lavorando.
    ///
    /// In CW no, qualunque cosa dica l'impostazione: la nota che si ascolta è
    /// una riga fissa, ed è esattamente ciò che l'ANF toglie. Non è una
    /// raccomandazione all'operatore ma un interlock (SPEC-003 §5) — il modo
    /// in cui il segnale sparisce è troppo somigliante a una radio guasta.
    /// L'ascolto binaurale è attivo: acceso, e in CW — altrove i due canali
    /// in quadratura non vorrebbero dire niente.
    bool binauralActive() const noexcept
    {
        return m_settings.binauralCw
            && (m_settings.mode == DemodMode::Cw || m_settings.mode == DemodMode::Cwr);
    }

    /// Il filtro di picco sta lavorando: acceso, e in un modo dove ha senso.
    bool peakFilterActive() const noexcept
    {
        return m_settings.apfEnabled
            && (m_settings.mode == DemodMode::Cw || m_settings.mode == DemodMode::Cwr);
    }

    bool autoNotchActive() const noexcept
    {
        return m_settings.anfEnabled
            && m_settings.mode != DemodMode::Cw
            && m_settings.mode != DemodMode::Cwr;
    }


    /// Banda base del canale dopo il filtro: è il tap da cui deriva il flusso
    /// IQ verso DECODIUM 4 (post-decimazione, pre-demodulazione, §5.1).
    const Complex *lastBaseband() const noexcept { return m_filtered.data(); }
    std::size_t lastBasebandFrames() const noexcept { return m_lastBasebandFrames; }

private:
    bool configureForMode();
    /// Notch manuale, notch automatico e riduzione di rumore, nell'ordine in
    /// cui devono agire. Sono tre righe ripetute in due rami della catena
    /// audio: raccoglierle qui evita che uno dei due resti indietro.
    void applyAudioFilters(float *audio, std::size_t count) noexcept;

    /// Da scostamento in RF a tono audio, secondo il modo.
    ///
    /// In USB il disturbo che sta 900 Hz sopra la portante si sente a 900 Hz;
    /// in LSB sta sotto e si sente lo stesso a 900; in CW il BFO ha già
    /// spostato tutto del pitch, quindi il conto parte da lì. Senza questa
    /// conversione un notch «ancorato» cambierebbe bersaglio al cambio di
    /// banda laterale.
    double notchAudioHz(double offsetHz) const;

    void redesignFilter();
    void computeFilterEdges(double &loHz, double &hiHz) const;
    double tuningOffsetHz() const;
    float processAudioLowpass(float sample) noexcept;
    std::size_t resampleAudio(const float *input, std::size_t count, float *output) noexcept;
    std::size_t processInternal(const Complex *iq, std::size_t n,
                                float *monoOut, float *stereoOut) noexcept;
    float processDeemphasis(float sample, float &state) noexcept;
    std::size_t resampleAudioStereo(const float *inputInterleaved,
                                    std::size_t count,
                                    float *outputInterleaved) noexcept;

    ChannelSettings m_settings;

    Nco m_nco;
    DecimatorChain m_chain;
    ComplexFir m_filter;
    Demodulator m_demod;
    Agc m_agc;
    std::array<NotchFilter, ChannelSettings::kMaxNotches> m_notches;
    PeakFilter m_apf;
    LmsFilter m_anf;
    SpectralDenoiser m_nr;
    AudioHighPass m_audioHighPassLeft;
    AudioHighPass m_audioHighPassRight;

    std::vector<Complex> m_mixed;
    std::vector<Complex> m_decimated;
    std::vector<Complex> m_filtered;
    std::vector<Complex> m_filterTaps;
    std::vector<float> m_demodulated;
    std::vector<float> m_audioFilterTaps;
    std::vector<float> m_audioFilterDelay;
    std::vector<float> m_resampleBuffer;
    std::vector<float> m_stereoAudio;
    std::vector<float> m_stereoAudioFilterDelay;
    std::vector<float> m_stereoResampleBuffer;

    BroadcastFmStereo m_broadcastFmStereo;
    CtcssDetector m_ctcss;
    FmIfNoiseReducer m_fmIfNoiseReducer;
    RdsDecoder m_rds;

    double m_deviceRate = 0.0;
    double m_channelRate = 0.0;
    double m_audioRate = 48000.0;
    double m_resampleStep = 1.0;
    double m_resamplePosition = 0.0;
    double m_stereoResamplePosition = 0.0;
    float m_signalLevelDb = -160.0f;
    bool m_squelchClosed = false;
    float m_squelchGain = 0.0f;   ///< apertura corrente, 0..1, per non scattare
    float m_deemphasisLeft = 0.0f;
    float m_deemphasisRight = 0.0f;
    std::size_t m_lastBasebandFrames = 0;
    std::size_t m_audioFilterPosition = 0;
    std::size_t m_stereoAudioFilterPosition = 0;
    bool m_squelchOpen = true;
    bool m_wideFm = false;
    bool m_resampleAudio = false;
    bool m_configured = false;
    float m_noiseFloorDb = -160.0f;
    float m_snrDb = 0.0f;
    float m_audioPower = 0.0f;
    float m_audioLevelDb = -160.0f;
    bool m_noiseFloorInitialized = false;

    void updateAudioMeter(float sample) noexcept;
};

} // namespace dsdr::dsp
