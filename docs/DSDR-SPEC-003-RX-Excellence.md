# DECODIUM SDR — RX Excellence

**Documento:** DSDR-SPEC-003
**Titolo:** Catena di ricezione avanzata: NB, NR classico, NR neurale, notch, misura
**Versione:** 0.1 (bozza per revisione)
**Data:** 9 agosto 2026
**Autori:** Martino (IU8LMC), con Salvatore Raccampo (9H1SR)
**Riferimenti:** DSDR-SPEC-001 (architettura), repo `iu8lmc/decodium-sdr` (stato Fase 1), CONSTITUTION.md, DSDR1-ADD-003 (dinamica hardware)
**Obiettivo:** portare la ricezione di DECODIUM SDR al vertice della categoria, con un pacchetto che culmina nel **primo NR neurale nativo e completamente locale in un client SDR**.

---

## 1. Principi

1. **Prevenire prima di curare.** L'ordine degli stadi segue la fisica: gli impulsi si tolgono a banda larga prima che i filtri li allunghino; l'overload si evita prima che generi intermodulazione. Nessun NR recupera informazione distrutta a monte.
2. **Ogni stadio è misurato.** Ogni feature di questo documento nasce con il suo banco di misura sul backend demo (segnali a SNR noto): il guadagno dichiarato è un numero verificato in CI, non un aggettivo.
3. **Clean-room confermato.** Gli algoritmi classici (MMSE-STSA, LMS, blanking) si implementano dalla letteratura; i modelli neurali si integrano da progetti con licenza permissiva, vendorizzati secondo CONSTITUTION §3.
4. **Tutto locale.** Nessuno stadio richiede rete. L'inferenza neurale gira su CPU (ONNX Runtime); la GPU è un'accelerazione opzionale, mai un requisito.
5. **Percorso caldo intatto.** Tutti gli stadi rispettano CONSTITUTION §5: buffer pre-allocati in `prepare()`, zero allocazioni in `process()`, dati nei ring, signal solo come notifiche.

---

## 2. La catena RX completa (ordine normativo)

```
IQ dal ring del backend (fs device)
  │
  ├─[A] Overload Guard ────── osserva i picchi ADC, comanda att/gain (§3)
  │
  ▼
 DDC / NCO  (esistente)
  ▼
[B] Noise Blanker wideband ── impulsi cancellati PRIMA dei filtri (§4)
  ▼
 Decimazione multistadio (esistente)
  ▼
 per canale: Filtro passa-banda complesso (esistente)
  ▼
[C] Notch: ANF adattivo + notch manuali traccianti (§5)      [dominio IF]
  ▼
 Demodulatore (esistente; SAM potenziato §7)
  ▼
 AGC con AGC-T (esistente)
  ▼
[D] NR classico EMNR (§6)                                    [dominio audio]
  ▼
[E] NR neurale ONNX (§8) ── opzionale, escludibile a runtime
  ▼
[F] APF / binaurale CW (§7)
  ▼
 Resampler → AudioRouter (esistente)

Parallelo, sempre attivo: [G] Stima noise floor + S-meter calibrato (§9)
```

Regole d'ordine non negoziabili: **B prima della decimazione** (un impulso passato in un FIR stretto "suona" per millisecondi e diventa incancellabile); **C prima della demodulazione** (una portante tolta in IF non pompa l'AGC); **D prima di E** (il neurale lavora meglio su rumore già stazionarizzato); **E dopo l'AGC** (livelli stabili = inferenza stabile).

---

## 3. [A] Overload Guard — la feature che "sente" più di tutte

**Problema.** Sui device a 8 bit (RTL-SDR) e in generale su ogni ADC, il clipping genera intermodulazione su tutta la banda: la sensibilità percepita crolla senza che l'utente capisca perché. È il difetto n.1 dell'esperienza SDR entry-level.

