# AGENTS.md — workflow multi-agente

Estensione del loop già usato su DECODIUM SDR One. Un agente per area, issue
etichettate per eleggibilità, piani di implementazione in PR draft, umano come
CODEOWNER bloccante.

## Ruoli

| Agente | Area | Può scrivere in |
|---|---|---|
| `hal` | Seam, backend, conformance | `src/hal/**`, `tests/hal/**`, `docs/backends/**` |
| `dsp` | Motore DSP, prestazioni | `src/dsp/**`, `tests/dsp/**`, `docs/dsp-threading.md` |
| `qml` | UI, tema, interazione | `src/app/**`, `tests/qml/**` |
| `core` | Sessione, modelli, audio | `src/core/**`, `src/audio/**`, `tests/integration/**` |
| `packaging` | Build, CI, release | `cmake/**`, `packaging/**`, `.github/**` |

Un agente che ha bisogno di toccare l'area di un altro apre una issue, non un
commit: le aree si sovrappongono poco per costruzione, e quando si
sovrappongono è un segnale che l'architettura va discussa.

## Eleggibilità delle issue

Un'issue è affidabile a un agente quando ha:

1. un criterio di accettazione verificabile da un test;
2. un'area di appartenenza chiara fra quelle sopra;
3. nessuna decisione di prodotto aperta.

Le issue etichettate `needs-human-decision` non sono eleggibili, per quanto
piccole sembrino.

## Ciclo di lavoro

1. L'agente apre una **PR draft** con il piano di implementazione, prima del
   codice: file toccati, test previsti, capability nuove se ce ne sono.
2. Implementazione, con i test nello stesso commit della feature.
3. CI verde sulle tre piattaforme.
4. Review umana (Martino o Salvatore). È bloccante e non delegabile: è ciò che
   la CONSTITUTION §1 chiama merge gate.

## Cosa un agente non decide da solo

- La firma di `IRadioBackend`.
- L'aggiunta di una dipendenza esterna.
- Le capability dichiarate da un backend verso l'hardware reale: vanno
  verificate sul ferro, non dedotte dalla documentazione.
- Qualunque cosa tocchi la CONSTITUTION.

## Verifica prima di dichiarare finito

```sh
cmake --build build && ctest --test-dir build --output-on-failure
./build/bin/decodium-sdr --auto-connect     # per ogni lavoro sulla UI
grep -rn "hal/backends/" src/core src/audio src/app   # deve restare vuoto
```

Un lavoro sulla UI dichiarato finito senza aver mai avviato l'applicazione non
è finito: i test non vedono un pannello che non si disegna.
