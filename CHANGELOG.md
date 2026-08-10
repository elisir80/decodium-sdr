# Changelog

Il formato segue [Keep a Changelog](https://keepachangelog.com/it/1.1.0/).

## [Non rilasciato]

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
