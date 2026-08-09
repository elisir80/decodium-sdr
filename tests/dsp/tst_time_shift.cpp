// SPDX-License-Identifier: GPL-3.0-or-later
// La memoria a scorrimento della banda: si scrive sempre, si legge indietro.
//
// I campioni portano un numero d'ordine al posto del segnale — così ogni
// lettura dice da sé di che istante è, e un errore di un solo campione
// nell'aritmetica dell'anello non può passare per rumore.

#include "dsp/TimeShiftBuffer.h"

#include <QTest>

#include <vector>

using namespace dsdr::dsp;

namespace {

/// Blocco in cui ogni campione vale il proprio indice assoluto: la parte I
/// porta il numero d'ordine, la Q il suo negativo per accorgersi di uno
/// scivolamento di mezzo campione fra le due componenti.
std::vector<float> counting(std::size_t first, std::size_t frames)
{
    std::vector<float> out(frames * 2);
    for (std::size_t i = 0; i < frames; ++i) {
        out[i * 2] = static_cast<float>(first + i);
        out[i * 2 + 1] = -static_cast<float>(first + i);
    }
    return out;
}

} // namespace

class TestTimeShift : public QObject
{
    Q_OBJECT

private slots:
    void readsTheHeadWithoutDelay();
    void readsThePastWithDelay();
    void wrapsAroundWithoutTearing();
    void historyGrowsUpToCapacity();
    void deepRequestStopsAtTheOldestSample();
    void clearForgetsEverything();
    void survivesBlocksLargerThanItself();
    void emptyBufferProducesNothing();
};

void TestTimeShift::readsTheHeadWithoutDelay()
{
    TimeShiftBuffer buffer;
    buffer.configure(1024);

    const auto block = counting(0, 256);
    buffer.write(block.data(), 256);

    std::vector<float> out(64 * 2, 0.0f);
    QCOMPARE(buffer.readDelayed(0, out.data(), 64), std::size_t(64));

    // Senza ritardo si leggono gli ultimi 64 scritti: da 192 a 255.
    QCOMPARE(out[0], 192.0f);
    QCOMPARE(out[1], -192.0f);
    QCOMPARE(out[63 * 2], 255.0f);
}

void TestTimeShift::readsThePastWithDelay()
{
    TimeShiftBuffer buffer;
    buffer.configure(1024);

    const auto block = counting(0, 512);
    buffer.write(block.data(), 512);

    std::vector<float> out(64 * 2, 0.0f);
    QCOMPARE(buffer.readDelayed(100, out.data(), 64), std::size_t(64));

    // Cento campioni indietro rispetto agli ultimi 64: la finestra parte da
    // 512 - 64 - 100 = 348.
    QCOMPARE(out[0], 348.0f);
    QCOMPARE(out[63 * 2], 411.0f);
}

void TestTimeShift::wrapsAroundWithoutTearing()
{
    TimeShiftBuffer buffer;
    buffer.configure(1000);      // capacità non potenza di due, di proposito

    // Si scrive ben oltre la capacità, a blocchi che non la dividono: la
    // giunzione cade ogni volta in un punto diverso.
    std::size_t written = 0;
    for (int i = 0; i < 40; ++i) {
        const auto block = counting(written, 137);
        buffer.write(block.data(), 137);
        written += 137;
    }

    std::vector<float> out(300 * 2, 0.0f);
    QCOMPARE(buffer.readDelayed(200, out.data(), 300), std::size_t(300));

    // La sequenza deve restare continua attraverso la giunzione dell'anello:
    // è lì che un errore di indice si manifesta come un salto nel tempo.
    const float first = out[0];
    for (std::size_t i = 0; i < 300; ++i) {
        QCOMPARE(out[i * 2], first + static_cast<float>(i));
        QCOMPARE(out[i * 2 + 1], -(first + static_cast<float>(i)));
    }
    QCOMPARE(first, static_cast<float>(written - 300 - 200));
}

void TestTimeShift::historyGrowsUpToCapacity()
{
    TimeShiftBuffer buffer;
    buffer.configure(512);
    QCOMPARE(buffer.availableFrames(), std::size_t(0));

    const auto block = counting(0, 200);
    buffer.write(block.data(), 200);
    QCOMPARE(buffer.availableFrames(), std::size_t(200));

    buffer.write(block.data(), 200);
    QCOMPARE(buffer.availableFrames(), std::size_t(400));

    // Oltre la capacità la storia non cresce più: si dimentica dal fondo.
    buffer.write(block.data(), 200);
    QCOMPARE(buffer.availableFrames(), std::size_t(512));
    QCOMPARE(buffer.capacityFrames(), std::size_t(512));
}

void TestTimeShift::deepRequestStopsAtTheOldestSample()
{
    TimeShiftBuffer buffer;
    buffer.configure(1024);

    const auto block = counting(0, 300);
    buffer.write(block.data(), 300);

    // Si chiedono trenta secondi di passato quando ce ne sono tre: si torna
    // indietro fin dove la storia arriva, e non oltre.
    std::vector<float> out(64 * 2, 0.0f);
    QCOMPARE(buffer.readDelayed(100000, out.data(), 64), std::size_t(64));
    QCOMPARE(out[0], 0.0f);      // il campione più vecchio che esista
    QCOMPARE(buffer.clampDelay(100000, 64), std::size_t(300 - 64));

    // E una lettura che non trova abbastanza storia restituisce ciò che c'è,
    // invece di riempire di zeri.
    std::vector<float> big(4096 * 2, 0.0f);
    QCOMPARE(buffer.readDelayed(0, big.data(), 4096), std::size_t(300));
}

void TestTimeShift::clearForgetsEverything()
{
    TimeShiftBuffer buffer;
    buffer.configure(256);

    const auto block = counting(0, 200);
    buffer.write(block.data(), 200);
    buffer.clear();

    QCOMPARE(buffer.availableFrames(), std::size_t(0));
    QCOMPARE(buffer.capacityFrames(), std::size_t(256));  // la memoria resta

    std::vector<float> out(16 * 2, 0.0f);
    QCOMPARE(buffer.readDelayed(0, out.data(), 16), std::size_t(0));
}

void TestTimeShift::survivesBlocksLargerThanItself()
{
    TimeShiftBuffer buffer;
    buffer.configure(128);

    const auto block = counting(0, 1000);
    buffer.write(block.data(), 1000);

    QCOMPARE(buffer.availableFrames(), std::size_t(128));

    std::vector<float> out(128 * 2, 0.0f);
    QCOMPARE(buffer.readDelayed(0, out.data(), 128), std::size_t(128));
    // Della piena si tiene la coda: gli ultimi 128 di 1000.
    QCOMPARE(out[0], 872.0f);
    QCOMPARE(out[127 * 2], 999.0f);
}

void TestTimeShift::emptyBufferProducesNothing()
{
    // Un buffer mai configurato non deve scrivere né leggere in memoria altrui.
    TimeShiftBuffer buffer;
    const auto block = counting(0, 16);
    buffer.write(block.data(), 16);

    std::vector<float> out(16 * 2, 7.0f);
    QCOMPARE(buffer.readDelayed(0, out.data(), 16), std::size_t(0));
    QCOMPARE(out[0], 7.0f);
    QCOMPARE(buffer.clampDelay(1000, 16), std::size_t(0));
}

QTEST_APPLESS_MAIN(TestTimeShift)

#include "tst_time_shift.moc"
