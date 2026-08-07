# Backend `nettcp`

Sorgenti IQ raggiungibili via TCP (RF-07). Oggi parla **rtl_tcp**: è ciò che
rende utilizzabile una chiavetta RTL-SDR da 30 €, anche montata su un Raspberry
in giardino con l'antenna lontana dai disturbi di casa.

Classe **raw-IQ**: il server consegna solo campioni, tutto il resto lo fa il
DSP client.

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

1. la variabile d'ambiente `DSDR_NETTCP_HOSTS` (`"host:porta,host:porta"`);
2. gli indirizzi aggiunti a runtime — dalla UI, che mostra il campo perché il
   backend dichiara `remoteCapable`, oppure via
   `nativeCommand("net.addEndpoint")`;
3. `127.0.0.1:1234`, il default di rtl_tcp.

Connettersi non basta come prova: qualunque servizio in ascolto accetta la
connessione. Un endpoint entra in lista solo se risponde con il saluto `RTL0`
entro 900 ms. È il caso coperto dal test
`discoveryIgnoresServersWithoutGreeting`.

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

- **SpyServer non è ancora implementato.** La spec RF-07 lo prevede e il
  backend è nato per ospitarlo come secondo protocollo dietro la stessa
  facciata, ma oggi `nettcp` parla solo rtl_tcp.
- **Nessuna riconnessione automatica.** Se il server cade, la sessione si
  chiude con un errore e tocca all'utente riconnettersi.
- **Il guadagno non è ancora esposto in UI**: si imposta solo via comando
  nativo. Serve il caricamento dei pannelli backend-specifici dichiarati in
  `capabilities().nativePanels`, previsto più avanti.
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
