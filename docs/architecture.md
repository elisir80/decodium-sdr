# DECODIUM SDR — Specifica di Architettura

**Documento:** DSDR-SPEC-001
**Versione:** 0.1 (bozza per revisione)
**Data:** 5 agosto 2026
**Autori:** Martino (IU8LMC), con Salvatore Raccampo (9H1SR)
**Licenza del progetto:** GPL-3.0-or-later
**Stato:** In revisione — documento di riferimento per il loop di build Claude Code

---

## 1. Visione

DECODIUM SDR è il client SDR universale dell'ecosistema DECODIUM: un'applicazione nativa cross-platform (Linux x86-64/aarch64, macOS Apple Silicon + Intel, Windows) scritta in **C++17 e QML puro (Qt 6.8 LTS)**, capace di pilotare **qualsiasi hardware SDR in commercio** — da una chiavetta RTL-SDR da 30 € fino al DECODIUM SDR One a 4 canali coerenti — con un'unica interfaccia, un unico tema, un'unica esperienza.

Principi fondanti:

1. **Universalità.** Nessun hardware è di serie A o di serie B. Ogni radio supportata passa dallo stesso seam architetturale (`IRadioBackend`) e riceve la stessa UI. Le differenze di capacità sono dichiarate, non hardcoded.
2. **Clean-room.** L'architettura è ispirata alle migliori pratiche osservate nel panorama open source (AetherSDR per il backend seam e il workflow AI, piHPSDR/linHPSDR per il protocollo HPSDR, WDSP per la teoria DSP), ma **ogni riga di codice è originale**. Nessun file è copiato da progetti terzi. La storia dei commit è la prova.
3. **Full QML.** L'interfaccia è interamente Qt Quick/QML con il tema DECODIUM dark, coerente con DECODIUM 4 "Core Shannon". Nessun QWidget. Lo spettro GPU usa `QQuickRhiItem` sul scene graph.
4. **DSP lato client come cittadino di prima classe.** A differenza dei client "thin" per radio con DSP a bordo, DECODIUM SDR include un motore DSP completo e originale: è ciò che rende possibile il supporto a RTL-SDR, HPSDR, SoapySDR e a tutto l'hardware raw-IQ.
5. **Ecosistema.** Integrazione nativa con DECODIUM 4 (decodifica FT2/FT8 da IQ pulito, senza virtual audio cable), DECOLINK (operazione remota) e DECODIUM SDR One (via DLINK).

---

## 2. Requisiti

### 2.1 Requisiti funzionali

| ID | Requisito |
|----|-----------|
| RF-01 | Supportare hardware SDR raw-IQ via SoapySDR (RTL-SDR, Airspy, SDRplay, HackRF, LimeSDR, PlutoSDR, USRP, ecc.) |
| RF-02 | Supportare radio OpenHPSDR Protocol 1 e Protocol 2 (Hermes, Hermes-Lite 2, ANAN, Red Pitaya) |
| RF-03 | Supportare DECODIUM SDR One via protocollo DLINK, inclusi i 4 canali RX coerenti e QuadBeam |
| RF-04 | Supportare FlexRadio serie 6000/8000 via protocollo SmartSDR (TCP command + VITA-49 UDP) |
| RF-05 | Supportare SunSDR / ExpertSDR via protocollo TCI (client TCI verso ExpertSDR o EE SDK dove documentato) |
| RF-06 | Supportare ricevitori KiwiSDR pubblici e privati via protocollo WebSocket kiwi |
| RF-07 | Supportare sorgenti IQ di rete: rtl_tcp, SpyServer (Airspy), stream IQ generici |
| RF-08 | Bridge CAT via Hamlib per radio tradizionali (controllo frequenza/modo + panadattatore da SDR ausiliario) |
| RF-09 | Demo mode: backend sintetico completo (segnali generati, rumore, QSB, stazioni CW/SSB/FT2 simulate) utilizzabile senza alcun hardware |
| RF-10 | Motore DSP client: decimazione, canalizzazione, demodulatori SSB/CW/AM/SAM/FM/NFM/DIGI/IQ, AGC multi-modalità con AGC-T, notch, NB, NR |
| RF-11 | Spettro + waterfall GPU a 60 fps via QRhi, multi-panadattatore, fino a 8 pan indipendenti |
| RF-12 | Multi-canale: fino a N canali RX simultanei (N dichiarato dal backend), con VFO color-coded stile slice |
| RF-13 | Audio virtuale + stream IQ verso applicazioni esterne (WSJT-X, fldigi) e canale nativo IPC verso DECODIUM 4 |
| RF-14 | Server TCI integrato (v1.5+) per esporre DECODIUM SDR come sorgente verso software terzi |
| RF-15 | Integrazione DECOLINK per operazione remota punto-punto |
| RF-16 | TX dove l'hardware lo consente (SDR One, HPSDR, Flex, TCI), con inibizione TX esplicita sui backend receive-only |
| RF-17 | Registrazione/riproduzione IQ (formato WAV RF64 + sidecar metadati) |
| RF-18 | i18n a 14 lingue con la stessa pipeline di DECODIUM 4 |

