// SPDX-License-Identifier: GPL-3.0-or-later
// L'interlock digitale (DSDR-SPEC-003 §8.3, IMPL-001 §7.3 prova 5).
//
// C'è una regola che non si può lasciare alla buona volontà: l'audio uscito
// dalla riduzione neurale non deve mai arrivare a un decodificatore.
//
// Il motivo è che una rete addestrata sulla voce fa il suo mestiere anche
// quando il segnale non è voce: toglie quello che non le somiglia. Su un FT8 al
// limite del rumore toglie il segnale. Il decodificatore non se ne accorge —
// decodifica meno, e nessuno ha modo di sapere perché. Non c'è errore, non c'è
// avviso: c'è una stazione che non si aggancia più.
//
// Per questo il rifiuto avviene quando si costruisce il collegamento, non
// quando passano i campioni: un controllo a runtime scatterebbe la millesima
// volta, in mezzo a un contest, e nessuno saprebbe leggerlo.

#include "audio/AudioGraph.h"
#include "dsp/SpscRing.h"

#include <QTest>

using namespace dsdr::audio;

class TestAudioGraph : public QObject
{
    Q_OBJECT

private slots:
    void theRuleIsPureAndCheckable();
    void theNeuralOutputReachesTheEar();
    void theNeuralOutputIsRefusedEverywhereElse();
    void aRefusalExplainsItself();
    void cleanAudioGoesAnywhere();
    void aRefusedRouteDoesNotEnterTheGraph();
    void aNodeWithoutARingIsNotARoute();
};

void TestAudioGraph::theRuleIsPureAndCheckable()
{
    // La regola si può verificare senza costruire niente: è una funzione, e
    // questo è il motivo per cui è una funzione.
    QVERIFY(mayRoute(AudioTag::EarOnly, AudioSink::Ear));
    QVERIFY(!mayRoute(AudioTag::EarOnly, AudioSink::DigitalDecoder));
    QVERIFY(!mayRoute(AudioTag::EarOnly, AudioSink::AudioRecorder));
    QVERIFY(!mayRoute(AudioTag::EarOnly, AudioSink::NetworkStream));
    QVERIFY(!mayRoute(AudioTag::EarOnly, AudioSink::Transmit));

    QVERIFY(mayRoute(AudioTag::Clean, AudioSink::Ear));
    QVERIFY(mayRoute(AudioTag::Clean, AudioSink::DigitalDecoder));
    QVERIFY(mayRoute(AudioTag::Clean, AudioSink::AudioRecorder));
    QVERIFY(mayRoute(AudioTag::Clean, AudioSink::NetworkStream));
    QVERIFY(mayRoute(AudioTag::Clean, AudioSink::Transmit));
}

void TestAudioGraph::theNeuralOutputReachesTheEar()
{
    dsdr::dsp::SpscRing<float> ring(1024);
    AudioGraph graph;

    const AudioNode neural{QStringLiteral("riduzione neurale"),
                           AudioTag::EarOnly, &ring};
    QVERIFY(graph.connect(neural, AudioSink::Ear));
    QCOMPARE(graph.routes().size(), 1);
}

void TestAudioGraph::theNeuralOutputIsRefusedEverywhereElse()
{
    dsdr::dsp::SpscRing<float> ring(1024);
    const AudioNode neural{QStringLiteral("riduzione neurale"),
                           AudioTag::EarOnly, &ring};

    for (AudioSink sink : {AudioSink::DigitalDecoder, AudioSink::AudioRecorder,
                           AudioSink::NetworkStream,
                           AudioSink::Transmit}) {
        AudioGraph graph;
        QVERIFY2(!graph.connect(neural, sink),
                 qPrintable(QStringLiteral("collegamento accettato verso %1")
                                .arg(sinkName(sink))));
    }
}

void TestAudioGraph::aRefusalExplainsItself()
{
    dsdr::dsp::SpscRing<float> ring(1024);
    AudioGraph graph;
    QString why;

    const AudioNode neural{QStringLiteral("riduzione neurale"),
                           AudioTag::EarOnly, &ring};
    QVERIFY(!graph.connect(neural, AudioSink::DigitalDecoder, &why));

    // Un rifiuto senza motivo è un vicolo cieco: chi scrive il collegamento
    // deve capire perché non si può, non solo che non si può.
    QVERIFY2(!why.isEmpty(), "il rifiuto non ha spiegato niente");
    QVERIFY(why.contains(QStringLiteral("decodificatori")));
    QVERIFY(why.contains(QStringLiteral("§8.3")));
}

void TestAudioGraph::cleanAudioGoesAnywhere()
{
    dsdr::dsp::SpscRing<float> ring(1024);
    AudioGraph graph;

    const AudioNode clean{QStringLiteral("mix del DSP"), AudioTag::Clean, &ring};
    QVERIFY(graph.connect(clean, AudioSink::Ear));
    QVERIFY(graph.connect(clean, AudioSink::AudioRecorder));
    QVERIFY(graph.connect(clean, AudioSink::NetworkStream));
    QVERIFY(graph.connect(clean, AudioSink::DigitalDecoder));
    QCOMPARE(graph.routes().size(), 4);
}

void TestAudioGraph::aRefusedRouteDoesNotEnterTheGraph()
{
    // Un grafo che accettasse il collegamento vietato «tanto poi lo
    // controlliamo» non servirebbe a niente: il rifiuto deve lasciare il grafo
    // esattamente com'era.
    dsdr::dsp::SpscRing<float> ring(1024);
    AudioGraph graph;

    const AudioNode neural{QStringLiteral("riduzione neurale"),
                           AudioTag::EarOnly, &ring};
    graph.connect(neural, AudioSink::Ear);
    const int before = graph.routes().size();

    graph.connect(neural, AudioSink::DigitalDecoder);
    QCOMPARE(graph.routes().size(), before);
}

void TestAudioGraph::aNodeWithoutARingIsNotARoute()
{
    AudioGraph graph;
    QString why;

    // Un nodo senza ring è un errore di cablaggio, e va detto adesso: a
    // runtime sarebbe silenzio, e il silenzio si attribuisce all'antenna.
    const AudioNode empty{QStringLiteral("niente"), AudioTag::Clean, nullptr};
    QVERIFY(!graph.connect(empty, AudioSink::Ear, &why));
    QVERIFY(!why.isEmpty());
    QVERIFY(graph.routes().isEmpty());
}

QTEST_MAIN(TestAudioGraph)
#include "tst_audio_graph.moc"
