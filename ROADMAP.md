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
| Backend colibri (ColibriNANO) | ✅ verificato sul ricevitore vero |
| Multi-canale | ✅ dalla Fase 0 |
| Registrazione IQ | ✅ WAV float32 + sidecar JSON, RF64 oltre i 4 GB |
| Riproduzione da file IQ | ⬜ arriverà come backend `iqfile`, dietro lo stesso seam |
| i18n — pipeline e selettore lingua | ✅ 14 lingue predisposte |
| i18n — traduzioni complete | 🔄 italiano (sorgente) e inglese; le altre 12 attendono revisori madrelingua |
| Pacchetti AppImage / DMG / ZIP | ✅ workflow di release su tag |

## Fase 2 — Radio vere

Backend **hpsdr** (P1 poi P2), TX, AGC-T completo, NR/NB/ANF, audio virtuale,
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
| Zoom e pan orizzontale del panadattatore | Lo shader ha già `uMin`/`uMax`: manca il controllo in UI | Fase 1 |
| i18n: le stringhe usano `qsTr()` ma mancano i file `.ts` | Nessuna traduzione disponibile | Fase 1 |
| Path CPU dello spettro (`DSDR_GPU_SPECTRUM=OFF`) non implementato | Hardware grafico antico | Fase 2 |
