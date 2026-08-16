// SPDX-License-Identifier: GPL-3.0-or-later
// Il percorso di ricezione dei backend server-DSP: audio dentro, audio fuori.
//
// Una radio tradizionale consegna l'audio che ha già demodulato. Il motore lo
// riporta a segnale analitico e lo tratta come banda base: se il conto è
// giusto, un tono di 1000 Hz entrato dal codec deve uscire a 1000 Hz dopo
// essere passato per il filtro di canale e il demodulatore — che nel frattempo
// gli hanno fatto fare il giro completo.
//
// È un giro che sembra inutile e non lo è: è ciò che permette a notch, EMNR,
// rete neurale, APF e binaurale (SPEC-003) di funzionare su una radio del 2016
// senza sapere che lo sono.

#include "core/DspEngine.h"
#include "dsp/SpscRing.h"

#include <QElapsedTimer>
#include <QTest>
#include <QThread>

#include <cmath>
#include <vector>

using namespace dsdr;
using namespace dsdr::core;

namespace {

constexpr double kRate = 48000.0;

std::vector<float> tone(double hz, std::size_t frames, float amplitude = 0.3f)
{
    std::vector<float> out(frames);
    for (std::size_t i = 0; i < frames; ++i) {
        out[i] = amplitude * static_cast<float>(
            std::sin(dsp::kTwoPi * hz * static_cast<double>(i) / kRate));
    }
    return out;
}

/// Ampiezza a una frequenza, misurata su audio **stereo interleaved**: si
/// guarda il canale sinistro, che con un demodulatore mono porta tutto.
double amplitudeAt(const std::vector<float> &stereo, double hz, std::size_t skipFrames)
{
    const std::size_t frames = stereo.size() / 2;
    if (frames <= skipFrames)
        return 0.0;
    const std::size_t n = frames - skipFrames;

    double re = 0.0;
    double im = 0.0;
    double norm = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double window = 0.5 - 0.5 * std::cos(dsp::kTwoPi * i / (n - 1));
        const double phase = -dsp::kTwoPi * hz * static_cast<double>(i) / kRate;
        const double sample = stereo[(skipFrames + i) * 2];
        re += window * sample * std::cos(phase);
        im += window * sample * std::sin(phase);
        norm += window;
    }
    // Un tono reale porta metà ampiezza su ciascuna delle due frequenze: il
    // fattore due la rimette com'era.
    return 2.0 * std::sqrt(re * re + im * im) / norm;
}

/// Un canale che non elabora nulla oltre il necessario: senza AGC il livello
/// d'uscita è confrontabile con quello d'ingresso, ed è proprio quello che
/// questi test vogliono misurare.
dsp::ChannelSettings plainChannel(DemodMode mode)
{
    dsp::ChannelSettings settings;
    settings.mode = mode;
    settings.offsetHz = 0.0;
    settings.filterLowHz = 300;
    settings.filterHighHz = 2700;
    settings.agcMode = AgcMode::Off;
    settings.volume = 1.0f;
    return settings;
}

/// Spinge l'audio nel ring a blocchi e raccoglie ciò che esce.
std::vector<float> runThrough(DspEngine &engine,
                              dsp::SpscRing<float> &input,
                              const std::vector<float> &audio)
{
    std::vector<float> collected;
    std::vector<float> chunk(8192);

    std::size_t offset = 0;
    while (offset < audio.size()) {
        const std::size_t count = std::min<std::size_t>(2048, audio.size() - offset);
        input.write(audio.data() + offset, count);
        offset += count;

        hal::AudioFrame frame;
        frame.sampleRate = kRate;
        frame.frameCount = static_cast<quint32>(count);
        engine.onAudioFrameReady(frame);

        while (true) {
            const std::size_t got = engine.audioRing()->read(chunk.data(), chunk.size());
            if (got == 0)
                break;
            collected.insert(collected.end(), chunk.begin(),
                             chunk.begin() + static_cast<std::ptrdiff_t>(got));
        }
    }
    return collected;
}

} // namespace

class TestAudioSource : public QObject
{
    Q_OBJECT

private slots:
    void aToneGoesInAndComesOutAtTheSameFrequency();
    void theLowerSidebandIsNotSilence();
    void whatIsOutsideThePassbandDoesNotSurvive();
    void withoutASourceNothingComesOut();
    void foreignFrameNotificationsLeaveRoomForChannelControl();
};

void TestAudioSource::aToneGoesInAndComesOutAtTheSameFrequency()
{
    DspEngine engine;
    dsp::SpscRing<float> input(1 << 20);

    engine.setAudioSource(&input, kRate, 7'100'000);
    engine.setAudioSideband(static_cast<int>(DspEngine::Sideband::Upper));
    engine.addChannel(1, plainChannel(DemodMode::Usb));

    const auto audio = tone(1000.0, 48000);
    const auto out = runThrough(engine, input, audio);

    QVERIFY2(out.size() > 20000, "il motore non ha prodotto audio");

    // Si salta l'inizio: il filtro analitico ha settecento tap, e la sua coda
    // è mezzo blocco di silenzio che non racconta niente.
    const std::size_t skip = 4096;
    const double wanted = amplitudeAt(out, 1000.0, skip);
    const double elsewhere = amplitudeAt(out, 1700.0, skip);

    QVERIFY2(wanted > 0.05,
             qPrintable(QStringLiteral("il tono esce a %1").arg(wanted)));
    QVERIFY2(elsewhere < wanted * 0.05,
             qPrintable(QStringLiteral("energia fuori posto: %1 contro %2")
                            .arg(elsewhere).arg(wanted)));
}

