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

/// I due toni della prova di linearità. Non armonicamente imparentati — 1900
/// non è un multiplo di 700 — perché i prodotti di intermodulazione devono
/// cadere dove non c'è nient'altro, altrimenti si confondono con le armoniche
/// e la prova non dice niente.
constexpr double kToneAHz = 700.0;
constexpr double kToneBHz = 1900.0;

/// Tono singolo, in mezzo alla passata SSB: da lì passa qualunque filtro di
/// trasmissione senza attenuarlo, e il livello che si legge è quello vero.
constexpr double kTuneToneHz = 1500.0;

/// Ampiezza di picco del segnale di prova. Non fondo scala: il livello si
/// regola con il comando d'uscita, e partire già al massimo toglierebbe la
/// possibilità di alzarlo.
constexpr float kTestAmplitude = 0.8f;

/// Trasformata del monitor. Più corta di quella della ricezione: qui non si
/// cercano segnali deboli in mezzo al rumore, si guarda la forma del proprio
/// segnale, e una risoluzione più grossa la disegna con meno ritardo.
constexpr int kMonitorFftSize = 2048;

/// Capienza del ring del riascolto, in frame stereo.
constexpr std::size_t kPlaybackRingFrames = 24000;
} // namespace

TxEngine::TxEngine(QObject *parent)
    : QObject(parent)
    , m_spectrum(new SpectrumFeed(this))
{
    // Una sola trasformata per riga: mediarle addolcirebbe proprio i picchi
    // che si sta cercando di vedere.
    m_spectrum->setAveraging(1);

    m_speech.configure(m_audioRate);
    m_modulator.configure(m_audioRate);
    m_cfc.configure(m_audioRate);
    m_gate.configure(m_audioRate);
    // Il registratore con gli altri: dieci secondi di due tracce sono meno di
    // quattro megabyte, e nascono qui perché nel percorso caldo non si alloca.
    m_recorder.configure(m_audioRate);
    m_dry.assign(kAudioBlockFrames, 0.0f);
    m_playbackMono.assign(kAudioBlockFrames, 0.0f);
    m_playbackStereo.assign(kAudioBlockFrames * 2, 0.0f);
    m_playback.reset(kPlaybackRingFrames * 2);
    m_leveller.configure(m_audioRate);
    m_limiter.configure(m_audioRate);
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
        configureMonitor();
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
    configureMonitor();
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
    if (qFuzzyCompare(m_offsetHz, hz))
        return;
    m_offsetHz = hz;
    if (m_deviceRate > 0.0)
        m_nco.setFrequency(hz);
    if (m_domain == Domain::Audio)
        configureMonitor();
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
        m_tonePhaseA = 0.0;
        m_tonePhaseB = 0.0;
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

void TxEngine::setTestSignal(int signal)
{
    m_testSignal.store(signal, std::memory_order_release);
    m_tonePhaseA = 0.0;
    m_tonePhaseB = 0.0;
}

void TxEngine::setMonitorCenter(qint64 hz)
{
    if (m_monitorCenterHz == hz)
        return;
    m_monitorCenterHz = hz;
    configureMonitor();
}

void TxEngine::configureMonitor()
{
    // La banda del monitor è quella in cui si trasmette: con una radio che
    // modula a bordo sono i 48 kHz dell'audio, con un SDR quelli del device.
    // Deve combaciare con il panadattatore della ricezione, o passando in
    // trasmissione la scala salterebbe sotto gli occhi.
    const double span = m_domain == Domain::Audio ? m_audioRate : m_deviceRate;
    if (span <= 0.0)
        return;

    // In banda base i campioni escono già traslati dall'NCO, e il centro
    // della finestra è quello della banda. Con una radio che modula a bordo
    // non c'è alcuna traslazione: il segnale nasce attorno alla frequenza del
    // canale, ed è lì che va ancorata la scala.
    const qint64 center = m_monitorCenterHz
        + (m_domain == Domain::Audio ? static_cast<qint64>(m_offsetHz) : 0);

    m_analyzer.configure(kMonitorFftSize, span);
    m_analyzer.setAveraging(0.4f);
    m_analyzer.setOverlap(0.5f);
    m_spectrum->configure(kMonitorFftSize, span, center);
}

void TxEngine::pump()
{
    // Il riascolto viene prima, e gira anche a PTT libero: è proprio in quel
    // momento che serve. Il timer c'è già, e il registratore vive su questo
    // thread — farlo pompare da qualcun altro vorrebbe dire un secondo thread
    // e un lucchetto per niente.
    pumpPlayback();

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

void TxEngine::pumpPlayback()
{
    const bool wasPlaying = m_playbackActive;

    if (!m_recorder.isPlaying()) {
        if (wasPlaying) {
            m_playbackActive = false;
            emit playbackChanged();
        }
        return;
    }

    // Si riempie il ring fino a lasciarlo mezzo pieno: abbastanza da coprire
    // un giro di timer perso, poco abbastanza che «ferma» fermi subito e non
    // mezzo secondo dopo.
    const std::size_t target = m_playback.capacity() / 2;
    while (m_playback.available() < target) {
        const std::size_t room = (m_playback.space() / 2);
        const std::size_t want = std::min(kAudioBlockFrames, room);
        if (want == 0)
            break;

        const std::size_t got = m_recorder.pull(m_playbackMono.data(), want);
        if (got == 0)
            break;

        // Il registratore tiene una traccia mono, l'uscita audio vuole due
        // canali: si duplica. La voce riascoltata sta in mezzo alla testa, che
        // è dove sta anche quando la si sente parlando.
        for (std::size_t i = 0; i < got; ++i) {
            m_playbackStereo[i * 2] = m_playbackMono[i];
            m_playbackStereo[i * 2 + 1] = m_playbackMono[i];
        }
        m_playback.write(m_playbackStereo.data(), got * 2);
    }

    if (!wasPlaying) {
        m_playbackActive = true;
        m_playbackCountdown = 0;
    }

    // Mentre si riascolta non si trasmette, quindi gli indicatori non battono:
    // senza questo, la barra del riascolto resterebbe ferma sull'inizio.
    if (--m_playbackCountdown <= 0) {
        m_playbackCountdown = kMeterEvery;
        emit playbackChanged();
    }
}

void TxEngine::playRecording(int source)
{
    if (m_transmitting.load(std::memory_order_acquire)) {
        emit refused(tr("Non ci si può riascoltare mentre si trasmette."));
        return;
    }
    if (!m_recorder.hasContent()) {
        emit refused(tr("Non c'è ancora niente da riascoltare: la registrazione "
                        "si riempie mentre si trasmette."));
        return;
    }

    // Il ring si svuota prima: quello che c'era dentro è la coda del riascolto
    // precedente, e sentirla in testa a quello nuovo sarebbe un salto.
    m_playback.clear();
    m_recorder.startPlayback(source == 0 ? dsp::VoiceRecorder::Source::Dry
                                         : dsp::VoiceRecorder::Source::Wet);
    pumpPlayback();
}

void TxEngine::stopRecordingPlayback()
{
    m_recorder.stopPlayback();
    m_playback.clear();
    if (m_playbackActive) {
        m_playbackActive = false;
        emit playbackChanged();
    }
}

void TxEngine::setPlaybackSource(int source)
{
    const auto wanted = source == 0 ? dsp::VoiceRecorder::Source::Dry
                                    : dsp::VoiceRecorder::Source::Wet;
    if (m_recorder.playbackSource() == wanted)
        return;

    m_recorder.setPlaybackSource(wanted);

    // Il ring va svuotato, altrimenti si continuerebbe a sentire la traccia di
    // prima per il tempo che ci sta dentro — mezzo secondo di ritardo fra il
    // gesto e l'effetto, che è quanto basta a non capire più quale sia quale.
    m_playback.clear();
    pumpPlayback();
    emit playbackChanged();
}

void TxEngine::produce(std::size_t audioFrames)
{
    const bool cw = m_settings.mode == DemodMode::Cw || m_settings.mode == DemodMode::Cwr;
    const auto test = static_cast<TestSignal>(m_testSignal.load(std::memory_order_acquire));

    if (test != TestSignal::None) {
        if (cw) {
            // In CW accordare vuol dire tenere giù il tasto: l'inviluppo resta
            // a uno e la portante è piena. Un tono ci farebbe uscire una
            // portante modulata in ampiezza, che non è quello che chiede
            // l'accordatore.
            std::fill_n(m_audio.begin(), audioFrames, kTestAmplitude);
        } else {
            const double stepA = dsp::kTwoPi
                * (test == TestSignal::TwoTone ? kToneAHz : kTuneToneHz) / m_audioRate;
            const double stepB = dsp::kTwoPi * kToneBHz / m_audioRate;
            // Due toni si dividono l'ampiezza, non la raddoppiano: la potenza
            // di picco resta quella del tono singolo, ed è l'unico modo di
            // confrontare le due prove fra loro.
            const float amplitude = test == TestSignal::TwoTone
                ? kTestAmplitude * 0.5f : kTestAmplitude;

            for (std::size_t i = 0; i < audioFrames; ++i) {
                m_tonePhaseA += stepA;
                if (m_tonePhaseA > dsp::kTwoPi)
                    m_tonePhaseA -= dsp::kTwoPi;
                float sample = amplitude * static_cast<float>(std::sin(m_tonePhaseA));

                if (test == TestSignal::TwoTone) {
                    m_tonePhaseB += stepB;
                    if (m_tonePhaseB > dsp::kTwoPi)
                        m_tonePhaseB -= dsp::kTwoPi;
                    sample += amplitude * static_cast<float>(std::sin(m_tonePhaseB));
                }
                m_audio[i] = sample;
            }
        }

        // Niente processore di voce: comprimere un tono non ha senso, e un
        // limitatore che intervenisse falserebbe proprio la misura per cui la
        // prova esiste.
        m_micPeak.store(0.0f, std::memory_order_relaxed);
        m_compressionDb.store(0.0f, std::memory_order_relaxed);
    } else if (cw) {
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
        // ── La catena, nell'ordine in cui il segnale la attraversa ──────
        //
        // Il gate per primo: dopo la compressione il rumore è già stato alzato
        // al livello della voce, e non c'è più una soglia che li separi. Poi
        // il leveller, che insegue la distanza dal microfono in secondi, e
        // solo dopo il compressore, che doma le punte in millisecondi — una
        // costante di tempo sola non può fare bene entrambe.
        // La voce com'è, prima che la catena la tocchi. Va copiata adesso:
        // ogni stadio lavora sul posto, e dopo il gate l'originale non c'è più.
        std::copy_n(m_audio.data(), audioFrames, m_dry.data());

        m_gate.process(m_audio.data(), audioFrames);
        m_leveller.process(m_audio.data(), audioFrames);

        m_speech.process(m_audio.data(), audioFrames);
        // Il multibanda dopo il processore di voce: quello livella e comprime
        // la voce nel suo insieme, questo la divide in quattro e tratta ogni
        // parte per conto suo. Invertirli vorrebbe dire dare al multibanda un
        // segnale già schiacciato, su cui non ha più niente da distinguere.
        m_cfc.process(m_audio.data(), audioFrames);
        // Il limiter chiude: tutto quello che viene prima lavora sul suono,
        // lui lavora sul fondo scala. Oltre il tetto il modulatore tosa, e
        // tosare in banda base vuol dire allargarsi sui vicini.
        m_limiter.process(m_audio.data(), audioFrames);

        // Le due tracce finiscono qui, sempre, senza che nessuno abbia dovuto
        // premere «registra» prima di parlare: ci si accorge di voler
        // riascoltare solo dopo aver parlato.
        m_recorder.record(m_dry.data(), m_audio.data(), audioFrames);

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

        // Per il monitor moduliamo noi lo stesso audio: il segnale in aria lo
        // farà la radio, e questo è ciò che ne uscirà. Costa un filtro
        // complesso a 48 kHz, e solo mentre si trasmette.
        m_modulator.process(m_audio.data(), audioFrames, m_baseband.data());
        if (m_analyzer.push(m_baseband.data(), audioFrames))
            m_spectrum->publish(m_analyzer.magnitudesDb().data());
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
        m_upsampled[i] = z;
        m_interleaved[i * 2] = z.real();
        m_interleaved[i * 2 + 1] = z.imag();
        peak = std::max(peak, dsp::magnitudeSquared(z));
    }
    m_outputPeak.store(std::sqrt(peak), std::memory_order_relaxed);

    if (m_txRing)
        m_txRing->write(m_interleaved.data(), produced * 2);

    // Il monitor guarda esattamente quello che è finito nel ring, livello
    // compreso: uno spettro che ignorasse il comando d'uscita mostrerebbe un
    // segnale che nessuno sta trasmettendo.
    if (m_analyzer.push(m_upsampled.data(), produced))
        m_spectrum->publish(m_analyzer.magnitudesDb().data());
}

} // namespace dsdr::core
