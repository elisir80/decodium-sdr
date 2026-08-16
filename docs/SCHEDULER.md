# Scheduler operativo

Lo scheduler di Decodium SDR pianifica operazioni di **sola ricezione**. È un
oggetto del thread della sessione, separato dal DSP: non esegue timer, I/O o
operazioni di lifecycle nel percorso IQ/audio in tempo reale.

La UI è in **Strumenti → Scheduler e moduli IQ**. Tutte le scadenze sono in
UTC ISO-8601, per esempio `2026-08-16T18:30:00.000Z`. Un orario senza fuso
orario è rifiutato invece di essere interpretato con il fuso locale.

## Azioni disponibili

- `tune`: sintonizza il ricevitore alla frequenza richiesta;
- `scan`: avvia la scansione con inizio, fine, passo e sosta;
- `record-iq-start` / `record-iq-stop`: apre o chiude il registratore IQ;
- `record-audio-start` / `record-audio-stop`: apre o chiude il registratore
  WAV del mix RX.

Non esistono azioni PTT, TX, tune TX o riconnessione automatica. Se alla
scadenza la sorgente non è connessa, o la stazione sta trasmettendo, un'azione
che altera la ricezione o avvia una registrazione viene conclusa come
`failed`. Il motivo resta visibile nella coda e nel log `dsdr.scheduler`.

## Persistenza e stati

Le attività sono salvate in `QSettings` e mantengono id, parametri, orario,
stato e risultato. Gli stati sono `pending`, `running`, `completed`, `failed`,
`missed` e `cancelled`.

Alla chiusura una richiesta che era `running` diventa `missed`; al riavvio una
richiesta scaduta non viene mai eseguita in ritardo. In particolare, una
registrazione notturna persa non parte in modo inatteso alla prima apertura
dell'applicazione. La cronologia terminale può essere rimossa, mentre le
richieste in attesa possono essere disabilitate o annullate singolarmente. Una
richiesta disabilitata che supera comunque la propria ora diventa `missed` se
si prova a riattivarla, senza essere eseguita in ritardo.

`tst_operation_scheduler` copre la validazione delle azioni consentite, la
consegna una sola volta, il risultato e la persistenza delle attività in attesa.
`tst_session_scheduler` attraversa inoltre il confine scheduler → sessione →
backend demo e verifica che una sintonia pianificata venga davvero applicata.