void TestAudioSource::theLowerSidebandIsNotSilence()
{
    // In LSB il segnale sta sotto il VFO: se il motore non coniugasse il
    // segnale analitico, il filtro di canale — che in LSB guarda le frequenze
    // negative — non troverebbe nulla. Il difetto si presenterebbe come «la
    // radio in LSB non si sente», e nessuno lo cercherebbe qui.
    DspEngine engine;
    dsp::SpscRing<float> input(1 << 20);

    engine.setAudioSource(&input, kRate, 7'100'000);
    engine.setAudioSideband(static_cast<int>(DspEngine::Sideband::Lower));
    engine.addChannel(1, plainChannel(DemodMode::Lsb));

    const auto audio = tone(1000.0, 48000);
    const auto out = runThrough(engine, input, audio);

    QVERIFY(out.size() > 20000);
    const double wanted = amplitudeAt(out, 1000.0, 4096);
    QVERIFY2(wanted > 0.05,
             qPrintable(QStringLiteral("in LSB esce %1").arg(wanted)));
}

void TestAudioSource::whatIsOutsideThePassbandDoesNotSurvive()
{
    DspEngine engine;
    dsp::SpscRing<float> input(1 << 20);

    engine.setAudioSource(&input, kRate, 7'100'000);
    engine.setAudioSideband(static_cast<int>(DspEngine::Sideband::Upper));
    engine.addChannel(1, plainChannel(DemodMode::Usb));

    // 6 kHz: fuori dal filtro del canale e fuori dalla banda che una radio
    // consegna. Deve sparire, altrimenti il panadattatore mostrerebbe segnali
    // dove la radio non sente.
    const auto audio = tone(6000.0, 48000);
    const auto out = runThrough(engine, input, audio);

    QVERIFY(out.size() > 20000);
    QVERIFY2(amplitudeAt(out, 6000.0, 4096) < 0.005,
             qPrintable(QStringLiteral("passa a %1").arg(amplitudeAt(out, 6000.0, 4096))));
}

void TestAudioSource::withoutASourceNothingComesOut()
{
    DspEngine engine;
    engine.addChannel(1, plainChannel(DemodMode::Usb));

    hal::AudioFrame frame;
    frame.sampleRate = kRate;
    frame.frameCount = 1024;
    engine.onAudioFrameReady(frame);

    QCOMPARE(engine.audioRing()->available(), std::size_t(0));
}

void TestAudioSource::foreignFrameNotificationsLeaveRoomForChannelControl()
{
    // È il caso del backend reale: il produttore è su un thread diverso dal
    // DSP e può annunciare molti blocchi mentre la UI mette il primo canale
    // in coda. Non deve eseguire DSP nel proprio thread né riempire la coda
    // del DSP con una notifica per frame; altrimenti su ARM addChannel arriva
    // dopo che la banda è già stata consumata e non nasce né audio né spettro.
    auto *engine = new DspEngine;
    dsp::SpscRing<float> input(1 << 17);
    QThread dspThread;

    engine->setAudioSource(&input, kRate, 7'100'000);
    engine->setAudioSideband(static_cast<int>(DspEngine::Sideband::Upper));
    engine->moveToThread(&dspThread);

    // Quattro turni del DSP consumano 4 × kMaxBlockFrames: la raffica deve
    // superarli, altrimenti non rimane nulla da elaborare dopo addChannel.
    const auto audio = tone(1000.0, 6 * dsp::kMaxBlockFrames);
    QCOMPARE(input.write(audio.data(), audio.size()), audio.size());

    hal::AudioFrame frame;
    frame.sampleRate = kRate;
    frame.frameCount = 1024;
    for (int i = 0; i < 256; ++i)
        engine->onAudioFrameReady(frame);

    const bool channelQueued = QMetaObject::invokeMethod(engine, [engine] {
        engine->addChannel(1, plainChannel(DemodMode::Usb));
    }, Qt::QueuedConnection);
    // Il thread parte solo dopo aver fissato l'ordine: il primo giro DSP è
    // davanti a addChannel, la sua continuazione deve invece restare dietro.
    // È la versione deterministica della coda piena osservata sul runner ARM.
    dspThread.start();

    std::vector<float> chunk(4096);
    bool producedAudio = false;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 3000 && !producedAudio) {
        const std::size_t got = engine->audioRing()->read(chunk.data(), chunk.size());
        for (std::size_t i = 0; i < got; ++i) {
            if (std::abs(chunk[i]) > 0.01f) {
                producedAudio = true;
                break;
            }
        }
        if (!producedAudio)
            QTest::qWait(10);
    }
    // Il cleanup è esplicito anche quando l'asserzione sotto fallisce: un
    // QThread lasciato vivo trasformerebbe un test negativo in un SIGABRT.
    const bool destroyed = QMetaObject::invokeMethod(engine, [engine] {
        engine->clearSource();
        delete engine;
    }, Qt::BlockingQueuedConnection);
    dspThread.quit();
    const bool stopped = dspThread.wait(3000);

    QVERIFY(channelQueued);
    QVERIFY(destroyed);
    QVERIFY(stopped);
    QVERIFY2(producedAudio, "la raffica di frame ha affamato il comando addChannel");
}

QTEST_MAIN(TestAudioSource)
#include "tst_audio_source.moc"
