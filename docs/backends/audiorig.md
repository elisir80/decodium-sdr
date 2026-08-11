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
piano dati RX      ingresso audio della radio → ring audio del seam
piano dati TX      ring TX del seam → uscita audio verso la radio
piano di controllo ICatDriver su thread proprio → frequenza, modo, PTT, S-meter
```

Il ring TX del seam **è** quello dell'uscita audio, non una copia: chi
trasmette scrive direttamente dove il driver legge, e fra il PTT e l'antenna
non si aggiunge un passaggio. L'uscita si apre insieme alla ricezione e resta
aperta — aprirla al PTT costerebbe i primi decimi di secondo di ogni chiamata,
che sono esattamente quelli in cui si dice il nominativo.

Il polling CAT gira su un thread suo e non è mai bloccante verso il seam. Non è
un vezzo: ogni lettura sulla seriale costa qualche millisecondo di attesa, e a
5 Hz sono decine di attese al secondo. Farle sul thread del backend
produrrebbe un'applicazione che ogni tanto non risponde, e nessuno collegherebbe
la cosa alla porta seriale.

Tre letture a vuoto di fila e il CAT è dichiarato perso. Una sola sarebbe troppo
poco — basta un comando andato storto mentre la radio cambiava banda — ma
continuare a disegnare il panadattatore sulla vecchia frequenza sarebbe peggio
che fermarsi: senza CAT non sappiamo più dove siamo.

## Tre lingue: `newcat`, `civ` e `rigctld`

Sulla stessa porta seriale non c'è modo di sapere quale protocollo parli la
radio senza provare. La discovery prova la prima, poi la seconda; la terza non
sta su una seriale ma su TCP, e si guarda per prima perché costa una
connessione.

| Driver | Radio | Protocollo |
|---|---|---|
| `newcat` | Yaesu FT-991A, FT-891, FT-710, FT-DX10 | comandi ASCII chiusi da `;` |
| `civ` | Icom IC-7300, IC-7610, IC-7851, IC-9700, IC-705 | telai binari su bus |
| `rigctld` | qualunque radio raggiungibile via rigctl in rete | testo su TCP |

## Il driver `rigctld` (CAT in rete)

Hamlib parla con qualche centinaio di modelli, e nessuno di noi ha quei modelli
sul tavolo per scriverne i driver. `rigctld` è il modo in cui quel lavoro si
riusa **senza linkare hamlib e senza copiarne una riga**: è un demone che tiene
la porta seriale e accetta comandi su TCP.

```sh
rigctld -m 1035 -r COM5 -s 38400 -t 4532     # FT-991A
rigctld -m 3073 -r /dev/ttyUSB0 -t 4533      # IC-7300, su un'altra porta
rigctld -l                                    # l'elenco dei modelli
```

Da lì in poi è un `ICatDriver` come gli altri: il backend non sa che dall'altra
parte c'è una rete, e non deve saperlo. Cambia solo che l'indirizzo è un
`host:porta` e che la velocità di linea non esiste — la governa il demone, che
è chi ha la seriale in mano.

**Dove si cerca.** Di fabbrica `127.0.0.1:4532` e `127.0.0.1:4533`. La prima è
la porta di fabbrica del demone; la seconda perché quando il server rigctl di
DECODIUM SDR è acceso occupa la 4532 e chi avvia rigctld ripiega. Per un demone
su un'altra macchina — che è il caso per cui rigctld esiste — si passa
l'elenco:

```sh
DSDR_RIGCTLD=shack.lan:4532,192.168.1.40 decodium-sdr
```

**Non ci si attacca a sé stessi.** DECODIUM SDR espone a sua volta un server
rigctl sulla 4532: sondare quella porta trova noi. Il core dichiara al processo
la porta che si è preso e il backend la salta. Non lo si fa con un trucco di
protocollo — la prima stesura pretendeva un `dump_caps` riuscito, che il nostro
server non implementa: funzionava, ma escludeva anche tutti i rigctl minimi,
cioè proprio quelli che si vogliono raggiungere.

**Come si riconosce una radio.** Si chiede la frequenza. È l'unica cosa che
*ogni* rigctl implementa: `dump_caps` no — il server minimo risponde
`RPRT -11`, «non implementato» — e nemmeno `STRENGTH`. Una frequenza plausibile
è la prova; il nome del modello è un di più, e senza non ci si inventa niente.

**Due dialetti, e si riconoscono.** Con il prefisso `+` rigctld risponde in
forma estesa — righe `Nome: valore` chiuse da `RPRT n` — ed è la forma che non
si può fraintendere. Ma «rigctl_net» non è solo rigctld: mezzo ecosistema
espone un server rigctl **minimo**, che implementa la parte corta del
protocollo — i soli valori, una riga ciascuno, nessun esito — e ignora il `+`.
Il dialetto si stabilisce all'apertura con una domanda di sola lettura, e da
quel momento non si indovina più niente.

Nel protocollo corto contare le righe è obbligatorio: `\get_mode` ne manda due,
il modo e la larghezza, e leggerne una sola lascia l'altra nel socket — dove
verrà raccolta come risposta alla domanda successiva, sfasando il dialogo di un
giro per il resto della sessione. È il motivo per cui ogni comando dichiara
quanti valori si aspetta.

**Il caso DECODIUM 4.** È quello per cui il driver esiste sul banco di chi lo
scrive. La porta seriale della radio la può tenere un solo programma; se ce
l'ha DECODIUM 4, DECODIUM SDR non può aprirla — e non deve. DECODIUM 4 espone
un rigctl minimo sulla **4533**, e il CAT si prende da lì:

```
FT-991A → COM5 → DECODIUM 4 → rigctl 127.0.0.1:4533 → DECODIUM SDR
```

L'audio resta indipendente: viene dal codec della radio, che i due programmi si
dividono senza contendersi nulla.

| Comando | A cosa serve |
|---|---|
| `\get_freq` | c'è una radio? (ed è anche la prova del dialetto) |
| `\dump_caps` | il nome del modello, quando c'è |
| `\get_freq` / `\set_freq` | frequenza |
| `\get_mode` / `\set_mode` | modo (la larghezza si lascia a zero: quella della radio) |
| `\get_ptt` / `\set_ptt` | PTT |
| `\get_level STRENGTH` | S-meter, in decibel rispetto a S9 |

**L'S-meter arriva tarato, e resta tarato** — quando c'è. Le altre due lingue mandano la
lettura grezza dello strumento della radio — un numero fra 0 e 255 il cui
significato dipende dal modello. Hamlib invece restituisce decibel rispetto a
S9, cioè una misura già interpretata dal profilo che hamlib ha di quella radio:
`CatState::signalDbm` la porta fino al meter così com'è. Farla passare per la
scala grezza vorrebbe dire buttare via proprio ciò che la rende utile — quella
scala si ferma a −60 dBm, che è S9+13, e un locale a S9+40 ci arriverebbe
schiacciato contro il fondo senza che nessuno se ne accorga.

Se la radio non sa dire lo `STRENGTH` — capita, non tutti i backend di hamlib
lo implementano — si smette di chiederlo al primo rifiuto, e il livello resta
quello misurato sull'audio.

## Il driver `civ` (Icom)

Il CI-V è un **bus**, non un collegamento punto a punto, e da lì vengono le sue
due particolarità.

**Ogni telaio ha un indirizzo**, e i comandi lo portano dentro: un IC-7300 è
`0x94`, un IC-7610 `0x98`, un IC-7851 `0x8E`. Chi non conosce l'indirizzo non
riceve risposta, ed è per questo che si sondano — dal più diffuso.

**La radio rimanda indietro quello che le si è scritto**, perché sul bus tutti
sentono tutti. Chi non scarta la propria eco legge come risposta la domanda che
ha appena fatto: su una lettura di frequenza vuol dire leggere la frequenza che
si stava per impostare. I telai si scorrono finché non ne arriva uno indirizzato
al controllore.

| Comando | A cosa serve |
|---|---|
| `03` | leggi la frequenza |
| `05` | imposta la frequenza |
| `04` / `06` | leggi e imposta il modo |
| `1C 00` | stato e comando del PTT |
| `15 02` | S-meter |

**La frequenza viaggia in BCD, cinque byte, dal meno significativo.** È
l'aritmetica che si sbaglia in silenzio: invertire i byte non produce un errore,
produce una frequenza plausibile — 14,074 che diventa 47,041 — e la si
attribuisce alla radio. Ha il suo test, andata e ritorno su tutta la copertura,
dai 160 metri ai 23 centimetri.

**I modi dati non hanno un codice proprio.** Su una Icom si trasmette in banda
laterale con l'ingresso audio USB, e il modo resta USB o LSB: mandare il codice
della RTTY porterebbe la radio dove non si voleva. In lettura la RTTY si
riconosce lo stesso, perché la radio può esserci finita per conto suo.

### Sull'IC-7300 e famiglia

Il 7300 ha la stessa forma del FT-991A — codec USB e controllo sullo stesso cavo
— quindi tutto il resto di questa pagina vale identico. Sulla radio serve:

- **CI-V USB Echo Back** acceso o spento non cambia: l'eco la scartiamo noi;
- **CI-V USB Baud Rate** al valore che si preferisce, lo sondiamo;
- modo **USB-D** (dati) con **MOD Input** su USB per la trasmissione dal
  computer, esattamente come il DATA-USB della Yaesu;
- filtro largo, per non strozzare la passata che alimenta il panadattatore.

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

Due dettagli costati un pomeriggio, ed entrambi silenziosi:

- **Non si manda un `;` di cortesia all'apertura.** Serviva a chiudere un
  comando lasciato a metà da un programma uscito male, ma a una radio che sta
  bene fa l'effetto opposto: ammutolisce. Il difetto si presentava come «alla
  velocità giusta non risponde niente, a quelle sbagliate risponde spazzatura»
  — cioè esattamente al contrario di quel che si va a cercare. Adesso il
  terminatore si manda **solo dopo** un primo tentativo fallito.
- **`FA014250000;` sono dodici caratteri, non tredici.** Con la soglia
  sbagliata la risposta veniva scartata sempre: la frequenza restava a zero, il
  panadattatore non aveva dove ancorarsi, e niente segnalava un errore.

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
interi. Quel thread è **posseduto** dal backend e viene aspettato prima che
muoia: lasciarlo andare per conto suo faceva sopravvivere una sonda al backend
che l'aveva creata, e il segfault arrivava dentro il test di un backend
diverso.

Il tempo d'attesa della sonda è 150 ms e non 300: una radio che c'è risponde in
venti, e quell'attesa si paga per ogni velocità di ogni porta che radio non è.

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
- **CAT RTS**: noi usiamo `TX;` via comando per il PTT, quindi può restare
  disattivato. Ma se è **abilitato** — ed è il valore di fabbrica su alcune
  radio, fra cui il FTDX3000 — quella linea diventa il controllo di flusso: la
  radio non manda una risposta finché RTS non è alto. Il driver se ne accorge
  da sé, riprovando l'apertura con l'handshake hardware quando la radio non
  risponde; il secondo tentativo si fa solo sulla porta che ha scelto
  l'operatore, mai durante la scansione automatica — là si aprono porte di cui
  non si sa nulla, e alzare RTS su un'interfaccia in cui è il PTT manderebbe
  in aria una portante;
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
| `audiorig.status` | — | radio, porta CAT, velocità, ingresso e uscita audio, S-meter grezzo, scarti |
| `audiorig.outputs` | — | elenco delle uscite audio |
| `audiorig.setAudioInput` | `id` | vero se l'ingresso è stato aperto |
| `audiorig.setAudioOutput` | `id` | vero se l'uscita è stata aperta |

## Come l'audio ridiventa banda base

Il `DspEngine` non ha un secondo motore per i backend server-DSP, e non ne ha
bisogno: l'audio che una radio consegna **è** un segnale in banda base, reale
invece che complesso. Ricostruendone il segnale analitico si torna esattamente
al caso di sempre.

Una componente a 1500 Hz d'audio ridiventa una componente a VFO+1500 Hz di
radiofrequenza. Il panadattatore si ancora così alla frequenza vera (SPEC-004
§4), e tutti gli stadi della SPEC-003 — notch traccianti, EMNR, rete neurale,
APF, binaurale — si applicano senza sapere da dove venga il flusso. È lo stesso
percorso che servirà a un backend Flex o Kiwi.

Un solo filtro fa due mestieri: rende analitico il segnale tenendo le sole
frequenze positive, e lo limita alla banda che una radio consegna davvero
(200–4000 Hz). La transizione a 250 Hz è ciò che ne decide il costo — sotto i
200 il numero di tap supererebbe `kMaxFirTaps`, sopra i 400 comincerebbe a
mangiare le voci più basse.

**Da che parte del VFO stia il segnale non si indovina.** In USB l'audio sale
con la radiofrequenza, in LSB scende: il segnale analitico va coniugato, o il
filtro di canale — che in LSB guarda le frequenze negative — non trova nulla.
Il difetto si presenterebbe come «la radio in LSB non si sente», e nessuno lo
cercherebbe nel motore. Il lato lo decide il modo del canale, che è lo stesso
che il CAT ha impostato sulla radio.

In AM e FM non si applica niente di tutto questo: l'emissione occupa davvero
entrambi i lati della portante, e lo spettro speculare non è un artefatto — è
ciò che c'è in aria.

**Il canale segue il VFO.** Girando la manopola sulla radio la passata si
sposta con lei, e un canale che restasse alla vecchia frequenza finirebbe fuori
da ciò che la radio consegna: muto. Con un backend che demodula a bordo il
canale è il VFO, e lo segue.

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

**Niente `hamlib`.** La copertura universale (SPEC-004 §2) richiede una
dipendenza esterna che non è stata ancora introdotta: va verificata la
compatibilità con la GPL-3.0 e registrata in `THIRD_PARTY_LICENSES`. Per ora
funzionano le Yaesu newcat.

**Il layer newcat è scritto qui**, non ripreso da `yaesu-tci-bridge`. La
proposta di estrarre una `libnewcat` condivisa fra i due progetti (SPEC-004
§8.1) resta aperta: quando succederà, questo file sarà il primo a cambiare.

**Il livello di trasmissione parte al 25 %, non al 90.** Il fondo scala è il
livello giusto per un SDR — è lì che sta la dinamica — ma l'ingresso DATA di un
ricetrasmettitore ne vuole un decimo: spinto al massimo fa lavorare l'ALC in
permanenza, e quello che esce in aria è largo il doppio di quanto dovrebbe. Il
giudice è l'ALC della radio, non il nostro cursore.

**Una porta CAT, un programma solo.** Se un altro software tiene aperta la porta
— DECODIUM 4, WSJT-X, un rigctld — questo backend non la trova, e viceversa: chi
la prende per primo la tiene. Non è un limite nostro ma della porta seriale.

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

## Spegnere la sonda

```sh
DSDR_AUDIORIG_NO_PROBE=1 decodium-sdr
```

Con questa variabile il backend non apre nessuna porta seriale: niente
riconoscimento automatico della radio, tutto il resto funziona.

Serve a chi ha la radio accesa accanto e non vuole che il programma la tocchi
mentre cerca. Sondare una porta vuol dire aprirla, e su Windows l'apertura alza
DTR e RTS per qualche millisecondo prima che un programma possa abbassarli: su
una radio con «CAT RTS» attivo quello è il PTT. La sonda apre ogni porta **una
volta sola** per driver — è il minimo possibile — ma il minimo possibile non è
zero, e chi vuole zero deve poterlo avere.

La via giusta resta spegnere «CAT RTS» nei menù della radio: il PTT lo
comandiamo con un comando CAT e quella linea non ci serve.

La suite dei test la usa: nessuno deve poter mandare in aria una portante
lanciando `ctest`.
