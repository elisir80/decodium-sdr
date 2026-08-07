# Threading del DSP

Documento normativo richiesto dalla spec §4.1. Descrive chi possiede cosa, chi
scrive e chi legge. Le regole qui dentro discendono da CONSTITUTION §5.

## I thread

| Thread | Chi lo crea | Cosa fa |
|---|---|---|
| **GUI** | `main()` | Event loop di Qt, modelli, QML |
| **Render** | scene graph di Qt | `PanadapterRenderer`: consuma le righe di spettro e disegna |
| **DSP** | `SessionManager` | `DspEngine`: DDC, decimazione, demod, AGC, FFT |
| **Ingest** | il backend | Riceve dai driver o dalla rete e scrive l'IQ nel ring |
| **Audio** | Qt Multimedia | Tira i campioni dal ring dell'audio |

## Il percorso dei campioni

```
[ingest]  backend ──write──▶ SpscRing<float> (IQ interleaved)
                                   │
                                   │ read
[DSP]                       DspEngine ──┬──▶ ChannelProcessor ──▶ SpscRing<float> (audio)
                                        │                                  │ read
                                        └──▶ SpectrumAnalyzer              │
                                                   │ publish        [audio] QAudioSink
                                                   ▼
                                        SpectrumFeed (ring di righe)
                                                   │ fetchRows
                                                   ▼
                                            [render] PanadapterRenderer
```

Ogni freccia è un **ring SPSC**: un solo produttore, un solo consumatore,
nessun lock. I signal Qt attraversano gli stessi confini, ma trasportano solo
descrittori (`IqFrame`, `AudioFrame`): sequenza, conteggi, timestamp.

## Perché i signal non portano i campioni

Un `QVector<float>` in un signal queued viene copiato, allocato e distrutto per
ogni blocco. A 200 blocchi al secondo per canale significa migliaia di
allocazioni al secondo nel percorso caldo, con latenze impredicibili quando
l'allocatore decide di prendere un lock. Il ring ha un costo costante e
nessuna allocazione dopo `reset()`.

Il descrittore serve comunque: `sequence` rivela le discontinuità e
`droppedFrames` gli overrun. Senza di essi un consumatore lento sembrerebbe
soltanto "un po' silenzioso".

## Regole per chi tocca questo codice

1. **Un ring, un produttore, un consumatore.** Due lettori sullo stesso ring
   si rubano i dati a vicenda. Se serve un secondo consumatore, serve un
   secondo ring (o un fan-out esplicito).
2. **Le allocazioni stanno in `configure()`.** `ChannelProcessor::configure()`,
   `DecimatorChain::configure()`, `SpectrumAnalyzer::configure()` sono gli
   unici punti in cui è lecito allocare.
3. **Il cambio di impostazioni non è nel percorso caldo**, ma avviene nel
   thread DSP: arriva come chiamata queued e viene applicato fra un blocco e
   l'altro. I coefficienti dei filtri si riscrivono in vettori con capacità già
   prenotata (`reserve(kMaxFirTaps)`), quindi senza riallocare.
4. **In caso di ritardo si scarta, non si accumula.** Un ring pieno significa
   che il consumatore non tiene il passo: si scartano i campioni più vecchi e
   si segnala l'overrun. Accumulare farebbe crescere la latenza senza limite.
5. **Niente segnalazioni ad alta frequenza verso la GUI.** I meter sono
   limitati a ~15 Hz e gli overrun a 2 Hz: ogni signal verso il thread GUI
   costa un attraversamento di thread e, a valle, un `dataChanged` che rilancia
   le animazioni dei delegate.

## Priorità dei thread

Il thread DSP gira a `QThread::HighPriority`, **non** `TimeCriticalPriority`.
Con priorità time-critical un thread DSP che satura una CPU affama il thread
della GUI: l'applicazione continua a produrre audio corretto ma smette di
rispondere, ed è un difetto difficile da diagnosticare perché somiglia a un
deadlock senza esserlo.

## Vincoli verificati dai test

- `tests/dsp/tst_dsp_primitives.cpp` — il ring non corrompe i dati sotto wrap e
  dichiara l'overrun invece di sovrascrivere silenziosamente.
- `tests/hal/tst_hal_conformance.cpp` — la sequenza dei frame è monotona e la
  chiusura durante lo streaming non lascia frame in volo.
- `tests/integration/tst_session_demo.cpp` — la catena completa produce spettro
  e audio a tempo reale contro il backend demo.
