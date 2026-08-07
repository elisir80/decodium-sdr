# Changelog

Il formato segue [Keep a Changelog](https://keepachangelog.com/it/1.1.0/).

## [Non rilasciato]

### Aggiunto — Fase 1

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
