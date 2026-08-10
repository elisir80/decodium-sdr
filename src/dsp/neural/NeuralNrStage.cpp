// SPDX-License-Identifier: GPL-3.0-or-later
#include "dsp/neural/NeuralNrStage.h"

#include <QTimer>

#include <algorithm>
#include <cmath>

namespace dsdr::dsp::neural {

namespace {

constexpr double kSampleRate = 48000.0;

/// Duecentocinquanta millisecondi di coda per verso (IMPL-001 §4). Serve
/// larga: l'inferenza costa in modo irregolare, e una coda stretta
/// trasformerebbe una variazione di dieci millisecondi in un buco.
constexpr std::size_t kRingSamples = static_cast<std::size_t>(kSampleRate * 0.25);

/// La dissolvenza fra asciutto e bagnato. Venti millisecondi: sotto si sente
/// come uno scatto, sopra si sente come un ritardo nell'accensione.
constexpr int kCrossfadeMs = 20;

/// Oltre questa occupazione del ring d'ingresso il thread non sta stando
/// dietro: l'audio si sta accumulando perché non lo si consuma abbastanza in
/// fretta.
constexpr double kOccupancyThreshold = 0.6;

/// E deve durare: mezzo secondo. Un picco isolato è un'altra applicazione che
/// ha rubato la CPU per un istante, non una rete troppo pesante.
constexpr qint64 kOverThresholdNs = 500'000'000;

/// Prima di riprovare, trenta secondi. Uno stadio che si accende e si spegne
/// ogni due secondi è peggio di uno spento.
constexpr qint64 kRecoveryNs = 30'000'000'000LL;

constexpr int kPumpIntervalMs = 5;

/// Quanti fotogrammi al massimo per giro: ottanta millisecondi di audio.
///
/// Serve un tetto. Senza, un giro solo svuoterebbe tutto il ring qualunque
/// tempo ci voglia, e l'arretrato non comparirebbe mai — proprio la cosa che
/// si sta cercando di misurare. Con il tetto, un motore che non sta al passo
/// lascia indietro qualcosa a ogni giro, e l'arretrato si accumula dove lo si
/// può vedere.
constexpr int kMaxFramesPerPump = 8;

} // namespace

NeuralNrStage::NeuralNrStage(QObject *parent)
    : QObject(parent)
    , m_input(std::make_unique<SpscRing<float>>(kRingSamples))
    , m_output(std::make_unique<SpscRing<float>>(kRingSamples))
{
    m_crossfadeSamples = static_cast<int>(kSampleRate * kCrossfadeMs / 1000.0);
    m_frame.assign(static_cast<std::size_t>(m_frameSamples), 0.0f);
    m_dry.assign(static_cast<std::size_t>(m_frameSamples), 0.0f);
    m_clock.start();
}

NeuralNrStage::~NeuralNrStage() = default;

void NeuralNrStage::setEngine(std::unique_ptr<INrEngine> engine)
{
    m_engine = std::move(engine);
    if (!m_engine)
        return;

    const NrEngineInfo engineInfo = m_engine->info();
    if (engineInfo.frameSamples > 0)
        m_frameSamples = engineInfo.frameSamples;

    // Tutte le allocazioni stanno qui, fuori dal percorso caldo.
    m_frame.assign(static_cast<std::size_t>(m_frameSamples), 0.0f);
    m_dry.assign(static_cast<std::size_t>(m_frameSamples), 0.0f);

    // Il ritardo dichiarato: quello del motore più mezzo ring, che è la
    // profondità a cui il ring si assesta quando produttore e consumatore
    // vanno alla stessa velocità.
    const double engineMs = engineInfo.latencySamples * 1000.0 / kSampleRate;
    const double ringMs = (kRingSamples * 0.5) * 1000.0 / kSampleRate;
    m_latencyMs.store(engineMs + ringMs, std::memory_order_relaxed);
}

void NeuralNrStage::start()
{
    if (m_timer)
        return;
    m_timer = new QTimer(this);
    m_timer->setInterval(kPumpIntervalMs);
    connect(m_timer, &QTimer::timeout, this, &NeuralNrStage::pump);
    m_timer->start();
}

void NeuralNrStage::stop()
{
    if (!m_timer)
        return;
    m_timer->stop();
    m_timer->deleteLater();
    m_timer = nullptr;
}

void NeuralNrStage::setEnabled(bool enabled)
{
    if (m_enabled.load(std::memory_order_acquire) == enabled)
        return;
    m_enabled.store(enabled, std::memory_order_release);

    if (enabled) {
        // Si riparte dal degrado: chi riaccende a mano sta dicendo di
        // riprovare, e l'isteresi non deve tenerlo fermo.
        m_degradedAtNs = -1;
        m_overThresholdSinceNs = -1;
        setState(State::Warmup);
    } else {
        setState(State::Bypass);
    }
}

void NeuralNrStage::setIntensityDb(double db)
{
    if (m_engine)
        m_engine->setAttenuationLimitDb(static_cast<float>(db));
}

void NeuralNrStage::resetEngine()
{
    if (m_engine)
        m_engine->reset();
    // La dissolvenza riparte: dopo un azzeramento la rete ha di nuovo bisogno
    // di qualche fotogramma per capire dove si trova.
    m_crossfade = 0;
    if (m_enabled.load(std::memory_order_acquire))
        setState(State::Warmup);
}

void NeuralNrStage::setState(State state)
{
    const int value = static_cast<int>(state);
    if (m_state.exchange(value, std::memory_order_acq_rel) == value)
        return;
    emit stateChanged(state);
}

void NeuralNrStage::writeFrame(const float *frame, std::size_t count)
{
    m_output->write(frame, count);
}

void NeuralNrStage::pump()
{
    const auto frameSamples = static_cast<std::size_t>(m_frameSamples);
    if (frameSamples == 0)
        return;

    const qint64 startNs = m_clock.nsecsElapsed();
    std::size_t processed = 0;

    // L'arretrato si guarda **all'arrivo**, non alla fine: alla fine il ring è
    // vuoto per costruzione, ed è il motivo per cui la prima stesura non si
    // accorgeva mai di essere in ritardo.
    const double occupancyOnEntry = static_cast<double>(m_input->available())
                                  / static_cast<double>(kRingSamples);

    int frames = 0;
    while (m_input->available() >= frameSamples && frames < kMaxFramesPerPump) {
        ++frames;
        if (m_input->read(m_frame.data(), frameSamples) != frameSamples)
            break;

        const State current = state();
        const bool wants = m_enabled.load(std::memory_order_acquire)
                        && m_engine != nullptr
                        && current != State::Degraded;

        if (!wants) {
            // Passaggio pulito: in bypass l'uscita è l'ingresso, campione per
            // campione. Non «quasi uguale» — uguale, ed è ciò che il test
            // dell'identità verifica.
            if (m_crossfade > 0) {
                // Si stava suonando il bagnato: si scende, non si stacca.
                for (std::size_t i = 0; i < frameSamples; ++i)
                    m_dry[i] = m_frame[i];
                m_engine->processFrame(m_frame.data());
                for (std::size_t i = 0; i < frameSamples; ++i) {
                    const float mix = static_cast<float>(m_crossfade)
                                    / static_cast<float>(m_crossfadeSamples);
                    m_frame[i] = m_dry[i] * (1.0f - mix) + m_frame[i] * mix;
                    if (m_crossfade > 0)
                        --m_crossfade;
                }
            }
            writeFrame(m_frame.data(), frameSamples);
            processed += frameSamples;
            continue;
        }

        for (std::size_t i = 0; i < frameSamples; ++i)
            m_dry[i] = m_frame[i];

        m_engine->processFrame(m_frame.data());

        if (m_crossfade < m_crossfadeSamples) {
            // Salita: il bagnato entra gradualmente. Uno scatto si sente, e
            // per giunta si sente proprio nel momento in cui si sta
            // giudicando se lo stadio serva.
            for (std::size_t i = 0; i < frameSamples; ++i) {
                const float mix = static_cast<float>(m_crossfade)
                                / static_cast<float>(m_crossfadeSamples);
                m_frame[i] = m_dry[i] * (1.0f - mix) + m_frame[i] * mix;
                if (m_crossfade < m_crossfadeSamples)
                    ++m_crossfade;
            }
        } else if (current == State::Warmup) {
            setState(State::Engaged);
        }

        writeFrame(m_frame.data(), frameSamples);
        processed += frameSamples;
    }

    if (processed == 0)
        return;

    // ── Sorveglianza ────────────────────────────────────────────────────
    const qint64 elapsedNs = m_clock.nsecsElapsed() - startNs;
    const double realTimeNs = processed * 1e9 / kSampleRate;
    if (realTimeNs > 0.0) {
        const double instant = elapsedNs / realTimeNs;
        // Media mobile: il costo di un singolo fotogramma oscilla troppo per
        // decidere qualcosa, e su quel numero si accende una spia.
        const double previous = m_load.load(std::memory_order_relaxed);
        m_load.store(previous * 0.9 + instant * 0.1, std::memory_order_relaxed);
    }

    const qint64 now = m_clock.nsecsElapsed();

    if (occupancyOnEntry > kOccupancyThreshold) {
        if (m_overThresholdSinceNs < 0)
            m_overThresholdSinceNs = now;
        else if (now - m_overThresholdSinceNs > kOverThresholdNs
                 && state() != State::Degraded) {
            // L'audio si sta accumulando perché non lo si consuma abbastanza
            // in fretta. Continuare vorrebbe dire buchi, e i buchi non si
            // spiegano: si torna all'asciutto e lo si dice.
            m_degradedAtNs = now;
            m_degrades.fetch_add(1, std::memory_order_relaxed);
            setState(State::Degraded);
            emit degraded(m_load.load(std::memory_order_relaxed));
        }
    } else {
        m_overThresholdSinceNs = -1;
    }

    if (state() == State::Degraded && m_enabled.load(std::memory_order_acquire)
        && m_degradedAtNs >= 0 && now - m_degradedAtNs > kRecoveryNs) {
        m_degradedAtNs = -1;
        setState(State::Warmup);
    }
}

} // namespace dsdr::dsp::neural
