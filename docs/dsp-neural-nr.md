# Lo stadio di riduzione neurale

Riferimento: DSDR-IMPL-001. Questa pagina descrive **quello che c'è**: la fase
E1, cioè il telaio con il motore RNNoise. DeepFilterNet3 è la fase E2 e non è
ancora qui.

## Perché ha un thread suo

È l'unico stadio della catena con un thread proprio, e il motivo è uno solo:
l'inferenza non deve mai poter bloccare il DSP. Il costo di una rete varia da
fotogramma a fotogramma — dipende da cosa sta ascoltando — e un ritardo dentro
il thread del DSP non si sentirebbe su un canale, si sentirebbe su tutti
insieme.

L'audio entra da un ring e esce da un altro, e **passa sempre di qui**, anche a
stadio spento. Da spento copia, e una copia su audio a 48 kHz non si misura:
vale il prezzo di non dover cambiare ring sotto chi sta suonando, che è il
genere di cosa che funziona mille volte e la millesima consegna audio da un
anello smontato.

## La macchina a stati

```
Bypass  ──accensione──▶  Warmup  ──dissolvenza finita──▶  Engaged
   ▲                        ▲                                │
   │                        │                                │
   └────spegnimento─────────┴──────30 s di isteresi──── Degraded
```

| Stato | Che cosa esce |
|---|---|
| `Bypass` | l'asciutto, bit per bit |
| `Warmup` | dissolvenza di 20 ms dall'asciutto al bagnato |
| `Engaged` | il bagnato |
| `Degraded` | l'asciutto, e lo si dice |

**La dissolvenza non è un vezzo.** Uno scatto si sente, e per giunta si sente
proprio nel momento in cui si sta giudicando se lo stadio serva.

## Il degrado onesto

È la parte che conta. Una rete che non ce la fa non produce un errore: produce
buchi, e chi ascolta dà la colpa alla propagazione.

L'occupazione del ring d'ingresso viene sorvegliata **all'arrivo di ogni giro**,
non alla fine. Alla fine il ring è vuoto per costruzione — lo stadio l'ha appena
svuotato — ed è il motivo per cui la prima stesura non si accorgeva mai di
essere in ritardo. Per lo stesso motivo c'è un tetto di fotogrammi per giro:
senza, un giro solo svuoterebbe tutto qualunque tempo ci voglia, e l'arretrato
non comparirebbe mai.

Mezzo secondo oltre il 60 % di occupazione e lo stadio torna all'asciutto,
emette un segnale e conta l'episodio. Il riaggancio ha **trenta secondi di
isteresi**: uno stadio che si accende e si spegne ogni due secondi è peggio di
uno spento.

Chi riaccende a mano azzera l'isteresi: sta dicendo di riprovare.

## La latenza è dichiarata, non scoperta

`latencyMs()` somma il ritardo algoritmico del motore e la profondità dei ring.
Chi accende lo stadio deve poterla leggere prima di sentirla — e chi la sente in
un pile-up deve poter capire da dove viene.

## I motori

L'interfaccia è `INrEngine`, e il contratto duro è `processFrame`: dopo
`prepare` non alloca, non prende lock, non tocca il disco. Non è un consiglio, è
la condizione perché quel codice possa girare su un thread con una scadenza —
e c'è un test che **conta le allocazioni** sostituendo `operator new`, perché
alla domanda «questo codice alloca?» non si risponde leggendo.

### DeepFilterNet3

La C-API ufficiale, caricata a **runtime** con `QLibrary` — non linkata, e non
nel repository. È una deviazione dalla specifica, che al §2 chiedeva binari
prebuilt versionati, e ha due motivi verificati:

1. **Il progetto non pubblica una libreria C-API fra i suoi rilasci.** Pubblica
   un eseguibile e un plugin LADSPA; la C-API la costruisce in CI con
   `cargo-c`. «Vendorizzare i prebuilt» non era possibile perché i prebuilt non
   esistono.
2. **Qualunque binario di DeepFilterNet pesa fra i 25 e i 50 MB.** Cinque
   piattaforme sono duecento megabyte dentro la storia di git, per sempre.

Caricarla a runtime risolve entrambe le cose e ne risolve una terza: chi non ha
la libreria non deve installare niente — il motore dice che non c'è, e RNNoise
resta al suo posto. È lo stesso schema del ColibriNANO, già collaudato qui.

La C-API è piccola e stabile:

```
df_create(path, atten_lim, log_level)   crea lo stato dal modello .tar.gz
df_get_frame_length(state)              quanti campioni per fotogramma
df_process_frame(state, in, out)        elabora, e restituisce il SNR locale
df_set_atten_lim(state, db)             l'attenuazione, a caldo
df_free(state)
```

**La questione aperta №1 della specifica è risolta**: `df_set_atten_lim` esiste
a runtime, quindi il cursore d'intensità non richiede di ricreare il motore. La
dissolvenza serve comunque, ma per l'accensione e per il degrado.

`df_process_frame` restituisce il **rapporto segnale/rumore locale** stimato
dalla rete: è la misura che dice se lo stadio sta lavorando su un segnale o su
rumore, e si può mostrare invece di farla indovinare.

Per procurarsi la libreria, vedi `THIRD_PARTY_LICENSES`.

### RNNoise

Due dettagli che non si indovinano.

