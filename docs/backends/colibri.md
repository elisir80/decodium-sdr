# Backend `colibri`

Il **ColibriNANO** di Expert Electronics è un ricevitore SDR grande quanto una
chiavetta USB: ADC a 14 bit campionato a 122,88 MHz, copertura da 100 kHz a
55 MHz, alimentazione e dati sulla stessa presa. È il primo hardware vero su
cui DECODIUM SDR è stato provato, e resta il banco di riferimento per tutto
ciò che riguarda la catena RX.

Attiva con `-DDSDR_BACKEND_COLIBRI=ON` (predefinito).

```sh
./build/bin/decodium-sdr --backend colibri --auto-connect
```

## Il protocollo, che non è un protocollo

Il ColibriNANO non parla un protocollo documentato: parla con la libreria del
costruttore, `colibrinano_lib`, che incapsula il dialogo USB. È lei l'autorità
sul formato — noi ci limitiamo a chiamarla.

La libreria si carica a **runtime** con `QLibrary`, non si linka: un
eseguibile linkato a lei non partirebbe affatto sulle macchine che non ce
l'hanno, e il ColibriNANO è un backend fra molti. Se manca, il backend
compare nell'elenco, dichiara l'errore e resta fermo; tutto il resto
dell'applicazione funziona.

I simboli risolti sono undici:

| Simbolo | A cosa serve |
|---|---|
| `initialize`, `finalize` | apertura e chiusura della libreria |
| `version`, `information` | versione e descrizione del device |
| `devices` | quanti ricevitori sono collegati |
| `open`, `close` | presa e rilascio di un device per indice |
| `start`, `stop` | avvio e arresto dello streaming |
| `setFrequency` | sintonia del ricevitore, in hertz |
| `setPream` | preamplificatore e attenuatore, in dB |

> Il nome è davvero `setPream`, senza la `p` finale. È un refuso della
> libreria, non nostro: correggerlo qui vorrebbe dire non trovare il simbolo.
> Vale la pena saperlo prima di perderci mezz'ora.

I campioni arrivano da un **callback della libreria**, su un thread suo. Il
callback copia nel ring SPSC e non fa altro: nessuna allocazione, nessun
segnale Qt con dati dentro (CONSTITUTION §5). Il ring tiene circa 1,4 secondi
a 3,072 MS/s, perché la libreria consegna a raffiche e una pausa del DSP non
deve diventare un buco udibile.

Il callback porta anche un flag di **saturazione dell'ADC**, che il backend
accumula e rende leggibile dal comando nativo `colibri.health`.

## Procurarsi la libreria

`colibrinano_lib` è del costruttore e non è nostra da ridistribuire: non sta
nel repository. Chi ha la radio la mette in `third_party/colibrinano/`, e
CMake la trova da sé, la copia accanto all'eseguibile a ogni build e la
include nel pacchetto.

```
third_party/colibrinano/colibrinano_lib.dll        # Windows
third_party/colibrinano/libcolibrinano_lib.so      # Linux
```

Chi preferisce indicarla altrove usa `-DDSDR_COLIBRI_LIB=/percorso/della/lib`.

Questa disposizione nasce da un difetto vero: prima la libreria viveva
accanto all'eseguibile di *una* cartella di build, copiata a mano una volta.
Ogni nuova cartella di build nasceva senza, e il ColibriNANO «spariva» —
l'elenco dei device restava vuoto e sembrava un guasto della radio o del
driver.

Su Windows serve il driver **D2XX** dell'FTDI: il ColibriNANO si presenta
come un FT232H, e con il driver VCP al posto del D2XX la libreria non lo
riconosce.

## Capability dichiarate, e perché

| Capability | Valore | Perché |
|---|---|---|
| `maxRxChannels` | 4 | Il ricevitore è **uno**. I quattro canali sono logici: vivono dentro la banda campionata e li demodula il DSP client. Il limite è una scelta di ergonomia, non del device |
| `tx` | `None` | Non esiste trasmettitore. Il PTT quindi non è disabilitato: non viene creato (CONSTITUTION §7) |
| `demod`, `spectrum`, `agc` | `Client` | Il device consegna IQ grezzo e nient'altro: tutta la catena è nostra |
| `sampleRates` | 48 k → 3,072 MS/s | Nove passi, quelli che la libreria accetta. Il predefinito è 768 kS/s: banda comoda in HF senza chiedere troppo a un portatile |
| `minFrequencyHz` … `maxFrequencyHz` | 100 kHz … 55 MHz, o 245,76 MHz con le zone di Nyquist aperte | Vedi sotto: la copertura dichiarata è quella del costruttore, l'altra si accende di proposito |
| `hasPreamp`, `hasAttenuator` | `true` | Sono **la stessa manopola**: un solo valore da −31,5 a +6 dB. Il device non ha due comandi distinti, e fingere il contrario avrebbe prodotto una UI con due cursori che si contendono lo stesso registro |
| `maxGainReductionDb` | 31,5 | Quanto la guardia contro la saturazione (DSDR-SPEC-003 §3) può togliere: si scende lungo quella scala a partire dal livello scelto dall'operatore, che resta il tetto |
| `hasHardwareFilters` | `true` | Passa-basso commutato dal device |
| `adcBits` | 14 | Serve alla UI per giudicare la dinamica disponibile |
| `multiClient` | `false` | La libreria apre il device **in esclusiva**: una seconda applicazione — o una seconda istanza della nostra — trova la porta occupata |
| `remoteCapable` | `false` | È un device USB locale |
| `supportsRecording` | `true` | Il flusso IQ è nostro dal ring in poi |
| `nativePanels` | `ColibriDevicePanel` | Il pannello del preamplificatore e della salute del device |