### 2.2 Requisiti non funzionali

| ID | Requisito |
|----|-----------|
| RNF-01 | C++17, Qt 6.8 LTS come floor (stesso Qt in CI e nelle release), CMake + Ninja |
| RNF-02 | UI 100% QML/Qt Quick; zero QWidget nel prodotto finale |
| RNF-03 | Latenza audio RX ≤ 50 ms sul path locale (target 25 ms); PTT-to-RF verificabile per DECOLINK |
| RNF-04 | Un binario per piattaforma: AppImage (x86-64 + aarch64), DMG firmato/notarizzato, installer + ZIP portable Windows |
| RNF-05 | Il thread DSP non alloca a runtime nel percorso caldo (buffer pre-allocati, lock-free ring buffer) |
| RNF-06 | Ogni backend compilabile/escludibile a build-time (`DSDR_BACKEND_SOAPY=ON/OFF`, ecc.) |
| RNF-07 | Copertura test: unit test DSP con vettori noti, integration test contro il backend demo in CI headless |
| RNF-08 | Commit firmati, CI verde e review umana obbligatoria prima del merge (merge gate) |

---

## 3. Architettura a livelli

```
┌─────────────────────────────────────────────────────────────┐
│  UI — QML puro (tema DECODIUM dark)                         │
│  Panadapter QQuickRhiItem · Flag VFO · Channel strip ·      │
│  Pannelli backend-specifici generati dalle capability       │
├─────────────────────────────────────────────────────────────┤
│  Core — C++ (QObject, esposto a QML via context/singleton)  │
│  SessionManager · ChannelModel · DeviceDiscovery ·          │
│  SettingsStore (SQLite, documenti versionati per-radio) ·   │
│  AudioRouter · RecorderService · SpotService                │
├──────────────────────────┬──────────────────────────────────┤
│  DSP Engine (thread RT)  │  Integration Services            │
│  Channelizer · Demod ·   │  DECODIUM 4 IPC · DECOLINK ·     │
│  AGC · NR/NB/Notch ·     │  TCI Server · Virtual Audio ·    │
│  FFT/Spectrum pipeline   │  Hamlib CAT out                  │
├──────────────────────────┴──────────────────────────────────┤
│  HAL — IRadioBackend (il seam)                              │
│  dlink · soapy · hpsdr · flex · tci · kiwi · nettcp ·       │
│  hamlib-bridge · demo                                       │
└─────────────────────────────────────────────────────────────┘
```

Regola cardine ereditata (come idea) da AetherSDR: **nessun modulo sopra la HAL conosce il tipo concreto di radio**. La UI e il core parlano solo con `IRadioBackend` e con il descrittore di capability. Aggiungere una radio nuova non tocca mai la UI.

### 3.1 Le due classi di backend

La distinzione architetturale centrale, che AetherSDR non ha dovuto affrontare (Flex fa tutto a bordo), è tra:

