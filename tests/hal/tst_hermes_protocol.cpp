// SPDX-License-Identifier: GPL-3.0-or-later
// OpenHPSDR protocollo 1: gli offset e i bit.
//
// Tutta la difficoltà di questo protocollo sta nel contare. Un byte spostato
// non produce un errore: produce campioni plausibili — rumore al posto della
// banda — o una frequenza che non è quella chiesta, e in aria non se ne
// accorge nessuno tranne chi ascolta dall'altra parte.
//
// Qui i pacchetti si costruiscono e si leggono a mano, senza radio.

#include "hal/backends/hermes/HermesProtocol.h"

#include <QTest>

#include <cmath>

using namespace dsdr::hal::hermes;

namespace {

/// Un pacchetto dati come lo manda la radio, con un valore noto in ogni
/// campione: il primo gruppo del primo fotogramma porta il fondo scala
/// positivo, il secondo il fondo scala negativo.
QByteArray radioPacket(quint32 sequence, bool overload = false)
{
    QByteArray packet(kDataPacketSize, '\0');
    packet[0] = char(0xEF);
    packet[1] = char(0xFE);
    packet[2] = 0x01;
    packet[3] = 0x06;
    packet[4] = char((sequence >> 24) & 0xFF);
    packet[5] = char((sequence >> 16) & 0xFF);
    packet[6] = char((sequence >> 8) & 0xFF);
    packet[7] = char(sequence & 0xFF);

    for (int frame = 0; frame < 2; ++frame) {
        const int base = 8 + frame * kUsbFrameSize;
        packet[base] = 0x7F;
        packet[base + 1] = 0x7F;
        packet[base + 2] = 0x7F;
        packet[base + 3] = 0x00;   // C0: registro zero
        packet[base + 4] = overload ? 0x01 : 0x00;
    }

    // Primo gruppo: I = +max, Q = −max.
    const int first = 8 + 8;
    packet[first + 0] = char(0x7F);
    packet[first + 1] = char(0xFF);
    packet[first + 2] = char(0xFF);
    packet[first + 3] = char(0x80);
    packet[first + 4] = char(0x00);
    packet[first + 5] = char(0x00);

    // Secondo gruppo: I = 0, Q = un quarto del fondo scala (0x200000).
    const int second = first + kBytesPerSampleSet;
    packet[second + 3] = char(0x20);

    return packet;
}

} // namespace

class TestHermesProtocol : public QObject
{
    Q_OBJECT

private slots:
    void theStartPacketAsksOnlyForIq();
    void theStopPacketAsksForNothing();
    void theAddressLivesAboveThePtt();
    void aFrequencyTravelsBigEndian();
    void theSpeedIsTwoBitsAndNothingElse();
    void anUnknownSpeedFallsBackInsteadOfRounding();
    void theGainNeedsItsEnableBit();
    void samplesComeOutWhereTheyWentIn();
    void aPacketWithoutSyncGivesNothing();
    void aFrameWithABrokenSyncIsSkipped();
    void theSequenceIsReadWhole();
    void theOverloadFlagIsReadOnlyFromRegisterZero();
};

void TestHermesProtocol::theStartPacketAsksOnlyForIq()
{
    const QByteArray packet = buildStartStop(true);
    QCOMPARE(packet.size(), 64);
    QCOMPARE(static_cast<quint8>(packet.at(0)), quint8(0xEF));
    QCOMPARE(static_cast<quint8>(packet.at(1)), quint8(0xFE));
    QCOMPARE(static_cast<quint8>(packet.at(2)), quint8(0x04));
    // Bit 0 solo: la banda larga raddoppierebbe il traffico per dati che
    // nessuno legge.
    QCOMPARE(static_cast<quint8>(packet.at(3)), quint8(0x01));

    QCOMPARE(static_cast<quint8>(buildStartStop(true, false).at(3)), quint8(0x03));
}

