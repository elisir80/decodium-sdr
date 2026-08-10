# FlexRadio serie 6000 — a che punto siamo

**Non c'è ancora un backend.** C'è metà del protocollo, ed è la metà che si
può scrivere onestamente adesso.

Un FlexRadio 6000 parla due lingue insieme:

| | Trasporto | Cosa passa |
|---|---|---|
| **canale di comando** | TCP 4992, righe di testo | frequenze, modi, fette, stato della radio |
| **flusso dati** | UDP, pacchetti VITA-49 | IQ, audio, panadapter, waterfall |

Il primo è implementato e provato. Il secondo no, e la ragione è la stessa per
cui non ho scritto una sonda per il SunSDR: **non conosco con certezza i codici
di classe VITA-49 di Flex né il formato esatto del carico DAX IQ**, e scriverli
a memoria produrrebbe un decodificatore che gira, non fallisce, e consegna
campioni sbagliati. Un difetto così non si vede: si vede una banda che sembra
rumore, e si dà la colpa all'antenna.

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

## Quello che manca, e come si completa

1. I codici di classe VITA-49 usati da Flex, e il formato del carico per DAX IQ.
2. La sequenza di comandi che apre un flusso IQ: registrazione della porta UDP
   del client, creazione della fetta, creazione dello stream.
3. La decodifica dei pacchetti — che, come per l'Hermes-Lite, è aritmetica di
   offset e va scritta con i suoi test prima di vedere una radio.

I punti 1 e 2 vanno confermati sulla documentazione dell'API di SmartSDR o su
una radio vera. Il punto 3 è lavoro meccanico una volta noti i primi due.

Fino ad allora `DSDR_BACKEND_FLEX` resta spento e nell'elenco delle sorgenti non
compare niente: un backend che apre e non consegna campioni sarebbe la peggiore
delle promesse (CONSTITUTION §7).

## Dove guardare nel codice

```
src/hal/backends/flex/
    FlexProtocol.h/.cpp   le righe, pure e statiche
    FlexClient.h/.cpp     il canale di comando su TCP 4992
src/hal/RadioScout.h      il rilevamento, condiviso con le altre famiglie
tests/hal/tst_flex_protocol.cpp
```
