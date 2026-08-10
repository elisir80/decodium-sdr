# DECODIUM SDR — Stadio [E] "Decodium NR": specifica di implementazione

**Documento:** DSDR-IMPL-001
**Titolo:** Implementazione dello stadio NR neurale nel repo `iu8lmc/decodium-sdr`
**Versione:** 0.1 (bozza per revisione — pronta per il loop Claude Code)
**Data:** 10 agosto 2026
**Autori:** Martino (IU8LMC), con Salvatore Raccampo (9H1SR)
**Riferimenti:** DSDR-SPEC-003 §8 (requisiti), DSDR-SPEC-005 (programma corpus), CONSTITUTION §3/§5, `docs/dsp-threading.md`
**Upstream:** DeepFilterNet3 (Schröter et al., INTERSPEECH 2023, repo Rikorose/DeepFilterNet, MIT/Apache-2.0); RNNoise (Xiph, BSD-3)

---

## 1. Decisione di integrazione (aggiorna SPEC-003 §8.1)

Due motori dietro un'unica interfaccia; **per la v1 il motore DFN3 usa la C-API ufficiale `libdf`** (crate `deep-filter`, inferenza tract inclusa) invece di ONNX Runtime + pipeline di feature scritta a mano.

Razionale: la pre/post-elaborazione di DFN3 (banco ERB, STFT, deep filtering a due stadi, stati ricorrenti) deve combaciare *esattamente* con il training; reimplementarla in C++ è la strada maestra per bug sottili e derive di qualità. La C-API ufficiale espone `df_create(model, atten_lim)` / `df_process_frame()` su frame da 480 campioni @ 48 kHz: piccola, stabile, mantenuta dagli autori, licenza compatibile. ONNX Runtime resta la via futura se servirà (architetture diverse dal formato DF); l'interfaccia interna (§3) rende il cambio invisibile al resto del codice. I modelli fine-tuned del programma SPEC-005 si esportano con il tooling DF nello stesso formato tar del modello ufficiale: la promessa "il modello è un file sostituibile" è mantenuta.

RNNoise (C puro, frame 480 @ 48 kHz — stessa granularità, comodo) è il fallback bundled: presente nell'installer, funziona offline, costo < 5 % di un core.

---

## 2. Collocazione nel repo e build

```
src/dsp/neural/
├── INrEngine.h              // interfaccia motore (§3)
├── NeuralNrStage.{h,cpp}    // lo stadio: worker, ring, bypass, misure (§4)
├── DfnEngine.{h,cpp}        // wrapper C-API libdf
├── RnnoiseEngine.{h,cpp}    // wrapper rnnoise
└── ModelStore.{h,cpp}       // risoluzione/validazione file modello (§6)
third_party/
├── deep-filter/             // binari prebuilt per piattaforma + df.h + LICENSE (MIT/Apache)
└── rnnoise/                 // sorgente vendored + LICENSE (BSD-3)
tests/dsp/tst_neural_nr.cpp
docs/dsp-neural-nr.md        // obbligatorio prima del merge (CONSTITUTION §10)
```

