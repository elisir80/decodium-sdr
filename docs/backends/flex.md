# FlexRadio serie 6000 — a che punto siamo

**Il backend c'è, e non è mai stato provato su una radio vera.** Le due cose
stanno insieme, e vanno lette insieme.

La sequenza che apre il flusso DAX IQ è scritta sulle fonti pubbliche di
FlexRadio, ma il legame fra fetta, panadapter e canale DAX cambia fra le
versioni maggiori del firmware. Finché qualcuno non lo prova, non si può dire
che funzioni.

Il modo di essere onesti, qui, non è tenerlo spento — è **farlo parlare**. Se
la sequenza non passa, il backend dice su quale comando si è fermato e con che
codice la radio ha risposto. Se passa e non arrivano pacchetti, lo dice pure, e
nomina le due cause possibili — il firewall o un firmware che vuole una
sequenza diversa — perché si risolvono in modi diversi. Non tace e non finge.

Chi ha un Flex davanti chiude la questione in dieci minuti: apre il backend,
legge il diario, e se serve manda a mano il comando che manca con
`flex.send`.

Un FlexRadio 6000 parla due lingue insieme:

| | Trasporto | Cosa passa |
|---|---|---|
| **canale di comando** | TCP 4992, righe di testo | frequenze, modi, fette, stato della radio |
| **flusso dati** | UDP, pacchetti VITA-49 | IQ, audio, panadapter, waterfall |

Il canale di comando è implementato. Del flusso dati c'è la **decodifica**, ed
è scritta sui numeri che FlexRadio pubblica nel proprio SDK, non a memoria:
manca la sequenza di comandi che apre un flusso IQ, che va confermata su una
radio vera.

## Quello che c'è

`hal::flex::FlexClient` apre il canale di comando, fa la stretta di mano e
raccoglie quello che la radio dice di sé. Nella finestra delle sorgenti, accanto
a un FlexRadio trovato in rete, compare **Interroga**: apre il collegamento e
riporta modello, soprannome, nominativo e versione di SmartSDR.

Serve già. Chi ha un Flex e non sa se il problema è la rete, il firewall o il
programma, con questo ha una risposta: dal «c'è» — che lo dice il rilevamento —
al «ci parlo».

Il collegamento si chiude dopo cinque secondi. Il Flex concede un numero finito
di posti ai programmi collegati, e tenerne uno occupato per non riceverne
campioni sarebbe scortese verso SmartSDR, che gira quasi sempre accanto.

### Il protocollo di comando

Quattro tipi di riga arrivano dalla radio, e si distinguono dal primo carattere:

```
V<versione>              la versione del protocollo, appena connessi
H<handle>                l'identificativo assegnato a noi, in esadecimale
R<seq>|<codice>|<testo>  la risposta a un nostro comando
S<handle>|<stato>        un cambiamento di stato, non richiesto
M<codice>|<testo>        un messaggio per l'operatore
```

E una sola va verso la radio: `C<seq>|<comando>`.

Due trappole, in un protocollo di testo che sembrerebbe impossibile da
sbagliare:

- **Il codice d'errore è esadecimale.** `50000015` letto in decimale è un numero
  plausibile, e il comando fallito sembrerebbe riuscito.
- **I campi `chiave=valore` arrivano in ordine libero**, e il firmware ne
  aggiunge di nuovi. Leggerli per posizione funziona fino al primo
  aggiornamento della radio.

Entrambe hanno il loro test.

## Il flusso dati: VITA-49

I pacchetti arrivano su UDP. L'intestazione è quella dello standard; quello che
serve sapere di FlexRadio sono tre numeri, e stanno nell'SDK che il costruttore
pubblica:

| | |
|---|---|
| OUI | `0x001C2D` |
| information class | `0x534C` |
| packet class | un **campo di bit** |

Il terzo è la parte che conta, ed è quella che evita di indovinare. **Il codice
di classe non è un identificativo opaco da confrontare con una tabella: è la
descrizione del carico.** Dice quanti bit per campione, quanti canali, e se sono
in virgola mobile IEEE-754:

```
bit 5..6   bit per campione   (3 = 32)
bit 7..8   canali             (3 = due, cioè I e Q)
bit 9      IEEE-754
```

