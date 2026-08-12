// SPDX-License-Identifier: GPL-3.0-or-later
// Il registratore della propria voce: dieci secondi che devono essere quelli.
//
// Un buffer circolare sbaglia in un modo solo, e non produce mai un errore:
// produce audio. Un indice fuori posto di un campione fa un clic a ogni giro;
// un vecchio e un nuovo mescolati fanno un riascolto cucito con due prese
// diverse. Chi lo sente pensa di avere una catena audio che distorce, e va a
// cercare il difetto dove non c'è.
#include "dsp/VoiceRecorder.h"

#include <QTest>

#include <numeric>
#include <vector>

using namespace dsdr::dsp;

namespace {

constexpr double kRate = 1000.0;   // mille campioni al secondo: i conti si leggono

/// Una rampa: ogni campione dice qual è. È il segnale giusto per un buffer
/// circolare, perché qualunque cosa fuori posto si vede a occhio.
std::vector<float> ramp(float from, std::size_t count)
{
    std::vector<float> out(count);
    for (std::size_t i = 0; i < count; ++i)
        out[i] = from + static_cast<float>(i);
    return out;
}

std::vector<float> drain(VoiceRecorder &recorder, std::size_t chunk = 64)
{
    std::vector<float> out;
    std::vector<float> buffer(chunk);
    while (true) {
        const std::size_t got = recorder.pull(buffer.data(), chunk);
        if (got == 0)
            break;
        out.insert(out.end(), buffer.begin(),
                   buffer.begin() + static_cast<std::ptrdiff_t>(got));
    }
    return out;
}

} // namespace

class TestVoiceRecorder : public QObject
{
    Q_OBJECT

private slots:
    void aVuotoNonSiRiascoltaNiente();
    void quelCheEntraEsceUguale();
    void restanoGliUltimiSecondiNonIPrimi();
    void leDueTracceNonSiMescolano();
    void siCommutaSenzaPerdereIlPunto();
    void tornareAParlareFermaIlRiascolto();
    void unBloccoPiuLungoDellaMemoriaNonSfonda();
};

void TestVoiceRecorder::aVuotoNonSiRiascoltaNiente()
{
    VoiceRecorder recorder;
    recorder.configure(kRate, 1.0);

    // Un riascolto che parte su niente e suona silenzio sarebbe peggio di un
    // rifiuto: l'operatore penserebbe che il microfono non entra.
    QVERIFY(!recorder.hasContent());
    QVERIFY(!recorder.startPlayback(VoiceRecorder::Source::Wet));
    QVERIFY(!recorder.isPlaying());

    float sample = 0.0f;
    QCOMPARE(recorder.pull(&sample, 1), std::size_t(0));
}

void TestVoiceRecorder::quelCheEntraEsceUguale()
{
    VoiceRecorder recorder;
    recorder.configure(kRate, 1.0);          // mille campioni

    const auto dry = ramp(0.0f, 400);
    const auto wet = ramp(1000.0f, 400);
    recorder.record(dry.data(), wet.data(), dry.size());

    QVERIFY(recorder.hasContent());
    QCOMPARE(recorder.recordedSeconds(), 0.4);

    QVERIFY(recorder.startPlayback(VoiceRecorder::Source::Wet));
    const auto out = drain(recorder);

    QCOMPARE(out.size(), std::size_t(400));
    for (std::size_t i = 0; i < out.size(); ++i)
        QCOMPARE(out[i], wet[i]);

    // Finito il materiale il riascolto si chiude da sé: un tasto che resta
    // premuto su una riproduzione già finita è un tasto che mente.
    QVERIFY(!recorder.isPlaying());
}

void TestVoiceRecorder::restanoGliUltimiSecondiNonIPrimi()
{
    VoiceRecorder recorder;
    recorder.configure(kRate, 1.0);

    // Si parla per due secondi e mezzo in una memoria che ne tiene uno. Quello
    // che deve restare è l'ultimo secondo, non il primo: ci si riascolta per
    // sentire com'è venuta la frase appena detta.
    const auto voice = ramp(0.0f, 2500);
    recorder.record(voice.data(), voice.data(), voice.size());

    QCOMPARE(recorder.recordedSeconds(), 1.0);

    QVERIFY(recorder.startPlayback(VoiceRecorder::Source::Wet));
    const auto out = drain(recorder, 37);   // un blocco che non divide niente

    QCOMPARE(out.size(), std::size_t(1000));
    QCOMPARE(out.front(), 1500.0f);
    QCOMPARE(out.back(), 2499.0f);

    // E in mezzo non ci sono salti: è qui che un buffer circolare tradisce.
    for (std::size_t i = 1; i < out.size(); ++i)
        QCOMPARE(out[i] - out[i - 1], 1.0f);
}

