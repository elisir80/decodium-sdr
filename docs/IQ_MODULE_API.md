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
`QStandardPaths::AppLocalDataLocation/modules`. I moduli scoperti vengono
registrati nel catalogo UI con nome, percorso e stato di caricamento. La suite
di integrazione carica un modulo reale e verifica che riceva frame IQ.
