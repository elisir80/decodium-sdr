# DSDR-SPEC-005 — La catena di studio

> Stato: **in corso**. §3 è implementato; il resto è disegnato e non ancora
> costruito. Le parti non costruite non compaiono nell'interfaccia: un blocco
> che non fa niente è peggio di un blocco che manca (CONSTITUTION §7).

## 1. La tesi

**Il diagramma di flusso è l'interfaccia.**

Il concorrente di riferimento — Thetis — ha tutto, e per usarlo serve un
manuale di centoventinove pagine: gate, leveller, EQ, CFC e limiter esistono,
ma vivono in schede e sotto-menù che non dicono in che ordine agiscono né su
che cosa. Chi regola un compressore in una scheda e un equalizzatore in
un'altra non ha modo di sapere quale dei due viene prima, e l'ordine è tutto.

La catena si disegna. Ogni stadio è un blocco, i blocchi stanno in fila nel
verso in cui il segnale li attraversa, e fra un blocco e l'altro c'è un
indicatore di quanto passa. Chi guarda capisce **dove sta intervenendo** prima
ancora di aver toccato qualcosa.

```
  TX   MIC → Gate → Leveller → EQ → CFC → Limiter → Modulatore → RADIO
  RX   RF → NB → Filtro → NR → Notch → EQ → Decodium NR → CUFFIE
```

Non è una metafora grafica: è il layout del manuale di Thetis trasformato in
interfaccia.

## 2. Le regole che non si negoziano

1. **Un blocco che non fa niente non c'è.** Il diagramma mostra la catena
   *reale*, non quella progettata. Un blocco disegnato e disattivato è una
   promessa, e le promesse nell'interfaccia si pagano al primo operatore che
   ci clicca sopra.
2. **Il bypass sta sul blocco.** Non in un menù: sull'oggetto che si sta
   guardando, così che spegnere e riaccendere sia il gesto con cui si giudica
   se quello stadio serve.
3. **La misura sta fra i blocchi.** Un indicatore per collegamento, dove una
   misura vera esiste. Dove non esiste, il collegamento resta una linea: un
   indicatore fermo è peggio di nessun indicatore, perché lo si crede.
4. **L'interlock dei decoder.** La catena di studio è per le orecchie e per la
   voce. Il percorso che alimenta i decoder — FT8, RTTY, i moduli IQ — non
   passa di qui e resta lineare. Un compressore prima di un decodificatore non
   migliora niente e rovina la stima del rapporto segnale-rumore.

## 3. Quello che c'è (implementato)

Il diagramma disegna gli stadi che il DSP ha davvero, con il loro interruttore
e le misure che sappiamo fare.

| Catena | Blocco | Comando | Misura |
|---|---|---|---|
| RX | Ingresso RF | — | picco dBFS, spia di saturazione |
| RX | Noise blanker | acceso/spento, soglia | attività |
| RX | Filtro di canale | larghezza | — |
| RX | Riduzione di rumore | acceso/spento, intensità | — |
| RX | Notch automatico e manuali | acceso/spento, conteggio | — |
| RX | DECODIUM NR (neurale) | acceso/spento, intensità | carico, ritardo |
| RX | AGC | modo, soglia | guadagno |
| RX | EQ d'ascolto | 5 campane a curva | la curva stessa |
| RX | Uscita audio | volume, muto | picco, RMS, cresta |
| TX | Microfono | dispositivo, guadagno | livello |
| TX | Compressore | quantità | compressione applicata |
| TX | Filtro di trasmissione | larghezza | — |
| TX | Modulatore | modo | — |
| TX | Gate | soglia | apertura |
| TX | Leveller | bersaglio | guadagno applicato |
| TX | CFC multibanda | punch (0–10) | riduzione per banda |
| TX | Limiter | tetto | riduzione, ritardo |
| TX | Drive | livello | livello d'uscita |

## 4. Quello che manca (disegnato)

### 4.1 Gli stadi nuovi della catena TX
> Resta l'**EQ parametrico sulla trasmissione**: quello d'ascolto è fatto, e il
> componente a curve si riusa. Manca il posto in cui metterlo — fra il
> compressore e il multibanda — e i suoi cinque parametri sul percorso TX.


- ~~**Gate**~~: fatto. Tre tempi — attacco immediato per non mangiare la prima
  consonante, tenuta di 120 ms per non chiudersi fra due sillabe, rilascio
  lento per non tagliare la coda delle parole.
- ~~**Leveller**~~: fatto. AGC lento prima della compressione, con il tetto al
  guadagno: senza, nelle pause alzerebbe il rumore fino al livello della voce.
- ~~**EQ parametrico**~~: fatto sul percorso di ricezione — cinque campane
  RBJ sull'ascolto, con la curva trascinabile sopra lo spettro vivo. Sulla
  catena di trasmissione resta da fare: là serve prima il resto della catena,
  perché un equalizzatore prima di un gate e di un leveller equalizza il
  respiro della stanza insieme alla voce.
- ~~**CFC — compressore multibanda**~~: fatto. Quattro bande — 50–250 il corpo,
  250–700 il calore, 700–1800 la parola, 1800–4000 la presenza — separate con
  Linkwitz-Riley del quarto ordine e ricostruite esatte, così che a riposo non
  colori niente. Si comanda con un numero solo: sedici manopole sono il motivo
  per cui un multibanda resta spento nella maggior parte delle stazioni che ce
  l'hanno.