**Specifica.**
- Il DspEngine osserva il picco assoluto del flusso IQ su finestre da 100 ms (contatore, non copia: costo ~zero nel percorso caldo già attraversato).
- Isteresi a due soglie: picco > −1 dBFS per 3 finestre consecutive → richiesta di −6 dB (attenuatore o gain, secondo `capabilities()`); picco < −12 dBFS per 30 s → richiesta di +3 dB, mai oltre il livello impostato dall'utente.
- Il comando passa dal seam (`setGain`/attenuatore via API backend); i backend che non hanno controllo di guadagno dichiarano la capability assente e la guardia diventa **solo indicatore**: spia "OVL" rossa sul channel strip e sul pan.
- Modalità: `Auto` (default sui device senza manopole fisiche) / `Solo avviso` / `Off`. Ogni intervento è loggato e visibile (toast discreto "Attenuazione −6 dB: ingresso in saturazione"): la guardia non deve mai sembrare un AGC fantasma.
- **Interazione con IqRecorder:** i cambi di guadagno vengono scritti nel sidecar JSON con timestamp, così una registrazione resta interpretabile.

**Misura di accettazione (demo backend, poi RTL-SDR reale):** two-tone forte + segnale debole; con guardia ON il segnale debole deve restare decodificabile dove con guardia OFF sparisce sotto i prodotti IM3. Test CI: la macchina a stati (soglie, isteresi, rispetto del limite utente) su vettori sintetici.

---

## 4. [B] Noise Blanker wideband

Due blanker complementari, entrambi **sul flusso IQ post-DDC, pre-decimazione**:

### 4.1 NB1 — temporale (impulsi isolati)
- Rilevatore: magnitudo istantanea confrontata con mediana mobile su finestra ~10 ms; soglia = mediana × fattore (default 4×, regolabile 2–8×).
- Azione: sostituzione dei campioni dell'impulso (± margine di 2 campioni) con **interpolazione lineare complessa** tra i campioni sani adiacenti (non azzeramento: lo zero è esso stesso un gradino che suona).
- Durata massima blanking: 500 µs per evento; oltre, l'evento non è un impulso e il blanker si ritira (protezione contro l'auto-cancellazione di segnali forti, il difetto storico dei NB aggressivi in presenza di stazioni vicine).

### 4.2 NB2 — spettrale (impulsi ripetitivi: PLC, inverter, recinti)
- FFT corta (256 punti) sul flusso wideband, rilevazione delle righe impulsive periodiche per statistica inter-frame, sottrazione mirata.
- Fase 2b: attivabile solo dopo che NB1 è consolidato; condivide il worker FFT dello SpectrumAnalyzer per non aggiungere un thread.

**Misura:** treno di impulsi sintetici (10–200 Hz di ripetizione, ampiezza +30 dB sul rumore) sopra un segnale SSB a SNR 6 dB → il punteggio di intelligibilità proxy (SNR audio misurato in banda) deve migliorare ≥ 10 dB con NB1 ON; nessun degrado misurabile (< 0,1 dB) su banda pulita con NB1 ON (test di innocuità, obbligatorio in CI).

---

## 5. [C] Notch: ANF + notch manuali traccianti

