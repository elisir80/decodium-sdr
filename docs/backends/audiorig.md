# Backend `audiorig`

Una radio tradizionale — un FT-991A, un IC-7300, un TS-590 — non ha uscita IF e
non consegnerà mai un flusso IQ. Non è però fuori dall'architettura: è un
backend di classe **server-DSP**, esattamente come un Flex o un Kiwi. La radio
demodula, il client riceve audio e comanda via protocollo. Solo che il
protocollo è il CAT e il transport dei dati è la scheda audio.

Nessuna classe nuova nel seam: una riga nuova nella matrice.

Attiva con `-DDSDR_BACKEND_AUDIORIG=ON` (predefinito, se Qt porta con sé
`SerialPort` e `Multimedia`).

```sh
./build/bin/decodium-sdr --backend audiorig
```

## I due piani

```
piano dati        device audio d'ingresso → ring audio del seam
piano di controllo ICatDriver su thread proprio → frequenza, modo, PTT, S-meter
```

Il polling CAT gira su un thread suo e non è mai bloccante verso il seam. Non è
un vezzo: ogni lettura sulla seriale costa qualche millisecondo di attesa, e a
5 Hz sono decine di attese al secondo. Farle sul thread del backend
produrrebbe un'applicazione che ogni tanto non risponde, e nessuno collegherebbe
la cosa alla porta seriale.

Tre letture a vuoto di fila e il CAT è dichiarato perso. Una sola sarebbe troppo
poco — basta un comando andato storto mentre la radio cambiava banda — ma
continuare a disegnare il panadattatore sulla vecchia frequenza sarebbe peggio
che fermarsi: senza CAT non sappiamo più dove siamo.

## Il driver `newcat`

Il protocollo Yaesu newcat è documentato nei manuali del costruttore: comandi
ASCII di pochi caratteri chiusi da punto e virgola. Ne servono cinque.

| Comando | A cosa serve |
|---|---|
| `ID;` | identità della radio: `ID0670;` è un FT-991A |
| `FA;` / `FA007100000;` | frequenza del VFO A, nove cifre |
| `MD0;` / `MD0x;` | modo |
| `TX;` / `TX1;` / `TX0;` | stato e comando del PTT |
| `SM0;` | S-meter, 0…255 |

L'S-meter si legge **solo in ricezione**: in trasmissione lo stesso comando
risponde con la potenza, e prenderlo per segnale farebbe schizzare il misuratore
a ogni PTT.

Aprendo la porta, RTS e DTR vengono messi bassi prima di ogni altra cosa: su
molte interfacce quelle linee *sono* il PTT, e aprire la porta manderebbe la
radio in trasmissione prima di aver detto una parola.

I codici di modo si traducono così. DATA e RTTY finiscono entrambi su DigU/DigL
perché è quello che sono da noi — banda larga, nessuna elaborazione della voce —
e nella direzione della trasmissione si sceglie DATA, che è ciò che vuole chi
manda FT8 dal codec USB.

| newcat | DECODIUM |
|---|---|
| 1 LSB · 2 USB | Lsb · Usb |
| 3 CW · 7 CW-R | Cw · Cwr |
| 4 FM · B FM-N · A DATA-FM | Fm · Nfm |
| 5 AM · D AM-N | Am |
| 8 DATA-L · 6 RTTY-L | DigL |
| C DATA-U · 9 RTTY-U · E PSK | DigU |

Un modo che la radio non ha — DSB, IQ — non fa fallire nulla: si ripiega su USB.
Il canale è già stato creato, e lasciarlo senza modo sarebbe peggio.

## La discovery sonda, non indovina

Le porte seriali si aprono una per una e si chiede `ID;` a sei velocità diverse,
dalla più probabile. Chi risponde è una radio; tutto il resto — un mouse, un
modem, un lettore di codici a barre — resta fuori. La sonda gira su un thread a
parte perché sei velocità per porta, ognuna con la sua attesa, sono secondi
interi.

L'ingresso audio si indovina dalla descrizione: `USB Audio CODEC` è il codec
integrato delle Yaesu e delle Icom recenti. Non è una certezza, e per questo
resta scegliibile dal pannello — ma indovinarlo giusto è la differenza fra
«collega l'USB e funziona» e una tendina da leggere.

La chiave del device è il **numero di serie** dell'adattatore, non il nome della
porta: su Windows COM4 diventa COM7 al cambio di presa, e le impostazioni
per-radio si perderebbero.

## Radio di riferimento: Yaesu FT-991A

Un solo cavo USB porta tutto: il codec audio a 48 kHz e due porte COM virtuali,
la *Enhanced* per il CAT.

Impostazioni della radio da verificare (i numeri di menù cambiano con il
firmware):

- **CAT RATE** al massimo supportato;
- **CAT RTS** coerente con il metodo PTT scelto — noi usiamo `TX;` via comando,
  quindi RTS può restare disattivato;
- modo **DATA-USB** con sorgente di modulazione **USB** per il TX dati;
- filtri DATA **LCUT/HCUT aperti**: sono loro a decidere quanto spettro
  vedremo, e chiusi strozzano la passata che alimenta il panadattatore.