- ~~**Limiter**~~: fatto, con due millisecondi di anticipo — dichiarati, perché
  chi somma le latenze della catena deve poterli leggere.

### 4.2 Le curve

EQ e CFC si regolano **trascinando punti su una curva**, non con file di
cursori numerati (la lezione di Thetis), e la curva è disegnata **sopra lo
spettro vivo del segnale** (la lezione di SDR Console). Si trascina il punto e
si vede la voce cambiare forma sotto la curva: il legame fra il gesto e
l'effetto smette di passare dalla memoria.

### 4.3 Il contesto di misura

Regolare un trasmettitore ascoltandosi non si può: serve un secondo
ricevitore. Il pannello lo sostituisce.

- ~~**Spettro prima/dopo**~~: **fatto**, e **sovrapposti**, non affiancati.
  Due grafici accanto costringono a spostare lo sguardo, e fra uno sguardo e
  l'altro si mette in mezzo la memoria — che di un'immagine dura poco più che
  di un suono. Sovrapposti, la differenza fra le due catene è l'area fra le
  curve: si legge in un colpo, e non c'è niente da ricordare. Il «prima» sotto
  e spento, perché è il riferimento e non il risultato.

  L'orecchio dice se una voce è bella; questo dice *perché*.

- ~~**Monitor in cuffia**~~: **fatto**, un tasto. Suona **solo mentre si
  trasmette**, e non c'è modo di lasciarlo acceso per sbaglio: in mezzo duplex
  la ricezione tace comunque, quindi non si scontra con niente, e ad alzare il
  PTT smette da sé. Se chi ascolta resta indietro si butta il vecchio e non il
  nuovo — sentirsi con mezzo secondo di ritardo fa inciampare chi parla, ed è
  peggio di non sentirsi affatto.
- **Generatore di toni** — uno per accordare, due per l'intermodulazione (c'è
  già nel motore TX, va portato nel diagramma).
- ~~**Registra e riascolta**~~: **fatto**. Dieci secondi, due tracce — la voce
  com'è e quella che parte verso la radio — riascoltabili subito.

  Tre decisioni che valgono più del componente.

  **Non c'è niente da armare.** Registra da sé mentre si trasmette, sempre. Un
  registratore che va acceso prima arriva sempre tardi: ci si accorge di voler
  riascoltare solo *dopo* aver parlato.

  **Si commuta prima/dopo mentre suona, senza perdere il punto.** Si passa da
  una traccia all'altra a metà parola e si sente la stessa sillaba nei due
  modi, di seguito. Fermarsi e ripartire costringerebbe a ricordare com'era, e
  il ricordo di un suono dura meno di un secondo — è il motivo per cui i
  confronti A/B fatti a memoria non decidono niente.

  **Il riascolto prende il posto della ricezione, non ci si somma.** Un
  riascolto mescolato al fruscio dei quaranta metri non si giudica. La
  ricezione continua a scorrere sotto e si butta, così alla fine non c'è un
  ring pieno di passato da smaltire prima di tornare in banda.

  Tornare a premere il PTT ferma il riascolto: scrivere sotto la testina di
  lettura non darebbe un errore, darebbe un riascolto cucito con due prese
  diverse — il modo peggiore di essere rotti, perché sembra funzionare.

### 4.4 I profili per modo

`SSB chiacchierata`, `SSB DX/contest`, `DATI piatto`. Commutati **con il
modo**, automaticamente. Thetis ce li ha e li tiene sepolti; qui il profilo è
la prima cosa che si sceglie, e cambia da sé quando cambia il modo — perché è
quello che l'operatore intende quando passa da una chiacchierata a un pile-up.

### 4.5 Fase 2: il blocco «Plugin»

Un host LV2 (e VST3, se si può) come blocco della catena: l'utente carica il
compressore che preferisce. Renderebbe DECODIUM SDR l'unico client che
**ospita** la catena eSSB invece di costringerla in una matassa di cavi audio
virtuali.

- Su Linux LV2 è nativo e la sua licenza è ISC: compatibile.
- **Da verificare prima di scrivere una riga**: il VST3 SDK è a doppia licenza
  (GPLv3 o proprietaria). La via GPLv3 sembra percorribile per un programma già
  GPL-3.0-or-later, ma va confermata e registrata in `THIRD_PARTY_LICENSES`
  come qualunque altra dipendenza (CLAUDE.md, «cosa NON fare senza chiedere»).
- Un plugin che va in crash non deve portarsi dietro la radio: l'host va
  isolato, o almeno protetto, prima ancora di essere comodo.

## 5. Dove sta, nell'interfaccia

Un pannello della colonna, `FLUSSO`, **staccabile**: la catena disegnata vuole
larghezza, e in una striscia di trecento punti i blocchi diventano illeggibili.
Staccato è lo schermo del secondo monitor su cui si regola la voce.

Cliccando un blocco si aprono i suoi comandi sotto il diagramma. Uno per volta:
la catena resta visibile mentre si regola, che è tutto il punto.
