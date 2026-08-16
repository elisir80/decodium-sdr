# Backend `nettcp`

Sorgenti IQ raggiungibili via rete (RF-07). Parla **rtl_tcp**, **SpyServer**,
IQ grezzo dichiarato su TCP/UDP e **SDR++ Server**:
il primo rende utilizzabile una chiavetta RTL-SDR anche montata su un Raspberry
in giardino, il secondo apre agli Airspy condivisi in rete.

I due protocolli si riconoscono da come si comportano appena connessi —
rtl_tcp saluta per primo, SpyServer aspetta di essere salutato — quindi
l'utente non deve dichiarare che cosa ci sia dall'altra parte.

Classe **raw-IQ**: il server consegna solo campioni, tutto il resto lo fa il
DSP client.

## Endpoint dichiarati

I protocolli senza handshake non sono indovinabili in modo sicuro: l'URI è il
contratto esplicito. Si aggiunge dal campo «Sorgente» o in
`DSDR_NETTCP_HOSTS`; appare subito nell'elenco e la connessione verifica che
il peer sia davvero raggiungibile.

| URI | Direzione | Formato IQ | Esempio |
| --- | --- | --- | --- |
| `tcp://host:porta?rate=…&format=…` | Decodium è client | IQ interlacciato LE continuo | `tcp://192.168.1.20:7356?rate=2048000&format=int16` |
| `udp://host:porta?rate=…&format=…` | Decodium ascolta sulla porta locale | un datagramma o più datagrammi IQ interlacciati LE | `udp://0.0.0.0:7356?rate=48000&format=float32` |
| `sdrpp://host:porta?format=…` | Decodium è client | protocollo SDR++ Server | `sdrpp://192.168.1.20:5259?format=int16` |

`format` (o `type`) accetta `int8`, `int16`, `int32` e `float32`; SDR++
Server accetta `int8`, `int16` e `float32`. Il valore predefinito è `int16`.
Il sample rate di un flusso raw è `rate` (default 2,048 MS/s); per SDR++ Server
vale sempre il rate annunciato dal server all'apertura.

Il client SDR++ imposta esplicitamente il tipo PCM, disattiva la compressione,
invia frequenza e `Start`, poi riceve i frame Baseband non compressi. Un server
che ignori quella richiesta e invii IQ compresso viene rifiutato con un errore
esplicito: questa build non include Zstd e non deve presentare campioni
compressi come se fossero IQ.

## Audio di rete

Il pannello **Audio di rete** esporta il mix RX lineare (prima della riduzione
neurale) come PCM signed 16-bit little-endian, 48 kHz, senza intestazione:

- **UDP** invia al destinatario scelto (default `127.0.0.1:7355`);
- **TCP server** ascolta su un indirizzo locale (`0.0.0.0` per tutte le
  interfacce) e serve un client alla volta.

È il formato del Network Sink di SDR++. Si sceglie mono o stereo; il tap ha un
ring proprio, quindi un client remoto lento non può bloccare né gli
altoparlanti né il DSP.

## Il protocollo, in breve

rtl_tcp è volutamente scarno. Dopo la connessione il server invia 12 byte di
saluto e poi, senza altre cerimonie, un flusso continuo di campioni I/Q a
8 bit non segnati. Il client comanda scrivendo pacchetti di 5 byte: un opcode
e un intero a 32 bit big-endian. Non esistono conferme né risposte.

```
saluto:   "RTL0" | tunerType (uint32 BE) | gainStepCount (uint32 BE)
comando:  opcode (uint8) | valore (int32 BE)
campioni: I, Q, I, Q, …  (uint8, centrati su 127,5)
```

Implementato da documentazione pubblica e osservazione del protocollo: nessuna
riga deriva da librtlsdr o da altri client (CONSTITUTION §3).

## Discovery

rtl_tcp **non si annuncia** sulla rete: non esiste discovery broadcast. L'unico
modo di sapere se a un indirizzo risponde una radio è chiederglielo, e il
backend sonda un elenco di endpoint noti.

Da dove vengono gli endpoint, in ordine:

1. la variabile d'ambiente `DSDR_NETTCP_HOSTS` (`"host:porta,host:porta"` o
   URI dichiarati separati da virgola);
2. gli indirizzi aggiunti a runtime — dalla UI, che mostra il campo perché il
   backend dichiara `remoteCapable`, oppure via
   `nativeCommand("net.addEndpoint")`;
3. `127.0.0.1:1234`, il default di rtl_tcp.