Un decodificatore che lo legge non ha bisogno di sapere in anticipo che cosa gli
arriverà: se il pacchetto dichiara un formato che non sappiamo leggere, lo salta
e lo dice. È la differenza fra indovinare — e quando si sbaglia si consegnano
campioni plausibili, cioè rumore che sembra una banda — e verificare.

**Che non sia teoria lo dimostra FlexLib**, la libreria del costruttore: ha
avuto per un periodo esattamente questo difetto, trattando come virgola mobile
un flusso DAX IQ che la radio mandava in virgola fissa. Il risultato di quello
scambio non è silenzio — sono numeri assurdi che il DSP elabora diligentemente.
Qui i due casi si leggono entrambi, e quale sia lo dice il bit.

Due dettagli che si sbagliano in silenzio, ed entrambi hanno il loro test:

- **L'intestazione non è di misura fissa.** I marcatori temporali ci sono solo
  se i bit TSI e TSF lo dicono: sette parole con, cinque senza. Darla per fissa
  funziona finché una radio non ne manda uno senza, e allora tutti i campioni
  scivolano di due parole restando plausibili.
- **I float viaggiano in ordine di rete.** Leggerli com'è in memoria
  funzionerebbe solo su una macchina big endian, e le nostre non lo sono: quel
  che ne uscirebbe sono numeri enormi o denormali, cioè silenzio o rumore.

## La sequenza che apre il flusso

Quattro passi, e nessuno si indovina — sono attestati nella documentazione di
FlexRadio e nelle risposte dei suoi tecnici:

```
client udpport <porta>
stream create daxiq=<canale> ip=<nostro indirizzo> port=<porta>
display pan create x=<larghezza> y=<altezza>
dax iq s <canale> pan=<id del pan> daxiq_rate=<velocità> client_handle=<handle>
```

L'indirizzo si dice per esteso: su una macchina con più schede di rete la radio
non può indovinare su quale si vogliano ricevere i campioni.

**Il quarto passo è quello che decide la velocità**, e senza di lui il flusso
nasce a 48 kS/s qualunque cosa si sia chiesto — con il DSP che calcolerebbe
tutto sulla velocità sbagliata senza accorgersene. Le velocità vanno da 24 a
192 kS/s.

## Le due forme del comando di creazione

Le fonti pubbliche ne descrivono due, e non si sceglie a memoria:

```
stream create daxiq=<canale> ip=<indirizzo> port=<porta>
stream create type=iq daxiq_channel=<canale> ip=<indirizzo> port=<porta>
```

Si prova la prima; se la radio la rifiuta si prova la seconda, una volta sola.
Rifiutate entrambe, il problema non è la forma, e continuare a provare
nasconderebbe la diagnosi invece di darla.

## Quello che manca ancora

- **Provare la sequenza su una radio vera.** È l'unica cosa che manca al
  ricevitore.
- **La trasmissione.** Passa da DAX MIC, che è un'altra metà di protocollo e
  non è scritta: le capability dichiarano `TxSupport::None`, quindi la UI non
  mostra un PTT che non farebbe niente (CONSTITUTION §7).
- **Il DAX Audio** — i canali audio demodulati per fetta — non è affrontato:
  qui si prende l'IQ e si demodula da questa parte, con tutta la catena della
  SPEC-003.

## Nel frattempo, un Flex si usa già

Non da questo backend, ma da `audiorig`: SmartSDR espone i canali DAX come
dispositivi audio del sistema, quindi si sceglie il DAX RX come ingresso e si
prende il CAT dalla porta seriale di SmartSDR o da un `rigctld`. Si perde la
banda larga — si ascolta la passata della fetta, non lo spettro completo — ma
frequenza, modo e tutto lo studio audio funzionano.

## Diagnosi

`nativeCommand("flex.status")` restituisce il passo raggiunto, i pacchetti e i
campioni arrivati, i buchi nel contatore VITA, la porta UDP e se è stata usata
la forma alternativa del comando.

`nativeCommand("flex.send", {command})` manda una riga a mano sul canale di
comando: è così che si chiude la parte di sequenza che non abbiamo potuto
verificare, senza ricompilare niente.

## Dove guardare nel codice

```
src/hal/backends/flex/
    FlexProtocol.h/.cpp   le righe, pure e statiche
    FlexClient.h/.cpp     il canale di comando su TCP 4992
src/hal/RadioScout.h      il rilevamento, condiviso con le altre famiglie
tests/hal/tst_flex_protocol.cpp
```