void TestHermesProtocol::theStopPacketAsksForNothing()
{
    QCOMPARE(static_cast<quint8>(buildStartStop(false).at(3)), quint8(0x00));
}

void TestHermesProtocol::theAddressLivesAboveThePtt()
{
    // Il bit meno significativo di C0 è il PTT. Se l'indirizzo lo occupasse,
    // cambiare frequenza manderebbe la radio in trasmissione — ed è il genere
    // di errore che si scopre dal vicino di casa.
    Command command;
    command.address = 0x02;
    command.transmit = false;
    QByteArray packet = buildCommandPacket(0, command, command);
    QCOMPARE(static_cast<quint8>(packet.at(11)), quint8(0x04));

    command.transmit = true;
    packet = buildCommandPacket(0, command, command);
    QCOMPARE(static_cast<quint8>(packet.at(11)), quint8(0x05));

    // E il sincronismo del fotogramma deve esserci in entrambi.
    QCOMPARE(static_cast<quint8>(packet.at(8)), quint8(0x7F));
    QCOMPARE(static_cast<quint8>(packet.at(8 + kUsbFrameSize)), quint8(0x7F));
}

void TestHermesProtocol::aFrequencyTravelsBigEndian()
{
    // 14.074.000 Hz = 0x00D6C090. Il byte meno significativo va per ultimo:
    // scambiare l'ordine porterebbe la radio a 2,4 GHz, e il sintomo sarebbe
    // «non riceve niente».
    const Command command = frequencyCommand(0x02, 14'074'000, false);
    QCOMPARE(command.c1, quint8(0x00));
    QCOMPARE(command.c2, quint8(0xD6));
    QCOMPARE(command.c3, quint8(0xC0));
    QCOMPARE(command.c4, quint8(0x90));

    const QByteArray packet = buildCommandPacket(1, command, command);
    QCOMPARE(static_cast<quint8>(packet.at(12)), quint8(0x00));
    QCOMPARE(static_cast<quint8>(packet.at(13)), quint8(0xD6));
    QCOMPARE(static_cast<quint8>(packet.at(14)), quint8(0xC0));
    QCOMPARE(static_cast<quint8>(packet.at(15)), quint8(0x90));

    // E il secondo fotogramma porta lo stesso comando, 512 byte più in là.
    QCOMPARE(static_cast<quint8>(packet.at(12 + kUsbFrameSize)), quint8(0x00));
    QCOMPARE(static_cast<quint8>(packet.at(15 + kUsbFrameSize)), quint8(0x90));
}

void TestHermesProtocol::theSpeedIsTwoBitsAndNothingElse()
{
    QCOMPARE(speedCommand(48000.0, 1, false).c1, quint8(0));
    QCOMPARE(speedCommand(96000.0, 1, false).c1, quint8(1));
    QCOMPARE(speedCommand(192000.0, 1, false).c1, quint8(2));
    QCOMPARE(speedCommand(384000.0, 1, false).c1, quint8(3));

    // Il numero di ricevitori sta nei bit 5..3 di C4, meno uno.
    QCOMPARE(speedCommand(48000.0, 1, false).c4, quint8(0x00));
    QCOMPARE(speedCommand(48000.0, 2, false).c4, quint8(0x08));
}

void TestHermesProtocol::anUnknownSpeedFallsBackInsteadOfRounding()
{
    // 250 kS/s non esiste nel protocollo. Arrotondarla a 192 farebbe uscire
    // una radio che campiona a una velocità diversa da quella dichiarata, e il
    // DSP calcolerebbe tutte le frequenze sbagliate senza accorgersene.
    QCOMPARE(speedCommand(250000.0, 1, false).c1, quint8(0));
}

