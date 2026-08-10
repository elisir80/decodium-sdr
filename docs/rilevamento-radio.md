# Rilevamento delle radio in rete

`hal::RadioScout` risponde a una domanda sola: **quali radio sono
raggiungibili**. Anche quelle che DECODIUM SDR non sa ancora aprire.

Non è un backend e non ne diventerà uno. Non apre niente, non consegna
campioni, non ha capability. Le radio che trova compaiono in un elenco a parte
della finestra delle sorgenti, non fra i device: quello elenca ciò che si può
usare (CONSTITUTION §7), e mettere fra le sorgenti un FlexRadio che il
programma non apre sarebbe una promessa non mantenuta.

Serve perché senza, la domanda «l'ho collegata, perché non la vedo?» non ha
risposta. Con questo ne ha una precisa: *c'è, è a questo indirizzo, questo
firmware — e questa versione non la apre ancora*. Distingue il problema del
programma da quello del cavo, del firewall o della radio spenta.

## Che cosa trova, e come

### OpenHPSDR — Hermes-Lite 2, Hermes, Angelia, Orion, Metis

Si **chiede**. Un pacchetto di 63 byte in broadcast sulla porta 1024: due byte
di sincronismo (`EF FE`), il comando `02`, e zeri. Chi risponde dice chi è —
indirizzo MAC, versione del firmware, numero del modello.

La chiamata parte una volta al secondo per tutta la durata dell'ascolto: un
pacchetto solo si perde con niente, e una radio accesa mezzo secondo dopo di
noi resterebbe invisibile. Va anche sul broadcast di **ogni interfaccia**, non
solo su quello globale: su una macchina con una scheda cablata, una Wi-Fi e
qualche rete virtuale, il broadcast globale non esce sempre da tutte — e la
radio sta proprio su quella che il sistema non ha scelto.

Lo stato `03` — «già in uso da un altro programma» — è una risposta valida e
viene mostrata: è l'informazione che spiega perché la radio non si apre, e
senza la quale sembra un guasto.

Un modello che non conosciamo tiene il suo numero (`OpenHPSDR 0x7F`) invece di
sparire: una radio uscita dopo di noi resta riconoscibile.

### FlexRadio serie 6000

Si **ascolta**. Il Flex si annuncia da sé, una volta al secondo, in broadcast
sulla porta 4992: un pacchetto VITA-49 il cui carico utile è testo, coppie
`chiave=valore`. Da lì escono modello, numero di serie, versione di SmartSDR,
soprannome e stato.

Il testo si cerca dentro il pacchetto invece di decodificare l'intestazione
VITA-49, che cambia fra le versioni del protocollo: è più robusto, ed è tutto
ciò che serve per dire chi c'è.

La porta si apre in condivisione. Prendersela in esclusiva farebbe sparire la
radio da SmartSDR, che sta quasi sempre girando sulla stessa macchina — un
modo pessimo di farsi installare.

## Che cosa **non** trova, e perché

**SunSDR / ExpertSDR (TCI).** Il TCI è un servizio WebSocket che ascolta sulla
porta 40001: è il programma del costruttore a offrirlo, e non c'è un annuncio
in broadcast da intercettare. Trovarlo vorrebbe dire provare gli indirizzi
della rete uno per uno — cosa che si può fare, ma è una scansione della rete
locale e va chiesta, non fatta di nascosto.

**Perseus.** È un device USB, non di rete: si rileva enumerando il bus, e la
strada dipende dal sistema operativo. Non passa di qui.

In nessuno dei due casi è stata scritta una sonda «per completezza»: una che
non trova mai niente è peggio della sua assenza, perché sposta il sospetto
sulla radio.

## E poi?

Rilevare non è aprire. I backend veri — OpenHPSDR (RF-02), FlexRadio (RF-04),
TCI (RF-05) — restano da scrivere, e ciascuno è un protocollo intero: il flusso
IQ, il controllo, le capability, la pagina di documentazione, la conformance
suite. Le opzioni di build esistono già in `cmake/DsdrOptions.cmake`, spente.

Quello che questo pezzo aggiunge è il primo passo utile di ognuno di quei
lavori — trovare la radio — e nel frattempo una risposta onesta a chi ne ha
una collegata adesso.

## Dove guardare nel codice

```
src/hal/RadioScout.h/.cpp        le sonde e l'interpretazione delle risposte
tests/hal/tst_radio_scout.cpp    i pacchetti costruiti a mano, senza radio
src/app/qml/DiscoveryPane.qml    l'elenco, separato da quello dei device
```