In DATA-USB il 991A consegna circa 3,2 kHz utili. Lo spettro mostrerà quella
finestra e nient'altro: è il contratto onesto del backend, non un limite
temporaneo.

Per le radio **senza codec USB** va bene qualsiasi interfaccia audio+seriale.

## Capability dichiarate, e perché

| Capability | Valore | Perché |
|---|---|---|
| `maxRxChannels` | 1 | Il ricevitore è uno, e non c'è una banda campionata dentro cui aprirne altri |
| `tx` | `Ptt` | PTT via CAT; l'audio di trasmissione va al codec |
| `demod` | `Device` | Demodula la radio |
| `modulation` | `Device` | Modula la radio: nel ring TX va **audio mono a 48 kHz**, non banda base. Consegnarle IQ vorrebbe dire modulare due volte |
| `agc` | `Device` | L'AGC è quello della radio; gli stadi client restano disponibili sull'audio |
| `spectrum` | `Client` | La FFT la facciamo noi, ma **solo sulla passata**: una finestra di tre kilohertz attorno al VFO, non una vista di banda |
| `sampleRates` | solo 48 kHz | È la frequenza del codec e non si sceglie |
| `hasHardwareFilters` | `true` | Sono i filtri della radio, e si comandano dai suoi menù |
| `hasPreamp`, `hasAttenuator` | `false` | Esistono, ma sulla radio: offrirli qui darebbe manopole che non muovono niente |
| `adcBits` | 0 | Non c'è un nostro convertitore |
| `maxGainReductionDb` | 0 | Nessun controllo del guadagno d'ingresso dalla HAL: la guardia contro la saturazione (SPEC-003 §3) qui può solo avvertire |
| `multiClient` | `false` | La porta CAT si apre in esclusiva |
| `remoteCapable` | `false` | Lo diventerà con DECOLINK |
| `nativePanels` | `AudiorigPanel` | Scelta dell'ingresso audio e stato del collegamento |

## Comandi nativi

Usabili **solo** dal pannello `AudiorigPanel` (CONSTITUTION §4.1).

| Comando | Argomenti | Risposta |
|---|---|---|
| `audiorig.inputs` | — | elenco degli ingressi audio, con `likelyRadio` per quelli che sembrano un codec |
| `audiorig.status` | — | radio, porta CAT, velocità, ingresso audio, S-meter grezzo, overrun |
| `audiorig.setAudioInput` | `id` | vero se l'ingresso è stato aperto |

## Che cosa della SPEC-003 si applica

| Stadio | Qui |
|---|---|
| [A] Overload Guard | ridotta: nessun controllo del guadagno ADC, resta spia di clipping in ingresso |
| [B] NB wideband | **no**: gli impulsi arrivano già filtrati dalla radio |
| [C] ANF + notch traccianti | sì, in dominio audio, ancorati alla RF via CAT |
| [D] EMNR | sì, pieno valore |
| [E] NR neurale | sì — forse il caso d'uso migliore: il DSP di una radio del 2016 non si aggiorna, il nostro sì |
| [F] APF / binaurale CW | sì |
| S-meter | dalla lettura CAT, calibrata per profilo; il fondo si stima dall'audio |
| SAM potenziato | no: demodula la radio |

## Limiti noti, e lavoro che manca

**Il core non consuma ancora `audioStream()`.** Tutti i backend esistenti sono
raw-IQ, e il percorso di ricezione del `DspEngine` parte da `iqStream()`. Il
backend qui descritto è completo dalla sua parte — cattura, CAT, capability,
conformance — ma finché il core non impara a prendere audio da un backend
server-DSP, quello che arriva non si sente. È l'ondata successiva, ed è
condivisa con qualunque futuro backend Flex o Kiwi.

**Niente `hamlib`.** La copertura universale (SPEC-004 §2) richiede una
dipendenza esterna che non è stata ancora introdotta: va verificata la
compatibilità con la GPL-3.0 e registrata in `THIRD_PARTY_LICENSES`. Per ora
funzionano le Yaesu newcat.

**Il layer newcat è scritto qui**, non ripreso da `yaesu-tci-bridge`. La
proposta di estrarre una `libnewcat` condivisa fra i due progetti (SPEC-004
§8.1) resta aperta: quando succederà, questo file sarà il primo a cambiare.

**Modalità Survey** (SPEC-004 §5): non implementata.

**PTT via RTS** come alternativa al comando CAT: non implementato.

## Dove guardare nel codice

```
src/hal/backends/audiorig/
    ICatDriver.h              il seam interno del piano di controllo
    NewcatDriver.h/.cpp       Yaesu newcat su porta seriale
    CatController.h/.cpp      il polling, su thread proprio
    AudiorigBackend.h/.cpp    il seam: capability, apertura, audio, comandi
src/audio/MicSource.h         la cattura, condivisa con la catena TX
tests/hal/tst_newcat.cpp      la tabella dei modi, senza radio
```