- **ANF:** filtro adattivo LMS in dominio IF complesso, ordine 64 di default, convergenza < 200 ms su portante singola; sopprime fino a 4 toni simultanei. Escluso automaticamente in modo CW (l'ANF mangerebbe il segnale desiderato: interlock di modo, non fiducia nell'utente).
- **Notch manuali:** fino a 8 per canale, larghezza 25–1000 Hz, **ancorati alla frequenza RF assoluta** (non all'offset audio): restano sul disturbo quando l'utente si sintonizza altrove. Persistiti nel SettingsStore per-banda.
- UI: click destro sullo spettro → "Notch qui"; i notch visibili come tacche sul pan.

**Misura:** portante a +40 dB sul segnale, dentro il passa-banda → attenuazione ≥ 45 dB della portante, degrado del segnale utile < 0,5 dB, tempo di convergenza verificato su vettori.

---

## 6. [D] NR classico — EMNR (famiglia MMSE-STSA)

Lo stato dell'arte non-neurale, implementazione originale dalla letteratura (Ephraim & Malah 1984/85; Cohen 2002 per la stima del rumore MCRA e la probabilità di presenza del parlato):

- STFT a 48 kHz, finestra 512 con overlap 50 %, Hann, sintesi WOLA.
- Stima del rumore: minima statistica ricorsiva (MCRA) — insegue il rumore anche quando cambia (QSB del noise floor serale).
- Guadagno per-bin: MMSE-STSA log-spectral con smoothing decision-directed; limite inferiore di guadagno regolabile (il "quanto" di NR esposto all'utente come slider unico 0–10, che mappa il floor da 0 a −25 dB — un solo controllo, non dieci).
- Modalità **CW dedicata:** finestra più lunga (1024), floor più profondo — il CW non ha struttura di parlato e tollera aggressività maggiore.
- Latenza aggiunta: ~11 ms (una finestra) — dentro il budget RNF-03.

**Misura:** SSB sintetico a SNR 0/5/10 dB su rumore bianco e su rumore impulsivo residuo → guadagno SNR segmentale ≥ 8 dB a SNR 5 senza artefatti musicali oltre soglia (metrica: varianza spettrale del residuo). Confronto A/B registrato nel report.

---

## 7. Rifiniture di demodulazione

- **SAM potenziato:** PLL con range di cattura ±500 Hz e banda commutabile, selezione banda laterale (DSB/LSB/USB sincrono) — l'arma contro l'interferenza adiacente in AM; indicatore di lock sul channel strip.
- **APF CW:** risuonatore a picco regolabile (Q 5–50) centrato sul pitch CW impostato; attivabile con un tasto.
- **Binaurale CW:** il canale ruotato in fase tra L e R in funzione dell'offset dal centro filtro — la "spazializzazione" che fa emergere il segnale nel pile-up. Costo: una rotazione complessa per campione.
- **Passband tuning / IF shift** sul filtro di canale esistente (i coefficienti si rigenerano già: manca solo il controllo).

---

## 8. [E] NR neurale — "Decodium NR"

### 8.1 Integrazione (Fase 2)
- Runtime: **ONNX Runtime** (MIT) vendorizzato o come dipendenza di sistema, CPU con thread dedicato a priorità normale (mai il thread DSP: lo stadio consuma dal ring audio e ripubblica su un ring gemello con una finestra di latenza dichiarata).
- Modelli di serie: **DeepFilterNet3** (MIT/Apache-2.0) come primario; **RNNoise** (BSD-3) come fallback per hardware minimo (CM5, vecchi laptop). Entrambi operano a 48 kHz sull'audio demodulato — il punto d'inserzione [E] della catena.
- Il modello è un file: `~/.local/share/decodium-sdr/models/*.onnx`, selezionabile dalla UI. Aggiornare il modello non richiede una release del software.
- Budget dichiarati (da verificare su ferro): DFN3 ~15–25 % di un core moderno per canale; RNNoise < 5 %. Il pannello mostra il costo CPU misurato dello stadio, e lo stadio si disattiva da solo con log se il consumer non regge il real-time (mai audio a scatti silenziosi).
- Se lo stadio neurale è ON, EMNR passa automaticamente in modalità leggera (floor −10 dB): i due stadi cooperano, non competono.

### 8.2 Il modello nostro (Fase 2b–3): fine-tuning su HF vera
L'asso che nessun concorrente ha: il **corpus**.

- **Raccolta:** estensione di IqRecorder — un flag "dona questa registrazione al progetto Decodium NR" che esporta lo spezzone audio/IQ con il sidecar (banda, modo, device) e lo carica su un endpoint di ft2.it. Opt-in esplicito, licenza di conferimento CC0/CC-BY dichiarata nella finestra di dono, niente callsign nei metadati se l'utente non vuole.
- **Composizione target:** ≥ 50 h di SSB multilingue (la community a 14 lingue è il moltiplicatore), ≥ 20 h CW, ≥ 20 h di solo rumore (i "silenzi" HF: PLC, solari, QRN tropicale, riga a 50 Hz) — il rumore vero è la parte che i modelli generici non hanno mai visto.
- **Training:** fine-tuning di DeepFilterNet3 con coppie (pulito sintetico o registrato in locale, sporco = pulito + rumore corpus a SNR variabile). Hardware: la RTX 3060 Ti basta per il fine-tuning; le 13× RTX 3080 accorciano gli esperimenti.
- **Rilascio:** modello "Decodium NR v1" pubblicato con dataset card (composizione, licenze, limiti) — trasparenza totale, coerente con la linea del progetto.

### 8.3 Confine di applicazione
Lo stadio neurale è per **l'orecchio umano** (SSB/CW/AM). Non va mai inserito nel path IQ verso DECODIUM 4/WSJT-X: i decoder weak-signal vogliono il canale lineare, e un denoiser a valle *distrugge* le statistiche soft su cui lavora l'LDPC. Il router audio deve rendere impossibile instradare l'uscita [E] verso il canale digitale (interlock, non convenzione).

---

## 9. [G] Misura onesta

- **S-meter calibrato:** offset di calibrazione per-device nel SettingsStore (`dBFS→dBm`), procedura guidata con generatore o segnale noto; default dichiarati per i device noti (Colibri, RTL-SDR con dongle tipico). S-unit IARU (S9 = −73 dBm su HF).
- **Stima continua del noise floor** per canale (minima statistica, condivisa con MCRA di §6) → mostrata sul pan come linea sottile; abilita lo "SNR del segnale selezionato" in dB reali sul channel strip — il numero che trasforma le impressioni in confronti.
- **Registro delle condizioni:** noise floor per banda registrato nel tempo (SQLite) → mini-grafico "com'è messa la banda oggi vs ieri". Feature piccola, amatissima, che nessun client ha.

---

## 10. Fasi e priorità

| Ondata | Contenuto | Perché in quest'ordine |
|---|---|---|
| **2a** | [A] Overload Guard · [B] NB1 · [G] S-meter/floor · passband tuning | Massimo beneficio percepito, costo contenuto, zero dipendenze nuove |
| **2b** | [D] EMNR · [C] ANF+notch · [F] APF/binaurale · SAM potenziato | Il pacchetto "Thetis-class"; EMNR e MCRA condividono la stima rumore con [G] |
| **2c** | [E] neurale con DFN3/RNNoise di serie · avvio raccolta corpus | La feature-titolo della release di Fase 2 |
| **3** | NB2 spettrale · modello "Decodium NR v1" fine-tuned · cancellazione a antenna di riferimento (2 ch coerenti) · QuadBeam | Le armi architetturali |

Criterio d'uscita della Fase 2 (aggiorna ROADMAP): tutti gli stadi 2a–2c misurati sul banco demo con i numeri di questo documento in CI; su ferro, RTL-SDR + Colibri con confronto A/B registrato e pubblicato.

---

## 11. Licenze e vendoring

| Componente | Licenza | Uso |
|---|---|---|
| ONNX Runtime | MIT | runtime inferenza |
| DeepFilterNet3 (pesi+codice riferimento) | MIT / Apache-2.0 | modello primario |
| RNNoise | BSD-3-Clause | fallback |
| EMNR / MCRA / LMS / blanking | letteratura (Ephraim-Malah, Cohen, Widrow) | implementazione originale |

Tutto GPL-3.0-compatibile; ogni vendoring in `third_party/` con voce in THIRD_PARTY_LICENSES (la CI già lo verifica). WDSP resta riferimento di *studio* — nessun codice.

## 12. Questioni aperte

1. Endpoint e informativa del programma di donazione corpus su ft2.it (privacy, licenza di conferimento, moderazione).
2. NB1 e device server-DSP (Flex/TCI/Kiwi): non applicabile — la catena [A][B] esiste solo per la classe raw-IQ; documentare la differenza nella matrice capability.
3. Costo ONNX Runtime nel pacchetto (dimensione AppImage/DMG): valutare build minimale o download on-demand del runtime col primo uso dello stadio [E].
4. Nome pubblico dello stadio neurale: "Decodium NR" (proposta) — decidere prima dei materiali di release.

---

*Fine documento — DSDR-SPEC-003 v0.1. I numeri di accettazione dei §3–6 diventano test `tests/dsp/tst_rx_excellence.cpp` man mano che gli stadi entrano.*
