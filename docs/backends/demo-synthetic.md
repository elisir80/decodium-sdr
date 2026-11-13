# Generatore sintetico RX del backend demo

Il pannello nativo `DemoSignalPanel` configura una sorgente baseband IQ nel
backend demo. Il generatore sostituisce temporaneamente il piano HF/VHF demo;
quando è spento il piano originale e il loopback TX demo restano disponibili.
Quando è acceso il backend dichiara TX non disponibile e rifiuta PTT. Non
controlla o trasmette tramite hardware. Attivarlo durante il PTT demo viene
rifiutato.

## Integrazione dell'interfaccia

Aggiungere `qml/DemoSignalPanel.qml` alla lista `DSDR_QML_FILES` in
`src/app/CMakeLists.txt`. La capability `nativePanels` del backend contiene già
`DemoSignalPanel` e `BackendPanelHost` lo carica quando il demo è connesso;
non servono modifiche a SessionManager, Main o MainMenu. Senza la risorsa il
backend funziona, ma il pannello non viene caricato.

## Uso e ripetibilità

Scegliere portante, AM o FM, offset rispetto al centro del ricevitore,
ampiezza, modulazione, rumore RMS per componente I/Q, seed e impulsi periodici. La deriva è
sinusoidale, con escursione massima in Hz e periodo in secondi; l'interferente
è una portante indipendente con offset dal centro e ampiezza. Il fading è
sinusoidale sul solo segnale utile: la profondità 0 lo disattiva, 1 porta
l'ampiezza a zero al minimo; il periodo è in secondi. Deriva, interferente e
fading sono disattivati per default e per configurazioni salvate precedenti.
Premere **Applica / Ripeti**: i parametri sono salvati in QSettings sotto
`demo/syntheticSignal`, la configurazione viene inviata al worker senza
attenderlo e il generatore riparte dalla fase e dal seed iniziali. Il periodo
zero disattiva gli impulsi; altrimenti il primo è al campione zero. Il rumore è
una somma di quattro distribuzioni uniformi, non gaussiano esatto. L'uscita non
viene limitata: AM, rumore e impulsi possono superare 1. L'offset rimane
relativo al centro anche durante la sintonia; le impostazioni che occupano la
banda oltre Nyquist possono produrre alias.

A parità di parametri, frequenza di campionamento e piattaforma, la sequenza
sintetica dalla ripartenza è ripetibile e indipendente dalla dimensione dei
blocchi. Le funzioni trigonometriche possono differire lievemente tra sistemi.
L'applicazione è asincrona e il ring può contenere campioni della configurazione
precedente: il comando non fornisce un marcatore di inizio nel flusso, quindi un
confronto campione per campione deve allineare la sequenza o avviare una nuova
acquisizione, tenendo conto di eventuali perdite per overrun. Il tempo reale e
l'assenza di perdite dipendono dalla macchina e dal consumatore.

`demo.synthetic.status` restituisce la configurazione; `demo.synthetic.apply`
accetta una mappa completa (o campi omessi ai valori predefiniti) e restituisce
`{ok, error}`. I valori fuori campo non vengono applicati né salvati. Il
pannello segnala gli errori. Le modifiche ai campi diventano effettive solo con
Applica; il reset di fase riguarda il generatore, non gli stati dei filtri DSP.
