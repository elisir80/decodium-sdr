# Decodium SDR / SDR++ — gap analysis

Questo documento tiene separati i tre livelli di confronto:

1. il modo di demodulare correttamente il segnale;
2. gli strumenti operativi della radio;
3. i moduli, i device e la distribuzione.

## Modi e catena audio

| Area | Decodium SDR | SDR++ | Stato |
| --- | --- | --- | --- |
| USB / LSB | Filtro laterale, AGC, volume, ricampionamento | Demodulatore SSB e catena AF | Verificato con test DSP |
| DSB | Demodulatore double-sideband soppresso, AGC, squelch/NB/high-pass e catena AF | Demodulatore DSB, AGC, NB, squelch e high-pass | Implementato e verificato con tono DSB sintetico |
| CW / CW-R | BFO configurabile e filtro centrato sul pitch | CW/LSB/USB con catena AF | Verificato con test DSP |
| AM | Inviluppo con blocco DC, carrier-AGC opzionale, AGC audio con attacco/decadimento | AM con filtro, carrier-AGC e catena AF | Verificato con test DSP |
| SAM | PLL di portante e diagnostica di lock | AM sincrona | Verificato con test DSP |
| Wide-FM | Discriminatore, stereo MPX L/R, low-pass selezionabile, de-enfasi 0/22/50/75 us, squelch, ricampionamento, RDS PI/PS/RadioText/PTY/AF, tabelle PTY Europa/RBDS, metadati RDS avanzati, AF manuale/automatico | Stereo, low-pass, de-enfasi, squelch, RDS | Implementato e testato su multiplex RDS sintetico; AF automatico richiede validazione sul campo |
| NFM | Discriminatore con deviazione 2.5 kHz, low-pass AF selezionabile, de-enfasi 0/22/50/75 us, power squelch, CTCSS, noise blanker e IF-NR | NFM, low-pass, de-enfasi, power/CTCSS squelch | Implementato e testato |
| DIGU / DIGL | Catena SSB con filtri dedicati | Modi laterali radio | Verificato con test DSP |
| IQ / RAW | Monitor stereo I/Q equivalente al RAW di SDR++, registrazione IQ, API C ABI e catalogo UI dei moduli `.dylib/.so/.dll` | RAW/stream per moduli | Implementato e testato; caricamento CLI e directory standard |

L'uscita interna del DSP è stereo interleaved. I modi mono vengono duplicati su
L/R; Wide-FM usa invece il multiplex stereo. Se il dispositivo audio macOS non
accetta 48 kHz stereo, il router prova automaticamente 48 kHz mono.

## Funzioni operative da chiudere

| Funzione SDR++ | Situazione Decodium | Priorità |
| --- | --- | --- |
| Multi-VFO | Più canali RX, mixer stereo e auto-retune del centro quando un VFO esce dalla banda | Completato |
| Registrazione IQ / audio | IQ e WAV stereo del mix audio | Completato per IQ e audio locale |
| Frequency manager | Band-stack persistente e memorie richiamabili dalla UI | Implementato |
| Scanner | Scansione della banda corrente con dwell, soglia S-meter e risultati richiamabili | Implementato |
| RDS / PS / RadioText | Decoder 57 kHz con sincronismo CRC-10, PI, PS, RadioText, PTY Europa/RBDS, country/coverage/reference/callsign e AF manuale/automatico | Implementato; AF automatico richiede un segnale reale per la validazione sul campo |
| Power squelch | Presente per USB/LSB/DIG/AM/SAM/FM/NFM, con isteresi; escluso da CW/IQ | Completato |
| CTCSS | Rilevatore continuo, gate NFM e modalità decode-only separata dal mute | Completato |
| FM noise reduction / noise blanker | Noise blanker impulsivo sui modi compatibili + riduzione IF con preset Voice/Narrow/Broadcast e log | Completato |
| Rig control / rigctl | Server Hamlib locale su `127.0.0.1:4532`: frequenza, modo, volume AF e PTT capability-aware | Completato |
| SNR e volume meter separati | S-meter RF, fondo rumore, SNR e meter audio post-volume | Completato |
| High-pass audio / AGC timing | High-pass FIR configurabile per canale (20–1000 Hz UI) e attacco/decadimento AGC regolabili con preset Auto | Implementato e testato |

## Device, moduli e packaging

Decodium usa una HAL a backend. RTL-SDR V4 ora ha sia un percorso nativo
`librtlsdr` — discovery USB, sintonia, rate, gain, PPM, bias-tee, direct
sampling e log del flusso — sia il percorso SoapySDR universale. Il DMG macOS
porta con sé runtime e librerie necessari, così l'utente finale non deve
installare Homebrew. I moduli IQ esterni possono essere caricati con
`--iq-module /percorso/modulo.dylib`; l'header installato è
`include/decodium/IqModuleApi.h`. Restano come backlog i backend dedicati per
HPSDR, DLINK, FlexRadio, TCI e KiwiSDR.

Il server rigctl resta vincolato al loopback per sicurezza. Un logger o un
programma CAT locale può collegarsi alla porta 4532 e usare i comandi Hamlib
standard `f`, `F`, `m`, `M`, `t`, `T`, `l AF` e `L AF`, oltre agli alias e alle
query compatibili con SDR++ (`\\get_freq`, `\\set_freq`, `\\get_mode`,
`\\set_mode`, `M ?`, `v/V`, `s/S`, `\\chk_vfo`, `AOS/LOS` e `\\quit`). La
registrazione CAT controlla il recorder audio locale e restituisce errore se
l’uscita audio non è attiva. La frequenza viene applicata al VFO selezionato.
Il PTT viene rifiutato con `RPRT -4` quando il
backend dichiara una sorgente solo RX. Questo vale anche per Soapy quando il
driver rileva canali TX fisici ma il percorso TX di Decodium non è ancora
implementato: non viene mostrato un PTT che non genererebbe RF.

## Criterio di completamento

Ogni modo deve avere: filtro coerente con la propria geometria, frequenza di
campionamento interna valida, demodulazione non nulla su un segnale sintetico,
uscita finita, volume/mute/AGC applicati e log diagnostico. Le funzioni ancora
indicate come parziali restano backlog di distribuzione o integrazione hardware;
per il percorso DSP/UI del ricevitore ogni funzione riportata come implementata
ha codice, test e controllo nella UI.
