// SPDX-License-Identifier: GPL-3.0-or-later
// Il compressore multibanda: che a riposo non si senta, e che comprima dove dice.
//
// La prova che conta è la prima. Un multibanda con tutte le bande a riposo
// deve restituire il segnale che ha ricevuto: se le separazioni non sommano
// piatto, colora la voce prima ancora di comprimere, e chi lo accende per la
// prima volta lo giudica da quel colore — e lo rispegne.
//
// È anche l'errore più facile da fare e il più difficile da sentire: qualche
// decibel di buco attorno ai settecento hertz, in mezzo a una voce, somiglia
// a un microfono diverso.
#include "dsp/MultibandCompressor.h"

#include <QTest>

#include <cmath>
#include <vector>

#ifndef M_PI
#  define M_PI 3.14159265358979323846
#endif

using namespace dsdr::dsp;

namespace {

constexpr double kRate = 48000.0;

/// Livello d'uscita di un tono, in decibel rispetto all'ingresso.
double toneGainDb(MultibandCompressor &cfc, double frequencyHz, double amplitude)
{
    constexpr std::size_t kSettle = 24000;   // mezzo secondo: i rilasci sono lenti
    constexpr std::size_t kMeasure = 8192;

    std::vector<float> block(kSettle + kMeasure);
    for (std::size_t i = 0; i < block.size(); ++i) {
        block[i] = static_cast<float>(
            amplitude * std::sin(2.0 * M_PI * frequencyHz * static_cast<double>(i) / kRate));
    }

    cfc.reset();
    cfc.process(block.data(), block.size());

    double sum = 0.0;
    for (std::size_t i = kSettle; i < block.size(); ++i)
        sum += static_cast<double>(block[i]) * static_cast<double>(block[i]);

    const double rms = std::sqrt(sum / static_cast<double>(kMeasure));
    const double reference = amplitude / std::sqrt(2.0);
    return 20.0 * std::log10(std::max(rms, 1e-9) / reference);
}

} // namespace

class TestMultiband : public QObject
{
    Q_OBJECT

private slots:
    void spentoNonTocca();
    void aRiposoLaSommaEPiatta();
    void comprimeSoprLaSoglia();
    void unaBandaSolaNonAbbassaLeAltre();
    void ilPunchStringeDiPiu();
};

void TestMultiband::spentoNonTocca()
{
    MultibandCompressor cfc;
    cfc.configure(kRate);
    cfc.setPunch(10.0);

    QVERIFY(!cfc.isEnabled());
    // Spento significa che il segnale non passa nemmeno dai filtri: non deve
    // esserci nessuna colorazione da un blocco che si crede escluso.
    QVERIFY(std::abs(toneGainDb(cfc, 1000.0, 0.2)) < 0.01);
}

void TestMultiband::aRiposoLaSommaEPiatta()
{
    MultibandCompressor cfc;
    cfc.configure(kRate);
    cfc.setEnabled(true);
    // Punch a zero: le soglie stanno a −6 dB, e un tono a 0,02 (−34 dBFS) non
    // le tocca. Quello che resta è la sola somma delle quattro bande.
    cfc.setPunch(0.0);

    // Le frequenze da provare sono quelle *sulle* separazioni: è lì che una
    // ricostruzione sbagliata fa il buco, e in mezzo alla banda non si vede.
    for (const double hz : {120.0, 250.0, 450.0, 700.0, 1200.0, 1800.0, 3000.0}) {
        const double gain = toneGainDb(cfc, hz, 0.02);
        QVERIFY2(std::abs(gain) < 0.5,
                 qPrintable(QStringLiteral("a %1 Hz la somma delle bande fa %2 dB")
                                .arg(hz).arg(gain)));
    }
}

void TestMultiband::comprimeSoprLaSoglia()
{
    MultibandCompressor cfc;
    cfc.configure(kRate);
    cfc.setEnabled(true);
    cfc.setPunch(8.0);

    // Un tono forte in mezzo alla banda della voce: deve uscire più basso di
    // quanto è entrato, e la banda deve dirlo.
    const double gain = toneGainDb(cfc, 1200.0, 0.7);
    QVERIFY2(gain < -3.0,
             qPrintable(QStringLiteral("un tono forte esce a %1 dB: non sta comprimendo")
                            .arg(gain)));
    QVERIFY2(cfc.gainReductionDb(2) > 3.0,
             qPrintable(QStringLiteral("la banda 2 dichiara %1 dB di riduzione")
                            .arg(cfc.gainReductionDb(2))));
}

void TestMultiband::unaBandaSolaNonAbbassaLeAltre()
{
    MultibandCompressor cfc;
    cfc.configure(kRate);
    cfc.setEnabled(true);
    cfc.setPunch(8.0);

    // È la ragione per cui esiste un multibanda: un tono forte a tremila hertz
    // — una sibilante, un colpo sul microfono — non deve abbassare il corpo
    // della voce. Con un compressore a banda intera lo farebbe, e la voce si
    // accartoccerebbe a ogni consonante.
    toneGainDb(cfc, 3000.0, 0.7);
    QVERIFY2(cfc.gainReductionDb(3) > 3.0,
             qPrintable(QStringLiteral("la banda alta non ha compresso: %1 dB")
                            .arg(cfc.gainReductionDb(3))));
    QVERIFY2(cfc.gainReductionDb(0) < 1.0,
             qPrintable(QStringLiteral("la banda del corpo si è abbassata di %1 dB "
                                       "per un tono che non la riguarda")
                            .arg(cfc.gainReductionDb(0))));
}

void TestMultiband::ilPunchStringeDiPiu()
{
    MultibandCompressor cfc;
    cfc.configure(kRate);
    cfc.setEnabled(true);

    cfc.setPunch(2.0);
    const double gentle = toneGainDb(cfc, 1200.0, 0.5);
    cfc.setPunch(10.0);
    const double hard = toneGainDb(cfc, 1200.0, 0.5);

    // Un numero solo comanda quattro compressori: deve almeno andare nel verso
    // giusto, altrimenti è una manopola che non fa niente di riconoscibile.
    QVERIFY2(hard < gentle - 2.0,
             qPrintable(QStringLiteral("punch 2 → %1 dB, punch 10 → %2 dB")
                            .arg(gentle).arg(hard)));
}

QTEST_MAIN(TestMultiband)
#include "tst_multiband.moc"
