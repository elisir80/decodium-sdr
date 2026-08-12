// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — il protocollo fra il programma e l'ospite dei plugin.
//
// **Perché due processi.** Un plugin VST3 è codice di qualcun altro che gira
// dentro il nostro. Se sbaglia un indice, il programma che si porta dietro non
// è un editor audio: è una radio, e magari sta trasmettendo. La SPEC-005 lo
// dice come regola — «un plugin che va in crash non deve portarsi dietro la
// radio» — e l'unico modo di rispettarla davvero è che non giri qui dentro.
//
// Quindi c'è un secondo eseguibile, `decodium-vst-host`. Se muore, muore lui:
// da questa parte si vede una pipe che si chiude, il blocco va in bypass, e la
// radio continua a fare la radio. È esattamente quello che succede nel test.
//
// **Perché un protocollo così scarno.** I campioni passano su memoria
// condivisa e i comandi su una pipe di righe di testo. Non c'è
// serializzazione, non ci sono versioni negoziate, non c'è un formato
// estensibile: c'è un blocco di float e un pugno di verbi. Un protocollo
// ricco fra due processi che nascono e muoiono insieme, dallo stesso albero
// dei sorgenti e con lo stesso numero di versione, sarebbe complessità pagata
// per un problema che non esiste.
//
// **Il verso del flusso.** Il programma scrive l'ingresso nel segmento,
// manda `p`, e aspetta una riga di risposta. L'ospite elabora sul posto e
// risponde. È sincrono di proposito: l'audio del trasmettitore ha già il suo
// ritmo, e un secondo ritmo asincrono in mezzo vorrebbe dire un buffer, cioè
// altro ritardo fra la voce e l'antenna.
#pragma once

#include <QLatin1String>

namespace dsdr::plugins {

/// La versione del protocollo. Cambia solo se cambia la forma della memoria
/// condivisa: i due lati nascono dallo stesso albero, e un disallineamento
/// vuol dire un'installazione mescolata.
inline constexpr int kProtocolVersion = 1;

/// Quanti campioni al massimo può contenere un blocco.
///
/// Il motore TX lavora a blocchi di 1024: il doppio lascia margine senza
/// costare niente, perché il segmento si mappa una volta sola.
inline constexpr int kMaxBlockFrames = 2048;

/// Quanti canali. Due, perché quasi nessun plugin di studio è mono: un
/// compressore stereo che riceve un canale solo elabora metà del segnale e non
/// lo dice. La voce si duplica in ingresso e si riprende dal canale sinistro.
inline constexpr int kChannels = 2;

/// Quanti parametri si espongono al massimo.
///
/// Non c'è un motivo tecnico per questo numero: c'è un motivo di interfaccia.
/// Un plugin di studio serio ne dichiara anche duecento, e mostrarli tutti in
/// un pannello vuol dire non mostrarne nessuno. Si prendono i primi, che negli
/// SDK seri sono quelli che il costruttore ha messo davanti.
inline constexpr int kMaxParameters = 32;

/// L'intestazione in testa al segmento condiviso.
///
/// Sta insieme ai campioni e non su un canale a parte per una ragione sola: un
/// conteggio che viaggia separato dai dati che conta è un conteggio che prima
/// o poi li descrive male.
struct SharedHeader
{
    int version = kProtocolVersion;
    int frames = 0;         ///< quanti frame ci sono in questo giro
    int channels = kChannels;
    double sampleRate = 48000.0;
};

/// Il segmento: intestazione, poi i campioni per canale, non interlacciati.
///
/// Non interlacciati perché è così che li vuole VST3, e convertirli a ogni
/// blocco da una parte o dall'altra sarebbe lavoro fatto due volte.
inline constexpr int kSharedBytes =
    static_cast<int>(sizeof(SharedHeader)) + kChannels * kMaxBlockFrames * 4;

// ── I verbi, uno per riga ────────────────────────────────────────────────
//
// Dal programma verso l'ospite:
//
//   scan                       elenca i plugin trovati
//   load <percorso>            carica un plugin
//   unload                     lo scarica
//   prepare <rate> <frames>    prepara la catena
//   param <indice> <0..1>      cambia un parametro
//   p                          elabora quello che c'è nel segmento
//   quit
//
// Dall'ospite verso il programma:
//
//   ready <versione>           appena avviato
//   ok [testo]                 comando riuscito
//   err <testo>                comando fallito, e perché
//   plugin <percorso>|<nome>|<venditore>|<categoria>
//   par <indice>|<nome>|<unità>|<valore>
//   done <frame>               blocco elaborato
//
// Il testo dopo `err` finisce sotto gli occhi dell'operatore: va scritto per
// lui, non per chi ha scritto il codice.

inline constexpr QLatin1String kCmdScan{"scan"};
inline constexpr QLatin1String kCmdLoad{"load"};
inline constexpr QLatin1String kCmdUnload{"unload"};
inline constexpr QLatin1String kCmdPrepare{"prepare"};
inline constexpr QLatin1String kCmdParam{"param"};
inline constexpr QLatin1String kCmdProcess{"p"};
inline constexpr QLatin1String kCmdQuit{"quit"};

inline constexpr QLatin1String kRepReady{"ready"};
inline constexpr QLatin1String kRepOk{"ok"};
inline constexpr QLatin1String kRepError{"err"};
inline constexpr QLatin1String kRepPlugin{"plugin"};
inline constexpr QLatin1String kRepParameter{"par"};
inline constexpr QLatin1String kRepDone{"done"};

/// Il nome dell'eseguibile ospite, senza estensione.
inline constexpr QLatin1String kHostExecutable{"decodium-vst-host"};

} // namespace dsdr::plugins
