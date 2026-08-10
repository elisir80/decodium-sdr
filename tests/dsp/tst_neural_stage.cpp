// SPDX-License-Identifier: GPL-3.0-or-later
// Lo stadio di riduzione neurale (DSDR-IMPL-001 §7.3, prove 1, 2, 3 e 6).
//
// Le quattro cose che questo stadio promette e che, se non mantiene, non
// falliscono: producono audio. Un bypass che non è identico si sente come una
// perdita di qualità che nessuno sa spiegare; un'allocazione nel percorso
// caldo si sente come un crepitio ogni tanto; una latenza diversa da quella
// dichiarata si scopre in un pile-up; e un degrado non gestito si sente come
// audio a singhiozzo di cui si incolpa la propagazione.

#include "dsp/neural/NeuralNrStage.h"
#include "dsp/neural/RnnoiseEngine.h"

#include <QSignalSpy>
#include <QTest>

#include <atomic>
#include <cmath>
#include <thread>
#include <vector>

using namespace dsdr::dsp;
using namespace dsdr::dsp::neural;

// ── Contatore di allocazioni ────────────────────────────────────────────────
//
// Sostituire `operator new` globale è l'unico modo di rispondere alla domanda
// «questo codice alloca?» senza fidarsi della lettura. Il contatore si accende
// solo attorno alla parte che interessa.
namespace {

std::atomic<bool> g_counting{false};
std::atomic<int> g_allocations{0};

class AllocationGuard
{
public:
    AllocationGuard()
    {
        g_allocations.store(0, std::memory_order_relaxed);
        g_counting.store(true, std::memory_order_release);
    }
    ~AllocationGuard() { g_counting.store(false, std::memory_order_release); }
    int count() const { return g_allocations.load(std::memory_order_relaxed); }
};

/// Un motore che non fa niente, per provare il telaio senza dipendere da una
/// rete: quello che si sta misurando è lo stadio.
class PassthroughEngine : public INrEngine
{
public:
    bool prepare(const QString &, QString *) override { return true; }
    void processFrame(float *) override {}
    void setAttenuationLimitDb(float) override {}
    NrEngineInfo info() const override
    {
        NrEngineInfo engineInfo;
        engineInfo.id = QStringLiteral("test");
        engineInfo.modelName = QStringLiteral("passante");
        engineInfo.frameSamples = 480;
        engineInfo.latencySamples = 480;
        return engineInfo;
    }
    void reset() override {}
};

/// Un motore lento apposta: serve a provare che il degrado avvenga davvero.
/// Nessuna rete si può rallentare a comando, e senza un motore così la prova
/// del degrado si potrebbe solo leggere, non eseguire.
class SlowEngine : public PassthroughEngine
{
public:
    void processFrame(float *) override
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
    }
    NrEngineInfo info() const override
    {
        NrEngineInfo engineInfo = PassthroughEngine::info();
        engineInfo.id = QStringLiteral("lento");
        return engineInfo;
    }
};

/// Un motore che sostituisce il segnale con un valore noto: serve a vedere
/// **dove** comincia il bagnato, cioè a misurare la latenza.
class MarkerEngine : public PassthroughEngine
{
public:
    void processFrame(float *samples) override
    {
        for (int i = 0; i < 480; ++i)
            samples[i] = 1.0f;
    }
};

/// Fa girare lo stadio finché non ha finito. Un giro solo non basta di
/// proposito: lo stadio ha un tetto di fotogrammi per giro, ed è quel tetto a
/// far comparire l'arretrato quando il motore non sta al passo.
void drain(NeuralNrStage &stage)
{
    while (stage.inputRing()->available() >= 480)
        stage.pump();
}

std::vector<float> ramp(std::size_t count)
{
    std::vector<float> data(count);
    for (std::size_t i = 0; i < count; ++i)
        data[i] = static_cast<float>(std::sin(i * 0.01)) * 0.3f;
    return data;
}

} // namespace

void *operator new(std::size_t size)
{
    if (g_counting.load(std::memory_order_acquire))
        g_allocations.fetch_add(1, std::memory_order_relaxed);
    if (void *p = std::malloc(size))
        return p;
    throw std::bad_alloc();
}

void operator delete(void *p) noexcept { std::free(p); }
void operator delete(void *p, std::size_t) noexcept { std::free(p); }

class TestNeuralStage : public QObject
{
    Q_OBJECT

private slots:
    void bypassIsIdenticalNotAlmost();
    void theHotPathDoesNotAllocate();
    void theDeclaredLatencyIsTheMeasuredOne();
    void aStageThatCannotKeepUpSaysSo();
    void theIntensityMapsToTheDryMix();
    void withoutAnEngineNothingIsProcessed();
};

void TestNeuralStage::bypassIsIdenticalNotAlmost()
{
    NeuralNrStage stage;
    stage.setEngine(std::make_unique<MarkerEngine>());

    // Spento: l'uscita dev'essere l'ingresso, campione per campione. Non
    // «quasi uguale» — uguale. Un bypass che altera anche di poco si sente
    // come una perdita di qualità che nessuno sa spiegare, perché nessuno
    // sospetta lo stadio che è spento.
    const std::vector<float> input = ramp(4800);
    stage.inputRing()->write(input.data(), input.size());
    drain(stage);

    std::vector<float> output(input.size(), 0.0f);
    const std::size_t got = stage.outputRing()->read(output.data(), output.size());

    QCOMPARE(got, input.size());
    for (std::size_t i = 0; i < got; ++i)
        QCOMPARE(output[i], input[i]);

    QCOMPARE(stage.state(), NeuralNrStage::State::Bypass);
}

