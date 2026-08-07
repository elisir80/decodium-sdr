# DECODIUM SDR

**Il client SDR universale dell'ecosistema DECODIUM.**

Un'applicazione nativa cross-platform (Linux x86-64/aarch64, macOS, Windows)
scritta in C++17 e QML puro con Qt 6.8 LTS, capace di pilotare qualsiasi
hardware SDR — da una chiavetta RTL-SDR da 30 € fino al DECODIUM SDR One a
quattro canali coerenti — con un'unica interfaccia e un'unica esperienza.

> Stato: **Fase 0 — Fondamenta.** Il backend demo è completo e l'applicazione
> è pienamente operativa senza hardware. Vedi [ROADMAP.md](ROADMAP.md).

---

## Cosa c'è già

| Componente | Stato |
|---|---|
| Seam HAL `IRadioBackend` + descrittore di capability | ✅ |
| Backend **demo**: banda sintetica con CW, SSB, AM, digitale, QSB | ✅ |
| DSP engine: DDC, decimazione multistadio, filtri complessi, demod, AGC con AGC-T | ✅ |
| Spettro e waterfall su GPU (`QQuickRhiItem`, shader `.qsb`) | ✅ |
| Multi-canale RX con flag VFO colorati e channel strip | ✅ |
| Uscita audio a bassa latenza | ✅ |
| Conformance suite HAL + unit test DSP + integration test headless | ✅ |

I backend per hardware reale (SoapySDR, rtl_tcp, HPSDR, DLINK, FlexRadio, TCI,
KiwiSDR) arrivano nelle fasi successive: il seam è già lì ad aspettarli.

## Compilare

Servono Qt ≥ 6.8 (Core, Gui, Qml, Quick, QuickControls2, Multimedia,
ShaderTools, Test, QuickTest), CMake ≥ 3.24, Ninja, un compilatore C++17 e
FFTW3 in singola precisione.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
ctest --test-dir build --output-on-failure
```

Su MSYS2 (Windows):

```sh
pacman -S mingw-w64-x86_64-{qt6,cmake,ninja,gcc,fftw}
```

### Opzioni di build

Ogni backend si accende e si spegne a build-time (RNF-06):

```sh
cmake -S . -B build -DDSDR_BACKEND_SOAPY=ON -DDSDR_GPU_SPECTRUM=ON
```

| Opzione | Default | Effetto |
|---|---|---|
| `DSDR_BACKEND_DEMO` | ON | Backend sintetico; necessario per i test |
| `DSDR_BACKEND_SOAPY` … `DSDR_BACKEND_HAMLIB` | OFF | Backend hardware, fase per fase |
| `DSDR_GPU_SPECTRUM` | ON | Spettro su GPU; OFF compila il path CPU |
| `DSDR_BUILD_TESTS` | ON | Suite di test |
| `DSDR_WARNINGS_AS_ERRORS` | OFF | `-Werror` |

## Eseguire

```sh
./build/bin/decodium-sdr                    # scelta interattiva della sorgente
./build/bin/decodium-sdr --auto-connect     # va in onda sul primo device trovato
./build/bin/decodium-sdr --backend demo     # forza un backend
```

Opzioni diagnostiche: `--no-panadapter` stacca il rendering GPU dalla sorgente,
utile per separare i costi fra DSP e scene graph.

### Comandi rapidi nell'interfaccia

- **Clic sullo spettro** — sposta il canale selezionato su quella frequenza
- **Doppio clic** — crea un nuovo canale lì
- **Rotellina** — sintonia fine (100 Hz; `Shift` 10 Hz, `Ctrl` 1 kHz)
- **Trascinamento del flag VFO** — sintonia continua
- **Tasto destro sul flag** — chiude il canale

## Architettura in due righe

Sopra la HAL nessuno sa che radio ci sia sotto. La UI si genera dal descrittore
di capability, non da condizionali sul modello. I campioni non attraversano mai
i signal Qt: viaggiano su ring buffer lock-free, e i signal ne annunciano solo
la disponibilità.

Documenti: [architettura](docs/architecture.md) ·
[threading del DSP](docs/dsp-threading.md) ·
[principi non negoziabili](CONSTITUTION.md)

## Licenza

GPL-3.0-or-later. Vedi [LICENSE](LICENSE) e
[THIRD_PARTY_LICENSES](THIRD_PARTY_LICENSES).

## Ringraziamenti

L'architettura è **clean-room**: ispirata alle buone pratiche osservate nel
panorama open source, ma con ogni riga di codice originale.

- **AetherSDR** — l'idea del seam di backend e del workflow assistito da AI
- **piHPSDR / linHPSDR** — comprensione del protocollo OpenHPSDR
- **WDSP** (Warren Pratt) — riferimento *teorico* per l'AGC; nessun codice riusato
- **Lyons**, *Understanding Digital Signal Processing* — base del progetto dei filtri

FlexRadio, SunSDR/ExpertSDR, KiwiSDR, Airspy e gli altri marchi citati
appartengono ai rispettivi titolari e sono nominati solo a fini di
interoperabilità. Questo progetto non è affiliato con nessuno di essi.

## Autori

Martino (IU8LMC) e Salvatore Raccampo (9H1SR).
