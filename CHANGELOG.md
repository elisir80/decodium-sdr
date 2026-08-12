# Changelog

Il formato segue [Keep a Changelog](https://keepachangelog.com/it/1.1.0/).

## [Non rilasciato]

## [1.1.13] — 2026-08-12

### Aggiunto

- **Backend FlexRadio serie 6000.** Mette insieme le due metà che c'erano già —
  il canale di comando su TCP 4992 e il decodificatore VITA-49 — e in mezzo ci
  mette la sequenza che apre il flusso DAX IQ. Quattro velocità: 24, 48, 96 e
  192 kS/s. L'IQ arriva da questa parte e si demodula qui, con tutta la catena
  di ricezione.

  **Non è mai stato provato su una radio vera**, e lo dice. Il legame fra
  fetta, panadapter e canale DAX cambia fra le versioni maggiori del firmware:
  se la sequenza non passa, il backend nomina il comando su cui si è fermato e
  il codice con cui la radio ha risposto; se passa e dopo tre secondi non
  arriva un pacchetto, lo dice e nomina le due cause possibili — il firewall di
  questa macchina o un firmware che vuole una sequenza diversa — perché si
  risolvono in modi opposti. Le due forme documentate del comando di creazione
  si provano in sequenza invece di sceglierne una a memoria.

  Chi ha un Flex davanti chiude la questione senza ricompilare:
  `flex.status` dice a che passo si è arrivati e quanti pacchetti sono giunti,
  `flex.send` manda una riga a mano sul canale di comando.

  La trasmissione non c'è e non si finge: passa da DAX MIC, che è un'altra metà
  di protocollo, e le capability dichiarano che non si trasmette — così la UI
  non mostra un PTT che non farebbe niente.

### Corretto

- **Un `-Wreorder` nel percorso audio**: la build di casa e quella di CI
  avevano due severità diverse, e la differenza si è vista solo dopo aver
  taggato una versione. Da qui in poi si prova con i warning come errori prima
  di taggare.

## [1.1.12] — 2026-08-12

### Aggiunto

- **La catena di studio, disegnata come si attraversa** (DSDR-SPEC-005). Un
  pannello nuovo, `FLUSSO`, staccabile: i blocchi stanno in fila nel verso in
  cui il segnale li percorre, con l'interruttore addosso e la misura in mezzo —
  perché è fra un blocco e l'altro che una misura significa qualcosa. Due
  corsie, trasmissione e ricezione, ognuna con i suoi capi. Ogni blocco porta
  il disegno di quello che fa alla forma d'onda: «Gate» e «Limiter» sono due
  parole che a chi non le ha già imparate non dicono niente, mentre una soglia
  con un gradino e una cima tosata si capiscono prima di essere lette.
- **Equalizzatore parametrico d'ascolto**: cinque campane, e la curva si regola
  trascinando i punti **sopra lo spettro vivo** nello studio audio. Si muove il
  punto e si vede la voce cambiare forma sotto, nello stesso istante e nello
  stesso riquadro. La rotellina stringe e allarga la campana.
- **Compressore multibanda della voce (CFC)**: quattro bande — corpo, calore,
  parola, presenza — separate con Linkwitz-Riley del quarto ordine e
  ricostruite esatte, così che a riposo non colori niente. Un comando solo,
  `punch`, muove le quattro soglie: sedici manopole sono il motivo per cui un
  multibanda resta spento nella maggior parte delle stazioni che ce l'hanno.
- **Gate, leveller e limiter** completano la catena di trasmissione. Il gate
  toglie la stanza fra una frase e l'altra; il leveller corregge la distanza
  dal microfono, e sta prima del compressore perché insegue i secondi mentre
  quello insegue i millisecondi; il limiter guarda avanti di due millisecondi e
  tiene il tetto, perché oltre il fondo scala il modulatore tosa — e tosare in
  banda base vuol dire allargarsi sulle frequenze dei vicini.
- **Studio audio** come modulo a sé: spettro e waterfall dell'audio che si
  ascolta, oscilloscopio con base dei tempi e aggancio, livelli con picco, RMS
  e fattore di cresta, tono dominante e distorsione armonica, e i bordi del
  filtro disegnati sullo spettro — il termine di paragone che mancava a «il
  filtro taglia dove credo?».
- **I pannelli si staccano** in finestre proprie, e una fila di icone in testa
  alla colonna li accende e li spegne. Spento vuol dire che non viene proprio
  costruito.
- **La qualità del collegamento** in fondo alla finestra, dove la sorgente sta
  dall'altra parte di una rete.

### Nota

Gli stadi disegnati e non ancora costruiti — l'EQ sulla trasmissione, l'host
LV2/VST3, i profili per modo, il registra-e-riascolta — non compaiono
nell'interfaccia: un blocco che non fa niente è peggio di un blocco che manca.
Sono in `docs/DSDR-SPEC-005-Catena-di-studio.md`.

## [1.1.11] — 2026-08-12

### Aggiunto

- **La salute del collegamento in fondo alla finestra**, dove la sorgente sta
  dall'altra parte di una rete. È il rapporto fra i campioni arrivati
  nell'ultimo secondo e quelli che la frequenza di campionamento prometteva:
  risponde alla domanda che prima non aveva risposta — la banda è vuota, o i
  campioni non stanno arrivando? Sullo schermo le due cose si somigliano,
  traccia piatta e waterfall scuro, e portano a cercare il guasto in due posti
  opposti. Non compare su una radio attaccata al bus: lì quel rapporto sta
  incollato a uno, e un indicatore che dice sempre la stessa cosa smette di
  essere letto.

### Cambiato

- **La targa del ricevitore è su due righe** e larga quasi la metà. Sopra
  quello che si legge — quale ricevitore, dove sta, quanto arriva — sotto
  quello che si tocca. Una targa larga quanto la finestra nasconde proprio ciò
  per cui sta lì, e sul waterfall lo spazio verticale è quello che costa meno:
  una riga di storia in più non è un segnale in meno.
- **I filtri di disturbo stanno in un blocco solo**, con la sua cornice. Erano
  cinque interruttori in fila con gli altri comandi, e si leggevano come cinque
  comandi scollegati: quando il rumore peggiora si cerca *quella* zona della
  targa, non un interruttore per volta.
- **Banda e modo sono vicini.** Si scelgono insieme: si cambia banda e la prima
  cosa dopo è il modo, perché sotto i dieci megahertz si sta in LSB e sopra in
  USB. Stavano ai due capi della targa.
- **Le memorie escono dalla targa** e restano nel pannello di sintonia, con lo
  stesso archivio: erano un menu che si apriva di rado e occupava una larghezza
  fissa su una riga che sta sopra il segnale.

### Corretto

- **Niente più finestra nera all'avvio** su Windows. L'eseguibile era di
  sottosistema «console» e Windows gli apriva accanto un terminale che restava
  lì per tutta la sessione. Il log non si perde: chi lancia da un terminale
  ritrova tutto, e senza più dover impostare a mano una variabile d'ambiente.

## [1.1.10] — 2026-08-12

### Corretto

- **Il CAT di rete non è solo `rigctld`.** Il driver della 1.1.9 pretendeva il
  protocollo esteso e un `dump_caps` riuscito: su un server rigctl **minimo** —
  quello che espone DECODIUM 4, e con lui mezzo ecosistema — ogni domanda
  scadeva e la radio non compariva. Ora il dialetto si stabilisce all'apertura
  con una domanda di sola lettura, e la prova che dall'altra parte ci sia una
  radio è la frequenza, che è l'unica cosa che ogni rigctl implementa.
  Provato sul FT-991A vero, con la porta seriale in mano a DECODIUM 4 e il CAT
  preso dal suo rigctl sulla 4533.
- **Una porta seriale occupata da un altro programma lo dice.** Prima la si
  saltava in silenzio e in fondo restava «zero device», che è indistinguibile
  da una radio spenta, da un cavo staccato o da un CAT configurato male —
  quattro rimedi diversi e nessun indizio su quale. L'avviso arriva
  all'operatore, e parte solo se la ricerca è tornata davvero a mani vuote.
- **Non ci si connette più al proprio server rigctl.** Il nostro sta sulla
  4532, che è anche la porta di fabbrica di `rigctld`: la ricerca ci trovava e
  nel log compariva un client che non esiste. Il core dichiara la porta che si
  è preso e il backend la salta.
- **La riga «Backend attivi»** che CMake stampa a fine configurazione elencava
  una lista scritta a mano: `audiorig`, `hermes` e il sondaggio Flex ne erano
  fuori da sempre. È esattamente la riga che si guarda per sapere se un backend
  c'è.

## [1.1.9] — 2026-08-12

### Aggiunto

- **I filtri di disturbo sulla targa del ricevitore**: NR, NB, ANF e il
  contatore dei notch manuali, che li toglie tutti insieme. Si accendono
  guardando lo spettro — si alza il rumore, si preme, si sente se è servito —
  e finora stavano solo in fondo alla colonna, cioè lontano dal punto in cui si
  guarda mentre si ascolta.
- **Banda e memorie sulla targa.** Il bandstack è quello del pannello di
  sintonia: la banda ricorda dove la si era lasciata comunque la si scelga.
  Le memorie salvate dalla targa portano anche modo e filtro — una memoria di
  sola frequenza richiamata su una banda in SSB riporta il numero giusto e il
  suono sbagliato.
- **Appiattimento del fondo** sul waterfall. Il rumore non è piatto lungo la
  banda: un disturbo locale o la risposta del preselettore lo alzano su una
  porzione di spettro, e lì il colore si accende ovunque mentre altrove resta
  tutto nero. Si stima il fondo bin per bin e se ne toglie lo scarto dalla
  media, quanto si vuole. Vale solo per il waterfall: la traccia resta la
  misura.
- **Due palette nuove.** *Viridis* ha la luminanza monotona, che è quello che
  serve per decidere se sotto il rumore c'è una traccia o no; *Iride* ha più
  stop distinti di tutte le altre, per separare livelli vicini su una banda
  affollata.
- **Un mirino che legge lo spettro** sotto il puntatore: frequenza, livello e
  scarto dal ricevitore scelto. Con **Shift** si trascina un righello e si
  legge quanto è larga una emissione; con il **tasto centrale** si mette o si
  toglie un marcatore, che resta disegnato sullo spettro fra un avvio e
  l'altro.
- **Il 3D si legge davvero.** Un reticolo prospettico con le righe larghe un
  pixel a qualunque profondità — senza, si vede *che* c'è un segnale, non
  *dove* — e tre inquadrature pronte: dall'alto, in prospettiva, di taglio.
  Inclinazione, rotazione e rilievo sono tre numeri che si azzeccano solo
  insieme.
- **Zoom dell'asse dei tempi e fermo immagine.** A piena storia una sillaba è
  alta due pixel; il fermo ferma lo schermo e non la radio — le righe
  continuano a essere consumate, altrimenti il ring si riempie e il DSP
  comincia a scartare.
- **Radio via `rigctld`**, il demone di rete di hamlib: qualunque modello che
  hamlib conosca diventa comandabile, senza linkare hamlib e senza copiarne una
  riga. Si cerca da solo su `127.0.0.1:4532` e `:4533`; per un demone su
  un'altra macchina si passa `DSDR_RIGCTLD=host:porta`. Il livello arriva già
  in decibel rispetto a S9 e resta tale, invece di passare per la scala grezza
  che si ferma a S9+13.

### Corretto

- **La riduzione di rumore neurale era sparita dai pacchetti** dalla 1.1.3:
  RNNoise non veniva compilato in CI e il filtro non c'era, senza che nulla lo
  dicesse a chi installava.
- **Le radio Icom non rispondevano a nessun comando.** La ricerca le trovava
  con il driver CI-V, ma l'apertura costruiva sempre quello Yaesu: comparivano
  nell'elenco e poi restavano mute.

## [1.1.8] — 2026-08-11

### Aggiunto

- **Una targa per ogni ricevitore aperto** sopra lo spettro, e si spostano:
  si prendono per la maniglia a sinistra e si mettono dove non coprono il
  segnale che si sta guardando. Quella scelta sta sopra le altre.
- **Modo, filtro e AGC si cambiano dalla targa.** Erano etichette da leggere:
  per cambiare modo bisognava attraversare la finestra e ritrovare la scheda
  giusta fra quelle aperte. Le larghezze proposte seguono il modo — 500 Hz in
  CW, 2,4 kHz in SSB — e cambiando modo il filtro si adegua.
- **Ogni scheda della colonna ha la sua croce** per chiudere il ricevitore.
  Prima si poteva solo dal flag sullo spettro, che è raggiungibile soltanto se
  è in vista: con lo zoom stretto altrove il canale restava aperto e
  irraggiungibile. L'ultimo non si chiude, e il tasto sparisce invece di
  rifiutare il clic.
- **Scegliere un ricevitore dalla colonna lo porta in vista** sul waterfall,
  ma solo se era fuori campo: spostare la vista quando non serve fa perdere il
  segno a chi sta seguendo un segnale.
- **Installatore Inno Setup**, lo stesso strumento con cui si confeziona
  DECODIUM: chi installa i due programmi incontra la stessa procedura.

### Corretto

- **Le radio Yaesu con «CAT RTS» abilitato andavano in timeout.** Il driver
  apre la porta con RTS basso — su molte interfacce quella linea è il PTT — ma
  sulle Yaesu con porta USB integrata «CAT RTS» ne fa il controllo di flusso:
  la radio non risponde finché non lo vede alto, e sul FTDX3000 quel menù è
  abilitato di fabbrica. Ora, sulla porta scelta dall'operatore, si riprova con
  l'handshake hardware quando la radio tace. Mai durante la scansione
  automatica: lì si aprono porte di cui non si sa nulla.
- **Il pan dello spettro non sintonizza più.** `onClicked` scatta anche dopo un
  trascinamento: si spostava la vista per andare a vedere un segnale e il
  ricevitore si piazzava dove capitava di lasciare il dito.
- **I clic sulla targa non attraversano più verso lo spettro**: si premeva un
  pezzo di targa per leggere meglio un numero e la radio cambiava frequenza.

## [1.1.7] — 2026-08-11

### Corretto

- **L'S-meter è tarato, e la taratura sta ferma.** Nella 1.1.6 la scala
  inseguiva il fondo di rumore, che il DSP stima con un inseguitore di minimo
  dentro la banda del canale: si muove con la banda, con il filtro e con
  quanto è occupato il canale. La scala si spostava sotto i piedi e lo stesso
  segnale, in due momenti diversi, dava due rapporti diversi — che è la sola
  cosa che un S-meter non può fare, perché il rapporto serve a confrontare.
  Ora il riferimento è una taratura: si prende una volta e resta.
- **Il tasto `TARA`** porta il rumore a S1, qui e adesso. Si preme su un canale
  vuoto, dove il fondo è davvero il fondo. La prima taratura la fa il
  programma, sei secondi dopo la connessione — il tempo perché la stima del
  fondo sia scesa dove deve — e da lì in poi non si muove più da sola.
- La taratura **sopravvive al riavvio**: vale per il ricevitore, non per il
  canale, e cambiando canale la scala non si sposta.

## [1.1.6] — 2026-08-11

### Corretto

- **L'S-meter diceva S9 su tutto.** La scala era ancorata al tetto della
  dinamica — S9 sessanta decibel sotto il fondo scala — e su un ricevitore con
  guadagno alto questo manda ogni segnale oltre S9, con l'ago appoggiato al
  fermo. Il riferimento viene adesso dal fondo di rumore misurato: un segnale
  che si distingue appena dal rumore vale S1, e da lì si contano i sei decibel
  per punto. Non è una taratura assoluta — su dBFS non esiste — ma è una scala
  che si muove insieme al ricevitore invece che contro.
- **La scala in decibel non stampa più livelli impossibili.** Quando il rumore
  è alto la scala S non ci sta tutta nella dinamica che resta, ed è un fatto
  del ricevitore: le tacche che cadrebbero sopra lo zero del convertitore
  restano senza numero.
- **Il ColibriNANO torna nei pacchetti Windows e Linux.** La libreria del
  costruttore non c'era, e senza di lei il backend viene registrato ma non
  trova device: l'elenco delle sorgenti resta vuoto senza dire perché. È
  rilasciata sotto Unlicense — ridistribuibile anche in forma binaria — e ora
  il workflow di release la scarica verificandone l'impronta prima di metterla
  nel pacchetto. Su macOS il costruttore non pubblica una versione.
  Le release dalla 1.1.3 alla 1.1.5 ne sono prive: chi le ha installate può
  copiare `colibrinano_lib.dll` accanto all'eseguibile.

## [1.1.4] — 2026-08-11

**La 1.1.3 non vede le radio via CAT: usare questa.** Nei pacchetti Windows e
Linux della 1.1.3 mancava il backend `audiorig` — quello che parla con un
FT-991A, un IC-7300 e con ogni radio tradizionale collegata via audio e porta
seriale. Il pacchetto macOS non era interessato.

### Corretto

- **Il backend CAT torna nei pacchetti.** Nessuno dei due workflow installava
  `qt6-serialport`: senza quel modulo il backend si spegne da sé, e fa bene —
  chi non ha una radio tradizionale non deve installare mezzo Qt per compilare
  — ma lo diceva con una riga di log in mezzo a trecento. Nel pacchetto che
  arriva all'operatore quella riga non c'è più: c'è solo un elenco di device in
  cui la sua radio non compare.
- **Un backend non può più sparire in silenzio da un pacchetto.**
  `DSDR_STRICT_BACKENDS` trasforma un backend spento per dipendenze mancanti in
  un errore di configurazione, e la release lo accende. Chi compila per sé
  resta libero di farne a meno; chi costruisce per altri no.

## [1.1.3] — 2026-08-11

Due strumenti nella colonna e un pacchetto Windows che si regge in piedi.

Il pannello degli strumenti mostra il segnale in due letture — l'ago, che ha
inerzia, e le barre, che tengono il picco — o la potenza, e si sceglie con tre
tasti. Sotto, sempre gli stessi numeri: punti S, decibel, rapporto
segnale/rumore e fondo di rumore, che il DSP misurava già per ogni canale
senza che nessuno lo mostrasse.

Su Windows, il programma installato chiedeva librerie che non aveva: il
pacchetto raccoglieva le dipendenze del solo eseguibile, e quelle dei plugin
restavano fuori. Ora entrano, il pacchetto è più piccolo di ventitré megabyte,
e prima di ogni release si verifica che ogni DLL richiesta ci sia davvero.

### Aggiunto

- **DECØMETER-S**, lo strumento del segnale, in due letture che si scelgono:
  l'ago analogico, che ha inerzia e mostra il QSB battere, e le barre, che
  tengono il picco e dicono dove si è arrivati. Stesso quadrante e stessi
  numeri: due scale — punti S fuori, decibel dentro — il cuneo che separa il
  fondo di rumore dal segnale, il cursore del fondo, e tre letture del valore
  (picco, media, valore efficace). Segue il canale corrente e si sposta nella
  colonna come gli altri pannelli.
- **DECØMETER**, secondo strumento dello stesso pannello: potenza diretta,
  riflessa e rapporto di onde stazionarie su tre archi a segmenti, con quattro
  portate — 5, 50, 500 watt e 5 kW — che si scelgono a mano o da sole. La
  risposta è quella di un wattmetro a termocoppia: attacco immediato, rilascio
  lento, picco che tiene tre secondi, perché la potenza in SSB è tutta picchi.
- **Si sceglie quale strumento mostrare** — ago, barre o potenza — con tre
  tasti in testa al pannello, e la scelta si ricorda. Il titolo dice quale è
  attivo: a pannello chiuso è tutto quello che resta.
- **Le misure di trasmissione arrivano fino all'interfaccia.** Il seam le
  portava da sempre con `meterUpdate` e nel core non le raccoglieva nessuno.
  Finché nessun backend le consegna, lo strumento resta spento e lo dichiara —
  «nessun sensore» — invece di mostrare uno zero che sembra una misura.

### Corretto — Strumenti

- **I tre strumenti dicevano numeri diversi guardando lo stesso segnale.** La
  conversione in punti S ora sta in un posto solo, con la convenzione IARU —
  sei decibel per punto S — e il fondo scala a S9+60. Il quadrante a lancetta
  puntava sulla tacca «+60» mentre la sua stessa lettura diceva «S9+18 dB»; le
  tacche della barra sotto il VFO, dichiarate S3, S6 e S9, cadevano dove il
  segnale era rispettivamente S6, S9 e S9+30.

### Corretto — Windows

- **Il programma installato chiedeva DLL che non aveva.** Il pacchetto
  raccoglieva le dipendenze del solo eseguibile, e i plugin di Qt hanno le
  proprie: il backend multimediale si portava dentro FFmpeg — ventisette
  megabyte — e ne pretendeva altri novanta che non venivano copiati. Chi
  compila il progetto non poteva accorgersene, perché su quella macchina le
  librerie mancanti stanno tutte nel PATH di MSYS2.
- **Fuori il backend multimediale.** Di Qt Multimedia il programma usa solo
  l'uscita e l'ingresso audio, che stanno nella libreria e non nel plugin.
  Senza FFmpeg il pacchetto è più piccolo di ventitré megabyte e i dispositivi
  audio si vedono esattamente come prima.
- **Ora entrano anche le dipendenze dei plugin**, che prima restavano fuori in
  silenzio: le immagini JPEG non si aprivano perché mancava `libjpeg`, e il
  plugin che riconosce lo stato della rete non si caricava.
- **Un pacchetto incompleto non arriva più a nessuno**: prima di ogni release
  si verifica che ogni DLL richiesta sia nel pacchetto o parte di Windows.

## [1.1.2] — 2026-08-10

Prima versione con un **programma di installazione**. Su Windows CPack produce
un `.exe` NSIS accanto allo ZIP portable: collegamento nel menu Start, opzione
per quello sul desktop, l'icona dell'eseguibile, e disinstallando si toglie
quello che si è messo e nient'altro — le preferenze sono dell'operatore.

L'installatore mostra un avviso che vale la radio di qualcuno: chi collega un
ricetrasmettitore via CAT deve spegnere «CAT RTS» nei menù.

### Corretto — Sicurezza

- **La ricerca della radio mandava in trasmissione.** Aprire una porta seriale
  su Windows alza DTR e RTS per qualche millisecondo prima che il programma
  possa abbassarli, e su una radio con «CAT RTS» attivo quelle linee *sono* il
  PTT. La sonda apriva ogni porta undici volte — sei velocità newcat più cinque
  CI-V — e ogni apertura era un colpo di trasmissione. Ora apre una volta sola
  per driver, cambiando velocità sulla porta già aperta.
- **`DSDR_AUDIORIG_NO_PROBE=1`** spegne del tutto la sonda, per chi ha la radio
  accesa accanto e non vuole che il programma la tocchi. Si perde il
  riconoscimento automatico, e nient'altro. La suite dei test la usa: non deve
  essere possibile mandare in aria una portante lanciando `ctest`.

### Corretto — Ascolto

- **Lo squelch chiudeva di scatto.** Azzerare l'audio da un campione all'altro
  è un gradino, e un gradino ha uno spettro largo quanto si vuole: quello che
  si sentiva era un clic a ogni respiro della voce sul confine della soglia.
  Le costanti di apertura e chiusura erano dichiarate dal principio, e non
  erano mai state collegate a niente. Ora la porta si apre in un millisecondo
  e si chiude in dieci, e un test presidia la differenza fra una rampa e un
  colpo secco.
- L'isteresi dello squelch era un `3.0` scritto a mano accanto alla costante
  che lo nomina.

### Corretto — Impianto

- **Il core includeva un backend concreto** (`SessionManager` → `FlexClient`),
  contro la prima regola del progetto. L'interrogazione di una radio trovata in
  rete è passata in `RadioScout`, accanto alla ricerca: è lo stesso mestiere, e
  sopra il seam nessuno deve sapere che esista uno SmartSDR.

> La 1.1.1 non è mai stata pubblicata: il suo tag punta all'albero in cui il
> core includeva ancora un backend. È rimasto lì per non spostare un
> riferimento già visibile, ma non c'è niente da scaricare.

### Cambiato — Impianto dell'interfaccia

- **I comandi del waterfall smettono di galleggiare.** Erano un riquadro
  semitrasparente steso sopra il panadattatore: copriva la porzione di spettro
  che si stava cercando di rendere leggibile, e il rumore che passava dietro
  rendeva illeggibili le etichette proprio mentre le si regolava. Ora sono un
  pannello richiudibile nella colonna destra, come gli altri.
- **La targa del canale attivo sopra lo spettro.** Frequenza sintonizzabile a
  cifre, S-meter, modo, filtro e AGC in una riga sola, dove si guarda mentre si
  sintonizza — non in fondo alla colonna laterale. È il delegate del canale
  corrente, non una copia dei suoi dati.
- **Barra dei menu.** Le impostazioni non avevano una casa: la lingua stava in
  fondo alla barra di stato. Ora c'è File / Vista / Strumenti / Aiuto, con
  dentro solo comandi che fanno qualcosa.
- **Colonna dei comandi rapidi** a sinistra dello spettro: aggiungi un
  ricevitore, torna a piena banda, scegli la vista del waterfall.
- La barra di stato mostra l'**ora UTC**.

### Aggiunto — Lettura dello spettro

- **Scala delle ampiezze** (`LevelScale`): lo spettro mostrava le frequenze ma
  non i livelli, e i cursori del fondo e della vetta si regolavano alla cieca.
- **Piano bande sullo spettro** (`BandSegments`): la striscia CW / dati / fari
  / fonia sul confine con il waterfall, dai segmenti IARU Regione 1 aggiunti a
  `BandPlan`. È una guida alla lettura, non una licenza: i limiti che contano
  restano quelli della propria amministrazione.
- **Asse dei tempi del waterfall** (`TimeScale`), ricavato da una misura e non
  da una costante: `PanadapterView` conta le righe consumate e ne pubblica il
  ritmo. Finché la misura non c'è, l'asse non si disegna — meglio nessuna scala
  di una inventata.
- **Media fra le righe** (`SpectrumFeed::averaging`, 1–8 trasformate per riga,
  tre di fabbrica). Il fondo di una FFT non mediata respira di parecchi decibel
  da una riga all'altra, ed è tutto quello che si vede su una banda quieta. La
  media si fa in decibel — la media video degli analizzatori di spettro — e sul
  rumore converge circa 2,5 dB più in basso che sui segnali: il fondo si ferma
  *e* scende rispetto al traffico. Il prezzo è dichiarato nel pannello: meno
  righe al secondo, quindi più secondi di storia sullo schermo.
- **Tenuta dei picchi**: una seconda traccia che segna il massimo raggiunto da
  ogni bin e scende a velocità regolabile in dB al secondo. La traccia
  istantanea dice cosa c'è adesso; su una banda dove i segnali vanno e vengono,
  «adesso» è quasi sempre il momento sbagliato.

### Cambiato — Resa dello spettro

- **Le palette diventano calde dove sta il traffico.** Il salto verde → giallo è
  il primo appiglio che l'occhio trova in un waterfall, e stava a metà scala —
  dove lo mettono le tabelle nate per le mappe di calore. Ma con la scala
  automatica il traffico ordinario vive nel primo terzo, e la metà alta la
  raggiungono solo le stazioni locali: il salto è sceso a quota 0,40, con il
  tratto caldo tirato per non perdere risoluzione sui segnali forti.
- **La vista in rilievo ha una luce.** La normale della superficie si ricava dai
  campioni vicini nel vertex shader e una luce radente, fissa rispetto ai dati
  come nelle carte in rilievo, illumina un fianco delle creste e ne lascia
  l'altro in ombra. Il piano orizzontale vale esattamente uno: sul fondo di
  rumore, che è piatto, il colore resta quello che la palette gli assegna — la
  luce aggiunge volume dove c'è una forma e sparisce dove non ce n'è.

### Corretto

- **Il waterfall era un muro di colore.** La scala automatica posava il fondo
  sei decibel *sotto* il rumore misurato: siccome il rumore occupa quasi tutta
  la banda, ogni bin riceveva un colore e i segnali non staccavano più. Ora il
  fondo si posa sopra il rumore, che torna nero.
- **La soglia di nero non produceva nero.** Sotto soglia si prendeva il primo
  colore della palette, e Turbo — che nasce per le mappe di calore — parte da
  un viola pieno: il livello «niente», che copre la maggior parte
  dell'immagine, stendeva un velo viola su tutto. Le palette accese ricevono
  ora il fondo davanti.
- **L'S-meter mentiva sul livello.** Il gradiente era applicato alla barra e si
  comprimeva con essa: un S3 mostrava comunque la punta rossa. La barra è ora
  una finestra su un gradiente fermo.
- **Le preferenze del waterfall non venivano mai salvate**, nonostante il
  commento dicesse il contrario: si leggevano all'avvio e basta.
- Un cursore disabilitato non sembrava disabilitato: con la scala automatica
  attiva, fondo e vetta parevano manovrabili.
- Il backend ColibriNANO ripeteva a ogni discovery lo stesso messaggio di
  libreria mancante, fino a nascondere le righe che contano.
- Il pacchetto Windows usciva **senza la libreria del ColibriNANO**: si carica
  a runtime, quindi `file(GET_RUNTIME_DEPENDENCIES)` non la vedeva. La nuova
  opzione `DSDR_COLIBRI_LIB` la include quando chi confeziona il pacchetto
  indica dove sta — non d'ufficio, perché non è nostra da ridistribuire.

### Aggiunto — Fase 1

- Backend **iqfile**: le registrazioni tornano ascoltabili. Chiude il cerchio
  che `IqRecorder` apriva — quello che l'applicazione scrive, la stessa
  applicazione lo riascolta.
  - Dietro il seam una registrazione è una radio come un'altra:
    sintonizzabile, demodulabile, con il suo waterfall e i suoi quattro
    canali. Con una differenza sola, ma grossa: il tempo si può fermare e
    riavvolgere.
  - Legge il nostro WAV float32 e la sua promozione a RF64, ma anche PCM a 16
    e 8 bit: sono venti righe di conversione che lo rendono utile con i file
    registrati da altri programmi, non solo con i nostri.
  - La frequenza centrale non sta nel WAV. Viene dal sidecar JSON; quando
    manca si tenta il nome del file, e il pannello dichiara che quel numero è
    una deduzione — tutto il resto della UI lo tratterebbe come certo.
  - Sintonia e frequenza di campionamento si **rifiutano**: spostarle
    significherebbe mentire su cosa contengono i campioni. Il PTT non esiste,
    perché la capability dice `tx = None`.
  - La riproduzione va al ritmo della registrazione, non a quello del disco.
  - Un file troncato dichiara più byte di quanti ne abbia: ci si fida di ciò
    che c'è davvero invece di leggere oltre la fine.

- **Waterfall completo**, con vista in rilievo opzionale.
  - Cinque palette (DECODIUM, Raptor, Turbo, Fuoco, scala di grigi): la
    tabella è una texture 1D che si ricarica solo quando l'utente cambia
    scelta, non a ogni fotogramma.
  - **Contrasto e soglia** applicati nel fragment shader. La soglia lascia al
    fondo quello che sta sotto — senza, un waterfall affollato diventa un
    tappeto colorato uniforme — e il contrasto fa emergere i segnali deboli
    senza spostarla.
  - **Scala automatica**: fondo e vetta si misurano sull'ultima riga di
    spettro, per percentili anziché per minimo e massimo, così uno spurio
    isolato non spalanca la scala. Il fondo si muove piano, il picco si apre
    in fretta e si richiude piano. La misura si fa dove i campioni già ci
    sono — nel thread di rendering — e viene pubblicata alla UI attraverso il
    ciclo eventi, non emessa da lì.
  - **Vista in rilievo**: la stessa texture ad anello, letta però nel *vertex*
    shader per costruire la superficie. La storia sta già in memoria video,
    quindi non serve rimandare i campioni alla CPU a ogni fotogramma. La
    scena si adatta da sé al riquadro disponibile: inclinazione e rotazione
    cambiano l'ingombro, e un riquadro fisso funzionerebbe per una sola
    combinazione. Se l'hardware non regge il campionamento nel vertex stage,
    si resta sulla vista piatta senza che nulla si rompa.
  - In rilievo la superficie prende tutta l'area: il suo bordo vicino è già lo
    spettro istantaneo, e disegnarne una seconda copia sopra toglierebbe
    spazio proprio alla dimensione che quella vista serve a mostrare.
  - I comandi stanno sopra il panadattatore, non in un pannello laterale: si
    regolano guardando l'effetto. Da chiusi restano una barra sottile.
- Backend **colibri**: ColibriNANO di Expert Electronics, ricevitore USB a
  campionamento diretto (0,1–55 MHz, ADC 14 bit). **Primo hardware reale
  verificato sul ferro**, non solo contro un mock.
  - `colibrinano_lib` si carica a runtime con QLibrary: nel repository non
    entra alcun header di terze parti, e se la libreria manca il backend non
    trova device invece di impedire l'avvio.
  - La callback della libreria scrive **direttamente nel ring SPSC**: essendo
    lock-free a produttore singolo, non serve rimbalzare il blocco su un altro
    thread prima di consumarlo.
  - `ColibriComplex` è già float interleaved come il ring: nel caso normale il
    percorso dei campioni è un solo memcpy.
  - Il preamplificatore è un'unica manopola fra −31,5 e +6 dB, e il flag di
    sovraccarico dell'ADC — l'unica telemetria del device — arriva in UI.
  - Il segno della parte immaginaria è calibrabile a runtime: se le bande
    laterali risultano scambiate si coniuga, senza ricompilare.
- **SpyServer** dentro il backend `nettcp`: secondo protocollo dietro la
  stessa facciata, come il backend era stato pensato per ospitare.
  - Il protocollo si riconosce da come il server si comporta appena connesso:
    rtl_tcp saluta per primo, SpyServer resta in silenzio finché non ti
    presenti. L'ordine del sondaggio non è invertibile — mandare byte a un
    rtl_tcp prima del saluto significherebbe spedirgli comandi a caso.
  - Copertura, risoluzione e frequenze di campionamento arrivano dal messaggio
    DeviceInfo; i rate non sono liberi ma sono il massimo diviso per potenze
    di due.
- **Pannelli backend-specifici** caricati da `nativePanels`: guadagno,
  antenna, ppm e bias tee hanno finalmente un'interfaccia.
- **Packaging** AppImage, DMG e ZIP portable con workflow di release su tag.
- Backend **soapy** (RF-01): un solo backend per RTL-SDR, Airspy, SDRplay,
  HackRF, LimeSDR, PlutoSDR, USRP e chiunque altro pubblichi un driver
  SoapySDR. È il moltiplicatore di universalità previsto dalla spec.
  - Le capability non sono costanti: si leggono dal driver all'apertura. TX
    dichiarato solo con canali TX reali, copertura in frequenza presa dal
    device, `coherentRx` solo con più canali hardware.
  - Stream `CF32`: float interleaved, lo stesso formato del ring — dalla
    scheda al DSP non c'è alcuna conversione.
  - Il ciclo di lettura non gira su un event loop, perché `readStream()` è
    bloccante: i comandi passano da atomiche applicate fra una lettura e
    l'altra, che è anche l'unico modo corretto di toccare un device SoapySDR.
  - Nove test verificano la traduzione profilo → capability senza hardware: è
    la parte che guida la UI, e un errore qui fa comparire un PTT su una
    chiavetta che non trasmette.
- **Internazionalizzazione** (RF-18): pipeline completa per quattordici lingue
  con `lupdate`/`lrelease` integrati nel build, selettore in barra di stato,
  scelta ricordata fra un avvio e l'altro e cambio a caccia calda — i binding
  QML si rivalutano senza riavviare.
  - Le stringhe sorgente sono in italiano; l'**inglese** è tradotto per intero.
  - Le altre dodici lingue sono predisposte ma vuote: una lingua compare nel
    selettore solo quando la sua traduzione esiste davvero, invece di mostrare
    un'interfaccia mezza tradotta.
  - I nomi delle lingue sono scritti nella lingua stessa: in un elenco è
    l'unica forma che chi la parla riconosce a colpo d'occhio.
- **Registrazione IQ** (RF-17): WAV con campioni float32 su due canali,
  leggibile da SDR#, SDRuno e GNU Radio, più un sidecar JSON con frequenza,
  frequenza di campionamento, sorgente e istante d'inizio — senza il quale una
  registrazione IQ è una sequenza di numeri senza significato.
  - Il tap è preso prima di qualunque elaborazione: su disco finisce ciò che la
    radio ha consegnato, non ciò che il DSP ne ha fatto.
  - Il thread DSP scrive solo in un ring lock-free; il disco lo tocca un
    thread dedicato.
  - Le sessioni oltre i 4 GB non vengono troncate: un chunk JUNK riservato in
    testa diventa il `ds64` di RF64 alla chiusura.
  - Disconnettersi con la registrazione aperta la chiude correttamente, invece
    di lasciare un file con l'intestazione incompleta.
- Backend **nettcp** (RF-07): client rtl_tcp completo. Una chiavetta RTL-SDR
  diventa utilizzabile, anche remota — è il primo hardware reale supportato.
  - Discovery per sondaggio: rtl_tcp non si annuncia, e accettare una
    connessione non è prova sufficiente; entra in lista solo chi risponde con
    il saluto `RTL0`.
  - La copertura in frequenza dichiarata segue il tuner riportato
    dall'handshake, invece di promettere sempre la stessa banda.
  - Conversione da uint8 centrata e verificata: una deriva di mezzo LSB
    produrrebbe una riga fantasma a centro banda.
  - Comandi nativi per guadagno, correzione in ppm e bias tee.
- Server rtl_tcp finto nei test: il backend passa l'intera conformance suite
  in CI senza hardware, senza che sia stato scritto un test di conformità in
  più — è il seam a essere data-driven sui backend registrati.
- Convenzione `net.addEndpoint` per i backend che dichiarano `remoteCapable`:
  la UI mostra il campo «indirizzo:porta» in base alla capability, non al nome
  del backend.
- CI su Linux, macOS e Windows, con un job che verifica meccanicamente le
  regole della CONSTITUTION (seam, capability, tema, vendoring).

### Aggiunto — Fase 0

- Seam HAL `IRadioBackend` con descrittore di capability: la UI si genera dalle
  capacità dichiarate, non da condizionali sul modello di radio.
- Backend **demo**: banda sintetica con stazioni CW che trasmettono testo
  morse sagomato, SSB a banda laterale singola vera, AM, portanti e un segnale
  a salto di toni con cadenza FT8, tutto con QSB. Due device (HF 40 m e
  VHF 2 m) e quattro canali RX.
- Motore DSP originale: NCO/DDC, decimazione multistadio con filtri di Kaiser,
  passa-banda a coefficienti complessi (SSB senza trasformata di Hilbert),
  demodulatori SSB/CW/AM/SAM/FM/NFM, AGC multi-modalità con soglia AGC-T,
  analizzatore di spettro su FFTW3.
- Ring buffer SPSC lock-free come unico canale per i campioni fra thread.
- Spettro e waterfall su GPU con `QQuickRhiItem`: texture ad anello per il
  waterfall, colormap in shader, traccia e riempimento in pipeline separate.
- Interfaccia QML con tema DECODIUM dark, flag VFO trascinabili, channel strip
  con S-meter, controlli AGC-T e filtro, pagina di scelta sorgente generata
  dalle capability.
- Uscita audio a bassa latenza (~40 ms) alimentata dal ring del DSP.
- Suite di test: unit test DSP con vettori noti, conformance suite HAL
  data-driven sui backend registrati, integration test headless della sessione
  completa, test Qt Quick sui componenti QML con logica.
- Opzioni da riga di comando `--backend`, `--auto-connect`, `--no-panadapter`.

### Corretto

- Il calcolo del passo della griglia di frequenza poteva degenerare a zero
  durante il primo layout, rendendo `Infinity` il numero di tacche: il Repeater
  istanziava delegate finché il thread della UI smetteva di rispondere.
  Il passo è ora vincolato e il numero di tacche ha un tetto esplicito,
  presidiato dai test in `tests/qml/tst_FrequencyGrid.qml`.
- Il thread DSP girava a priorità time-critical e, saturando una CPU, affamava
  il thread della GUI. Ora gira a priorità alta.
- Meter e segnalazioni di overrun sono limitati in frequenza (15 Hz e 2 Hz):
  prima ogni blocco generava un attraversamento di thread e un `dataChanged`.
