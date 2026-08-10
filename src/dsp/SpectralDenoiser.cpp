// SPDX-License-Identifier: GPL-3.0-or-later
#include "dsp/SpectralDenoiser.h"
#include "dsp/FftwPlanning.h"
#include "dsp/DspTypes.h"

#include <fftw3.h>

#include <algorithm>
#include <cmath>

namespace dsdr::dsp {

namespace {

/// Levigatura della potenza per bin prima di cercarne il minimo. Troppo
/// rapida e il minimo insegue la voce, troppo lenta e il fondo non segue il
/// QSB del rumore serale.
constexpr float kPowerSmoothing = 0.85f;

/// Ogni quanti blocchi si riparte a cercare il minimo. Con finestre da 512 a
/// 48 kHz sono circa un secondo e mezzo: più lungo di qualunque pausa fra due
/// sillabe, più corto di un cambio di condizioni.
constexpr int kMinimumWindowFrames = 140;

/// Quanto il fondo stimato insegue il minimo trovato, **a ogni blocco**. La
/// salita è lenta di proposito e la discesa rapida: sovrastimare il rumore
/// vuol dire mangiarsi il segnale debole, che è esattamente ciò che si stava
/// cercando di sentire — e la prima stesura lo faceva, perché aggiornava solo
/// a fine finestra partendo da un blocco che conteneva la voce.
constexpr float kNoiseRise = 0.01f;
constexpr float kNoiseFall = 0.5f;

/// Il minimo di una potenza che fluttua sta *sotto* la sua media anche quando
/// è solo rumore: senza compensazione il fondo risulterebbe più basso del
/// vero, e il filtro lascerebbe passare tutto. Il fattore è quello classico
/// per una finestra di questa lunghezza.
constexpr float kMinimumBias = 1.5f;

/// Peso del passato nella stima *decision-directed* dell'SNR a priori. È il
/// parametro che governa gli artefatti musicali più di ogni altro: vicino a
/// uno l'audio è liscio ma il NR reagisce piano.
constexpr float kDecisionDirected = 0.96f;

} // namespace

SpectralDenoiser::~SpectralDenoiser()
{
    destroyPlans();
}

void SpectralDenoiser::destroyPlans()
{
    const std::lock_guard<std::mutex> guard(fftwPlanningMutex());
    destroyPlansLocked();
}

void SpectralDenoiser::destroyPlansLocked()
{
    if (m_forward) {
        fftwf_destroy_plan(reinterpret_cast<fftwf_plan>(m_forward));
        m_forward = nullptr;
    }
    if (m_inverse) {
        fftwf_destroy_plan(reinterpret_cast<fftwf_plan>(m_inverse));
        m_inverse = nullptr;
    }
    if (m_timeBuffer) {
        fftwf_free(m_timeBuffer);
        m_timeBuffer = nullptr;
    }
    if (m_freqBuffer) {
        fftwf_free(m_freqBuffer);
        m_freqBuffer = nullptr;
    }
}

bool SpectralDenoiser::configure(double sampleRate, int frameSize)
{
    if (!(sampleRate > 0.0) || frameSize < 64 || (frameSize & (frameSize - 1)) != 0)
        return false;

    // Un solo lucchetto per tutta la costruzione. Qui pesa più che altrove:
    // `FFTW_MEASURE` non si limita a pianificare, esegue trasformate vere e
    // scrive nella sapienza globale della libreria — due canali che
    // accendessero l'EMNR insieme si troverebbero a scriverci sopra a vicenda.
    const std::lock_guard<std::mutex> guard(fftwPlanningMutex());
    destroyPlansLocked();

    m_sampleRate = sampleRate;
    m_frameSize = frameSize;
    m_hop = frameSize / 2;              // sovrapposizione del 50 %
    m_bins = frameSize / 2 + 1;

    m_timeBuffer = fftwf_alloc_real(static_cast<std::size_t>(frameSize));
    m_freqBuffer = fftwf_alloc_complex(static_cast<std::size_t>(m_bins));
    if (!m_timeBuffer || !m_freqBuffer) {
        destroyPlansLocked();
        m_frameSize = 0;
        return false;
    }

    m_forward = reinterpret_cast<fftwf_plan_s *>(fftwf_plan_dft_r2c_1d(
        frameSize, m_timeBuffer, static_cast<fftwf_complex *>(m_freqBuffer),
        FFTW_MEASURE));
    m_inverse = reinterpret_cast<fftwf_plan_s *>(fftwf_plan_dft_c2r_1d(
        frameSize, static_cast<fftwf_complex *>(m_freqBuffer), m_timeBuffer,
        FFTW_MEASURE));
    if (!m_forward || !m_inverse) {
        destroyPlansLocked();
        m_frameSize = 0;
        return false;
    }

    // Radice di Hann su analisi e sintesi: applicata due volte dà un Hann
    // intero, che con salto di mezza finestra somma esattamente a uno. È la
    // condizione che rende la ricostruzione trasparente quando il guadagno
    // vale uno — e il primo test è proprio quello.
    m_window.resize(static_cast<std::size_t>(frameSize));
    for (int i = 0; i < frameSize; ++i) {
        const double hann = 0.5 - 0.5 * std::cos(kTwoPi * i / frameSize);
        m_window[static_cast<std::size_t>(i)] = static_cast<float>(std::sqrt(hann));
    }

    m_input.assign(static_cast<std::size_t>(frameSize), 0.0f);
    m_overlap.assign(static_cast<std::size_t>(m_hop), 0.0f);
    m_pending.assign(static_cast<std::size_t>(m_hop), 0.0f);
    // La coda d'uscita tiene una finestra: basta a coprire il ritardo e a
    // servire qualunque blocco il motore consegni.
    m_ready.assign(static_cast<std::size_t>(frameSize) * 2, 0.0f);

    const auto bins = static_cast<std::size_t>(m_bins);
    m_noise.assign(bins, 0.0f);
    m_minimum.assign(bins, 0.0f);
    m_smoothed.assign(bins, 0.0f);
    m_priorSnr.assign(bins, 1.0f);
    m_lastGain.assign(bins, 1.0f);

    reset();
    return true;
}

void SpectralDenoiser::reset() noexcept
{
    std::fill(m_input.begin(), m_input.end(), 0.0f);
    std::fill(m_overlap.begin(), m_overlap.end(), 0.0f);
    std::fill(m_pending.begin(), m_pending.end(), 0.0f);
    std::fill(m_ready.begin(), m_ready.end(), 0.0f);
    m_pendingCount = 0;
    m_readyCount = 0;
    std::fill(m_noise.begin(), m_noise.end(), 0.0f);
    std::fill(m_minimum.begin(), m_minimum.end(), 0.0f);
    std::fill(m_smoothed.begin(), m_smoothed.end(), 0.0f);
    std::fill(m_priorSnr.begin(), m_priorSnr.end(), 1.0f);
    std::fill(m_lastGain.begin(), m_lastGain.end(), 1.0f);
    m_minimumAge = 0;
    m_primed = false;
}

void SpectralDenoiser::setStrength(double zeroToTen) noexcept
{
    m_strength = std::clamp(zeroToTen, 0.0, 10.0);

    // Da 0 dB (inerte) a −25 dB di fondo. Sotto quella soglia il rumore
    // residuo smette di essere un fondo e diventa una manciata di
    // campanellini: è il punto in cui l'audio «pulito» diventa faticoso.
    const double floorDb = -2.5 * m_strength;
    m_gainFloor = static_cast<float>(std::pow(10.0, floorDb / 20.0));
}

void SpectralDenoiser::processFrame() noexcept
{
    auto *freq = static_cast<fftwf_complex *>(m_freqBuffer);

    // ── Analisi ─────────────────────────────────────────────────────────
    for (int i = 0; i < m_frameSize; ++i)
        m_timeBuffer[i] = m_input[static_cast<std::size_t>(i)] * m_window[static_cast<std::size_t>(i)];

    fftwf_execute(reinterpret_cast<fftwf_plan>(m_forward));

    const float norm = 1.0f / static_cast<float>(m_frameSize);
    ++m_minimumAge;
    const bool restartMinimum = m_minimumAge >= kMinimumWindowFrames;

    for (int k = 0; k < m_bins; ++k) {
        const auto bin = static_cast<std::size_t>(k);
        const float re = freq[k][0];
        const float im = freq[k][1];
        const float power = re * re + im * im;

        // ── Stima del rumore: minima statistica ricorsiva ────────────────
        //
        // Il fondo è il minimo che la potenza tocca in una finestra lunga,
        // non la sua media: una media la alza il parlato stesso, e su una
        // frequenza occupata si finirebbe a chiamare «rumore» la voce.
        m_smoothed[bin] = kPowerSmoothing * m_smoothed[bin] + (1.0f - kPowerSmoothing) * power;
        if (!m_primed) {
            m_smoothed[bin] = power;
            m_minimum[bin] = power;
            m_noise[bin] = power;
        }

        m_minimum[bin] = std::min(m_minimum[bin], m_smoothed[bin]);

        // Il fondo insegue il minimo a ogni blocco, non solo a fine finestra:
        // aspettare la fine significava restare sovrastimati per un secondo e
        // mezzo, e in quel secondo e mezzo il segnale spariva.
        const float target = m_minimum[bin] * kMinimumBias;
        const float rate = target > m_noise[bin] ? kNoiseRise : kNoiseFall;
        m_noise[bin] += (target - m_noise[bin]) * rate;

        // La finestra riparte dal livello corrente: senza, il minimo resta
        // agganciato a una pausa di dieci secondi fa e il fondo non risale
        // più quando le condizioni cambiano.
        if (restartMinimum)
            m_minimum[bin] = m_smoothed[bin];

        const float noise = std::max(m_noise[bin], 1e-12f);

        // ── Guadagno MMSE-STSA log-spettrale ─────────────────────────────
        //
        // `posterior` è quanto questo bin è sopra il rumore adesso;
        // `prior` è la stessa cosa stimata *decision-directed*, cioè
        // pesando ciò che è uscito dal blocco precedente. È quella memoria a
        // togliere i campanellini: senza, ogni bin decide da solo a ogni
        // blocco e il residuo balla.
        const float posterior = power / noise;
        const float fromPast = m_lastGain[bin] * m_lastGain[bin] * m_priorSnr[bin];
        const float instant = std::max(posterior - 1.0f, 0.0f);
        float prior = kDecisionDirected * fromPast + (1.0f - kDecisionDirected) * instant;
        prior = std::max(prior, 1e-6f);

        // Guadagno di Wiener corretto in log-ampiezza: la forma chiusa di
        // Ephraim-Malah richiede l'integrale esponenziale, che qui si
        // approssima — la differenza si misura in frazioni di dB e non vale
        // una funzione speciale nel percorso caldo.
        const float v = prior / (1.0f + prior) * posterior;
        const float expint = std::log(1.0f + 1.0f / std::max(v, 1e-6f));
        float gain = prior / (1.0f + prior) * std::exp(0.5f * expint);

        gain = std::clamp(gain, m_gainFloor, 1.0f);

        m_priorSnr[bin] = prior;
        m_lastGain[bin] = gain;

        freq[k][0] = re * gain;
        freq[k][1] = im * gain;
    }

    m_primed = true;
    if (restartMinimum)
        m_minimumAge = 0;

    // ── Sintesi ─────────────────────────────────────────────────────────
    fftwf_execute(reinterpret_cast<fftwf_plan>(m_inverse));

    for (int i = 0; i < m_frameSize; ++i) {
        m_timeBuffer[i] *= m_window[static_cast<std::size_t>(i)] * norm;
    }

    // La prima metà si somma alla coda del blocco precedente ed è pronta a
    // uscire; la seconda diventa la coda del prossimo.
    for (int i = 0; i < m_hop; ++i) {
        m_ready[m_readyCount + static_cast<std::size_t>(i)] =
            m_overlap[static_cast<std::size_t>(i)] + m_timeBuffer[i];
        m_overlap[static_cast<std::size_t>(i)] = m_timeBuffer[m_hop + i];
    }
    m_readyCount += static_cast<std::size_t>(m_hop);
}

void SpectralDenoiser::process(float *audio, std::size_t count) noexcept
{
    if (m_frameSize <= 0 || audio == nullptr || count == 0)
        return;

    const auto hop = static_cast<std::size_t>(m_hop);

    for (std::size_t i = 0; i < count; ++i) {
        const float sample = audio[i];

        // Prima si consegna, poi si accumula: al contrario, il campione
        // entrato in questo giro potrebbe uscire nello stesso, e la latenza
        // vera risulterebbe di un campione più corta di quella dichiarata —
        // uno scarto che si paga quando qualcuno allinea due flussi su questo
        // numero.
        if (m_readyCount > 0) {
            audio[i] = m_ready[0];
            std::move(m_ready.begin() + 1,
                      m_ready.begin() + static_cast<std::ptrdiff_t>(m_readyCount),
                      m_ready.begin());
            --m_readyCount;
        } else {
            // Sono i primi millisecondi dopo l'accensione: meglio silenzio che
            // campioni non filtrati, che si sentirebbero come uno sbuffo.
            audio[i] = 0.0f;
        }

        m_pending[m_pendingCount++] = sample;
        if (m_pendingCount == hop) {
            std::move(m_input.begin() + static_cast<std::ptrdiff_t>(hop), m_input.end(),
                      m_input.begin());
            std::copy_n(m_pending.begin(), hop,
                        m_input.end() - static_cast<std::ptrdiff_t>(hop));
            m_pendingCount = 0;
            processFrame();
        }
    }
}

} // namespace dsdr::dsp