**La rete lavora in scala PCM a 16 bit, non in ±1.** Passarle il nostro audio
normalizzato le darebbe un segnale quarantamila volte più piccolo di quello su
cui è stata addestrata, e concluderebbe che è tutto rumore.

**L'attenuazione non è un suo parametro**: RNNoise toglie quello che decide lei.
Il cursore di intensità agisce quindi fuori, mescolando asciutto e bagnato — che
è anche il modo in cui si ottiene un passaggio morbido invece di un
interruttore. La miscela è in ampiezza e non in decibel: a metà cursore si vuole
metà effetto, e una scala logaritmica renderebbe il comando tutto-o-niente.

## L'interlock digitale

C'è una regola che non si può lasciare alla buona volontà: **l'audio uscito
dalla riduzione neurale non deve mai arrivare a un decodificatore.**

Il motivo è che una rete addestrata sulla voce fa il suo mestiere anche quando
il segnale non è voce: toglie quello che non le somiglia. Su un FT8 al limite
del rumore toglie il segnale. Il decodificatore non se ne accorge — decodifica
meno, e nessuno ha modo di sapere perché. Non c'è errore, non c'è avviso: c'è
una stazione che non si aggancia più.

Perciò l'audio porta un'etichetta. `EarOnly` è quello passato dalla rete e va
all'orecchio soltanto; `Clean` è quello lineare e può andare ovunque. Il
rifiuto avviene **quando si costruisce il collegamento**, non quando passano i
campioni: un controllo a runtime scatterebbe la millesima volta, in mezzo a un
contest, e nessuno saprebbe leggerlo — un rifiuto alla costruzione lo vede chi
scrive il collegamento, subito, con il motivo scritto.

```cpp
graph.connect(neuralOutput, AudioSink::DigitalDecoder, &why);  // false
```

E il rifiuto **spiega**: un «non si può» senza il perché è un vicolo cieco.

La registrazione audio prende il mix **prima** della rete — il tap è dentro il
`DspEngine` — quindi per costruzione registra il lineare, ed è il motivo per
cui il sidecar non deve dichiarare `nr_neural`. Il grafo lo mette per iscritto
invece di lasciarlo a un commento: `Session.audioRoutes` elenca i collegamenti
in forma leggibile, e risponde alla domanda «quello che sto registrando è
passato dalla rete?».

## Il pannello

`NeuralNrControl.qml`, dentro la catena RX. Tre cose e non una di più — se è
accesa, quanto forte, con quale modello — più il distintivo dello stato, che è
la parte a cui serve davvero un'occhiata: **`Degraded` vuol dire che la
macchina non ce la fa**, e senza vederlo si crede che la riduzione sia accesa
mentre non lo è.

Accanto, latenza e costo **misurati**: sono i due numeri che dicono se questo
stadio si può tenere acceso.

Le scelte sopravvivono alla chiusura. Rifarle a ogni avvio sarebbe un rito, e
ci si accorgerebbe di averle dimenticate ascoltando.

## I modelli

`ModelStore` guarda in `<dati applicativi>/models/` e **dice quale sia quella
cartella**: è il posto giusto per starci e quello sbagliato per trovarla, e chi
ha un file da mettere deve poter leggere il percorso. La cartella viene creata
se non c'è, per lo stesso motivo.

Un file troppo piccolo — un download interrotto — non diventa una voce
utilizzabile: dirlo adesso costa niente, scoprirlo scegliendolo costa una
sessione.

L'elenco vuoto è il caso normale: RNNoise ha i pesi dentro di sé.

## Che cosa manca

| Fase | Contenuto |
|---|---|
| **E2** | fatto il motore; restano il modello base nel pacchetto e la prova SI-SNR completa, che hanno bisogno della libreria costruita |
| **E4** | manifest remoto delle stagioni, e il download degli aggiornamenti |

Lo scambio con l'implementazione precedente è fatto: `core::NeuralNrWorker` non
esiste più, e l'applicazione usa questo stadio. `dsp::NeuralDenoiser` resta
soltanto perché lo usa `ChannelProcessor` per il proprio stadio per-canale — è
un'altra cosa, e non va confusa con questa.

**Un canale, un motore.** L'audio verso l'AudioRouter è stereo interlacciato, e
lo stadio ne tratta i canali separatamente: la rete non deve mai vedere due
canali correlati (IMPL-001 §9.3), e darle uno stereo binaurale le farebbe
scambiare per rumore la differenza fra i due orecchi — che è proprio il segnale.

La dissolvenza avanza **una volta per fotogramma, non una per canale**:
altrimenti con lo stereo durerebbe la metà, e la metà di venti millisecondi si
sente di nuovo come uno scatto.

Resta aperta la questione dell'ordine (IMPL-001 §9.3): la spec propone la
riduzione **prima** dello stadio binaurale, mentre qui sta dopo, sul mix. Farla
prima vuol dire spostarla dentro `ChannelProcessor`, che è un lavoro suo e non
una variante di questo.

## Dove guardare nel codice

```
src/dsp/neural/
    INrEngine.h            l'interfaccia dei motori
    RnnoiseEngine.h/.cpp   il motore che sta nel pacchetto
    NeuralNrStage.h/.cpp   il telaio: thread, ring, stati, degrado
tests/dsp/tst_neural_stage.cpp
```
