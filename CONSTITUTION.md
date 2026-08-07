# CONSTITUTION — DECODIUM SDR

Principi non negoziabili del progetto. Valgono per ogni contributore, umano o
agente. Una PR che viola uno di questi punti è rotta anche se compila e anche
se i test passano.

---

## 1. Merge gate

Ogni commit passa dal merge gate: CI verde su Linux, macOS e Windows **più**
review umana. Nessuna eccezione.

## 2. Commit firmati

I commit sono firmati GPG. La storia dei commit è la prova della natura
clean-room del progetto: deve restare verificabile.

## 3. Nessun codice copiato

Nessuna riga proviene da repository terzi. Il vendoring va in `third_party/`,
con licenza verificata compatibile GPL-3.0 e una voce in
`THIRD_PARTY_LICENSES`. Studiare un progetto altrui è lecito; copiarne il
codice no.

> WDSP e `wcpAGC.c` restano materiale di **sola lettura** finché la questione
> di compatibilità GPLv2/v3 non è risolta. L'AGC di DECODIUM SDR è
> un'implementazione originale: l'algoritmo non è copyrightabile, il codice sì.

## 4. Il seam HAL è inviolabile

Nessun `#include` di un backend concreto sopra la HAL. La conformance suite
(`tests/hal/`) è bloccante: un backend nuovo entra solo passandola tutta.

Verificabile meccanicamente:

```sh
grep -rn "hal/backends/" src/core src/audio src/app   # deve restare vuoto
```

## 5. Il percorso caldo non alloca, non locka, non emette signal con dati

Il DSP lavora su buffer pre-allocati e ring lock-free SPSC. I signal Qt
segnalano *disponibilità*; i campioni viaggiano nei ring. Un signal con un
contenitore di campioni nel payload è un difetto, non una scelta di stile.

## 6. UI solo QML

Zero QWidget nel prodotto. Ogni componente usa il singleton `Theme`: nessun
colore hardcoded, mai.

## 7. Le capability guidano la UI

Vietati i branch sul tipo di backend fuori dalla HAL. Se `tx == None` il
pulsante PTT **non esiste** — non è disabilitato. Se serve un ramo nuovo,
serve una capability nuova, non un `if (backendId == ...)`.

Verificabile meccanicamente:

```sh
grep -rn 'backendId *===\?' src/app/qml   # deve restare vuoto
```

## 8. Ogni feature nasce con il suo test

Ogni bug fix nasce con il test che lo riproduce. Un test scritto dopo la
correzione, che non è mai stato visto fallire, non conta come tale.

## 9. Demo mode sempre funzionante

Se una PR rompe il backend demo, la PR è rotta. È l'unico backend che gira
ovunque, ed è il banco su cui poggiano gli integration test in CI.

## 10. Documentazione backend obbligatoria

`docs/backends/<nome>.md` esiste **prima** del merge del backend: protocollo,
limiti noti, capability dichiarate e perché.

---

## Come si estende questo documento

Aggiungere un principio richiede la stessa cura di cambiare l'architettura:
proposta scritta, discussione, e accordo esplicito di Martino (IU8LMC) e
Salvatore (9H1SR). Rimuoverne uno richiede di spiegare cosa è cambiato nel
mondo, non cosa è scomodo oggi.
