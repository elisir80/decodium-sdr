# CLAUDE.md — guida per gli agenti

Leggi questo file **per primo**, poi [CONSTITUTION.md](CONSTITUTION.md). Se un
suggerimento di questo file contraddice la CONSTITUTION, vince la CONSTITUTION.

## Il progetto in una frase

Client SDR universale in C++17 + QML puro (Qt 6.8 LTS): ogni radio passa dallo
stesso seam `IRadioBackend`, la UI si genera dalle capability, il DSP è
originale e gira su thread dedicati con ring lock-free.

## Mappa del codice

```
src/common/   Types.h — il vocabolario condiviso (DemodMode, ChannelId, …)
src/dsp/      Motore DSP puro, senza Qt Gui: FIR, decimazione, demod, AGC, FFT
src/hal/      Il seam. backends/<nome>/ contiene i backend concreti
src/audio/    AudioRouter: sink Qt alimentato dal ring del DSP
src/core/     SessionManager, DspEngine, modelli per QML, SpectrumFeed
src/app/      Modulo QML DecodiumSdr + PanadapterView (QQuickRhiItem) + main
tests/        dsp/ · hal/ (conformance) · integration/ (headless) · qml/
```

## Regole che sbagliano più spesso gli agenti

1. **Non includere un backend sopra la HAL.** Il core parla con
   `IRadioBackend` e `BackendCapabilities`, mai con `DemoBackend`.
2. **Non mettere campioni nei signal Qt.** I frame (`IqFrame`, `AudioFrame`…)
   sono *descrittori*: sequenza, conteggio, timestamp. I campioni stanno nei
   ring `SpscRing<float>` esposti da `iqStream()` / `audioStream()`.
3. **Non allocare nel percorso caldo.** Tutti i buffer nascono in
   `configure()`. Se serve un vettore nuovo a runtime, il progetto è sbagliato.
4. **Non ramificare sul backend in QML.** `Session.capabilities.canTransmit`,
   non `Session.backendId === "demo"`.
5. **Non usare colori letterali in QML.** Esiste `Theme`.
6. **Attenzione ai `model` calcolati in JavaScript.** Un `model` che diventa
   `Infinity` o `NaN` fa istanziare delegate all'infinito e blocca il thread
   della UI senza alcun messaggio d'errore. È già successo: vedi
   `FrequencyGrid.qml` e il test che lo presidia.

## Flusso di lavoro

```sh
cmake -S . -B build -G Ninja && cmake --build build
ctest --test-dir build --output-on-failure
./build/bin/decodium-sdr --auto-connect      # verifica visiva
```

Prima di dichiarare finito un lavoro sulla UI: **avvia l'applicazione**. I test
non vedono un pannello che non si disegna. Se la finestra smette di rispondere,
il primo sospetto è un ciclo nel thread GUI — uno stack trace con `gdb -p`
sul thread 1 dice in pochi secondi se si è dentro il motore JavaScript.

## Aggiungere un backend

1. `src/hal/backends/<nome>/`, sottoclasse di `IRadioBackend`.
2. Opzione `DSDR_BACKEND_<NOME>` in `cmake/DsdrOptions.cmake` e blocco
   condizionale in `src/hal/CMakeLists.txt`.
3. Registrazione in `registerBuiltinBackends()`, dentro il suo `#ifdef`.
4. Dichiara le capability con onestà: meglio `false` che una promessa non
   mantenuta — la UI ci crede.
5. La conformance suite gira automaticamente sul backend nuovo: è
   data-driven sui backend registrati, non serve scrivere test in più.
6. `docs/backends/<nome>.md` prima del merge (CONSTITUTION §10).

## Cosa NON fare senza chiedere

- Cambiare la firma di `IRadioBackend`: tocca ogni backend presente e futuro.
- Introdurre una dipendenza nuova: va verificata la compatibilità GPL-3.0 e
  registrata in `THIRD_PARTY_LICENSES`.
- Copiare codice da altri progetti SDR. Mai, per nessun motivo.