Connettersi non basta come prova: qualunque servizio in ascolto accetta la
connessione.

Il sondaggio procede in due tempi, e l'ordine non è invertibile:

1. **Si aspetta in silenzio** per ~450 ms. Se arriva `RTL0`, è un rtl_tcp.
2. Se non arriva nulla, si manda l'handshake SpyServer. Se risponde con un
   `DeviceInfo`, è un SpyServer.

Mandare l'handshake per primo significherebbe spedire a un eventuale rtl_tcp
una manciata di byte che lui interpreterebbe come comandi, cambiandogli
frequenza durante una semplice ricerca.

## Capability

| Capability | Valore | Perché |
|---|---|---|
| `maxRxChannels` | 4 | I canali sono del DSP client: il limite è sul carico, non sul protocollo |
| `coherentRx` | `true` | Derivano tutti dallo stesso flusso IQ |
| `tx` | `None` | Una chiavetta RTL non trasmette. `setPtt(true)` viene rifiutato, non ignorato |
| `demod`, `spectrum`, `agc` | `Client` | Il server manda solo campioni |
| `sampleRates` | 250 kS/s, poi 1,024–3,2 MS/s | Fra 300 e 900 kS/s il chip non è affidabile: quell'intervallo è escluso di proposito |
| `minFrequencyHz`/`maxFrequencyHz` | dipende dal tuner | Prima dell'handshake la copertura è generica; dopo è quella del tuner dichiarato |
| `remoteCapable` | `true` | Abilita in UI il campo per aggiungere un indirizzo |
| `multiClient` | `false` | rtl_tcp serve un client alla volta |
| `adcBits` | 8 | |

La copertura segue il tuner: un FC0012 arriva a ~948 MHz, un R820T a ~1766. Se
la dichiarassimo sempre uguale, la UI prometterebbe bande che il ferro non ha.

## Conversione dei campioni

rtl_tcp manda byte non segnati centrati su 127,5. La conversione a float passa
per una tabella di 256 voci costruita una volta sola: sottrarre e dividere per
ogni campione costerebbe due operazioni in virgola mobile su milioni di
campioni al secondo, mentre la tabella sta in cache.

Il test `samplesAreCentredAndScaled` verifica che la componente continua
residua resti sotto 0,05: una conversione sbagliata di mezzo LSB produce una
riga a centro banda che sembrerebbe un segnale.

## Comandi nativi

Sono qui e non nell'interfaccia generale perché nessun'altra radio ha un
"bias tee" o una correzione in ppm (§4.1). Vanno usati solo da pannelli
backend-specifici.

| Comando | Argomenti | Effetto |
|---|---|---|
| `net.addEndpoint` | `endpoint` | Aggiunge un indirizzo da sondare (convenzione condivisa fra backend remoti) |
| `net.endpoints` | — | Elenca gli indirizzi configurati |
| `nettcp.setGain` | `tenthsDb` (int) | Guadagno in decimi di dB; negativo = automatico |
| `nettcp.setPpm` | `ppm` (int) | Correzione di frequenza |
| `nettcp.setBiasTee` | `enabled` (bool) | Alimentazione phantom sull'antenna |

## Limiti noti

- **SpyServer: solo IQ.** Il canale FFT del protocollo non è usato — lo
  spettro lo calcola comunque il DSP client — e i formati compressi non sono
  supportati: si richiede int16, che ogni server offre.
- **Nessuna riconnessione automatica.** Se il server cade, la sessione si
  chiude con un errore e tocca all'utente riconnettersi.
- La correzione in ppm non è persistente: si perde alla chiusura, in attesa
  del SettingsStore.

## Provare senza hardware

```sh
# Con una chiavetta vera, su questa macchina o su un'altra:
rtl_tcp -a 0.0.0.0 -p 1234

# Poi, se il server non è in locale:
DSDR_NETTCP_HOSTS=192.168.1.20:1234 ./build/bin/decodium-sdr --backend nettcp
```

I test usano un server rtl_tcp finto (`tests/hal/RtlTcpMockServer`) che
riproduce saluto, comandi e flusso di campioni generando un tono a offset
noto: è ciò che permette al backend di passare l'intera conformance suite in
CI, senza hardware.

`tests/hal/NetworkIqMockServer` verifica inoltre IQ raw TCP, IQ raw UDP e il
negoziato SDR++ Server (sample rate, formato, frequenza, `Start` e campioni).
`tests/dsp/tst_network_audio` riceve un vero datagramma UDP localhost e
verifica il PCM16/48 kHz senza header.
