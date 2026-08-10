// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — OpenHPSDR protocollo 1, la parte che si può leggere e scrivere
// senza una radio.
//
// Il protocollo è pubblico e sta nella documentazione OpenHPSDR. È fatto di
// pacchetti UDP di misura fissa, e tutta la sua difficoltà sta negli offset:
// un byte contato male non produce un errore, produce campioni plausibili e
// sbagliati — rumore al posto della banda, o una frequenza che non è quella
// chiesta. Per questo la codifica e la decodifica stanno qui, pure e statiche,
// e hanno il loro test.
//
// La forma dei pacchetti dati, in entrambe le direzioni:
//
//   0..2   EF FE 01        sincronismo
//   3      endpoint        0x06 dalla radio, 0x02 verso la radio
//   4..7   numero d'ordine, big endian
//   8..519    primo fotogramma da 512 byte
//   520..1031 secondo fotogramma da 512 byte
//
// E ogni fotogramma da 512:
//
//   0..2   7F 7F 7F        sincronismo
//   3..7   C0..C4          i cinque byte di comando e stato
//   8..511 504 byte di campioni
//
// Con un ricevitore, ogni gruppo di campioni occupa otto byte: tre di I, tre
// di Q — interi a 24 bit con segno, big endian — e due del microfono della
// radio, che qui non serve. Sessantatré gruppi per fotogramma.
#pragma once

#include "dsp/DspTypes.h"

#include <QByteArray>

#include <cstdint>

namespace dsdr::hal::hermes {

/// Misure fisse del protocollo. Sono numeri del protocollo, non scelte
/// nostre: cambiarli non configura niente, rompe tutto.
inline constexpr int kDataPacketSize = 1032;
inline constexpr int kUsbFrameSize = 512;
inline constexpr int kUsbFramePayload = 504;
inline constexpr int kSamplesPerFrame = 63;   ///< con un ricevitore
inline constexpr int kBytesPerSampleSet = 8;  ///< 3 I + 3 Q + 2 microfono
inline constexpr quint16 kPort = 1024;

/// Le velocità che il protocollo sa esprimere, nell'ordine dei due bit di C1.
inline constexpr double kSampleRates[] = {48000.0, 96000.0, 192000.0, 384000.0};

/// I byte di comando che il PC manda alla radio. `address` è il registro:
/// 0 impostazioni generali, 1 frequenza di trasmissione, 2 frequenza del primo
/// ricevitore, 0x0A guadagno d'ingresso.
struct Command
{
    quint8 address = 0;
    bool transmit = false;      ///< il bit meno significativo di C0
    quint8 c1 = 0;
    quint8 c2 = 0;
    quint8 c3 = 0;
    quint8 c4 = 0;
};

/// Costruisce il pacchetto di avvio o di arresto dello streaming.
///
/// `iqOnly` chiede solo il flusso IQ: la banda larga non ci serve, e chiederla
/// raddoppierebbe il traffico per dati che nessuno legge.
QByteArray buildStartStop(bool start, bool iqOnly = true);

/// Costruisce un pacchetto dati verso la radio. Il carico è silenzio — in sola
/// ricezione non c'è niente da trasmettere — ma il pacchetto va mandato lo
/// stesso: è l'unico veicolo dei byte di comando, e senza di lui la radio non
/// saprebbe mai su che frequenza mettersi.
QByteArray buildCommandPacket(quint32 sequence, const Command &first,
                              const Command &second);

/// Il comando del registro 0: velocità di campionamento e numero di
/// ricevitori.
Command speedCommand(double sampleRate, int receivers, bool ptt);

/// Il comando che porta una frequenza a un ricevitore (registro 2 per il
/// primo) o al trasmettitore (registro 1).
Command frequencyCommand(quint8 address, quint32 hz, bool ptt);

/// Guadagno d'ingresso dell'Hermes-Lite 2, da −12 a +48 dB.
Command gainCommand(double db, bool ptt);

/// Vero se il datagramma è un pacchetto dati della radio.
bool isDataPacket(const QByteArray &datagram);

/// Numero d'ordine del pacchetto: serve a scoprire i buchi. La radio non
/// ritrasmette, e un salto è audio perduto — saperlo è l'unico modo di
/// distinguere una rete che perde pacchetti da un DSP troppo lento.
quint32 packetSequence(const QByteArray &datagram);

/// Estrae i campioni IQ di un pacchetto dati e li scrive interleaved I,Q
/// normalizzati in ±1. Restituisce il numero di coppie scritte — 126 con un
/// ricevitore, due fotogrammi da 63 — oppure zero se il pacchetto è malformato.
///
/// `out` deve avere spazio per `kSamplesPerFrame * 2 * 2` float.
std::size_t decodeIq(const QByteArray &datagram, float *out);

/// Il flag di sovraccarico dell'ADC, che la radio manda dentro C1 del primo
/// fotogramma quando C0 vale zero.
bool hasAdcOverload(const QByteArray &datagram);

} // namespace dsdr::hal::hermes
