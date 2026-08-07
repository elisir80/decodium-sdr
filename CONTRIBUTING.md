# Contribuire a DECODIUM SDR

Grazie per l'interesse. Prima di scrivere codice, leggi
[CONSTITUTION.md](CONSTITUTION.md): sono dieci punti, si legge in tre minuti, e
descrive i vincoli che una PR non può violare.

## Preparare l'ambiente

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
ctest --test-dir build --output-on-failure
```

Dipendenze: Qt ≥ 6.8, CMake ≥ 3.24, Ninja, compilatore C++17, FFTW3 in singola
precisione. Su MSYS2: `pacman -S mingw-w64-x86_64-{qt6,cmake,ninja,gcc,fftw}`.

## Il minimo che chiediamo a una PR

1. **Un test.** Ogni feature nasce con il suo; ogni bug fix nasce con il test
   che lo riproduce — scritto prima della correzione e visto fallire.
2. **CI verde** su Linux, macOS e Windows.
3. **Commit firmato** GPG.
4. **Nessun codice copiato** da altri progetti. Studiarli sì, copiarli mai.
5. Se tocchi la UI, **avvia l'applicazione** prima di dichiarare finito:
   `./build/bin/decodium-sdr --auto-connect`.

## Dove mettere le cose

| Se stai scrivendo… | va in… |
|---|---|
| un algoritmo di segnale | `src/dsp/` (niente Qt Gui: deve restare testabile headless) |
| il supporto a una radio | `src/hal/backends/<nome>/` |
| logica di sessione, modelli per la UI | `src/core/` |
| interfaccia | `src/app/qml/` (e `Theme` per ogni colore) |

## Aggiungere un backend

Il percorso completo è in [CLAUDE.md](CLAUDE.md). In sintesi: sottoclasse di
`IRadioBackend`, opzione `DSDR_BACKEND_<NOME>`, registrazione nel registro
dentro il proprio `#ifdef`, capability dichiarate con onestà, e
`docs/backends/<nome>.md` prima del merge.

La conformance suite gira automaticamente sul backend nuovo: è data-driven sui
backend registrati. Se non passa, il problema è nel backend, non nel test.

## Tradurre l'interfaccia

Le stringhe sorgente sono **in italiano**; l'inglese è la prima traduzione. I
file stanno in `src/app/translations/decodium_sdr_<lingua>.ts` e si aprono con
Qt Linguist.

```sh
cmake --build build --target update_translations   # riestrae le stringhe
# ... traduci con Qt Linguist ...
cmake --build build                                # ricompila i .qm
```

Una lingua compare nel selettore solo quando il suo `.qm` esiste: finché un
file `.ts` è vuoto, quella lingua resta nascosta invece di mostrare
un'interfaccia mezza tradotta.

Cerchiamo revisori madrelingua per tutte le lingue diverse da italiano e
inglese: preferiamo una lingua assente a una tradotta male.

## Stile

Segui il codice che hai intorno: stessa densità di commenti, stessi nomi,
stessi idiomi. I commenti spiegano *perché*, non *cosa* — il cosa lo dice già
il codice.

I messaggi di commit sono in italiano o in inglese, purché la prima riga dica
cosa cambia e il corpo dica perché.

## Segnalare un problema

Una issue utile contiene: cosa ti aspettavi, cosa è successo, il backend e il
device usati, e — se riguarda audio o prestazioni — l'output di
`QT_LOGGING_RULES="dsdr.*=true" ./build/bin/decodium-sdr`.

## Licenza dei contributi

Contribuendo accetti che il tuo lavoro sia distribuito sotto GPL-3.0-or-later.