void TestVoiceRecorder::leDueTracceNonSiMescolano()
{
    VoiceRecorder recorder;
    recorder.configure(kRate, 1.0);

    const auto dry = ramp(0.0f, 1800);
    const auto wet = ramp(10000.0f, 1800);
    recorder.record(dry.data(), wet.data(), dry.size());

    recorder.startPlayback(VoiceRecorder::Source::Dry);
    const auto before = drain(recorder);
    recorder.startPlayback(VoiceRecorder::Source::Wet);
    const auto after = drain(recorder);

    QCOMPARE(before.size(), std::size_t(1000));
    QCOMPARE(after.size(), std::size_t(1000));

    // Le due tracce sono avvolte allo stesso punto ma sono due: se
    // condividessero un indice, il confronto prima/dopo mostrerebbe due pezzi
    // di momenti diversi — e non lo direbbe nessuno.
    for (std::size_t i = 0; i < before.size(); ++i)
        QCOMPARE(after[i] - before[i], 10000.0f);
}

void TestVoiceRecorder::siCommutaSenzaPerdereIlPunto()
{
    VoiceRecorder recorder;
    recorder.configure(kRate, 1.0);

    const auto dry = ramp(0.0f, 1000);
    const auto wet = ramp(10000.0f, 1000);
    recorder.record(dry.data(), wet.data(), dry.size());

    recorder.startPlayback(VoiceRecorder::Source::Dry);
    std::vector<float> buffer(400);
    QCOMPARE(recorder.pull(buffer.data(), 400), std::size_t(400));
    QCOMPARE(buffer.front(), 0.0f);

    // Si commuta a metà. Il punto non si tocca: è tutto il senso del
    // confronto, sentire la stessa sillaba nei due modi di seguito. Ripartire
    // da capo costringerebbe a ricordare com'era, e il ricordo di un suono
    // dura meno di un secondo.
    recorder.setPlaybackSource(VoiceRecorder::Source::Wet);
    QCOMPARE(recorder.pull(buffer.data(), 400), std::size_t(400));
    QCOMPARE(buffer.front(), 10400.0f);
    QVERIFY(recorder.isPlaying());
}

void TestVoiceRecorder::tornareAParlareFermaIlRiascolto()
{
    VoiceRecorder recorder;
    recorder.configure(kRate, 1.0);

    const auto voice = ramp(0.0f, 1000);
    recorder.record(voice.data(), voice.data(), voice.size());
    recorder.startPlayback(VoiceRecorder::Source::Wet);
    QVERIFY(recorder.isPlaying());

    // Premere il PTT mentre ci si sta risentendo vuol dire tornare a parlare.
    // Continuare a scrivere sotto la testina di lettura non darebbe un errore:
    // darebbe un riascolto cucito con due prese diverse.
    const auto more = ramp(5000.0f, 100);
    recorder.record(more.data(), more.data(), more.size());
    QVERIFY(!recorder.isPlaying());

    float sample = 0.0f;
    QCOMPARE(recorder.pull(&sample, 1), std::size_t(0));
}

void TestVoiceRecorder::unBloccoPiuLungoDellaMemoriaNonSfonda()
{
    VoiceRecorder recorder;
    recorder.configure(kRate, 0.1);          // cento campioni

    // Di un blocco più lungo della memoria resterebbe comunque solo la coda:
    // copiarlo tutto vorrebbe dire girare più volte sullo stesso buffer, e in
    // un componente che non deve mai scrivere fuori dai suoi limiti è il punto
    // esatto in cui succederebbe.
    const auto huge = ramp(0.0f, 350);
    recorder.record(huge.data(), huge.data(), huge.size());

    QCOMPARE(recorder.recordedSeconds(), 0.1);
    recorder.startPlayback(VoiceRecorder::Source::Wet);
    const auto out = drain(recorder);

    QCOMPARE(out.size(), std::size_t(100));
    QCOMPARE(out.front(), 250.0f);
    QCOMPARE(out.back(), 349.0f);
}

QTEST_MAIN(TestVoiceRecorder)
#include "tst_voice_recorder.moc"
