# Changelog

Il formato segue [Keep a Changelog](https://keepachangelog.com/it/1.1.0/).

## [Non rilasciato]

### Aggiunto — Fase 1

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
