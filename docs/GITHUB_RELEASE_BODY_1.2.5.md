# DECODIUM SDR 1.2.5 «Armstrong»

## Italiano

DECODIUM SDR 1.2.5 chiude tre aree rimaste aperte nella 1.2.4: il
panadapter IF collegato alla radio via CAT, l'interoperabilità IQ/audio in
rete con SDR++, e una gestione operativa vera di attività pianificate e moduli
IQ. È inoltre la prima release con pacchetti nativi per Windows x64, macOS
Apple Silicon e Intel, Linux x86-64 e Linux ARM64.

### Panadapter IF, RTL-SDR e CAT universale

- Aggiunto il backend **RTL-SDR + CAT (panadapter IF)**: l'uscita IF della
  radio viene acquisita dall'RTL-SDR, mentre il VFO radio resta il riferimento
  autorevole della sintonia.
- La frequenza hardware viene calcolata come **IF + shift USB/LSB** senza il
  normale offset anti-DC quando si usa un'IF fissa. Questo allinea portante,
  spettro e VFO come il panadapter di SDR++ e evita una seconda traslazione nel
  DSP.
- La sincronizzazione CAT è bidirezionale per frequenza e modo, con protezione
  dal loop: cambiare il VFO nell'app aggiorna la radio e il polling della radio
  aggiorna l'app senza rincorrersi.
- La protezione TX ora è *fail closed*: un polling PTT dedicato e preciso
  sospende immediatamente il flusso IQ dell'RTL-SDR se la radio trasmette, se
  lo stato PTT non è verificabile o se il CAT viene perso. La ricezione riparte
  solo dopo uno stato RX valido.
- Il pannello RTL-SDR salva e ripristina direct sampling Q ADC, IF, banda
  laterale, shift USB/LSB, inversione dello spettro, gain, PPM, bias-tee e
  offset tuning. Il pannello non invia più comandi PPM/offset non necessari
  che alcuni RTL-SDR Blog V4 rifiutano senza che vi sia un errore reale.
- Il direct sampling mostra chiaramente quando il tuner viene bypassato e
  disabilita i controlli AGC/gain che non hanno effetto sul Q ADC.

### Hamlib e profili CAT locali

- Aggiunto il driver **Hamlib locale**: Decodium SDR avvia `rigctld` per la
  porta USB/seriale selezionata e usa poi il normale protocollo rigctl. Il
  catalogo dei modelli arriva dall'installazione Hamlib, invece di mantenere
  una piccola lista interna; ciò rende disponibili le radio supportate da
  Hamlib, compreso il Kenwood TS-940S.
- Il dialogo CAT espone e conserva modello Hamlib, baud rate, bit dati,
  parità, bit di stop, handshake e stato DTR/RTS. I valori proposti possono
  essere letti dai default dichiarati da Hamlib per il modello scelto.
- I profili della radio dichiarata e del panadapter CAT vengono salvati in modo
  persistente. La ricerca seriale e il cambio sorgente non bloccano più la UI
  in attesa di un timeout CAT; i risultati di una ricerca ormai annullata non
  possono comparire nella ricerca successiva.

### Compatibilità di rete con SDR++ e IQ generico

- Il backend di rete ora supporta, oltre a `rtl_tcp` e SpyServer, endpoint IQ
  dichiarati **TCP**, **UDP** e **SDR++ Server**. I formati supportati sono
  `int8`, `int16`, `int32` e `float32` per IQ grezzo e `int8`, `int16`,
  `float32` per SDR++ Server.
- Il client SDR++ Server negozia esplicitamente rate, tipo di campione,
  frequenza e avvio dello stream. Rifiuta i frame compressi invece di trattarli
  erroneamente come campioni IQ.
- Aggiunto il pannello **Audio di rete**: invia il mix RX lineare come PCM16
  little-endian a 48 kHz via UDP oppure come server TCP, in mono o stereo,
  compatibile con il Network Sink di SDR++. Il suo ring dedicato impedisce che
  un client lento blocchi il DSP o l'audio locale.

### Scheduler e gestore dei moduli IQ

- Nuova finestra **Strumenti → Scheduler e moduli IQ**.
- Lo scheduler persistente UTC può pianificare sintonia, scansione,
  avvio/arresto della registrazione IQ e avvio/arresto della registrazione
  audio. Non contiene PTT, TX, sintonia TX o riconnessione automatica.
