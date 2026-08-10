// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — SmartSDR, il canale di comando.
//
// Un FlexRadio serie 6000 si comanda con righe di testo su TCP 4992. È un
// protocollo semplice e stabile, e questa è la parte che si può leggere e
// scrivere senza una radio davanti.
//
// Quattro tipi di riga arrivano dalla radio, e si distinguono dal primo
// carattere:
//
//   V<versione>              la versione del protocollo, appena connessi
//   H<handle>                l'identificativo assegnato a noi, in esadecimale
//   R<seq>|<codice>|<testo>  la risposta a un nostro comando
//   S<handle>|<stato>        un cambiamento di stato, non richiesto
//   M<codice>|<testo>        un messaggio per l'operatore
//
// E una sola va verso la radio:
//
//   C<seq>|<comando>
//
// Il numero d'ordine lo scegliamo noi e torna nella risposta: è così che si
// sa a quale domanda si sta rispondendo, su un canale dove le risposte si
// mescolano agli aggiornamenti di stato.
//
// Il codice della risposta è esadecimale e zero significa «fatto». Trattarlo
// come decimale sarebbe un errore silenzioso: `50000015` letto in decimale è
// un numero plausibile, e il comando fallito sembrerebbe riuscito.
#pragma once

#include <QHash>
#include <QString>

namespace dsdr::hal::flex {

/// Che cosa è arrivato dalla radio.
enum class LineKind {
    Unknown,
    Version,
    Handle,
    Response,
    Status,
    Message,
};

/// Una riga interpretata.
struct Line
{
    LineKind kind = LineKind::Unknown;
    quint32 sequence = 0;     ///< per le risposte: a quale comando
    quint32 code = 0;         ///< codice d'errore, o codice del messaggio
    QString payload;          ///< il resto, così com'è
    QString handle;           ///< per Handle e Status

    bool isError() const noexcept { return kind == LineKind::Response && code != 0; }
};

/// Interpreta una riga arrivata dalla radio.
Line parseLine(const QString &line);

/// Costruisce un comando con il suo numero d'ordine.
QString buildCommand(quint32 sequence, const QString &command);

/// Le coppie `chiave=valore` di uno stato o di una risposta.
///
/// La radio le manda separate da spazi, e l'ordine non è garantito: leggerle
/// per posizione funzionerebbe finché il firmware non ne aggiunge una in
/// mezzo, che è il momento in cui smetterebbe di funzionare senza dire niente.
QHash<QString, QString> parseFields(const QString &payload);

/// Il modello leggibile dal numero di serie o dal campo `model`.
QString describeRadio(const QHash<QString, QString> &fields);

// ── I comandi che aprono un flusso IQ ────────────────────────────────────
//
// Quattro passi, e nessuno di loro si indovina: sono attestati nella
// documentazione di FlexRadio e nelle risposte dei suoi tecnici.
//
//   1. si dichiara la porta UDP su cui si vuole ricevere
//   2. si crea il flusso DAX IQ verso quella porta
//   3. si crea un panadapter, che è ciò a cui il flusso va legato
//   4. si lega il canale al panadapter e si sceglie la velocità
//
// Il quarto passo è quello che decide la frequenza di campionamento: senza,
// il flusso nasce a 48 kS/s qualunque cosa si sia chiesto, e il DSP
// calcolerebbe tutto sulla velocità sbagliata senza accorgersene.

QString commandUdpPort(quint16 port);
QString commandCreateIqStream(int channel, const QString &clientIp, quint16 port);
QString commandCreatePanadapter(int width, int height);
QString commandBindIqStream(int channel, const QString &panStreamId,
                            int sampleRate, const QString &clientHandle);

} // namespace dsdr::hal::flex