- **Backend server-DSP** — la radio demodula e calcola lo spettro; il client riceve audio demodulato + tile spettro (Flex, TCI/SunSDR, KiwiSDR). Il DSP Engine locale opera in modalità *pass-through arricchito* (NR client-side opzionale, EQ, meter).
- **Backend raw-IQ** — la radio consegna solo campioni IQ; tutto il resto lo fa DECODIUM SDR (SoapySDR, rtl_tcp, SpyServer, HPSDR, registrazioni IQ). Il DSP Engine opera in modalità *full chain*.
- **Backend ibrido** — DLINK/SDR One: la FPGA Agilex 3 fa DDC, decimazione e beamforming QuadBeam; demodulazione, AGC e NR restano lato client per massima flessibilità. È il caso che giustifica la separazione netta tra canalizzazione (hardware o software) e demodulazione (sempre software).

Il descrittore di capability (§4.2) dichiara dove vive ogni stadio; il core instrada di conseguenza.

---

## 4. La HAL: IRadioBackend

### 4.1 Interfaccia (estratto normativo)

```cpp
// src/hal/IRadioBackend.h
#pragma once
#include <QObject>
#include "BackendCapabilities.h"
#include "DeviceDescriptor.h"
#include "Frames.h"

namespace dsdr::hal {

class IRadioBackend : public QObject {
    Q_OBJECT
public:
    explicit IRadioBackend(QObject* parent = nullptr) : QObject(parent) {}
    ~IRadioBackend() override = default;

    // Identità e capacità (valide dopo la connessione; parziali in discovery)
    virtual QString backendId() const = 0;           // "dlink", "soapy", ...
    virtual BackendCapabilities capabilities() const = 0;

    // Ciclo di vita
    virtual void startDiscovery() = 0;               // asincrono, emette deviceFound()
    virtual void stopDiscovery() = 0;
    virtual void open(const DeviceDescriptor& dev) = 0;
    virtual void close() = 0;

    // Canali RX (slice). L'handle è opaco; il backend valida i limiti.
    virtual ChannelId createRxChannel(const RxChannelConfig& cfg) = 0;
    virtual void destroyRxChannel(ChannelId ch) = 0;
    virtual void setFrequency(ChannelId ch, qint64 hz) = 0;
    virtual void setDemod(ChannelId ch, DemodMode mode) = 0;      // no-op se DSP client
    virtual void setFilter(ChannelId ch, int loHz, int hiHz) = 0; // idem

    // Panadattatori (sorgente spettro dichiarata nelle capability)
    virtual PanId createPanadapter(const PanConfig& cfg) = 0;
    virtual void destroyPanadapter(PanId pan) = 0;

    // TX (solo se capabilities().tx != TxSupport::None)
    virtual void setPtt(bool tx) = 0;
    virtual void setTxFrequency(qint64 hz) = 0;

    // Escape hatch controllato: comandi nativi del protocollo,
    // usato SOLO dai pannelli backend-specifici, mai dal core.
    virtual QVariant nativeCommand(const QString& cmd, const QVariantMap& args);

signals:
    void deviceFound(const DeviceDescriptor& dev);
    void stateChanged(BackendState state);           // Idle/Connecting/Ready/Error
    void error(const BackendError& err);

    // Dati — emessi dal thread di rete/driver, consumati via ring buffer
    void iqFrameReady(ChannelId ch, const IqFrame& frame);        // raw-IQ e ibridi
    void audioFrameReady(ChannelId ch, const AudioFrame& frame);  // server-DSP
    void spectrumFrameReady(PanId pan, const SpectrumFrame& frame);
    void meterUpdate(ChannelId ch, const MeterFrame& meters);
    void capabilitiesChanged();                       // es. dopo handshake completo
};

} // namespace dsdr::hal
```

Note normative:

- I frame dati **non** attraversano il meccanismo signal/slot Qt nel percorso caldo. I signal esistono per notifiche di controllo; i campioni viaggiano su ring buffer lock-free (SPSC) di cui i signal segnalano solo la disponibilità. Documentare questo pattern in `docs/dsp-threading.md`.
- `nativeCommand()` è la valvola di sfogo per feature specifiche di un protocollo (es. impostazioni ATU Flex) senza inquinare l'interfaccia. Ogni uso deve essere confinato in un pannello QML backend-specifico.
- Nessuna eccezione C++ attraverso il boundary della HAL: errori sempre via `error()`.

