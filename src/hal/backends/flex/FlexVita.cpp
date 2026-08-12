// SPDX-License-Identifier: GPL-3.0-or-later
#include "hal/backends/flex/FlexVita.h"

#include <QtEndian>

#include <cstring>

namespace dsdr::hal::flex {

namespace {

// Le maschere dell'intestazione VITA-49. Sono dello standard, non di
// FlexRadio: la stessa intestazione la usano tutti quelli che parlano VITA.
constexpr quint32 kPacketTypeMask = 0xF0000000;
constexpr quint32 kPacketTypeIfDataWithStreamId = 0x10000000;
constexpr quint32 kClassIdPresent = 0x08000000;
constexpr quint32 kTrailerPresent = 0x04000000;
constexpr quint32 kTsiMask = 0x00C00000;
constexpr quint32 kTsfMask = 0x00300000;
constexpr quint32 kPacketCountMask = 0x000F0000;
constexpr quint32 kPacketSizeMask = 0x0000FFFF;

constexpr quint32 kOuiMask = 0x00FFFFFF;

inline quint32 word(const uchar *p)
{
    return qFromBigEndian<quint32>(p);
}

} // namespace

VitaPacket parseVita(const QByteArray &datagram)
{
    VitaPacket packet;

    // Quattro parole sono il minimo per avere intestazione, flusso e classe.
    if (datagram.size() < 16)
        return packet;

    const auto *base = reinterpret_cast<const uchar *>(datagram.constData());
    const quint32 header = word(base);

    // Solo i pacchetti dati con identificativo di flusso: gli altri — contesto,
    // dati estesi — parlano d'altro, e decodificarli darebbe campioni che non
    // sono campioni.
    if ((header & kPacketTypeMask) != kPacketTypeIfDataWithStreamId)
        return packet;
    if (!(header & kClassIdPresent))
        return packet;

    packet.streamId = word(base + 4);

    const quint32 classHigh = word(base + 8);
    const quint32 classLow = word(base + 12);
    if ((classHigh & kOuiMask) != kFlexOui)
        return packet;
    if (static_cast<quint16>(classLow >> 16) != kFlexInformationClass)
        return packet;

    packet.packetClass = static_cast<quint16>(classLow & 0xFFFF);
    packet.packetCount = static_cast<quint8>((header & kPacketCountMask) >> 16);

    // L'intestazione non è di misura fissa: i marcatori temporali ci sono solo
    // se i bit lo dicono. Darla per fissa funziona finché una radio non ne
    // manda uno senza — e allora tutti i campioni scivolano di due parole,
    // restando plausibili.
    int words = 4;                                  // intestazione, flusso, classe
    if ((header & kTsiMask) != 0)
        words += 1;                                 // secondi interi
    if ((header & kTsfMask) != 0)
        words += 2;                                 // frazione, 64 bit
    packet.payloadOffset = words * 4;

    // La misura dichiarata è in parole da 32 bit e comprende tutto.
    const int declared = static_cast<int>(header & kPacketSizeMask) * 4;
    int available = qMin(declared > 0 ? declared : datagram.size(), datagram.size());
    if (header & kTrailerPresent)
        available -= 4;

    packet.payloadBytes = available - packet.payloadOffset;
    if (packet.payloadBytes <= 0) {
        packet.payloadBytes = 0;
        return packet;
    }

    packet.valid = true;
    return packet;
}

std::size_t decodeIq(const QByteArray &datagram, const VitaPacket &packet, float *out)
{
    if (!packet.valid || !out)
        return 0;

    // Il formato lo dichiara il pacchetto. Un canale solo, o una precisione
    // diversa da trentadue bit, non è un flusso IQ e si salta: interpretarlo
    // lo stesso darebbe campioni plausibili e sbagliati, che è il modo
    // peggiore di essere rotti.
    if (!packet.isPair32())
        return 0;

    const int values = packet.payloadBytes / 4;
    if (values < 2)
        return 0;

    const auto *p = reinterpret_cast<const uchar *>(datagram.constData())
                  + packet.payloadOffset;

    // Tutto viaggia in ordine di rete: leggere com'è in memoria funzionerebbe
    // solo su una macchina big endian, e le nostre non lo sono.
    if (packet.isFloat()) {
        for (int i = 0; i < values; ++i) {
            const quint32 bits = qFromBigEndian<quint32>(p + i * 4);
            float value = 0.0f;
            std::memcpy(&value, &bits, sizeof(value));
            out[i] = value;
        }
    } else {
        // Interi in virgola fissa a 32 bit. È la forma che FlexLib stessa
        // aveva scambiato per virgola mobile: il risultato non è silenzio, ma
        // numeri assurdi elaborati diligentemente dal DSP — rumore che sembra
        // una banda.
        constexpr float kScale = 1.0f / 2147483648.0f;   // 2^31
        for (int i = 0; i < values; ++i) {
            const auto raw = static_cast<qint32>(qFromBigEndian<quint32>(p + i * 4));
            out[i] = static_cast<float>(raw) * kScale;
        }
    }

    return static_cast<std::size_t>(values / 2);
}

void buildTxAudioPacket(QByteArray &packet, quint32 streamId, quint8 packetCount,
                        const float *samples, int count)
{
    if (!samples || count <= 0) {
        packet.clear();
        return;
    }

    // Intestazione, identificativo di flusso, due parole di classe, e poi i
    // campioni: la stessa forma che si legge in ricezione, costruita
    // all'incontrario. Niente marcatori temporali — la radio non li chiede per
    // l'audio, e metterceli allungherebbe ogni pacchetto di due parole per
    // un'informazione che nessuno legge.
    const int payloadWords = count * 2;              // due canali
    const int totalWords = 4 + payloadWords;

    packet.resize(totalWords * 4);
    auto *out = reinterpret_cast<uchar *>(packet.data());

    // Tipo: dati con identificativo di flusso, classe presente, nessun
    // marcatore temporale. La misura è in parole a 32 bit e comprende
    // l'intestazione: è il campo che la radio usa per sapere dove finisce il
    // pacchetto, e sbagliarlo non produce un errore — produce campioni letti
    // oltre la fine.
    const quint32 header = kPacketTypeIfDataWithStreamId | kClassIdPresent
        | ((static_cast<quint32>(packetCount) << 16) & kPacketCountMask)
        | (static_cast<quint32>(totalWords) & kPacketSizeMask);

    qToBigEndian<quint32>(header, out);
    qToBigEndian<quint32>(streamId, out + 4);

    // Le due parole di classe: l'OUI del costruttore, poi la classe
    // d'informazione e il codice di pacchetto. Il codice **descrive** il
    // carico — trentadue bit per campione, due canali, virgola mobile — invece
    // di essere un numero opaco: è la stessa scelta che rende leggibile il
    // verso opposto.
    qToBigEndian<quint32>(kFlexOui & kOuiMask, out + 8);
    const quint32 classWord = (static_cast<quint32>(kFlexInformationClass) << 16)
        | kClassBitsPerSample32 | kClassChannelsPair | kClassIeee754;
    qToBigEndian<quint32>(classWord, out + 12);

    // I campioni, in ordine di rete. Leggerli come stanno in memoria
    // funzionerebbe solo su una macchina big endian, e le nostre non lo sono:
    // quel che ne uscirebbe sono numeri enormi o denormali — cioè silenzio, o
    // rumore, in aria.
    uchar *payload = out + 16;
    for (int i = 0; i < count; ++i) {
        quint32 bits = 0;
        const float value = samples[i];
        std::memcpy(&bits, &value, sizeof(bits));
        qToBigEndian<quint32>(bits, payload + i * 8);
        qToBigEndian<quint32>(bits, payload + i * 8 + 4);
    }
}

QByteArray buildTxAudioPacket(quint32 streamId, quint8 packetCount,
                              const float *samples, int count)
{
    QByteArray packet;
    buildTxAudioPacket(packet, streamId, packetCount, samples, count);
    return packet;
}

} // namespace dsdr::hal::flex
