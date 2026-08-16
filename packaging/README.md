# Packaging

Obiettivo (RNF-04): **un binario per piattaforma**, che parta su una macchina
che non ha mai visto un ambiente di sviluppo.

I pacchetti li costruisce `.github/workflows/release.yml` quando si spinge un
tag `v*`, e finiscono in una release **in bozza**: le note di rilascio le
scrive una persona, e il pacchetto va provato prima di renderlo pubblico.

| Piattaforma | Formato | Come nasce |
|---|---|---|
| Linux x86-64 | AppImage | `linuxdeploy` con il plugin Qt sull'albero installato |
| macOS Apple Silicon | DMG | bundle `.app` dal deploy di Qt, poi `hdiutil` |
| Windows x86-64 | ZIP portable | albero installato, compresso così com'è |

## Costruire un pacchetto in locale

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
cmake --install build --prefix staging
```

`staging/` è un albero autosufficiente. Su Windows contiene anche `qt.conf`,
senza il quale l'applicazione non trova il plugin della piattaforma ed **esce
in silenzio**: il deploy di Qt usa un layout `bin/` + `share/`, mentre Qt su
Windows cerca i plugin accanto all'eseguibile.

Le release ufficiali includono i moduli Soapy **RTL-SDR** e **HackRF**, con le
loro dipendenze runtime. Il loader dell'app dà priorità al percorso incluso:

| Piattaforma | Directory dei moduli inclusi |
|---|---|
| macOS | `decodium-sdr.app/Contents/lib/SoapySDR/modules0.8` |
| Linux / Windows | `lib/SoapySDR/modules0.8` sotto la root del pacchetto |

Quindi il pacchetto non dipende da Homebrew, MSYS2 o da un'installazione Soapy
preesistente. Altri driver possono essere aggiunti con
`DSDR_SOAPY_BUNDLE_MODULES` e, se sono fuori dai prefissi standard, con
`DSDR_SOAPY_MODULE_PATHS`; la configurazione fallisce se un modulo richiesto
non si trova.

### Verificare che sia davvero autosufficiente

Non basta che parta sulla macchina di sviluppo, dove l'ambiente è nel PATH.
La prova vera è avviarlo con un PATH ridotto all'osso:

```powershell
$env:PATH = "C:\Windows\System32"
.\staging\bin\decodium-sdr.exe --auto-connect
```

Se esce subito e senza messaggi, manca una libreria: è un errore del *loader*,
che avviene prima che il programma possa scrivere alcunché.

## Dipendenze non-Qt

Il deploy di Qt porta con sé le librerie Qt. I moduli Soapy sono plugin caricati
dopo l'avvio e vengono quindi raccolti esplicitamente: su macOS il loro grafo
di dipendenze viene ricorsivamente copiato in `Contents/Frameworks` e i link
Mach-O sono riscritti nel bundle; su Linux linuxdeploy riceve ogni modulo dal
manifest CMake; su Windows il deploy raccoglie le DLL dal relativo staging.

Per una build macOS completa RTL-SDR + HackRF:

```sh
brew install qt@6 fftw ninja soapysdr soapyrtlsdr hackrf soapyhackrf hamlib
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DDSDR_QT_CMAKE_DEPLOY=OFF \
  -DDSDR_BUNDLE_SOAPY=ON \
  -DDSDR_SOAPY_BUNDLE_MODULES='librtlsdrSupport;libHackRFSupport'
cmake --build build
cmake --install build --prefix staging
macdeployqt staging/decodium-sdr.app -qmldir="$PWD/src/app/qml" -no-codesign -always-overwrite
cmake -DCMAKE_INSTALL_PREFIX="$PWD/staging" -P build/BundleMacOSSoapyModules.cmake
cmake -DCMAKE_INSTALL_PREFIX="$PWD/staging" -P build/BundleMacOSHamlib.cmake
find staging/decodium-sdr.app/Contents -type f \
  \( -name '*.dylib' -o -name '*.so' -o -path '*/MacOS/*' \
     -o -path '*/Resources/hamlib/bin/*' \) \
  -exec codesign --remove-signature {} \; 2>/dev/null || true
find staging/decodium-sdr.app/Contents -type f \
  \( -name '*.dylib' -o -name '*.so' -o -path '*/MacOS/*' \
     -o -path '*/Resources/hamlib/bin/*' \) -print0 | \
  xargs -0 -n1 codesign --force --sign -
codesign --force --sign - staging/decodium-sdr.app
```

Prima di creare il DMG, eseguire:

```sh
bash scripts/check-soapy-package.sh staging/decodium-sdr.app \
  librtlsdrSupport libHackRFSupport
```

e verificare anche `staging/decodium-sdr.app/Contents/Resources/hamlib/bin/rigctld`.

## Quello che manca

- **Firma e notarizzazione del DMG**: serve un Apple Developer ID. Fino ad
  allora macOS chiede conferma al primo avvio. È un passo amministrativo, non
  tecnico.
- **Installer Windows** (oltre allo ZIP portable): previsto dalla spec §RNF-04.
- **AppImage aarch64**: il target Raspberry Pi della spec §12.5 richiede un
  runner arm64 o la cross-compilazione.
- **Icona dell'applicazione**: oggi è un segnaposto generato al volo. Arriverà
  con l'identità visiva di DECODIUM.
- **Flathub**: previsto in Fase 5.

## Il programma di installazione

Lo fa **CPack**, che sta dentro CMake e non aggiunge dipendenze a chi compila:

```sh
cmake --build build
cd build && cpack -G NSIS      # Windows: l'installatore
cd build && cpack -G ZIP       # ovunque: l'archivio portable
```

Su Windows serve `makensis`:

```sh
pacman -S mingw-w64-x86_64-nsis
```

Senza, `cpack` fa comunque lo ZIP e dice che il generatore manca. È la stessa
regola delle opzioni di build: quella che esiste deve funzionare, e quella che
non può funzionare non deve comparire.

L'installatore mette un collegamento nel menu Start, offre quello sul desktop,
usa l'icona dell'eseguibile — la stessa, o il programma sembrerebbe un altro a
metà dell'installazione — e disinstallando toglie quello che ha messo e
nient'altro: preferenze e registrazioni sono dell'operatore.

**All'installazione mostra un avviso che vale la radio di qualcuno**: chi
collega un ricetrasmettitore via CAT deve spegnere «CAT RTS» nei menù. Noi il
PTT lo comandiamo con un comando CAT e quella linea non ci serve; lasciandola
attiva, qualunque programma che apra la porta seriale può mandare la radio in
trasmissione per un istante — è il transitorio che Windows produce dentro
l'apertura della porta, prima che un programma possa farci niente.
