# ROADMAP

L'ordine delle fasi risponde a una scelta precisa: dare per prima cosa la
platea più ampia possibile (chiunque abbia una chiavetta da 30 €), così da
avere tester, issue e community prima ancora che arrivi l'hardware SDR One.
Flex, SunSDR e Kiwi vengono dopo perché quegli utenti hanno già client maturi:
DECODIUM SDR deve prima valere la pena per ciò che gli altri non hanno.

---

## Fase 0 — Fondamenta ✅

Scheletro, CMake, HAL con capability, backend **demo** completo, spettro e
waterfall su QRhi in QML, channel strip, DSP full-chain (SSB/CW/AM/SAM/FM),
tema DECODIUM.

**Criterio d'uscita:** l'app gira in CI sulle tre piattaforme, il demo mode è
pienamente operativo, la conformance suite è verde. **Raggiunto.**

## Fase 1 — Universale subito 🔄

Backend **soapy** e **nettcp** (rtl_tcp / SpyServer), multi-canale,
registrazione IQ, i18n a 14 lingue.

**Criterio d'uscita:** un RTL-SDR da 30 € e un Airspy funzionano
out-of-the-box; prime release pubbliche AppImage / DMG / ZIP.

| Voce | Stato |
|---|---|
| Backend nettcp — rtl_tcp | ✅ |
| Backend nettcp — SpyServer | ✅ riconoscimento automatico del protocollo |
| Backend soapy | ✅ capability lette dal driver, stream CF32 |
| Backend colibri (ColibriNANO) | ✅ verificato sul ricevitore vero, [documentato](docs/backends/colibri.md) |
| Multi-canale | ✅ dalla Fase 0 |
| Registrazione IQ | ✅ WAV float32 + sidecar JSON, RF64 oltre i 4 GB |
| Riproduzione da file IQ | ✅ backend `iqfile`: legge il nostro WAV/RF64 e anche PCM 16/8 bit |
| Macchina del tempo (riascolto della diretta) | ✅ nel motore, quindi per ogni backend: il registratore conserva, questa rimedia — un tetto di 96 MiB decide quanti secondi, la UI mostra quelli veri |
| Stadio neurale con RNNoise (ondata 2c, §8) | ✅ thread dedicato, ring gemello, costo misurato, interlock verso i decoder digitali. ONNX Runtime e DeepFilterNet3 restano da valutare |
| **Ondata 2b completa** ([DSDR-SPEC-003](docs/DSDR-SPEC-003-RX-Excellence.md)): EMNR §6, notch ancorati alla RF §5, APF e binaurale CW §7, SAM con banda laterale scelta e spia di aggancio §7 | ✅ |
| EMNR spettrale (ondata 2b, §6) | ✅ il predittore adattivo resta solo come notch automatico |
| **Ondata 2a completa** ([DSDR-SPEC-003](docs/DSDR-SPEC-003-RX-Excellence.md)): Overload Guard §3 con correzione automatica dal seam, NB1 §4, fondo di rumore e S/N §9, passband tuning §7 | ✅ |
| NB / NR / ANF / notch manuale | ✅ anticipati dalla Fase 2: DSP originale, spenti di fabbrica perché nessuno dei quattro è gratis. NB e ANF conformi a [DSDR-SPEC-003](docs/DSDR-SPEC-003-RX-Excellence.md) §4–5; restano da fare EMNR (§6), notch ancorati alla RF e Overload Guard (§3) |
| i18n — pipeline e selettore lingua | ✅ 14 lingue predisposte |
| i18n — traduzioni complete | 🔄 italiano (sorgente) e inglese; le altre 12 attendono revisori madrelingua |
| Pacchetti AppImage / DMG / ZIP | ✅ workflow di release su tag |

## Fase 2 — Radio vere

Backend **hpsdr** (P1 poi P2), TX, AGC-T completo, audio virtuale,
server TCI.

**Criterio d'uscita:** QSO completo con un Hermes-Lite 2; DECODIUM 4 decodifica
FT2 attraverso il canale IPC.

## Fase 3 — SDR One

Backend **dlink**, quattro canali coerenti, pannello QuadBeam, integrazione
DECOLINK.

**Criterio d'uscita:** SDR One pilotato end-to-end, QuadBeam operativo.

## Fase 4 — Server-DSP

Backend **flex**, **tci** (client), **kiwi** con KiwiBrowser.

**Criterio d'uscita:** le tre famiglie connesse e operative con i loro pannelli
nativi.

## Fase 5 — Coda lunga

Backend **hamlib**, raffinamenti, Flathub, feature dalla community.

**Criterio d'uscita:** backlog guidato dalle issue.

---

## Debito tecnico noto

| Voce | Impatto | Quando |
|---|---|---|
| Firma GPG dei commit non ancora attiva | CONSTITUTION §2 non applicata: manca una chiave sulla macchina di sviluppo | Fase 0 |
| Branch protection su `main` non attiva | CODEOWNERS indica i revisori ma la review non è ancora bloccante | Fase 1 |
| Lingua sorgente delle stringhe: italiano | **Decisione aperta.** Lo standard open source è l'inglese come sorgente, così chi traduce in lettone o giapponese non deve conoscere l'italiano. La migrazione è meccanica (~90 stringhe) e non tocca la pipeline | Fase 1 |
| Persistenza impostazioni (SettingsStore SQLite) | Le impostazioni non sopravvivono al riavvio | Fase 1 |
| Resampler frazionario per rate non multipli di 48 kHz | Alcuni device Soapy | Fase 1 |
| La conformance suite crasha a volte con il ColibriNANO collegato | Il segfault è dentro `colibrinano_lib.dll`, in un suo thread, dopo la chiusura del device: nessun frame del nostro codice nello stack. Non riproducibile a comando — tre esecuzioni successive sono pulite. In CI non si vede, perché senza hardware il backend viene saltato. Il primo sospetto è `finalize()`, che risolviamo dalla libreria e non chiamiamo mai | Fase 1 |
| **I test che usano `RtlTcpMockServer` crashano a intermittenza in CI** | Due occorrenze su piattaforme diverse: `tst_nettcp` su Windows e `tst_hal_conformance` su Linux. Stessa firma — il processo muore **senza produrre output**, quindi non è un'asserzione fallita ma un segfault, e il denominatore comune fra i due è il mock. Non riproducibile in locale: 44 esecuzioni di `tst_nettcp` pulite, 12 in sequenza e 32 in parallelo sotto contesa. Una lettura del mock non ha mostrato la race: `stop()` azzera già il membro prima di `abort()`, e `readyRead` su un socket in `deleteLater` trova `m_client` nullo. Serve una sessione dedicata, con ASan su un runner Linux — rilanciare non insegna più nulla, l'abbiamo già fatto tre volte | Fase 1 |
| i18n: i tredici `.ts` esistono ma le traduzioni sono vuote | Servono revisori madrelingua; le stringhe si riestraggono con `update_translations` | Fase 1 |
| Path CPU dello spettro (`DSDR_GPU_SPECTRUM=OFF`) non implementato | Hardware grafico antico | Fase 2 |
