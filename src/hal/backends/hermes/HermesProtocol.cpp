// SPDX-License-Identifier: GPL-3.0-or-later
#include "hal/backends/hermes/HermesProtocol.h"

#include <algorithm>
#include <cmath>

namespace dsdr::hal::hermes {

namespace {

constexpr char kSync0 = char(0xEF);
constexpr char kSync1 = char(0xFE);
constexpr char kDataMarker = 0x01;
constexpr char kFrameSync = 0x7F;

constexpr int kEndpointToRadio = 0x02;
constexpr int kEndpointFromRadio = 0x06;

/// Un intero a 24 bit con segno, big endian. L'estensione del segno va fatta a
/// mano: spostare a sinistra e poi a destra su un intero con segno la ottiene
/// senza rami, ed è il modo in cui il valore resta corretto anche per i
/// negativi — che sono metà dei campioni.
inline float sample24(const uchar *p) noexcept
{
    const qint32 raw = (qint32(p[0]) << 24) | (qint32(p[1]) << 16) | (qint32(p[2]) << 8);
    // Il fondo scala di un intero a 24 bit portato in ±1.
    return static_cast<float>(raw >> 8) / 8388608.0f;
}

/// Scrive i cinque byte di comando dentro un fotogramma.
void writeCommand(char *frame, const Command &command)
{
    frame[0] = kFrameSync;
    frame[1] = kFrameSync;
    frame[2] = kFrameSync;
    // C0: l'indirizzo sta nei sette bit alti, e il bit meno significativo è il
    // PTT. Metterlo altrove manderebbe la radio in trasmissione al primo
    // cambio di frequenza.
    frame[3] = static_cast<char>((command.address << 1) | (command.transmit ? 1 : 0));
    frame[4] = static_cast<char>(command.c1);
    frame[5] = static_cast<char>(command.c2);
    frame[6] = static_cast<char>(command.c3);
    frame[7] = static_cast<char>(command.c4);
}

} // namespace

QByteArray buildStartStop(bool start, bool iqOnly)
{
    // Quattro byte di comando dentro un pacchetto da 64: il resto a zero.
    QByteArray packet(64, '\0');
    packet[0] = kSync0;
    packet[1] = kSync1;
    packet[2] = 0x04;   // «avvia/ferma»
    // Bit 0: flusso IQ. Bit 1: banda larga, che non chiediamo — raddoppierebbe
    // il traffico per dati che nessuno legge.
    packet[3] = start ? char(iqOnly ? 0x01 : 0x03) : char(0x00);
    return packet;
}

QByteArray buildCommandPacket(quint32 sequence, const Command &first,
                              const Command &second)
{
    QByteArray packet(kDataPacketSize, '\0');
    packet[0] = kSync0;
    packet[1] = kSync1;
    packet[2] = kDataMarker;
    packet[3] = char(kEndpointToRadio);
    packet[4] = static_cast<char>((sequence >> 24) & 0xFF);
    packet[5] = static_cast<char>((sequence >> 16) & 0xFF);
    packet[6] = static_cast<char>((sequence >> 8) & 0xFF);
    packet[7] = static_cast<char>(sequence & 0xFF);

    writeCommand(packet.data() + 8, first);
    writeCommand(packet.data() + 8 + kUsbFrameSize, second);
    // Il resto resta a zero: in sola ricezione non c'è niente da trasmettere,
    // ma il pacchetto va mandato lo stesso — è l'unico veicolo dei comandi.
    return packet;
}

Command speedCommand(double sampleRate, int receivers, bool ptt)
{
    Command command;
    command.address = 0x00;
    command.transmit = ptt;

    // I due bit bassi di C1 scelgono la velocità. Un valore che non è fra
    // quelli non si arrotonda: si resta a 48 k, che è la sola cosa che ogni
    // radio del protocollo sa fare.
    quint8 speed = 0;
    for (int i = 0; i < 4; ++i) {
        if (std::abs(sampleRate - kSampleRates[i]) < 1.0) {
            speed = static_cast<quint8>(i);
            break;
        }
    }
    command.c1 = speed;

    // C4 bit 5..3: numero di ricevitori meno uno.
    const int count = std::clamp(receivers, 1, 8);
    command.c4 = static_cast<quint8>((count - 1) << 3);
    return command;
}

Command frequencyCommand(quint8 address, quint32 hz, bool ptt)
{
    Command command;
    command.address = address;
    command.transmit = ptt;
    // La frequenza viaggia in hertz, big endian, su C1..C4: è la radio a
    // ricavarne la parola di fase.
    command.c1 = static_cast<quint8>((hz >> 24) & 0xFF);
    command.c2 = static_cast<quint8>((hz >> 16) & 0xFF);
    command.c3 = static_cast<quint8>((hz >> 8) & 0xFF);
    command.c4 = static_cast<quint8>(hz & 0xFF);
    return command;
}

Command gainCommand(double db, bool ptt)
{
    Command command;
    command.address = 0x0A;
    command.transmit = ptt;

    // L'Hermes-Lite 2 accetta da −12 a +48 dB in un registro da sette bit, con
    // il bit 6 che abilita il controllo: senza quello la radio ignora il resto
    // e resta al guadagno di fabbrica, in silenzio.
    const int value = std::clamp(static_cast<int>(std::lround(db)) + 12, 0, 60);
    command.c4 = static_cast<quint8>(0x40 | value);
    return command;
}

bool isDataPacket(const QByteArray &datagram)
{
    if (datagram.size() < kDataPacketSize)
        return false;
    if (datagram.at(0) != kSync0 || datagram.at(1) != kSync1)
        return false;
    if (datagram.at(2) != kDataMarker)
        return false;
    return static_cast<quint8>(datagram.at(3)) == kEndpointFromRadio;
}

quint32 packetSequence(const QByteArray &datagram)
{
    if (datagram.size() < 8)
        return 0;
    const auto *p = reinterpret_cast<const uchar *>(datagram.constData());
    return (quint32(p[4]) << 24) | (quint32(p[5]) << 16)
         | (quint32(p[6]) << 8) | quint32(p[7]);
}

std::size_t decodeIq(const QByteArray &datagram, float *out)
{
    if (!isDataPacket(datagram) || !out)
        return 0;

    const auto *base = reinterpret_cast<const uchar *>(datagram.constData());
    std::size_t written = 0;

    for (int frame = 0; frame < 2; ++frame) {
        const uchar *usb = base + 8 + frame * kUsbFrameSize;
        // Il sincronismo del fotogramma va verificato: un pacchetto che
        // arriva sfasato produrrebbe campioni plausibili e sbagliati, che è
        // il modo peggiore di essere rotti.
        if (usb[0] != uchar(kFrameSync) || usb[1] != uchar(kFrameSync)
            || usb[2] != uchar(kFrameSync)) {
            continue;
        }

        const uchar *samples = usb + 8;
        for (int i = 0; i < kSamplesPerFrame; ++i) {
            const uchar *set = samples + i * kBytesPerSampleSet;
            out[written++] = sample24(set);
            out[written++] = sample24(set + 3);
            // I due byte del microfono della radio restano dove sono: qui non
            // servono, e leggerli costerebbe senza dare niente.
        }
    }

    return written / 2;
}

bool hasAdcOverload(const QByteArray &datagram)
{
    if (!isDataPacket(datagram))
        return false;

    for (int frame = 0; frame < 2; ++frame) {
        const auto *usb = reinterpret_cast<const uchar *>(datagram.constData())
                        + 8 + frame * kUsbFrameSize;
        if (usb[0] != uchar(kFrameSync))
            continue;
        // Lo stato arriva solo quando la radio sta rispondendo al registro 0:
        // negli altri fotogrammi C1 vuol dire altro, e leggerlo comunque
        // farebbe lampeggiare la spia a caso.
        if ((usb[3] >> 3) != 0)
            continue;
        if (usb[4] & 0x01)
            return true;
    }
    return false;
}

} // namespace dsdr::hal::hermes
