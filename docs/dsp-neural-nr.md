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

## Che cosa manca

| Fase | Contenuto |
|---|---|
| **E2** | `DfnEngine` sulla C-API `libdf`, binari prebuilt vendored, modello base nel pacchetto, prova di efficacia (SI-SNR) |
| **E3** | `ModelStore`, pannello QML, persistenza, interlock digitale a costruzione del grafo |
| **E4** | manifest remoto delle stagioni |

E c'è un debito da saldare: nel repository **convivono due implementazioni**
della riduzione neurale. Quella vecchia — `NeuralDenoiser` più
`core::NeuralNrWorker` — è quella che l'applicazione usa adesso; questa è il
telaio nuovo, provato ma non ancora innestato. Lo scambio è il primo passo di
E3, e va fatto in un commit suo: sostituire uno stadio sotto l'audio che suona
non è una cosa da mescolare ad altro.

## Dove guardare nel codice

```
src/dsp/neural/
    INrEngine.h            l'interfaccia dei motori
    RnnoiseEngine.h/.cpp   il motore che sta nel pacchetto
    NeuralNrStage.h/.cpp   il telaio: thread, ring, stati, degrado
tests/dsp/tst_neural_stage.cpp
```
