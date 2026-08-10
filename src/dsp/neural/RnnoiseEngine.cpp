// SPDX-License-Identifier: GPL-3.0-or-later
#include "dsp/neural/RnnoiseEngine.h"

#ifdef DSDR_HAVE_RNNOISE
#include <rnnoise.h>
#endif

#include <algorithm>
#include <cmath>

namespace dsdr::dsp::neural {

namespace {

/// La scala in cui RNNoise è stata addestrata: PCM a 16 bit, non ±1.
constexpr float kPcmScale = 32768.0f;

/// Dieci millisecondi a 48 kHz. Lo dichiara la libreria, ma va saputo anche
/// prima di averla: è la misura con cui lo stadio dimensiona i suoi buffer.
constexpr int kFrameSamples = 480;

} // namespace

RnnoiseEngine::RnnoiseEngine() = default;

RnnoiseEngine::~RnnoiseEngine()
{
#ifdef DSDR_HAVE_RNNOISE
    if (m_state)
        rnnoise_destroy(m_state);
#endif
    m_state = nullptr;
}

bool RnnoiseEngine::isAvailable()
{
#ifdef DSDR_HAVE_RNNOISE
    return true;
#else
    return false;
#endif
}

float RnnoiseEngine::dryMixFor(float attenuationDb)
{
    // Cento decibel significa «tutto il bagnato»; zero, «tutto l'asciutto».
    // In mezzo si mescola in ampiezza, non in decibel: a metà cursore si vuole
    // metà effetto, e una scala logaritmica lo renderebbe tutto o niente.
    const float clamped = std::clamp(attenuationDb, 0.0f, 100.0f);
    return 1.0f - clamped / 100.0f;
}

bool RnnoiseEngine::prepare(const QString &modelPath, QString *error)
{
    Q_UNUSED(modelPath)   // RNNoise ha i pesi dentro di sé

#ifdef DSDR_HAVE_RNNOISE
    if (m_state)
        rnnoise_destroy(m_state);
    m_state = rnnoise_create(nullptr);
    if (!m_state) {
        if (error)
            *error = QStringLiteral("RNNoise non si è inizializzata.");
        return false;
    }

    m_frameSamples = rnnoise_get_frame_size();
    if (m_frameSamples <= 0)
        m_frameSamples = kFrameSamples;

    // Tutte le allocazioni stanno qui: da `processFrame` in poi non se ne
    // fanno più, ed è il contratto che un test verifica contandole.
    m_scaled.assign(static_cast<std::size_t>(m_frameSamples), 0.0f);
    m_dry.assign(static_cast<std::size_t>(m_frameSamples), 0.0f);
    m_dryMix = dryMixFor(m_attenuationDb);
    return true;
#else
    if (error) {
        *error = QStringLiteral("Questa compilazione non include RNNoise: "
                                "riconfigurare con DSDR_NEURAL_NR=ON.");
    }
    return false;
#endif
}

void RnnoiseEngine::processFrame(float *samples)
{
    if (!samples)
        return;

#ifdef DSDR_HAVE_RNNOISE
    if (!m_state)
        return;

    const std::size_t count = static_cast<std::size_t>(m_frameSamples);

    for (std::size_t i = 0; i < count; ++i) {
        m_dry[i] = samples[i];
        m_scaled[i] = samples[i] * kPcmScale;
    }

    rnnoise_process_frame(m_state, m_scaled.data(), m_scaled.data());

    // La miscela fra asciutto e bagnato è dove agisce l'intensità: RNNoise
    // toglie quello che decide lei, e l'unico modo di chiederle di meno è
    // rimettere dentro un po' dell'originale.
    const float wet = 1.0f - m_dryMix;
    for (std::size_t i = 0; i < count; ++i)
        samples[i] = m_dry[i] * m_dryMix + (m_scaled[i] / kPcmScale) * wet;
#else
    Q_UNUSED(samples)
#endif
}

void RnnoiseEngine::setAttenuationLimitDb(float db)
{
    m_attenuationDb = std::clamp(db, 0.0f, 100.0f);
    m_dryMix = dryMixFor(m_attenuationDb);
}

NrEngineInfo RnnoiseEngine::info() const
{
    NrEngineInfo engineInfo;
    engineInfo.id = QStringLiteral("rnnoise");
    engineInfo.modelName = QStringLiteral("RNNoise (Xiph)");
    engineInfo.frameSamples = m_frameSamples;
    // RNNoise elabora il fotogramma che le si dà e restituisce quello: il
    // ritardo algoritmico è il fotogramma stesso. Quello che lo stadio
    // aggiunge — la profondità dei ring — lo conta lui, non il motore.
    engineInfo.latencySamples = m_frameSamples;
    return engineInfo;
}

void RnnoiseEngine::reset()
{
#ifdef DSDR_HAVE_RNNOISE
    if (!m_state)
        return;
    // RNNoise non ha un azzeramento: si ricrea lo stato. Costa poco e
    // succede fuori dal percorso caldo — a un cambio di canale o di modo.
    rnnoise_destroy(m_state);
    m_state = rnnoise_create(nullptr);
#endif
}

} // namespace dsdr::dsp::neural