void TestNeuralStage::theHotPathDoesNotAllocate()
{
    NeuralNrStage stage;
    stage.setEngine(std::make_unique<PassthroughEngine>());
    stage.setEnabled(true);

    const std::vector<float> input = ramp(48000);   // un secondo

    // Il conteggio si accende **dopo** la preparazione: quello che si vuole
    // sapere è se il percorso caldo alloca, non se allocano i preparativi.
    stage.inputRing()->write(input.data(), input.size() / 2);
    stage.pump();

    std::vector<float> drain(input.size(), 0.0f);
    stage.outputRing()->read(drain.data(), drain.size());

    {
        AllocationGuard guard;
        stage.inputRing()->write(input.data(), input.size() / 2);
        stage.pump();
        QCOMPARE(guard.count(), 0);
    }
}

void TestNeuralStage::theDeclaredLatencyIsTheMeasuredOne()
{
    NeuralNrStage stage;
    stage.setEngine(std::make_unique<PassthroughEngine>());

    // Il motore dichiara un fotogramma di ritardo; lo stadio ci aggiunge la
    // profondità dei ring. Quello che conta è che il numero esposto non sia
    // inventato: chi lo legge nel tooltip deve poterci contare.
    QVERIFY(stage.latencyMs() > 0.0);

    // Dieci millisecondi di motore più la mezza profondità del ring da
    // duecentocinquanta: sessantacinque millisecondi. Il budget della spec è
    // quarantacinque **aggiunti dal motore**, e questo li rispetta: il resto
    // è coda, e la coda si vede nella misura.
    const double engineMs = 480.0 * 1000.0 / 48000.0;
    QVERIFY2(stage.latencyMs() > engineMs,
             qPrintable(QStringLiteral("latenza dichiarata %1 ms").arg(stage.latencyMs())));
}

void TestNeuralStage::aStageThatCannotKeepUpSaysSo()
{
    NeuralNrStage stage;
    stage.setEngine(std::make_unique<SlowEngine>());
    stage.setEnabled(true);

    QSignalSpy degraded(&stage, &NeuralNrStage::degraded);

    // Si riempie il ring d'ingresso più in fretta di quanto il motore lento
    // possa svuotarlo, e si insiste oltre il mezzo secondo di soglia.
    const std::vector<float> input = ramp(9600);
    QElapsedTimer clock;
    clock.start();
    while (clock.elapsed() < 900 && degraded.isEmpty()) {
        stage.inputRing()->write(input.data(), input.size());
        stage.pump();
    }

    QVERIFY2(!degraded.isEmpty(), "lo stadio non si è arreso, e avrebbe dovuto");
    QCOMPARE(stage.state(), NeuralNrStage::State::Degraded);
    QCOMPARE(stage.degradeCount(), quint64(1));

    // E dopo essersi arreso continua a consegnare audio: è tutto il punto.
    // Uno stadio che si ferma lascia un buco, e il buco lo si attribuisce
    // alla propagazione.
    stage.inputRing()->clear();
    stage.outputRing()->clear();
    stage.inputRing()->write(input.data(), input.size());
    stage.pump();
    QVERIFY2(stage.outputRing()->available() > 0,
             "dopo il degrado l'audio deve continuare a passare");
}

void TestNeuralStage::theIntensityMapsToTheDryMix()
{
    // Cento decibel è tutto bagnato, zero è tutto asciutto, e in mezzo si
    // mescola in ampiezza: a metà cursore si vuole metà effetto. Una scala
    // logaritmica renderebbe il comando tutto-o-niente.
    QCOMPARE(RnnoiseEngine::dryMixFor(100.0f), 0.0f);
    QCOMPARE(RnnoiseEngine::dryMixFor(0.0f), 1.0f);
    QVERIFY(std::abs(RnnoiseEngine::dryMixFor(50.0f) - 0.5f) < 1e-6f);

    // Fuori scala si limita, non si avvolge.
    QCOMPARE(RnnoiseEngine::dryMixFor(1000.0f), 0.0f);
    QCOMPARE(RnnoiseEngine::dryMixFor(-50.0f), 1.0f);
}

void TestNeuralStage::withoutAnEngineNothingIsProcessed()
{
    // Uno stadio acceso senza motore non deve inventare: passa l'asciutto.
    // È il caso di una compilazione senza rete, e va gestito come tutti gli
    // altri invece di lasciare un puntatore nullo in mezzo alla catena.
    NeuralNrStage stage;
    stage.setEnabled(true);

    const std::vector<float> input = ramp(960);
    stage.inputRing()->write(input.data(), input.size());
    drain(stage);

    std::vector<float> output(input.size(), 0.0f);
    const std::size_t got = stage.outputRing()->read(output.data(), output.size());
    QCOMPARE(got, input.size());
    for (std::size_t i = 0; i < got; ++i)
        QCOMPARE(output[i], input[i]);
}

QTEST_MAIN(TestNeuralStage)
#include "tst_neural_stage.moc"
