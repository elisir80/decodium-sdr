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

## La trasmissione

La voce fa la strada opposta, e la fa con lo stesso formato: pacchetti VITA-49
verso la **porta 4993** della radio, con il codice di classe che dichiara
trentadue bit per campione, due canali, virgola mobile — esattamente quello che
si legge in ricezione, costruito all'incontrario.

Tre cose vanno dette con precisione, perché sono quelle che si sbagliano.

**Il formato è a due canali e la voce è una sola.** Si duplica su entrambi.
Mandarla su uno e lasciare l'altro a zero non dà silenzio: dà metà livello, e
chi trasmette è l'ultimo ad accorgersene.

**La velocità è 24 kHz, il motore TX produce a 48**, e il dimezzamento passa da
un passa-basso — non dal prendere un campione su due. Decimare senza filtro
butta la banda fra 12 e 24 kHz addosso a quella che resta, e lì sopra qualcosa
c'è sempre: il limiter in coda alla catena genera armoniche per mestiere.
Ripiegate non si sentono come acuti, si sentono come una voce sporca.

**La frequenza di trasmissione non la decide DECODIUM.** Su un Flex trasmette la
*slice* marcata TX, e questo backend le slice non le crea né le governa: apre un
flusso DAX IQ e un panadapter, che sono la ricezione. Mandare `slice tune` a una
slice altrui vorrebbe dire spostare la frequenza di SmartSDR aperto accanto, che
su queste radio è il caso normale. Quindi `setTxFrequency` **non manda niente**,
e lo scrive nel diario invece di far credere il contrario. Chi preme PTT
trasmette dove la radio è messa.

### Il guinzaglio

`xmit 1` è l'unica riga di tutto il programma che manda una radio in aria, e la
regola qui è più stretta che altrove: **niente può lasciare il trasmettitore
inserito.**

- Se cade il canale di comando mentre si trasmette, il PTT si rilascia subito —
  senza aspettare niente, perché si sa già che il `xmit 0` non arriverebbe.
- Se nessuno chiude la trasmissione entro **due minuti**, la chiude il
  temporizzatore, e lo dice. Non è un caso da manuale: è quello che succede
  quando il programma si impianta o la rete cade, e una portante lasciata in
  frequenza non se ne va da sola.
- `close()` rilascia il PTT prima di ogni altra cosa.
- Il PTT si rifiuta se il flusso audio di trasmissione non si è aperto: una
  portante muta occupa la frequenza e non se ne accorge nessuno tranne i vicini.

### Che cosa è verificato e che cosa no

Il pacchetto che mandiamo alla radio viene **riletto dal decodificatore che
legge quelli che la radio manda a noi**: se le due metà non si parlano, il
difetto è nostro e non del firmware. È l'unica parte che si può stabilire senza
avere l'apparato, ed è verificata (`tst_flex_protocol`).

Quello che resta da provare su una radio vera è il comando che apre il flusso —
anche qui le fonti ne descrivono due forme, `stream create type=dax_tx` e
`dax tx 1`, e valgono le stesse regole della ricezione: si prova la prima, e se
viene rifiutata la seconda, una volta sola.

## Quello che manca ancora

- **Provare tutto su una radio vera.** Ricezione e trasmissione: è la sola cosa
  che manca, e nessun test la sostituisce.
- **Il DAX Audio** — i canali audio demodulati per fetta — non è affrontato:
  qui si prende l'IQ e si demodula da questa parte, con tutta la catena della
  SPEC-003.
- **Le slice**, che sono il modo in cui un Flex governa frequenza e modo di
  trasmissione. Finché non ci sono, la trasmissione segue la radio.

## Nel frattempo, un Flex si usa già

Non da questo backend, ma da `audiorig`: SmartSDR espone i canali DAX come
dispositivi audio del sistema, quindi si sceglie il DAX RX come ingresso e si
prende il CAT dalla porta seriale di SmartSDR o da un `rigctld`. Si perde la
banda larga — si ascolta la passata della fetta, non lo spettro completo — ma
frequenza, modo e tutto lo studio audio funzionano.

## Diagnosi

`nativeCommand("flex.status")` restituisce il passo raggiunto, i pacchetti e i
campioni arrivati, i buchi nel contatore VITA, la porta UDP e se è stata usata
la forma alternativa del comando. Dal lato trasmissione: se il flusso si è
aperto, quanti pacchetti sono partiti, se si è in aria, e la frequenza TX che il
client ha chiesto — quella che non viene mandata alla radio.

`nativeCommand("flex.send", {command})` manda una riga a mano sul canale di
comando: è così che si chiude la parte di sequenza che non abbiamo potuto
verificare, senza ricompilare niente.

## Dove guardare nel codice

```
src/hal/backends/flex/
    FlexProtocol.h/.cpp   le righe, pure e statiche
    FlexClient.h/.cpp     il canale di comando su TCP 4992
    FlexVita.h/.cpp       VITA-49 nei due versi: si legge e si costruisce
    FlexBackend.h/.cpp    la sequenza, il flusso, il PTT e il suo guinzaglio
src/hal/RadioScout.h      il rilevamento, condiviso con le altre famiglie
tests/hal/tst_flex_protocol.cpp
```
