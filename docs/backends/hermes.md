# Backend `hermes`

**Hermes-Lite 2** e famiglia OpenHPSDR: ricevitore di rete a campionamento
diretto, protocollo aperto, nessuna libreria del costruttore. Si trova da sé
sulla rete locale.

È la stessa forma del ColibriNANO — raw-IQ, tutto il DSP dalla nostra parte —
con la rete al posto dell'USB e un protocollo documentato al posto di una
libreria da caricare a runtime. È anche il primo backend che non richiede
niente da installare: solo UDP.

Attiva con `-DDSDR_BACKEND_HERMES=ON` (predefinito).

```sh
./build/bin/decodium-sdr --backend hermes
```

## Il protocollo, in breve

OpenHPSDR protocollo 1. Tutto passa dalla porta UDP 1024, in pacchetti di
misura fissa.

**Non è una radio che parla e un client che ascolta: sono due flussi.** La
radio manda campioni, e il PC **deve** rispondere con un pacchetto per ognuno —
anche in sola ricezione, anche senza niente da trasmettere — perché quei
pacchetti sono l'unico veicolo dei byte di comando. Un client che tacesse
riceverebbe campioni per sempre, sempre dalla stessa frequenza, senza modo di
cambiarla.

Il ritmo lo detta la radio: un pacchetto ricevuto, un pacchetto mandato. Non
serve un timer, e non può derivare — l'unico quarzo in mezzo al flusso è il suo.

```
0..2   EF FE 01   sincronismo
3      endpoint   0x06 dalla radio, 0x02 verso la radio
4..7   numero d'ordine, big endian
8      primo fotogramma da 512 byte
520    secondo fotogramma da 512 byte
```

E dentro ogni fotogramma da 512:

```
0..2   7F 7F 7F   sincronismo
3..7   C0..C4     i cinque byte di comando e stato
8..511 504 byte di campioni
```

Con un ricevitore ogni gruppo occupa otto byte — tre di I, tre di Q, interi a
24 bit con segno big endian, e due del microfono della radio che qui non serve
— per 63 gruppi a fotogramma, 126 coppie a pacchetto.

**Il bit meno significativo di C0 è il PTT**, e l'indirizzo del registro sta
nei sette bit sopra. Se l'indirizzo lo occupasse, cambiare frequenza manderebbe
la radio in trasmissione: è il genere di errore che si scopre dal vicino di
casa, e ha il suo test.

I registri si alternano fra un pacchetto e l'altro — velocità, frequenza del
ricevitore, guadagno, frequenza del trasmettitore — e la radio ricorda l'ultimo
valore di ciascuno.

## Il rilevamento

Lo fa `hal::RadioScout`, lo stesso che serve a dire «c'è una radio in rete»
(vedi [rilevamento-radio.md](../rilevamento-radio.md)): un pacchetto di 63 byte
in broadcast sulla porta 1024, e chi risponde dice chi è.

La chiave del device è il **MAC**, non l'indirizzo IP: quello lo cambia il
DHCP, e con lui si perderebbero le impostazioni per-radio.

## Capability dichiarate, e perché

| Capability | Valore | Perché |
|---|---|---|
| `maxRxChannels` | 4 | Il ricevitore della radio è uno; i quattro canali sono logici e li demodula il DSP client |
| `coherentRx` | `true` | Nascono tutti dallo stesso flusso IQ |
| `tx` | **`None`** | Vedi sotto |
| `demod`, `spectrum`, `agc` | `Client` | La radio consegna IQ grezzo e nient'altro |
| `sampleRates` | 48, 96, 192, 384 kS/s | Le quattro che il protocollo sa esprimere, non una in più: una velocità che non sa esprimere verrebbe ignorata dalla radio, che continuerebbe alla precedente mentre il DSP calcola tutto sull'altra |
| `minFrequencyHz` … `maxFrequencyHz` | 10 kHz … 38,4 MHz | Prima zona di Nyquist dell'ADC a 76,8 MHz. Sopra si riceve nelle zone successive, ma senza filtro d'ingresso e senza garanzie |
| `hasPreamp`, `hasAttenuator` | `true` | Sono **la stessa manopola**: un solo valore da −12 a +48 dB |
| `maxGainReductionDb` | 60 | L'intera scala del guadagno: è da lì che la guardia contro la saturazione (SPEC-003 §3) prende quello che toglie |
| `adcBits` | 12 | L'ADC dell'Hermes-Lite 2 |
| `remoteCapable` | `true` | Vive in rete: è raggiungibile da un'altra stanza |
| `multiClient` | `false` | Il protocollo 1 parla con un solo programma per volta |
| `nativePanels` | `HermesDevicePanel` | Guadagno e salute del collegamento |

**Perché `tx = None`.** Il protocollo trasmette, e nei pacchetti che mandiamo
il posto per i campioni c'è già: la trasmissione sarebbe un riempimento di
quei 504 byte. Ma finché non è provata su una radio vera resta `None` — meglio
niente PTT che un PTT che manda in aria qualcosa che nessuno ha misurato
(CONSTITUTION §7). È il primo lavoro da fare su questo backend.

## Comandi nativi

| Comando | Argomenti | Risposta |
|---|---|---|
| `hermes.setGain` | `db` | il guadagno applicato, da −12 a +48 |
| `hermes.gainRange` | — | `min`, `max`, `value` |
| `hermes.health` | — | sovraccarico dell'ADC, pacchetti persi, indirizzo |

Toccare il guadagno a mano **azzera** la riduzione in corso della guardia
contro la saturazione, e il nuovo valore diventa il tetto: chi rimette le mani
sulla manopola sta dicendo qual è il livello che vuole.

## Limiti noti

**Solo ricezione**, per la ragione detta sopra.

**Un solo programma per volta.** Il protocollo 1 non prevede più client: la
radio manda i campioni all'indirizzo da cui ha ricevuto l'ultimo comando.

**Nessuna prova su hardware.** Il protocollo è implementato dalla
documentazione OpenHPSDR e ogni pezzo che si può verificare senza radio ha il
suo test — offset, ordine dei byte, estensione del segno a 24 bit, bit di
abilitazione del guadagno. Ma una radio vera non l'ha ancora vista, e finché
non succede questa pagina non può dire di più.

**Le altre schede della famiglia** — Hermes, Angelia, Orion, Metis — parlano lo
stesso protocollo e vengono riconosciute, ma le capability dichiarate sono
quelle dell'Hermes-Lite 2: frequenze, guadagno e bit dell'ADC delle altre non
sono stati verificati, e dichiararli sarebbe una promessa presa a prestito.

**I buchi nella numerazione** vengono contati e mostrati (`lostPackets`): la
radio non ritrasmette, e un salto è audio perduto. Saperlo distingue una rete
che perde pacchetti da un DSP troppo lento, che è la diagnosi che altrimenti si
sbaglia.

## Dove guardare nel codice

```
src/hal/backends/hermes/
    HermesProtocol.h/.cpp   codifica e decodifica, pure e statiche
    HermesWorker.h/.cpp     i due flussi UDP, su un thread proprio
    HermesBackend.h/.cpp    il seam: capability, apertura, guadagno, comandi
src/hal/RadioScout.h        il rilevamento, condiviso con le altre famiglie
tests/hal/tst_hermes_protocol.cpp   i pacchetti costruiti byte per byte
```
