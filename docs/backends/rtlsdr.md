# Backend `rtlsdr`

Il backend `rtlsdr` usa direttamente `librtlsdr`, senza passare da SoapySDR.
È il percorso consigliato per RTL-SDR Blog V4 su macOS quando si vuole una
catena corta e diagnostica simile al modulo nativo di SDR++.

## Funzioni

- discovery USB con nome, modello, produttore e seriale;
- sintonia 24 MHz–1,766 GHz con RTL-SDR Blog V4/R828D;
- sample rate da 250 kS/s a 3,2 MS/s, con default conservativo a 2,048 MS/s;
- gain automatico prudente o manuale sui passi reali del tuner; in AUTO
  l'AGC hardware resta spento e la guardia anti-overflow dell'applicazione
  regola il gain per evitare la saturazione dell'ADC a 8 bit;
- correzione PPM;
- bias-tee;
- direct sampling I/Q per la copertura HF;
- offset tuning quando il device lo supporta;
- conversione IQ unsigned 8-bit → float interleaved nel ring lock-free HAL;
- log di apertura, tuner, rate, gain, overrun e fine streaming.

Il backend è RX-only: non mostra né simula un PTT.

## Comandi nativi UI

Il pannello `RtlSdrDevicePanel` usa esclusivamente il seam controllato
`Session.nativeCommand()`:

| Comando | Parametri |
| --- | --- |
| `rtlsdr.setGain` | `db`, negativo = automatico |
| `rtlsdr.setPpm` | `ppm` |
| `rtlsdr.setBiasTee` | `enabled` |
| `rtlsdr.setDirectSampling` | `mode`: 0 off, 1 I, 2 Q |
| `rtlsdr.setOffsetTuning` | `enabled` |

## Build

Su macOS:

```sh
brew install librtlsdr
cmake -S . -B build -G Ninja -DDSDR_BACKEND_RTLSDR=ON
cmake --build build
```

Per una build senza dipendenza nativa si può usare
`-DDSDR_BACKEND_RTLSDR=OFF`; il backend Soapy resta indipendente.
