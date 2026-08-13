# Backend `soapy`

> «SoapySDR è il moltiplicatore di universalità: un solo backend copre decine
> di hardware. Ogni device esotico che ha un driver Soapy funziona gratis.»
> — spec §4.3

Backend raw-IQ per qualunque radio con un driver SoapySDR: RTL-SDR, Airspy,
SDRplay, HackRF, LimeSDR, PlutoSDR, USRP e le altre. È l'unico backend le cui
capability **non sono costanti**: si leggono dal driver all'apertura, perché
una chiavetta da 30 € e un USRP passano entrambi da qui.

## Come si scoprono le capacità

`SoapySDR::Device::enumerate()` elenca i device; all'apertura si legge un
*profilo* (`SoapyDeviceProfile`) con canali RX/TX, frequenze di campionamento,
copertura in frequenza, guadagno e antenne. `capabilitiesFrom()` traduce il
profilo nel linguaggio della HAL.

Tenere separate le due cose non è pedanteria: la traduzione è la parte che
guida la UI, ed è l'unica verificabile **senza hardware attaccato** — quindi
l'unica che può essere provata in CI. Le regole che presidia:

| Regola | Perché |
|---|---|
| TX solo con percorso TX reale | I canali TX fisici vengono rilevati, ma il worker attuale è RX-only e non mostra PTT simulati |
| `coherentRx` solo con più canali hardware | Un canale solo non è coerente con nessuno |
| Copertura dalla frequenza del device | Altrimenti la UI promette bande che il ferro non ha |
| Default ≤ 2,4 MS/s | Oltre, molti device perdono campioni su USB e l'utente lo legge come un difetto del programma |

I canali che l'utente apre restano **logici**: vivono dentro la banda
campionata e li demodula il DSP client. Un device con più canali hardware non
li moltiplica — li rende coerenti.

## Threading

Il ciclo di lettura **non gira su un event loop**: `readStream()` è bloccante e
un ciclo che lo chiama non può contemporaneamente servire connessioni queued. I
comandi arrivano da variabili atomiche che il ciclo applica fra una lettura e
l'altra — che è anche l'unico modo corretto di toccare un device SoapySDR, che
non è thread-safe.

Il formato è `CF32`: float interleaved, esattamente ciò che il nostro ring si
aspetta. Dalla scheda al DSP non c'è alcuna conversione.

SoapySDR segnala gli overflow (`SOAPY_SDR_OVERFLOW`) quando il device ha perso
campioni perché non li abbiamo letti in tempo: vengono contati e riportati come
`droppedFrames`, non ignorati.

Nessuna eccezione attraversa il seam (§4.1): SoapySDR ne lancia per qualunque
cosa, e questo backend è dove si fermano.

## Installare SoapySDR

```sh
# Debian/Ubuntu
sudo apt install libsoapysdr-dev soapysdr-module-rtlsdr

# macOS
brew install soapysdr

# MSYS2
pacman -S mingw-w64-x86_64-soapysdr mingw-w64-x86_64-soapyrtlsdr
```

I **driver sono pacchetti separati**: SoapySDR senza moduli enumera zero
device. È il motivo per cui la conformance suite dichiara `skip` invece di
fallire quando non trova hardware.

Il backend si esclude dalla build con `-DDSDR_BACKEND_SOAPY=OFF`.

## Comandi nativi

| Comando | Argomenti | Effetto |
|---|---|---|
| `soapy.setGain` | `db` (double, negativo = automatico) | Guadagno RX |
| `soapy.gainRange` | — | `min`, `max`, `hasAgc` del device |
| `soapy.antennas` | — | Antenne disponibili |
| `soapy.driver` | — | Driver in uso |

## Limiti noti

- **La trasmissione fisica è ancora backlog**: il profilo rileva i canali TX
  Soapy, ma finché non esistono uno stream TX e una sorgente audio/IQ di
  trasmissione il backend espone volutamente `TxSupport::None`. Così il PTT
  non compare e rigctl rifiuta `T 1` invece di registrare uno stato che non
  produce alcuna portante.
- **Guadagno, AGC hardware e antenna sono nel pannello Soapy della UI**; il
- **AUTO non abilita l'AGC hardware alla cieca**: parte da un guadagno prudente
  entro 20 dB sopra il minimo del device, disabilita l'AGC del driver e lascia
  alla guardia anti-overflow del client la correzione successiva. Il pannello
  viene caricato tramite `capabilities().nativePanels` e usa i comandi nativi
  solo all’interno del seam backend-specifico.
- **Un solo canale hardware usato** (`channel 0`), anche su device che ne hanno
  di più. La coerenza fra canali viene dichiarata ma non ancora sfruttata:
  arriverà con QuadBeam in Fase 3.
- Il cambio di frequenza di campionamento avviene a stream attivo: alcuni
  driver lo gradiscono poco. Se dà problemi, la strada è chiudere e riaprire.

## Nota per chi sviluppa su MSYS2

Se i binari crashano all'avvio con `0xC0000139`
(`STATUS_ENTRYPOINT_NOT_FOUND`), il pacchetto `mingw-w64-x86_64-soapysdr` è
stato compilato con una versione di GCC più recente di quella installata: anche
`SoapySDRUtil.exe` fallisce, quindi non è un problema di questo progetto. Le
strade sono due: aggiornare il toolchain (`pacman -Syu`, che aggiorna anche il
compilatore) oppure compilare SoapySDR dai sorgenti con il GCC locale e
puntarci con `-DSoapySDR_DIR=<prefix>/cmake`.
