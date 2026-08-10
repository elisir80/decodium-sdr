// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/TxEngine.h"

#include <QLoggingCategory>
#include <QTimer>

#include <algorithm>
#include <cmath>

Q_DECLARE_LOGGING_CATEGORY(dsdrCore)

namespace dsdr::core {

namespace {
/// Blocco di elaborazione, in campioni audio. A 48 kHz sono ~21 ms: la stessa
/// misura del blocco del backend demo, e per lo stesso motivo — abbastanza
/// corto da non aggiungere latenza al PTT, abbastanza lungo da rendere
/// trascurabile il costo del giro di timer.
constexpr std::size_t kAudioBlockFrames = 1024;

constexpr int kPumpIntervalMs = 5;

/// Un aggiornamento degli indicatori ogni venti giri: cinquanta volte al
/// secondo l'occhio non le distingue, e ogni segnale attraversa un thread.
constexpr int kMeterEvery = 20;

/// Venti aggiornamenti degli indicatori: una riga di diario al secondo.
constexpr int kLogEvery = 20;
} // namespace

TxEngine::TxEngine(QObject *parent)
    : QObject(parent)
{
    m_speech.configure(m_audioRate);
    m_modulator.configure(m_audioRate);
    m_keyer.configure(m_audioRate);

    m_audio.resize(kAudioBlockFrames);
    m_baseband.resize(kAudioBlockFrames);
}

TxEngine::~TxEngine() = default;

void TxEngine::start()
{
    if (m_timer)
        return;
    m_timer = new QTimer(this);
    m_timer->setInterval(kPumpIntervalMs);
    m_timer->setTimerType(Qt::PreciseTimer);
    connect(m_timer, &QTimer::timeout, this, &TxEngine::pump);
    m_timer->start();
}

void TxEngine::stop()
{
    m_transmitting.store(false, std::memory_order_release);
    if (m_timer) {
        m_timer->stop();
        m_timer->deleteLater();
        m_timer = nullptr;
    }
}

void TxEngine::attach(dsp::SpscRing<float> *txRing, double deviceRate, Domain domain)
{
    m_txRing = txRing;
    m_deviceRate = deviceRate;
    m_domain = domain;
    m_chainReady = rebuildChain();
}

void TxEngine::detach()
{
    m_transmitting.store(false, std::memory_order_release);
    m_txRing = nullptr;
    m_deviceRate = 0.0;
    m_chainReady = false;
}

void TxEngine::setMicSource(dsp::SpscRing<float> *micRing, double micRate)
{
    m_micRing = micRing;
    if (micRate > 0.0 && std::abs(micRate - m_audioRate) > 1.0) {
        // La catena è tarata su una sola frequenza audio. Un microfono che ne
        // imponesse un'altra andrebbe ricampionato, e finché non c'è quel
        // pezzo è più onesto dirlo che trasmettere fuori tono.
        emit refused(tr("Il microfono lavora a %1 Hz invece di %2: "
                        "la trasmissione userebbe la velocità sbagliata.")
                         .arg(micRate).arg(m_audioRate));
        m_micRing = nullptr;
    }
}

bool TxEngine::rebuildChain()
{
    if (!m_txRing || m_deviceRate <= 0.0)
        return false;

    if (m_domain == Domain::Audio) {
        // Verso una radio che modula da sé non c'è nulla da costruire: si
        // consegna audio, e l'audio è già alla frequenza giusta. Se non lo
        // fosse servirebbe un ricampionatore, e vale il rifiuto di sotto.
        if (std::abs(m_deviceRate - m_audioRate) > 1.0) {
            emit refused(tr("Il codec della radio lavora a %1 Hz invece di %2: "
                            "servirebbe un ricampionatore.")
                             .arg(m_deviceRate, 0, 'f', 0).arg(m_audioRate));
            return false;
        }
        m_interleaved.assign(kAudioBlockFrames, 0.0f);
        return true;
    }

    const double ratio = m_deviceRate / m_audioRate;
    const int factor = static_cast<int>(std::lround(ratio));
    if (factor < 1 || std::abs(ratio - factor) > 1e-6) {
        // Un rapporto non intero vorrebbe dire ricampionare a fattore
        // razionale: si può fare, ma non fingiamo di averlo. Meglio un
        // rifiuto spiegato che una portante fuori frequenza.
        emit refused(tr("La radio campiona a %1 S/s, che non è un multiplo "
                        "intero dei 48 kHz dell'audio: trasmissione non "
                        "disponibile a questa velocità.")
                         .arg(m_deviceRate, 0, 'f', 0));
        return false;
    }

    m_interpolation = factor;
    // La banda utile che deve sopravvivere alla salita: quanto il filtro di
    // trasmissione lascia passare, con un margine.
    const double passband = std::max(4000.0, static_cast<double>(m_settings.highHz) + 1000.0);
    if (!m_interpolator.configure(m_audioRate, factor, passband))
        return false;

    m_upsampled.assign(kAudioBlockFrames * static_cast<std::size_t>(factor),
                       dsp::Complex(0.0f, 0.0f));
    m_interleaved.assign(m_upsampled.size() * 2, 0.0f);

    m_nco.configure(m_deviceRate, m_offsetHz);
    return true;
}

void TxEngine::setSettings(const dsp::TxSettings &settings)
{
    const bool bandChanged = settings.highHz != m_settings.highHz;
    m_settings = settings;
    m_modulator.setSettings(settings);
    if (bandChanged && m_chainReady)
        m_chainReady = rebuildChain();
}

void TxEngine::setMicGainDb(double db)
{
    m_speech.setMicGainDb(db);
}

void TxEngine::setCompressionDb(double db)
{
    m_speech.setCompressionDb(db);
}

void TxEngine::setDrive(double drive)
{
    m_drive = static_cast<float>(std::clamp(drive, 0.0, 1.0));
}

void TxEngine::setOffsetHz(double hz)
{
    m_offsetHz = hz;
    if (m_deviceRate > 0.0)
        m_nco.setFrequency(hz);
}

void TxEngine::setTransmitting(bool transmitting)
{
    if (transmitting == m_transmitting.load(std::memory_order_acquire))
        return;

    if (transmitting && !m_chainReady) {
        emit refused(tr("La catena di trasmissione non è pronta."));
        return;
    }

    m_transmitting.store(transmitting, std::memory_order_release);

    if (transmitting) {
        // Si riparte puliti: la coda del microfono contiene quel che è entrato
        // mentre non si trasmetteva, e manderlo in aria adesso vorrebbe dire
        // trasmettere il passato.
        if (m_micRing)
            m_micRing->clear();
        m_speech.reset();
        m_modulator.reset();
        m_interpolator.reset();
        m_nco.reset();
        m_cwPhase = 0.0;
        m_framesSent = 0;
        m_starved.store(0, std::memory_order_relaxed);
        m_clock.restart();
    } else {
        m_outputPeak.store(0.0f, std::memory_order_relaxed);
        m_compressionDb.store(0.0f, std::memory_order_relaxed);
    }
}

void TxEngine::setKeyDown(bool down)
{
    m_keyDown = down;
    m_keyer.setKeyDown(down);
}

void TxEngine::pump()
{
    if (!m_transmitting.load(std::memory_order_acquire) || !m_chainReady)
        return;

    // Debito calcolato sull'orologio: è il numero di campioni audio che
    // sarebbero dovuti uscire da quando si è premuto il PTT.
    const qint64 elapsedNs = m_clock.nsecsElapsed();
    const quint64 due = static_cast<quint64>(
        static_cast<double>(elapsedNs) * 1e-9 * m_audioRate);
    if (due <= m_framesSent)
        return;

    quint64 pending = due - m_framesSent;

    // Se siamo rimasti indietro a lungo non si recupera all'infinito: un
    // burst in aria è peggio di qualche campione perso.
    constexpr quint64 kMaxCatchUp = kAudioBlockFrames * 4;
    if (pending > kMaxCatchUp) {
        m_framesSent += pending - kMaxCatchUp;
        pending = kMaxCatchUp;
    }

    while (pending > 0) {
        const std::size_t count = static_cast<std::size_t>(
            std::min<quint64>(pending, kAudioBlockFrames));
        produce(count);
        m_framesSent += count;
        pending -= count;
    }

    if (--m_meterCountdown <= 0) {
        m_meterCountdown = kMeterEvery;
        emit metersUpdated();

        // Una riga al secondo mentre si trasmette. È poca cosa, ed è l'unico
        // modo di distinguere «il motore non produce» da «produce e non
        // arriva alla radio» senza attaccare un debugger a una stazione che
        // sta chiamando.
        if (++m_logCountdown >= kLogEvery) {
            m_logCountdown = 0;
            qCInfo(dsdrCore) << "TX: campioni" << m_framesSent
                             << "picco" << m_outputPeak.load(std::memory_order_relaxed)
                             << "microfono a vuoto" << m_starved.load(std::memory_order_relaxed)
                             << (m_domain == Domain::Audio ? "audio" : "banda base");
        }
    }
}

void TxEngine::produce(std::size_t audioFrames)
{
    const bool cw = m_settings.mode == DemodMode::Cw || m_settings.mode == DemodMode::Cwr;

    if (cw) {
        // In CW il microfono non c'entra: la sorgente è il tasto, e l'audio
        // che entra nel modulatore è l'inviluppo.
        m_keyer.process(m_audio.data(), audioFrames);

        if (m_domain == Domain::Audio) {
            // Verso il codec di una radio, il punto dev'essere un **suono**:
            // l'inviluppo da solo è una tensione continua, e una radio in SSB
            // non trasmetterebbe nulla. Il tono è quello del monitor, così
            // quello che la radio manda in aria e quello che l'operatore
            // sente sono la stessa cosa.
            const double step = dsp::kTwoPi * m_settings.cwPitchHz / m_audioRate;
            for (std::size_t i = 0; i < audioFrames; ++i) {
                m_cwPhase += step;
                if (m_cwPhase > dsp::kTwoPi)
                    m_cwPhase -= dsp::kTwoPi;
                m_audio[i] *= static_cast<float>(std::sin(m_cwPhase));
            }
        }

        m_micPeak.store(m_keyDown ? 1.0f : 0.0f, std::memory_order_relaxed);
        m_compressionDb.store(0.0f, std::memory_order_relaxed);
    } else {
        const std::size_t got = m_micRing
            ? m_micRing->read(m_audio.data(), audioFrames) : 0;
        if (got < audioFrames) {
            std::fill(m_audio.begin() + static_cast<std::ptrdiff_t>(got),
                      m_audio.begin() + static_cast<std::ptrdiff_t>(audioFrames), 0.0f);
            m_starved.fetch_add(audioFrames - got, std::memory_order_relaxed);
        }
        m_speech.process(m_audio.data(), audioFrames);
        m_micPeak.store(m_speech.lastInputPeak(), std::memory_order_relaxed);
        m_compressionDb.store(m_speech.lastCompressionDb(), std::memory_order_relaxed);
    }

    if (m_domain == Domain::Audio) {
        // La radio ha già il suo modulatore, il suo filtro di banda e il suo
        // oscillatore: qui finisce la catena. Restano il processore di voce e
        // il livello, che sono nostri e servono comunque.
        float peak = 0.0f;
        for (std::size_t i = 0; i < audioFrames; ++i) {
            const float sample = m_audio[i] * m_drive;
            m_interleaved[i] = sample;
            peak = std::max(peak, std::abs(sample));
        }
        m_outputPeak.store(peak, std::memory_order_relaxed);
        if (m_txRing)
            m_txRing->write(m_interleaved.data(), audioFrames);
        return;
    }

    m_modulator.process(m_audio.data(), audioFrames, m_baseband.data());

    const std::size_t produced =
        m_interpolator.process(m_baseband.data(), audioFrames, m_upsampled.data());

    // Traslazione nella banda del device e livello d'uscita in un solo giro:
    // sono entrambe moltiplicazioni, e farne due passate raddoppierebbe il
    // traffico di memoria del punto più caldo della catena.
    m_nco.mixUp(m_upsampled.data(), m_upsampled.data(), produced);

    float peak = 0.0f;
    for (std::size_t i = 0; i < produced; ++i) {
        const dsp::Complex z = m_upsampled[i] * m_drive;
        m_interleaved[i * 2] = z.real();
        m_interleaved[i * 2 + 1] = z.imag();
        peak = std::max(peak, dsp::magnitudeSquared(z));
    }
    m_outputPeak.store(std::sqrt(peak), std::memory_order_relaxed);

    if (m_txRing)
        m_txRing->write(m_interleaved.data(), produced * 2);
}

} // namespace dsdr::core