## Le zone di Nyquist

L'ADC campiona a 122,88 MHz e **non ha un mescolatore davanti**: sopra
61,44 MHz — metà di quel ritmo — non c'è più niente da sintonizzare. I segnali
però continuano ad arrivare, ripiegati dentro la prima zona. È il campionamento
in sottofrequenza, ed è una tecnica, non un difetto.

Ripiegare è aritmetica di tre righe:

| Zona | Frequenza | DDC | Spettro |
|---|---|---|---|
| 1 | 0 … 61,44 MHz | la frequenza stessa | dritto |
| 2 | 61,44 … 122,88 | 122,88 − f | **rovesciato** |
| 3 | 122,88 … 184,32 | f − 122,88 | dritto |
| 4 | 184,32 … 245,76 | 245,76 − f | **rovesciato** |

Nelle zone pari la frequenza ripiegata **scende** mentre quella vera sale: lo
spettro esce a rovescio e il backend lo raddrizza coniugando. La coniugazione
di convenzione — il ColibriNANO consegna con il segno opposto al nostro,
sempre — e quella della zona si sommano: due rovesciamenti fanno uno spettro
dritto, ed è un `!=` fra due booleani, non due cicli.

L'aritmetica è pubblica e statica apposta (`ColibriBackend::tuningFor`): si
sbaglia in silenzio — si chiede 144,300 e si ascolta due megahertz più in là —
e va potuta verificare senza il device attaccato. Lo fa
`tests/hal/tst_colibri_nyquist.cpp`.

L'interruttore sta nel pannello `ColibriNANO`, nella colonna dei pannelli, e
**si ricorda**: chi ha un filtro davanti all'antenna non lo smonta ogni sera, e
farsi rispegnere le zone a ogni avvio vorrebbe dire ritrovare il ricevitore
fermo a 55 MHz senza ricordarsi perché.

**Perché è un interruttore e non il valore predefinito.** Senza un passa-banda
davanti all'antenna tutte le zone arrivano insieme e si sovrappongono: quello
che si vede a 144 MHz potrebbe essere una stazione a 100 MHz, o a 21. Chi
accende le zone superiori sta dicendo che sa cosa aspettarsi, e che davanti ci
mette un filtro. Il pannello mostra la zona corrente e se lo spettro è stato
raddrizzato, perché nella zona sbagliata il silenzio ha lo stesso aspetto di
un'antenna staccata.

Il limite di 245,76 MHz sono quattro zone. Oltre, l'ingresso analogico
dell'ADC non arriva: qualcuno riceve fino a mezzo gigahertz con un
preamplificatore esterno, ma dichiararlo vorrebbe dire promettere il suo banco
a tutti.

## Comandi nativi

Usabili **solo** dal pannello `ColibriDevicePanel` (CONSTITUTION §4.1): il core
non li conosce e non deve conoscerli.

| Comando | Argomenti | Risposta |
|---|---|---|
| `colibri.setPreamp` | `db` | il valore applicato, limitato a −31,5…+6 |
| `colibri.preampRange` | — | `min`, `max`, `value` correnti |
| `colibri.health` | — | `adcOverload` (adesso) e `overloadBlocks` (da quando è aperto) |
| `colibri.setExtendedRange` | `enabled` | apre o chiude le zone di Nyquist superiori |
| `colibri.nyquist` | — | `zone`, `inverted`, `deviceHz` e il limite corrente |

Toccare il preamplificatore a mano **azzera** la riduzione in corso della
guardia contro la saturazione, e il nuovo valore diventa il tetto: chi rimette
le mani sulla manopola sta dicendo qual è il livello che vuole.

## Limiti noti

**Un solo programma per volta.** La libreria apre il device in esclusiva. Se
l'apertura fallisce con «è già in uso da un altro programma?», quasi sempre è
un'altra istanza rimasta aperta — capita facilmente durante lo sviluppo.

**La conformance suite crasha, a volte, con il device collegato.** Il segfault
è dentro `colibrinano_lib`, in un suo thread, dopo la chiusura del device:
nello stack non c'è un solo frame di codice nostro. Non è riproducibile a
comando — tre esecuzioni di fila sono pulite. Il primo sospetto è `finalize()`,
che risolviamo dalla libreria e non chiamiamo mai: chiuderla mentre un suo
thread sta ancora consegnando campioni potrebbe essere peggio del non
chiuderla affatto, ma va verificato con un debugger attaccato. In CI non si
vede, perché senza hardware il backend viene saltato.

**Sopra i 55 MHz** serve sapere cosa si sta facendo: vedi la sezione sulle
zone di Nyquist. Senza un passa-banda esterno le zone arrivano tutte insieme e
si sovrappongono.

**Nessuna trasmissione**, per costruzione.

## Dove guardare nel codice

```
src/hal/backends/colibri/
    ColibriLibrary.h/.cpp    caricamento della libreria e risoluzione dei simboli
    ColibriBackend.h/.cpp    il seam: capability, apertura, streaming, comandi
src/app/qml/ColibriDevicePanel.qml   preamplificatore e salute del device
```
