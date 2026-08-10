// SPDX-License-Identifier: GPL-3.0-or-later
#include "dsp/NeuralDenoiser.h"

#include <algorithm>

#ifdef DSDR_HAVE_RNNOISE
#include <rnnoise.h>
#endif

namespace dsdr::dsp {

namespace {

/// RNNoise lavora su campioni in scala PCM a 16 bit, ma in virgola mobile:
/// l'audio del progetto sta in ±1, quindi si scala all'ingresso e si torna
/// indietro all'uscita. Saltare questo passaggio non fa esplodere niente — la
/// rete continua a funzionare — ma lavora su un livello per cui non è stata
/// addestrata, e il risultato è un denoiser che sembra semplicemente inerte.
constexpr float kPcmScale = 32768.0f;

} // namespace

NeuralDenoiser::~NeuralDenoiser()
{
#ifdef DSDR_HAVE_RNNOISE
    if (m_state)
        rnnoise_destroy(m_state);
#endif
    m_state = nullptr;
}

bool NeuralDenoiser::isAvailable() noexcept
{
#ifdef DSDR_HAVE_RNNOISE
    return true;
#else
    return false;
#endif
}

bool NeuralDenoiser::configure(double sampleRate)
{
#ifdef DSDR_HAVE_RNNOISE
    // La rete è addestrata a 48 kHz e non si adatta: a un ritmo diverso
    // sentirebbe voci più acute o più gravi di quelle che conosce, e
    // deciderebbe di conseguenza. Meglio non accendersi affatto.
    if (std::abs(sampleRate - 48000.0) > 1.0)
        return false;

    if (m_state)
        rnnoise_destroy(m_state);
    m_state = rnnoise_create(nullptr);
    if (!m_state)
        return false;

    m_frameSize = rnnoise_get_frame_size();
    m_pending.assign(static_cast<std::size_t>(m_frameSize), 0.0f);
    m_scratch.assign(static_cast<std::size_t>(m_frameSize), 0.0f);
    m_ready.assign(static_cast<std::size_t>(m_frameSize) * 4, 0.0f);
    reset();
    return true;
#else
    (void)sampleRate;
    return false;
#endif
}

void NeuralDenoiser::reset() noexcept
{
    std::fill(m_pending.begin(), m_pending.end(), 0.0f);
    std::fill(m_ready.begin(), m_ready.end(), 0.0f);
    m_pendingCount = 0;
    m_readyCount = 0;
    m_speech = 0.0f;
}

void NeuralDenoiser::process(float *audio, std::size_t count) noexcept
{
#ifdef DSDR_HAVE_RNNOISE
    if (!m_state || audio == nullptr || count == 0)
        return;

    const auto frame = static_cast<std::size_t>(m_frameSize);

    for (std::size_t i = 0; i < count; ++i) {
        const float sample = audio[i];

        // Prima si consegna, poi si accumula: il campione entrato adesso non
        // può uscire nello stesso giro, e la latenza dichiarata dev'essere
        // quella vera perché ci si allineano altri flussi.
        if (m_readyCount > 0) {
            audio[i] = m_ready[0];
            std::move(m_ready.begin() + 1,
                      m_ready.begin() + static_cast<std::ptrdiff_t>(m_readyCount),
                      m_ready.begin());
            --m_readyCount;
        } else {
            audio[i] = 0.0f;
        }

        m_pending[m_pendingCount++] = sample * kPcmScale;
        if (m_pendingCount < frame)
            continue;

        m_speech = rnnoise_process_frame(m_state, m_scratch.data(), m_pending.data());
        m_pendingCount = 0;

        for (std::size_t k = 0; k < frame; ++k)
            m_ready[m_readyCount + k] = m_scratch[k] / kPcmScale;
        m_readyCount += frame;
    }
#else
    (void)audio;
    (void)count;
#endif
}

} // namespace dsdr::dsp
