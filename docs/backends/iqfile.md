# Backend `iqfile`

Riproduzione di registrazioni IQ (RF-17, seconda metà). Chiude il cerchio che
`IqRecorder` apre: quello che l'applicazione scrive, la stessa applicazione lo
riascolta.

Dietro il seam una registrazione è una radio come un'altra — sintonizzabile,
demodulabile, con il suo waterfall e i suoi quattro canali. Con una differenza
sola, ma grossa: il tempo si può fermare e riavvolgere. È quella differenza a
renderlo utile per lavorare sul DSP, perché è l'unica sorgente che ripete
esattamente lo stesso segnale a ogni esecuzione.

## Classe e capability

Backend **raw-IQ**: consegna campioni, tutto il resto lo fa il DSP client.

| Capability | Valore | Perché |
|---|---|---|
| `maxRxChannels` | 4 | Come il demo: la registrazione contiene tutta la banda, i canali sono entità del DSP |
| `coherentRx` | `true` | I canali nascono dallo stesso flusso: la coerenza è reale |
| `maxPanadapters` | 4 | |
| `tx` | `None` | Da un file non si trasmette. Il pulsante PTT non esiste, non è disabilitato |
| `demod`, `spectrum`, `agc` | `Client` | |
| `sampleRates` | **una sola**, dopo l'apertura | Il rate è quello con cui la registrazione fu fatta. Prima di aprire si dichiarano i valori consueti, perché il contratto vieta a un backend raw-IQ di lasciare la lista vuota |
| `minFrequencyHz` … `maxFrequencyHz` | `0` … `0` | Non dichiarati: la copertura è un punto solo, e vincolarla farebbe rifiutare alla UI sintonie legittime dentro la banda registrata |
| `supportsRecording` | `false` | Ri-registrare una registrazione non serve a nessuno |
| `nativePanels` | `IqFileDevicePanel` | Il trasporto |

## Come si trovano le registrazioni

La discovery elenca i `.wav` di:

1. i percorsi in **`DSDR_IQFILE_PATH`** — file o cartelle, più voci separate da
   `;`. È anche il modo in cui i test puntano il backend su una registrazione
   appena creata;
2. la cartella predefinita del registratore: *Musica* → `DECODIUM SDR`.

Ogni file viene sondato leggendone l'intestazione, non il contenuto: elencare
una cartella piena di registrazioni da un'ora deve restare istantaneo. Un file
illeggibile viene **saltato in silenzio** — la cartella delle registrazioni può
contenere qualunque WAV, e un errore per ciascuno sarebbe rumore. Aprirne uno
illeggibile, invece, è un errore esplicito.

Il `deviceId` è il percorso del file: stabile fra riavvii, così lo stesso file
riaperto domani ritrova le proprie impostazioni.

## Formati letti

| Formato | Da dove arriva |
|---|---|
| WAV float32, 2 canali | Il nostro `IqRecorder` |
| RF64 float32, 2 canali | Idem, oltre i 4 GB |
| WAV PCM 16 bit, 2 canali | SDR#, SDRuno, la maggior parte dei registratori |
| WAV PCM 8 bit senza segno | Dump grezzi da RTL-SDR (128 = zero) |

Sono venti righe di conversione che rendono il backend utile con i file di
qualcun altro, non solo con i nostri.

Un file **troncato** — registrazione interrotta, copia parziale — dichiara nel
chunk `data` più byte di quanti ne abbia. Ci si fida di ciò che c'è davvero
invece di leggere oltre la fine.

## La frequenza centrale, e perché serve il sidecar

Il WAV sa dire quanti campioni al secondo, non su quale frequenza: senza il
sidecar JSON che scriviamo accanto, una registrazione IQ è un file di numeri
senza contesto. Quando il sidecar manca si tenta il **nome del file**, che nei
nostri contiene i MHz (`…_7.100MHz_192kSps.wav`).

È un ripiego dichiarato, non una fonte: il pannello mostra un avviso, perché
tutto il resto della UI tratterebbe quella frequenza come certa.

## Sintonia e frequenza di campionamento

Entrambe si **rifiutano** con `BackendError::Unsupported`, e il valore vero
viene riemesso subito dopo. Spostare la frequenza di una registrazione
significherebbe mentire su cosa contengono i campioni; accettare in silenzio
sarebbe peggio, perché la scala del panadattatore direbbe una cosa e i segnali
un'altra.

Sintonizzare *dentro* la banda registrata funziona normalmente: è il DSP a
spostarsi, come su qualsiasi backend raw-IQ.

## Modello di threading

Un `IqFileWorker` su thread dedicato legge dal file e scrive nel
`SpscRing<float>` da 2²⁰ elementi. Il ritmo è quello della registrazione, non
quello del disco: si calcola quanti campioni sarebbero dovuti uscire da
`start()` a ora, sull'orologio monotono. Una registrazione riprodotta a tutta
forza riempirebbe il ring in un lampo e il DSP vedrebbe una banda che scorre a
caso.

Se il consumatore resta indietro oltre otto blocchi, i campioni si dichiarano
persi invece di rincorrere: recuperare all'infinito farebbe scorrere la
registrazione più in fretta del vero.

## Comandi nativi

Il trasporto non esiste per nessuna radio vera, quindi non entra nel seam
generale: passa dalla valvola di §4.1, e solo `IqFileDevicePanel` lo usa.

| Comando | Argomenti | Ritorno |
|---|---|---|
| `iqfile.setPaused` | `paused` (bool) | Lo stato applicato |
| `iqfile.paused` | — | `bool` |
| `iqfile.setLoop` | `loop` (bool) | Lo stato applicato |
| `iqfile.setSpeed` | `speed` (double, 0,1…8) | La velocità applicata, limitata |
| `iqfile.seek` | `ms` (qint64) | La posizione richiesta |
| `iqfile.status` | — | Mappa: `path`, `positionMs`, `durationMs`, `paused`, `loop`, `speed`, `hasSidecar`, `recordedWith` |
| `iqfile.recordings` | — | Elenco delle registrazioni visibili |

Riavvolgere una riproduzione finita la fa ripartire: è ciò che ci si aspetta
premendo «torna all'inizio».

## Limiti noti

- **Fuori da 1× l'audio cambia tono.** Il DSP demodula comunque, ma i campioni
  arrivano a un ritmo diverso da quello con cui furono acquisiti. La velocità
  serve a cercare un punto in una registrazione lunga, non ad ascoltare.
- **Nessun ricampionamento.** Il rate è quello del file; se il resto della
  catena non lo gradisce, è la catena a doversi adattare.
- **Un file per volta.** Aprire due registrazioni in parallelo richiederebbe
  due istanze del backend, che il core oggi non gestisce.
- La riproduzione parte **automaticamente** all'apertura. Chi vuole aprire in
  pausa deve premere pausa subito dopo: aprire una registrazione e non sentire
  nulla sarebbe più sorprendente.

## Verifica

`tests/integration/tst_iqfile_playback.cpp` fa il giro completo su un file
vero: discovery, apertura, campioni (un tono a modulo unitario, che smaschera
formato sbagliato, disallineamento o scala errata), rifiuto della sintonia,
trasporto, ripiego sul nome del file, file illeggibile.

La conformance suite della HAL lo prende in carico da sé, essendo data-driven
sui backend registrati.
