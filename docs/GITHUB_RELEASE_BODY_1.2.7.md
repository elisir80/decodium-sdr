## English

Decodium SDR 1.2.7 expands receive-side tools and adds experimental, independently selectable decoders. Processing is asynchronous and uses bounded queues; the new modes complement the existing Decodium 4 modes rather than duplicating them.

### Receive decoders

- **ADS-B:** aircraft messages, positions where CPR data permits, and available velocity and altitude details.
- **APRS:** AFSK 1200 packet reception and position information when supplied.
- **POCSAG:** paging reception at supported baud rates.
- **AIS:** maritime message reception.
- **ACARS and VDL Mode 2:** experimental aviation data reception, message presentation and integrity checks.
- **NAVTEX and DSC:** experimental maritime text and selected MF/HF digital selective-calling messages.
- **VOR and SELCAL:** experimental identification/radial display and selective-call detection.

Decoder coverage, supported message types and operational limitations differ. VOR is experimental and **must not be used for navigation**; its absolute bearing accuracy needs further investigation. These functions are not safety-of-life equipment. Reception depends on the source, antenna, tuning, signal conditions and local regulations.

### SDR tools and reliability

- IQ recording analysis tools and file-based receive workflows.
- Configurable synthetic signals for reproducible receiver tests and demonstrations.
- Easier multichannel selection and receiver access.
- Asynchronous decoder lifecycle, source-change and shutdown handling, including queue and gap protections.
- RTL-SDR mixer tuning recalculation after sample-rate changes.
- Tests comparing signals and negative controls, including real recorded samples where available; third-party notices and OnAir attribution are included.

The A/B and recorded-sample tests do not establish live RF performance for every receiver and operating system. Additional interference, long-duration and hardware verification remains useful. VDL2 is an experimental optional component included in release builds.

## Italiano

Decodium SDR 1.2.7 amplia gli strumenti di ricezione e introduce decoder sperimentali selezionabili separatamente. L'elaborazione è asincrona e utilizza code a capacità limitata; i nuovi modi completano quelli già disponibili in Decodium 4.

### Decoder di ricezione

- **ADS-B:** messaggi degli aeromobili, posizione quando disponibile dai dati CPR e informazioni disponibili su velocità e quota.
- **APRS:** ricezione pacchetti AFSK 1200 e posizione quando presente.
- **POCSAG:** ricezione cercapersone alle velocità supportate.
- **AIS:** ricezione di messaggi marittimi.
- **ACARS e VDL Mode 2:** ricezione sperimentale di dati aeronautici, visualizzazione dei messaggi e controlli di integrità.
- **NAVTEX e DSC:** testo marittimo e alcuni messaggi sperimentali di chiamata selettiva digitale MF/HF.
- **VOR e SELCAL:** visualizzazione sperimentale di identificativo/radiale e rilevamento delle chiamate selettive.

La copertura dei messaggi e i limiti operativi variano tra decoder. Il VOR è sperimentale e **non va usato per la navigazione**: l'accuratezza assoluta del rilevamento richiede ulteriori verifiche. Queste funzioni non sono strumenti per la sicurezza della vita umana. La ricezione dipende da sorgente, antenna, sintonia, propagazione e normative locali.

### Strumenti SDR e affidabilità

- Analisi delle registrazioni IQ e flussi di ricezione da file.
- Segnali sintetici configurabili per test e dimostrazioni ripetibili.
- Selezione più semplice dei canali e accesso ai ricevitori.
- Gestione asincrona del ciclo di vita dei decoder, del cambio sorgente e della chiusura, con protezioni per code e discontinuità.
- Ricalcolo della sintonia del mixer RTL-SDR dopo il cambio della frequenza di campionamento.
- Test comparativi e controlli negativi, anche con registrazioni reali quando disponibili; inclusi avvisi di terze parti e attribuzione a OnAir.

I test A/B e su registrazioni non dimostrano il funzionamento RF con ogni ricevitore e sistema operativo. Restano utili ulteriori prove con interferenze, sessioni prolungate e hardware reale. VDL2 è un componente opzionale sperimentale incluso nelle build di rilascio.