- Le attività mantengono stato, parametri, risultato e log; al riavvio quelle
  scadute non vengono eseguite in ritardo. Un'azione che altererebbe la
  ricezione durante TX o con sorgente disconnessa viene segnata come fallita.
- Il catalogo dei moduli IQ ora è persistente e non esegue librerie durante la
  discovery. È possibile aggiungere cartelle/file, aggiornare il catalogo,
  abilitare/disabilitare una libreria, scaricarla realmente dal DSP e leggere
  il motivo degli errori ABI o di caricamento. Solo i moduli già abilitati
  vengono ricaricati al successivo avvio.

### Pacchetti distribuiti e affidabilità dei driver

- I pacchetti includono un baseline verificato di moduli **SoapySDR RTL-SDR**
  e **HackRF** con le relative dipendenze runtime. L'app cerca prima i moduli
  inclusi, quindi non richiede Homebrew, MSYS2 o un'installazione Soapy di
  sviluppo per questi ricevitori.
- Il bundle macOS raccoglie ricorsivamente i moduli Soapy e le dipendenze,
  riscrive i link Mach-O e include `rigctld`/Hamlib. Windows include il runtime
  ufficiale Hamlib 4.7.2, bloccato con SHA-256; Linux e Windows usano il
  medesimo manifest CMake e controlli di completezza del pacchetto.
- La CI produce artefatti nativi per **Windows x64**, **macOS arm64**,
  **macOS x86-64**, **Linux x86-64** e **Linux ARM64**. L'AppImage ARM64 non
  include `colibrinano_lib`, perché il produttore non fornisce una libreria
  Linux ARM64 compatibile; gli altri backend restano disponibili.

### Interfaccia, strumenti e verifiche

- Migliorati pannelli di sorgente, RTL-SDR, S-meter/Decometer e frequenza:
  le impostazioni operative e la calibrazione del meter vengono conservate,
  il comando di reset ripristina la calibrazione, e le capability hardware
  guidano i controlli mostrati.
- Estesa la diagnostica di backend, CAT, sicurezza TX, rete, DSP e pacchetti
  per rendere distinguibili un'impostazione non supportata, una radio non
  raggiungibile e un reale errore di ricezione.
- Aggiunti test per protocollo SDR++ Server, IQ raw TCP/UDP, audio UDP,
  configurazione seriale Hamlib, sicurezza TX RTL+CAT, piano di sintonia
  RTL-SDR, catalogo moduli, scheduler, sessione e pannelli QML. La suite della
  release esegue 49 test CTest.

### Artefatti della release

- Codice sorgente: gli archivi **Source code (zip)** e **Source code (tar.gz)**
  sono generati da GitHub dal tag `1.2.5`.
- Windows x64: installatore NSIS, installatore Inno Setup e ZIP portable.
- macOS: DMG separati per Apple Silicon e Intel.
- Linux: AppImage separati per x86-64 e ARM64.

> I DMG sono firmati ad-hoc per la coerenza interna del bundle ma non sono
> firmati con Developer ID né notarizzati. Gatekeeper può quindi chiedere una
> conferma esplicita al primo avvio.

---

## English (United Kingdom)

DECODIUM SDR 1.2.5 closes three areas left open in 1.2.4: CAT-linked IF
panadapter operation, SDR++-compatible IQ/audio networking, and proper
operational management of scheduled work and IQ modules. It is also the first
release with native packages for Windows x64, macOS Apple Silicon and Intel,
Linux x86-64, and Linux ARM64.

### IF panadapter, RTL-SDR and universal CAT

- Added the **RTL-SDR + CAT (IF panadapter)** backend: the radio's fixed IF
  output is captured by the RTL-SDR while the radio VFO remains the
  authoritative tuning reference.
- The hardware frequency is calculated as **IF + USB/LSB shift**, without the
  usual DC-avoidance offset when using a fixed IF. Carrier, spectrum and VFO
  therefore align with SDR++ panadapter operation and no second shift is
  applied in the DSP.
- CAT frequency and mode synchronisation is bidirectional and loop-safe:
  changing the application VFO updates the radio, while radio polling updates
  the application without the two sides chasing one another.
- TX protection is now *fail closed*: a dedicated precise PTT poll immediately
  suspends RTL-SDR IQ input when the radio is transmitting, when PTT state
  cannot be proven, or when CAT is lost. Reception resumes only after a valid
  RX state is observed.
- The RTL-SDR panel persists direct sampling Q ADC, IF, sideband, USB/LSB
  shifts, spectrum inversion, gain, PPM, bias tee and offset tuning. It no
  longer sends needless zero PPM/offset commands that some RTL-SDR Blog V4
  devices reject even though tuning is healthy.
