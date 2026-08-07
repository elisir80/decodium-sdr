# Backend `demo`

Backend sintetico (RF-09). Non parla con nessun hardware: genera i campioni IQ
che una radio consegnerebbe, in tempo reale.

Non è un giocattolo. È il banco su cui girano gli integration test in CI
(RNF-07), è ciò che rende l'applicazione dimostrabile senza hardware, ed è
l'unico backend che CONSTITUTION §9 rende bloccante: se una PR lo rompe, la PR
è rotta.

## Classe e capability

Backend **raw-IQ**: consegna solo campioni, tutto il resto lo fa il DSP client.

| Capability | Valore | Perché |
|---|---|---|
| `maxRxChannels` | 4 | Esercita il multi-canale come SDR One |
| `coherentRx` | `true` | I canali nascono dallo stesso flusso IQ: la coerenza è reale, anche se il flusso è sintetico |
| `maxPanadapters` | 4 | |
| `tx` | `Ptt` | TX simulato: in trasmissione il ricevitore ammutolisce |
| `demod`, `spectrum`, `agc` | `Client` | Nessun DSP "a bordo" |
| `sampleRates` | 192, 384, 768, 960, 1536 kS/s | 192 kHz è il default: copre la porzione di banda interessante con 47 Hz di risoluzione a FFT 4096 |
| `minFrequencyHz` … `maxFrequencyHz` | 100 kHz … 2 GHz | Copertura dichiarata larga: è sintetico |
| `adcBits` | 16 | Valore dichiarativo |

## Device offerti

| `deviceId` | Centro | Contenuto |
|---|---|---|
| `synthetic-hf` | 7,100 MHz | Un pomeriggio sui 40 m |
| `synthetic-vhf` | 145,000 MHz | Ripetitori FM, un beacon CW e una SSB in gamma bassa |

La discovery è **asincrona** anche se le risposte sono note in anticipo: se
consegnasse i device durante la chiamata, il core svilupperebbe per errore
l'assunzione che ogni backend faccia altrettanto.

## La banda sintetica

Piano dei 40 m (`SyntheticBand::hfBandPlan()`):

| Frequenza | Tipo | Livello | Note |
|---|---|---|---|
| 7,005 MHz | CW | −42 dBFS | `CQ CQ DE IU8LMC`, 24 WPM, QSB 11 s |
| 7,0125 MHz | CW | −55 dBFS | `TEST DE 9H1SR`, 28 WPM |
| 7,023 MHz | CW | −63 dBFS | `QRL? DE DL1ABC`, 18 WPM |
| 7,030 MHz | CW | −70 dBFS | Beacon, nessun fading |
| 7,040 MHz | Portante | −75 dBFS | Marcatore fisso |
| 7,074 MHz | Digitale | −50 dBFS | 8 toni a passo 6,25 Hz, ciclo 15 s |
| 7,100 MHz | AM | −48 dBFS | Portante + modulazione |
| 7,145 MHz | LSB | −40 dBFS | Voce sintetica a tre formanti |
| 7,182 MHz | LSB | −58 dBFS | Idem, più debole |

Rumore di fondo gaussiano complesso a −95 dBFS.

Ogni segnale è costruito correttamente nel dominio complesso: una SSB
sintetica è davvero a banda laterale unica (segnale analitico, coniugato per
la laterale inferiore), non un tono mascherato. La manipolazione CW ha fronti a
coseno rialzato da 5 ms: senza sagomatura i click si spalmerebbero su tutta la
banda e il waterfall mostrerebbe artefatti che una stazione vera non ha.

## Modello di threading

Un `DemoWorker` su thread dedicato genera a ritmo reale: il debito di campioni
è calcolato sull'orologio monotono, non sul numero di tick del timer, così il
flusso non deriva anche se il timer jitta. Se il consumatore resta indietro
oltre otto blocchi, il worker dichiara i campioni persi invece di produrre un
burst di recupero inutile.

I campioni finiscono in un `SpscRing<float>` da 2²⁰ elementi (~2,7 s a
192 kHz). Il signal `iqFrameReady` porta solo il descrittore.

## Limiti noti

- La coerenza fra canali è strutturale, non misurata: non serve a validare
  algoritmi di beamforming, per i quali serve `dlink` (Fase 3).
- Il TX è puramente logico: nessuna forma d'onda viene generata in
  trasmissione, il ricevitore semplicemente ammutolisce.
- Il piano di banda è statico. Se servisse variarlo da UI, la strada è
  `nativeCommand()` in un pannello backend-specifico — mai un'estensione
  dell'interfaccia generale.

## Comandi nativi

| Comando | Argomenti | Ritorno |
|---|---|---|
| `demo.setNoiseFloorDb` | `db` (double) | Il livello impostato |
| `demo.stationCount` | — | Numero di stazioni nel piano corrente |

Sono dimostrativi: mostrano la forma della valvola di sfogo di §4.1 senza che
il core ne sappia nulla.