### 4.2 BackendCapabilities

```cpp
struct BackendCapabilities {
    // Topologia
    int      maxRxChannels   = 1;
    bool     coherentRx      = false;   // true solo per SDR One (QuadBeam)
    int      maxPanadapters  = 1;
    TxSupport tx             = TxSupport::None;   // None | Ptt | FullDuplex

    // Dove vive il DSP
    DspLocation demod        = DspLocation::Client;  // Client | Device
    DspLocation spectrum     = DspLocation::Client;
    DspLocation agc          = DspLocation::Client;

    // Segnale
    QList<double> sampleRates;          // rate IQ disponibili (se raw-IQ)
    qint64   minFrequencyHz  = 0;
    qint64   maxFrequencyHz  = 0;
    bool     hasHardwareFilters = false;

    // Rete/sessione
    bool     remoteCapable   = false;   // già dietro rete (kiwi, nettcp, flex)
    bool     multiClient     = false;

    // UI hints
    QStringList nativePanels;           // pannelli QML extra da caricare
};
```

La UI è **generata dalle capability**: se `tx == None` il pulsante PTT non esiste (non è disabilitato: non esiste), se `coherentRx` compare il pannello QuadBeam, se `demod == Device` la tendina filtri riflette i filtri della radio. Questo è ciò che rende il supporto universale sostenibile: una matrice di capability, non una matrice di `if (backend == ...)`.

### 4.3 Matrice dei backend

| Backend | Protocollo | Classe | RX max | Coerenti | TX | Spettro | Fase |
|---|---|---|---|---|---|---|---|
| **demo** | sintetico | raw-IQ | 4 | sì (simulata) | simulato | client | 0 |
| **soapy** | SoapySDR API | raw-IQ | per-device | no | per-device¹ | client | 1 |
| **nettcp** | rtl_tcp / SpyServer | raw-IQ | 1 | no | no | client | 1 |
| **hpsdr** | OpenHPSDR P1/P2 UDP | raw-IQ | fino a 7 (P2) | parziale² | sì | client | 2 |
| **dlink** | DLINK (SDR One) | ibrido | 4 | **sì** | sì | client (IQ decimato da FPGA) | 3 |
| **flex** | SmartSDR TCP + VITA-49 | server-DSP | slice per modello | no | sì | device | 4 |
| **tci** | TCI WebSocket (SunSDR/ExpertSDR) | server-DSP | 2+ | no | sì | device/TCI | 4 |
| **kiwi** | WebSocket kiwi | server-DSP | 1/sessione³ | no | no (inibito) | device | 4 |
| **hamlib** | CAT (rigctld) | control-only | — | — | key/PTT | da SDR ausiliario | 5 |

¹ HackRF/LimeSDR/PlutoSDR trasmettono; RTL-SDR no. Dichiarato per-device dal driver Soapy.
² P2 supporta RX multipli sincroni su alcuni hardware; trattato come `coherentRx=false` finché non validato.
³ Sessioni multiple verso Kiwi diversi = più canali logici (diversity di ricevitori remoti, in RX-only).

**Regola d'oro:** SoapySDR è il moltiplicatore di universalità — un solo backend copre decine di hardware. Ogni device esotico che ha un driver Soapy funziona gratis. I backend nativi (hpsdr, flex, tci, kiwi, dlink) esistono solo dove Soapy non arriva o dove il protocollo nativo dà accesso a feature che Soapy appiattisce.

---

## 5. DSP Engine

