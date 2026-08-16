# API moduli IQ

Decodium espone un ABI C minimale per aggiungere decoder o strumenti esterni
senza dipendere dagli header Qt o dalle classi interne del DSP.

L'header è [IqModuleApi.h](../src/core/IqModuleApi.h) e viene installato in
`include/decodium/`. Una libreria deve esportare:

```c
dsdr_iq_module_v1 *dsdr_create_iq_module_v1(void);
```

La struttura restituita contiene nome, versione ABI, distruttore opzionale e
`process_iq`. Il callback riceve coppie `float I/Q`, il rate del baseband
filtrato, l'ID del canale, il centro RF e l'offset del VFO.

Il callback viene eseguito nel thread DSP: non deve allocare memoria, bloccare
su I/O o conservare il puntatore al buffer dopo il ritorno. Il modulo si carica
all'avvio con:

```sh
decodium-sdr --iq-module /percorso/modulo.dylib
```

Lo stesso parametro vale per `.so` e `.dll` sulle altre piattaforme. Anche
senza parametro, Decodium cerca moduli nel bundle macOS
`Contents/PlugIns/DecodiumSdr` e nella cartella utente
`QStandardPaths::AppLocalDataLocation/modules`.

## Gestore moduli

La ricerca non carica le librerie: la scansione riempie il catalogo con
percorso, provenienza, presenza su disco, stato e ultimo errore, ma non esegue
codice esterno. I moduli scoperti per la prima volta sono disabilitati. Quelli
che l'operatore ha abilitato in precedenza vengono invece ricaricati all'avvio;
`--iq-module` registra, abilita e carica esplicitamente il percorso passato.

Dal menu **Strumenti → Scheduler e moduli IQ** è possibile:

- aggiungere cartelle o un file di modulo al catalogo;
- aggiornare la discovery senza riavviare;
- abilitare/disabilitare una singola libreria, con unload reale sul thread DSP;
- leggere gli stati `ready`, `active`, `disabled` ed `error` e il motivo del
  rifiuto ABI/caricamento;
- dimenticare un file aggiunto manualmente, senza cancellarlo dal disco.

Il contratto ABI v1 non espone metadati prima del caricamento: fino
all'attivazione il nome mostrato è quello del file, dopo è il `name` dichiarato
dal modulo. Le librerie native non sono isolate in un processo separato:
abilitare un modulo equivale a eseguire codice locale e va fatto solo con file
attendibili. Il callback resta nel thread DSP e deve quindi rispettare il
vincolo originario di non allocare, bloccare o fare I/O.

La suite di integrazione carica un modulo reale, verifica il callback sul
baseband e controlla l'abilitazione/disabilitazione individuale.
