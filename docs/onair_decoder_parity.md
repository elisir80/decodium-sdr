# Parità decoder con OnAir: inventario e criteri

Riferimento congelato: OnAir v3.9.0, commit `15a2907f02f7486fc64b8e439a1645eb7373171f`. Il file `onair_mode_inventory_v3.9.0.json` elenca tutte le 333 righe di `MODES.md`; due righe sono strumenti di estensione e non compaiono nell'elenco modi dell'app OnAir. La presenza nell'inventario non dichiara supporto in Decodium SDR.

Per ciascuna voce, prima di dichiarare la parità occorrono: ingresso e rate effettivi, requisiti hardware, tipo di risultato, dipendenze e relativi avvisi di licenza, test A/B con campioni identici, carico e perdite, arresto e discontinuità, verifica dei pacchetti per piattaforma. Distinguere decodifica, sola identificazione e risultati sperimentali; segnali simulati e RF reale sono evidenze differenti.

La radio resta gestita da Decodium SDR. I decoder devono ricevere campioni tramite code limitate, senza bloccare il thread DSP; discontinuità e saturazioni vanno segnalate. Campionamento, frequenza, finestra di acquisizione e timestamp non possono essere indovinati. La GUI deve mostrare errori e capacità realmente disponibili.

Il codice di OnAir non viene incorporato dalla sola presenza dell'inventario. In caso di riuso occorre registrare versione, file, licenze transitive e termini di attribuzione di OnAir (`ADDITIONAL-TERMS.md`); l'inventario è un elenco di nomi e non abilita alcun decoder.

## Stato iniziale verificabile

- ADS-B: primo ricevitore locale, con CRC e posizione CPR globale in volo da coppie recenti. Su una registrazione IQ reale di riferimento, dopo la regolazione delle soglie il confronto ha prodotto 133 trame lunghe valide per Decodium e 127 per il demodulatore OnAir, con risultati non identici; vedere `tests/standalone/onair_vertical/README.md`. Un singolo campione non dimostra parità, funzionalità ADS-B completa o ricezione con hardware live.
- APRS AFSK 1200: collegato al canale NFM dell'app e verificato con IQ sintetico, non ancora convalidato su RF reale. Lo shutdown completo necessita ancora di una verifica coordinata.
- POCSAG: catena sperimentale IQ/FM e BCH testata con segnali sintetici a 512/1200/2400 baud. Mancano collegamento all'app e verifica con registrazioni o radio. Vedere `docs/decoders/pocsag-iq-standalone.md`.
- Altre voci: da valutare individualmente, comprese quelle che in OnAir sono identificatori, strumenti, sperimentali o richiedono codec e sorgenti esterne.