Il motore DSP è **codice originale**, scritto da zero con riferimento alla letteratura (Lyons, *Understanding DSP*; Gallager per LDPC già in DECODIUM 4; paper AGC di Warren Pratt come riferimento teorico). **Attenzione licenze:** WDSP e il codice OpenHPSDR (`wcpAGC.c`) restano materiale di *studio*, non di riuso, finché la questione di compatibilità GPLv2/v3 già aperta non è risolta. L'implementazione AGC-T di DECODIUM SDR è una riscrittura originale dell'algoritmo (l'algoritmo non è copyrightabile, il codice sì).

### 5.1 Catena raw-IQ (full chain)

```
IQ in (fs device) → DDC/NCO → Decimazione multistadio (CIC-comp FIR / polyphase)
  → per canale: filtro passa-banda variabile → Demod (SSB/CW/AM/SAM/FM/DIGI)
  → AGC (Fast/Med/Slow/Long + AGC-T threshold) → Notch/ANF → NB → NR
  → Resampler → AudioRouter (48 kHz interno)
Parallelo: tap post-decimazione → FFT (fftw3f) → averaging → SpectrumFrame
```

- Thread model: un thread RT per device (ingest), un thread pool DSP per canale, worker FFT separato. Comunicazione solo via ring buffer SPSC pre-allocati.
- FFT: FFTW3 single-precision (GPL-compatibile). Valutare in Fase 2 un path SIMD custom (pffft, licenza BSD) per aarch64.
- Il canale IQ verso DECODIUM 4 viene derivato **post-decimazione, pre-demodulazione**: FT2 decodificato da IQ pulito a 12 kHz, il collo di bottiglia identificato nell'analisi VPS DECOLINK.

### 5.2 Modalità pass-through (server-DSP)

Per Flex/TCI/Kiwi il motore applica solo gli stadi client-side opzionali (NR aggiuntivo, EQ, meter, registrazione) sull'audio già demodulato. La UI espone i controlli DSP della radio via `nativeCommand()` mappato nei pannelli specifici.

### 5.3 QuadBeam (solo dlink)

Il beamforming sui 4 canali coerenti resta in FPGA come da spec QuadBeam di SDR One; DECODIUM SDR espone controllo di steering, pesi e null, e riceve i beam già formati come canali IQ virtuali aggiuntivi. Il fallback software (beamforming client-side sui 4 stream IQ grezzi) è previsto come modalità diagnostica a rate ridotto, non come modalità operativa.

---

## 6. UI full QML

### 6.1 Stack di rendering

- **Spettro e waterfall:** `QQuickRhiItem` (Qt 6.8) — nodo custom nel scene graph, shader compilati con Qt Shader Tools (`.qsb`), backend OpenGL/Metal/D3D11/Vulkan scelto da Qt. Target 60 fps con trace per-pixel.
  - Waterfall: texture ring a scorrimento (una riga nuova per frame, scroll via offset UV — mai ridisegnare la storia).
  - Il path CPU esiste solo come build alternativo (`DSDR_GPU_SPECTRUM=OFF`) per hardware Metal/GL antico, decisione a build-time come in AetherSDR — non è un fallback runtime.
- **Tutto il resto:** Qt Quick standard + componenti del design system DECODIUM (riuso diretto della libreria QML di DECODIUM 4: palette dark, tipografia, controlli).
- Layout: pan multipli in `SplitView`/finestre staccabili (`QQuickWindow` multiple), flag VFO color-coded trascinabili sullo spettro, channel strip laterale.

### 6.2 Struttura QML

```
src/qml/
├── Main.qml
├── theme/            ← importato/allineato da DECODIUM 4 (singleton Theme)
├── panadapter/       ← SpectrumView (RhiItem), WaterfallView, VfoFlag, BandBar
├── channel/          ← ChannelStrip, DemodSelector, AgcPanel (con AGC-T), FilterEdit
├── device/           ← DiscoveryPage, capability-driven DevicePanel
├── backend-panels/   ← pannelli nativi: FlexPanel, KiwiBrowser, QuadBeamPanel, ...
├── integration/      ← Decodium4Dock, DecolinkPanel, TciServerPanel
└── common/
```

Il `KiwiBrowser` (directory dei ricevitori pubblici, rispettosa delle policy API kiwi) è un pannello backend, non una feature core — coerente con la regola che la UI generale non conosce i backend.

---

## 7. Integrazione ecosistema

| Servizio | Meccanismo | Note |
|---|---|---|
| **DECODIUM 4** | IPC locale (Unix socket / named pipe) con stream IQ 12 kHz + canale controllo JSON | Decodifica FT2/FT8 senza virtual audio; frequenza/modo sincronizzati; click-to-tune dallo waterfall di DECODIUM SDR al decoder |
| **DECOLINK** | Il backend attivo diventa sorgente per una sessione decolink.ft2.it; riuso del transport audio esistente + canale IQ opzionale a 12 kHz | La verifica di latenza PTT già in backlog DECOLINK vale anche qui |
| **TCI Server** | Server WebSocket TCI 1.5+ integrato | Riuso dell'esperienza yaesu-tci-bridge; espone DECODIUM SDR a logger, skimmer e software terzi |
| **Audio virtuale** | Dispositivi loopback per app esterne (WSJT-X, fldigi) | 4 canali RX + 1 TX come baseline |
| **Hamlib out** | rigctld emulato | I logger che parlano solo CAT vedono DECODIUM SDR come una radio |

---

## 8. Layout del repository

```
decodium-sdr/
├── CLAUDE.md                  ← guida canonica per gli agenti (letta per prima)
├── AGENTS.md                  ← workflow multi-agente, ruoli, merge gate
├── CONSTITUTION.md            ← principi non negoziabili del progetto (§10)
├── README.md · ROADMAP.md · CHANGELOG.md · CONTRIBUTING.md
├── LICENSE (GPL-3.0-or-later) · THIRD_PARTY_LICENSES
├── CMakeLists.txt
├── cmake/                     ← moduli, opzioni DSDR_BACKEND_*
├── src/
│   ├── core/
│   ├── hal/
│   │   ├── IRadioBackend.h · BackendCapabilities.h · Frames.h
│   │   └── backends/{demo,soapy,nettcp,hpsdr,dlink,flex,tci,kiwi,hamlib}/
│   ├── dsp/
│   ├── audio/
│   ├── integration/
│   └── qml/
├── tests/
│   ├── dsp/                   ← vettori noti: demod, AGC, decimazione
│   ├── hal/                   ← conformance suite: ogni backend contro lo stesso test
│   └── integration/           ← app completa contro il backend demo, headless in CI
├── docs/
│   ├── architecture.md (questo documento) · dsp-threading.md
│   └── backends/{dlink,hpsdr,flex,tci,kiwi}.md
├── packaging/{appimage,dmg,windows}/
├── scripts/setup/
└── third_party/               ← solo vendored GPL-compatibili, ognuno con LICENSE
```

**Conformance suite HAL:** ogni backend deve passare la stessa batteria di test (discovery, open/close, creazione canali oltre il limite, frame ordering, teardown durante streaming). È il contratto che tiene onesto il seam — e il modo in cui il loop Claude Code valida un backend nuovo senza hardware, tramite mock del transport.

---

## 9. Piano di fasi

| Fase | Contenuto | Criterio di uscita |
|---|---|---|
| **0 — Fondamenta** | Skeleton repo, CMake, HAL + capability, backend **demo** completo, spettro/waterfall QRhi in QML, channel strip, DSP full-chain per 1 canale (SSB/CW/AM), tema DECODIUM | L'app gira su 3 piattaforme in CI, demo mode pienamente operativo, conformance suite verde |
| **1 — Universale subito** | Backend **soapy** + **nettcp** (rtl_tcp/SpyServer), multi-canale, registrazione IQ, i18n | Un RTL-SDR da 30 € e un Airspy funzionano out-of-the-box; prime release pubbliche AppImage/DMG/ZIP |
| **2 — Radio vere** | Backend **hpsdr** (P1 poi P2), TX, AGC-T completo, NR/NB/ANF, audio virtuale, TCI server | QSO completo con Hermes-Lite 2; DECODIUM 4 decodifica FT2 via IPC |
| **3 — SDR One** | Backend **dlink**, 4 canali coerenti, pannello QuadBeam, integrazione DECOLINK | SDR One pilotato end-to-end; QuadBeam operativo |
| **4 — Server-DSP** | Backend **flex**, **tci** (client), **kiwi** + KiwiBrowser | Le tre famiglie connesse e operative con pannelli nativi |
| **5 — Coda lunga** | Backend **hamlib**, raffinamenti, Flathub, feature community | Backlog guidato dalle issue |

Razionale dell'ordine: la Fase 1 con SoapySDR dà subito la platea più ampia possibile (chiunque abbia una chiavetta) e quindi tester, issue e community prima ancora che l'hardware SDR One arrivi in Fase 3. Flex/SunSDR/Kiwi (Fase 4) arrivano dopo perché quegli utenti hanno già client maturi: DECODIUM SDR deve prima valere la pena per ciò che gli altri non hanno.

---

## 10. Governance e workflow AI (CONSTITUTION.md — traccia)

1. Ogni commit passa dal merge gate: CI verde su Linux/macOS/Windows + review umana. Nessuna eccezione, umano o agente.
2. Commit firmati GPG.
3. Nessun codice copiato da repository terzi; il vendoring va in `third_party/` con licenza verificata GPL-3.0-compatibile e voce in `THIRD_PARTY_LICENSES`.
4. Il seam HAL è inviolabile: nessun `#include` di un backend concreto sopra la HAL; la conformance suite è bloccante.
5. Il percorso caldo DSP non alloca, non locka, non emette signal Qt con payload dati.
6. UI solo QML; ogni componente usa il singleton Theme; nessun colore hardcoded.
7. Le capability guidano la UI; vietati i branch sul tipo di backend fuori dalla HAL.
8. Ogni feature nasce con il suo test; ogni bug fix nasce con il test che lo riproduce.
9. Demo mode sempre funzionante: se una PR rompe il demo backend, la PR è rotta.
10. Documentazione backend obbligatoria in `docs/backends/` prima del merge del backend.

Il loop Claude Code di SDR One viene esteso: un agente per fase/area (hal, dsp, qml, packaging), issue etichettate per eleggibilità agli agenti, piani di implementazione in PR draft, umano (Martino/Salvatore) come CODEOWNER bloccante.

---

## 11. Licenze e note legali

- Progetto: **GPL-3.0-or-later**. Compatibile con l'ecosistema DECODIUM esistente.
- FFTW3: GPL — ok. SoapySDR: Boost license — ok. PortAudio/RtAudio: MIT — ok. Hamlib: LGPL — ok.
- **WDSP / wcpAGC.c:** solo riferimento teorico fino a risoluzione della questione GPLv2/v3 già tracciata; l'AGC di DECODIUM SDR è implementazione originale.
- Protocolli (SmartSDR, TCI, OpenHPSDR, kiwi, rtl_tcp): implementati da documentazione pubblica e osservazione del protocollo; nessun SDK proprietario vendorizzato senza verifica di licenza.
- Marchi: FlexRadio, SunSDR/ExpertSDR, KiwiSDR ecc. citati solo per interoperabilità, con disclaimer di non affiliazione nel README.
- Ispirazione architetturale (AetherSDR e altri) dichiarata apertamente nel README nella sezione *Acknowledgements* — trasparenza totale, zero codice condiviso.

---

## 12. Questioni aperte

1. **DLINK:** la spec attuale copre il transport SDR One; serve l'estensione per discovery multi-device su LAN e per la negoziazione capability (proposta: blocco `CAPS` nel handshake).
2. **TCI come client:** TCI nasce come protocollo *server* lato ExpertSDR; verificare copertura reale di controllo (alcune funzioni SunSDR passano solo dal software EE). Fallback: TCI per IQ/audio/CAT + pannello ridotto.
3. **KiwiSDR policy:** rispettare i limiti della directory pubblica e i termini d'uso dei singoli ricevitori (time limit, canali); il KiwiBrowser deve esporli, non aggirarli.
4. **Audio virtuale su macOS:** driver loopback richiede estensione firmata (DriverKit) — valutare costo/beneficio vs documentare BlackHole come prerequisito.
5. **aarch64/Raspberry Pi:** target dichiarato (il CM5 di SDR One lo rende quasi obbligatorio); definire il budget CPU del DSP full-chain su CM5 a 4 canali.
6. **Nome release:** proposta CalVer `YY.M.patch` per coerenza con la cadenza reale, oppure allineamento alla numerazione DECODIUM (5.x?). Da decidere prima della Fase 1.

---

*Fine documento — DSDR-SPEC-001 v0.1. Prossima revisione dopo feedback di Martino e Salvatore.*