- Direct sampling now makes tuner bypass explicit and disables AGC/gain
  controls that have no effect on the Q ADC.

### Hamlib and local CAT profiles

- Added a **local Hamlib** driver: Decodium SDR starts `rigctld` for the
  selected USB/serial port, then uses the normal rigctl protocol. The model
  catalogue comes from the installed Hamlib rather than a small internal list,
  making all Hamlib-supported radios available, including the Kenwood TS-940S.
- The CAT dialogue exposes and persists the Hamlib model, baud rate, data bits,
  parity, stop bits, handshaking, DTR and RTS. Suggested values can be read
  from the defaults declared by Hamlib for the selected model.
- Declared-radio and CAT-panadapter profiles are persistent. Serial discovery
  and source switching no longer hold the UI while a CAT timeout is pending,
  and results from a cancelled discovery cannot leak into the next search.

### SDR++ and generic IQ network compatibility

- In addition to `rtl_tcp` and SpyServer, the network backend now supports
  declared **TCP**, **UDP** and **SDR++ Server** IQ endpoints. Raw IQ accepts
  `int8`, `int16`, `int32` and `float32`; SDR++ Server accepts `int8`,
  `int16` and `float32`.
- The SDR++ Server client explicitly negotiates sample rate, sample type,
  frequency and stream start. Compressed frames are rejected rather than being
  mistaken for IQ samples.
- Added the **Network Audio** panel: it sends the linear RX mix as 48 kHz
  PCM16 little-endian over UDP or as a TCP server, mono or stereo, compatible
  with SDR++ Network Sink. Its dedicated ring prevents a slow client from
  blocking either DSP or local audio.

### Scheduler and IQ module manager

- New **Tools → Scheduler and IQ modules** window.
- The persistent UTC scheduler can schedule tuning, scanning, IQ-recording
  start/stop and audio-recording start/stop. It deliberately contains no PTT,
  TX, TX tuning or automatic reconnection action.
- Jobs retain state, arguments, result and log; overdue work is not run late
  after a restart. An action that would alter reception during TX or while the
  source is disconnected is recorded as failed.
- The IQ-module catalogue is now persistent and never executes a library
  during discovery. Operators can add directories/files, refresh the
  catalogue, enable/disable a library, unload it from the DSP, and read ABI or
  load errors. Only previously enabled modules are reloaded at the next start.

### Distributed packages and driver reliability

- Packages include a verified **RTL-SDR** and **HackRF** SoapySDR baseline with
  their runtime dependencies. The application prioritises bundled modules, so
  Homebrew, MSYS2 or a development Soapy installation is not required for
  those receivers.
- The macOS bundle recursively collects Soapy modules and dependencies,
  rewrites Mach-O links, and contains `rigctld`/Hamlib. Windows includes the
  official Hamlib 4.7.2 runtime, pinned by SHA-256; Linux and Windows use the
  same CMake manifest and package-completeness checks.
- CI builds native **Windows x64**, **macOS arm64**, **macOS x86-64**,
  **Linux x86-64** and **Linux ARM64** artefacts. The ARM64 AppImage does not
  ship `colibrinano_lib`, because its publisher does not provide a compatible
  Linux ARM64 library; the other backends remain available.

### Interface, diagnostics and verification

- Improved source, RTL-SDR, S-meter/Decometer and frequency panels: operating
  settings and meter calibration are preserved, reset restores calibration,
  and presented controls follow actual hardware capabilities.
- Extended backend, CAT, TX-safety, network, DSP and package diagnostics so an
  unsupported setting, an unreachable radio and a genuine reception problem
  can be distinguished.
- Added tests for SDR++ Server protocol, raw TCP/UDP IQ, UDP audio, Hamlib
  serial configuration, RTL+CAT TX safety, RTL-SDR tuning plan, module
  catalogue, scheduler, session and QML panels. The release suite runs 49
  CTest tests.

### Release artefacts

- Source: GitHub generates the **Source code (zip)** and **Source code
  (tar.gz)** archives from tag `1.2.5`.
- Windows x64: NSIS installer, Inno Setup installer and portable ZIP.
- macOS: separate DMGs for Apple Silicon and Intel.
- Linux: separate AppImages for x86-64 and ARM64.

> The DMGs are ad-hoc signed for internal bundle consistency but are not
> Developer ID signed or notarised. Gatekeeper may therefore require an
> explicit confirmation on first launch.