- Opzione CMake: `DSDR_NR_NEURAL` (default ON). OFF esclude `src/dsp/neural/` e il pannello UI: build sempre verde senza dipendenze neurali (lezione del punto GPU/CPU: l'opzione che esiste deve funzionare).
- `deep-filter`: **binari prebuilt vendored** per linux-x86_64/aarch64, macos-arm64/x86_64, windows-x64 — niente toolchain Rust richiesta a chi compila; hash dei binari verificati a configure-time; provenienza e versione in THIRD_PARTY_LICENSES (il check CI esistente li vede).
- `rnnoise`: compilato dai sorgenti nel tree (è C, piccolo).

---

## 3. Interfaccia motore

```cpp
// src/dsp/neural/INrEngine.h
namespace dsdr::dsp::neural {

struct NrEngineInfo {
    QString id;              // "dfn3", "rnnoise"
    QString modelName;       // "DeepFilterNet3 (base)", "Decodium NR 2027.1", ...
    int     frameSamples;    // 480 per entrambi (10 ms @ 48 kHz)
    int     latencysamples;  // ritardo algoritmico dichiarato dal motore
};

class INrEngine {
public:
    virtual ~INrEngine() = default;
    // Init pesante (caricamento modello, allocazioni): SOLO qui. Ritorna false con motivo.
    virtual bool prepare(const QString& modelPath, QString* err) = 0;
    // Un frame esatto di frameSamples float mono 48 kHz, in place. Real-time safe:
    // nessuna allocazione, nessun lock, nessuna I/O dopo prepare().
    virtual void processFrame(float* samples) = 0;
    virtual void setAttenLimitDb(float db) = 0;   // 0 = bypass morbido … 100 = pieno
    virtual NrEngineInfo info() const = 0;
    virtual void reset() = 0;                     // azzera stati ricorrenti (cambio canale/modo)
};

} // namespace
```

Vincolo di accettazione per i wrapper: `processFrame` di `DfnEngine` chiama solo `df_process_frame*`; qualunque conversione di formato avviene su buffer pre-allocati in `prepare`. Verifica: il test §7.4 conta le allocazioni.

---

## 4. `NeuralNrStage`: threading e inserzione in catena

Punto di inserzione: **post-AGC, pre-resampler** (ordine normativo SPEC-003 §2), come stadio *asincrono disaccoppiato* — l'unico della catena con un thread proprio, perché l'inferenza non deve mai poter bloccare il thread DSP.

```
ChannelProcessor (thread DSP)
   └─ scrive audio post-AGC → ringIn (SPSC, pre-allocato, 250 ms)
NeuralNrStage worker (QThread dedicato, priorità normale)
   └─ legge blocchi da 480 → engine->processFrame → scrive → ringOut (SPSC)
AudioRouter
   └─ consuma da ringOut quando lo stadio è ENGAGED, da ringIn quando BYPASS
```

Regole:

1. **Mai buchi audio.** Macchina a stati `Bypass → Warmup → Engaged → Degraded`. In `Warmup` (dopo l'accensione) l'uscita resta il segnale non processato finché `ringOut` non ha riempito la latenza di regime: l'attacco è un crossfade di 20 ms tra dry e wet, non uno scatto.
2. **Auto-degrado onesto.** Se il worker non regge il real-time (occupancy di `ringIn` oltre soglia per > 500 ms), lo stadio passa a `Degraded`: crossfade verso il dry, evento sul log, badge giallo in UI ("CPU insufficiente per Decodium NR"). Riaggancio automatico con isteresi (30 s di margine). Mai audio a singhiozzo silenzioso.
3. **Latenza dichiarata, non scoperta:** `latencyMs()` = ritardo algoritmico del motore + profondità media dei ring; esposta a `MeterFrame` e mostrata nel tooltip dello switch. Budget di accettazione: ≤ 45 ms aggiunti con DFN3 (RNF-03 complessivo resta rispettato per l'ascolto; il path digitale non passa di qui per costruzione — §5).
4. **`reset()`** del motore a ogni cambio frequenza > passata o cambio modo: gli stati ricorrenti di un contesto non devono colorare il successivo.
5. Cambio modello/motore a caldo: si prepara un secondo engine in `Warmup` e si commuta col crossfade; il vecchio si distrugge fuori dal worker.

## 5. Interlock digitale (SPEC-003 §8.3) — enforcement

Nel grafo dell'`AudioRouter`, l'uscita di `NeuralNrStage` è un nodo con tag `EAR_ONLY`. Le destinazioni `Decodium4Ipc`, `VirtualAudioTx`, `RecorderIq` accettano solo nodi senza quel tag — **rifiuto a costruzione del grafo**, non a runtime, con messaggio esplicito. La registrazione audio (non IQ) può registrare il wet, ma il file riporta nel sidecar `"nr_neural": true` (una registrazione processata donata al corpus SPEC-005 va riconosciuta e scartata dalla quarantena). Test dedicato §7.5: il tentativo di rotta vietata deve fallire il build del grafo.

## 6. Modelli: risoluzione e gestione

- Directory: `<AppDataLocation>/models/` (QStandardPaths). `ModelStore` enumera, valida (dimensione, hash se noto, apertura di prova del motore) e espone il QAbstractListModel per la UI.
- **Bundled:** RNNoise (pesi integrati nel binario) + **modello DFN3 base incluso nel pacchetto** (pochi MB: dentro l'installer, decisione che chiude la questione aperta №3 della SPEC-003 — niente download obbligatorio al primo uso; il download on-demand resta per gli aggiornamenti stagionali).
- Il manifest remoto delle stagioni (`nr.ft2.it/models/manifest.json`) è **fase E3**: la v1 funziona interamente offline.

## 7. UI (QML) e test

### 7.1 Pannello
`src/app/qml/channel/NeuralNrControl.qml`, dentro il gruppo NR del channel strip: switch "Decodium NR", slider unico "Intensità" (mappa `atten_lim` 0–100 dB), selettore modello (da ModelStore), badge di stato (Warmup/Engaged/Degraded con i colori del Theme), tooltip con latenza e CPU misurate. Nessun colore hardcoded (il check CI vigila). Lo stadio appare solo se `DSDR_NR_NEURAL` è compilato — la UI dal descrittore, come sempre.

### 7.2 Persistenza
Stato switch, intensità e modello per-profilo nel SettingsStore (si aggancia al lavoro di persistenza già in corso per la Fase 1).

### 7.3–7.6 Test (`tst_neural_nr.cpp`)
1. **Identità in bypass:** stadio OFF → uscita bit-identica all'ingresso.
2. **Real-time safety:** contatore di allocazioni attorno a 10 s di `processFrame` (entrambi i motori): zero dopo `prepare`.
3. **Latenza:** dichiarata = misurata (impulso marcato) ± 1 frame.
4. **Efficacia (gate con modello presente):** SSB sintetico (dal generatore del banco demo) + rumore bianco a SNR 5 dB → SI-SNR out − in ≥ +6 dB con DFN3, ≥ +3 dB con RNNoise. In CI il job scarica il modello base con cache; se assente, il test si marca SKIP (mai verde-falso).
5. **Interlock:** rotta `NeuralNr → Decodium4Ipc` rifiutata a costruzione grafo.
6. **Degrado:** worker rallentato artificialmente → transizione a Degraded senza underrun sul consumer (contatore buchi = 0).

## 8. Fasi di consegna (issue per il loop)

| Issue | Contenuto | Dipende da |
|---|---|---|
| **E1** | `INrEngine` + `RnnoiseEngine` + `NeuralNrStage` con macchina a stati e test 1–3, 6 | — |
| **E2** | `DfnEngine` + vendoring prebuilt + modello base bundled + test 4 | E1 |
| **E3** | `ModelStore` UI completa + pannello QML + persistenza + interlock (test 5) + `docs/dsp-neural-nr.md` | E1 |
| **E4** | Manifest remoto stagioni + download/aggiornamento (post SPEC-005 P0 lato server) | E3, server |

E1 è volutamente RNNoise-first: valida tutto il telaio (stati, ring, degrado, test) con il motore più semplice; DFN3 arriva su un telaio già collaudato.

## 9. Questioni aperte

1. Versione esatta di `deep-filter` da vendorizzare e verifica che la C-API esponga il controllo `atten_lim` a runtime (in caso contrario: ricreazione engine al cambio slider, con crossfade §4.5 — accettabile ma da decidere).
2. macOS: firma/notarizzazione dei binari prebuilt dentro il bundle — verificare col workflow DMG esistente.
3. Stereo/binaurale CW: la v1 processa mono pre-binaurale; se lo stadio binaurale (SPEC-003 §7) arriva prima, definire l'ordine (proposta: NR sul mono, binaurale dopo — il NR non deve vedere due canali correlati).

---

*Fine documento — DSDR-IMPL-001 v0.1. Le issue E1–E4 sono pronte da aprire con questo documento come riferimento; E1 non ha dipendenze e può partire oggi.*