void TestHermesProtocol::theGainNeedsItsEnableBit()
{
    // Senza il bit 6 la radio ignora il registro e resta al guadagno di
    // fabbrica, in silenzio: la manopola sembrerebbe rotta.
    QCOMPARE(gainCommand(-12.0, false).c4, quint8(0x40));
    QCOMPARE(gainCommand(0.0, false).c4, quint8(0x40 | 12));
    QCOMPARE(gainCommand(48.0, false).c4, quint8(0x40 | 60));

    // Fuori scala si limita, non si avvolge: +60 dB che diventassero −12
    // sarebbero un ricevitore sordo senza spiegazione.
    QCOMPARE(gainCommand(100.0, false).c4, quint8(0x40 | 60));
    QCOMPARE(gainCommand(-100.0, false).c4, quint8(0x40));
}

void TestHermesProtocol::samplesComeOutWhereTheyWentIn()
{
    std::vector<float> out(kSamplesPerFrame * 2 * 2, 0.0f);
    const std::size_t frames = decodeIq(radioPacket(7), out.data());

    // Due fotogrammi da sessantatré gruppi.
    QCOMPARE(frames, std::size_t(kSamplesPerFrame * 2));

    // Fondo scala positivo e negativo, letti a 24 bit con segno.
    QVERIFY(std::abs(out[0] - 1.0f) < 1e-5f);
    QVERIFY(std::abs(out[1] + 1.0f) < 1e-5f);

    // E un quarto di fondo scala: è il campione che si sbaglia quando
    // l'estensione del segno è fatta male, perché resta positivo comunque.
    QVERIFY(std::abs(out[2]) < 1e-6f);
    QVERIFY(std::abs(out[3] - 0.25f) < 1e-5f);
}

void TestHermesProtocol::aPacketWithoutSyncGivesNothing()
{
    std::vector<float> out(kSamplesPerFrame * 4, 0.0f);

    QCOMPARE(decodeIq(QByteArray(), out.data()), std::size_t(0));
    QCOMPARE(decodeIq(QByteArray(100, '\0'), out.data()), std::size_t(0));

    // Lunghezza giusta ma endpoint sbagliato: è il **nostro** pacchetto che
    // torna indietro, e decodificarlo darebbe silenzio spacciato per segnale.
    QByteArray mine = radioPacket(1);
    mine[3] = 0x02;
    QCOMPARE(decodeIq(mine, out.data()), std::size_t(0));
}

void TestHermesProtocol::aFrameWithABrokenSyncIsSkipped()
{
    // Un fotogramma sfasato produrrebbe campioni plausibili e sbagliati: si
    // salta, e ne resta uno solo.
    QByteArray packet = radioPacket(3);
    packet[8 + kUsbFrameSize] = 0x00;

    std::vector<float> out(kSamplesPerFrame * 4, 0.0f);
    QCOMPARE(decodeIq(packet, out.data()), std::size_t(kSamplesPerFrame));
}

void TestHermesProtocol::theSequenceIsReadWhole()
{
    // Trentadue bit interi: leggerne solo sedici farebbe comparire un buco
    // ogni 65536 pacchetti — meno di un minuto a 192 kS/s — e lo si
    // attribuirebbe alla rete.
    QCOMPARE(packetSequence(radioPacket(0xDEADBEEF)), quint32(0xDEADBEEF));
    QCOMPARE(packetSequence(radioPacket(0)), quint32(0));
}

void TestHermesProtocol::theOverloadFlagIsReadOnlyFromRegisterZero()
{
    QVERIFY(hasAdcOverload(radioPacket(1, true)));
    QVERIFY(!hasAdcOverload(radioPacket(1, false)));

    // Con un registro diverso da zero, C1 vuol dire altro: leggerlo comunque
    // farebbe lampeggiare la spia a caso.
    QByteArray other = radioPacket(1, true);
    other[8 + 3] = char(0x08);                    // registro 1
    other[8 + kUsbFrameSize + 3] = char(0x08);
    QVERIFY(!hasAdcOverload(other));
}

QTEST_MAIN(TestHermesProtocol)
#include "tst_hermes_protocol.moc"
