// SPDX-License-Identifier: GPL-3.0-or-later
// La soglia AGC-T automatica.
//
// È il comando più utile e il meno usato di una radio, e il motivo è pratico:
// va rimesso a mano a ogni cambio di banda, di antenna, di ora del giorno.
// Lasciato dov'era o non fa niente o assorda — troppo basso e l'AGC insegue il
// rumore alzandolo fino al volume del segnale, troppo alto e i segnali deboli
// non muovono più il guadagno.
//
// Automatizzarlo è aritmetica di due righe, e sono due righe che si sbagliano
// in silenzio: una soglia che segue il fondo troppo in fretta scende dentro il
// respiro del rumore e fa pompare il guadagno — il difetto che si attribuisce
// all'AGC e che invece nasce qui.

#include "dsp/ChannelProcessor.h"

#include <QTest>

#include <cmath>

using namespace dsdr::dsp;

class TestAgcThreshold : public QObject
{
    Q_OBJECT

private slots:
    void theThresholdSitsAboveTheNoise();
    void itMovesSlowlyOnPurpose();
    void itClimbsAndDescendsAtTheSameSpeed();
    void itNeverLeavesTheUsefulRange();
    void itConvergesWhereItShould();
};

void TestAgcThreshold::theThresholdSitsAboveTheNoise()
{
    // Sei decibel sopra il fondo. Sotto, la soglia finisce dentro il respiro
    // del rumore; sopra, i segnali appena emersi non muovono più il guadagno e
    // si sentono deboli anche quando ci sono.
    //
    // Con un passo abbastanza grande da arrivarci in un colpo, si legge il
    // bersaglio invece del cammino.
    const float target = ChannelProcessor::autoThresholdFor(-110.0f, -110.0f, 100.0f);
    QVERIFY2(std::abs(target - (-104.0f)) < 0.01f,
             qPrintable(QStringLiteral("soglia a %1 per un fondo a −110").arg(target)));
}

void TestAgcThreshold::itMovesSlowlyOnPurpose()
{
    // Un salto di quaranta decibel — un cambio di banda — non deve essere
    // percorso in un blocco: il guadagno seguirebbe a scatti, e si sentirebbe.
    const float step = 0.1f;
    const float next = ChannelProcessor::autoThresholdFor(-60.0f, -110.0f, step);

    QVERIFY2(std::abs(next - (-110.0f + step)) < 1e-4f,
             qPrintable(QStringLiteral("si è mossa di %1 dB").arg(next + 110.0f)));
}

void TestAgcThreshold::itClimbsAndDescendsAtTheSameSpeed()
{
    // Salita e discesa simmetriche. Un'asimmetria qui vorrebbe dire una soglia
    // che sale con la statica e non torna più giù quando la banda si calma —
    // e il ricevitore resterebbe sordo senza che nessuno tocchi niente.
    const float step = 0.5f;
    const float up = ChannelProcessor::autoThresholdFor(-40.0f, -100.0f, step);
    const float down = ChannelProcessor::autoThresholdFor(-140.0f, -100.0f, step);

    QVERIFY(std::abs((up - (-100.0f)) - step) < 1e-4f);
    QVERIFY(std::abs((down - (-100.0f)) + step) < 1e-4f);
}

void TestAgcThreshold::itNeverLeavesTheUsefulRange()
{
    // I limiti sono quelli del cursore: fuori di lì la soglia o non fa niente
    // o rende il ricevitore sordo, e in entrambi i casi sembra un guasto.
    const float low = ChannelProcessor::autoThresholdFor(-200.0f, -139.0f, 100.0f);
    QVERIFY2(low >= -140.0f, qPrintable(QStringLiteral("scesa a %1").arg(low)));

    const float high = ChannelProcessor::autoThresholdFor(0.0f, -21.0f, 100.0f);
    QVERIFY2(high <= -20.0f, qPrintable(QStringLiteral("salita a %1").arg(high)));
}

void TestAgcThreshold::itConvergesWhereItShould()
{
    // E alla fine ci arriva: cinquecento blocchi a un decimo di decibel sono
    // cinquanta decibel di corsa, più che abbastanza per un cambio di banda.
    float threshold = -130.0f;
    for (int i = 0; i < 500; ++i)
        threshold = ChannelProcessor::autoThresholdFor(-95.0f, threshold, 0.1f);

    QVERIFY2(std::abs(threshold - (-89.0f)) < 0.2f,
             qPrintable(QStringLiteral("arrivata a %1 invece che a −89").arg(threshold)));
}

QTEST_MAIN(TestAgcThreshold)
#include "tst_agc_threshold.moc"
