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

Su macOS il bundle installato contiene anche il modulo SoapyRTLSDR e il
runtime `librtlsdr`/`libusb` quando `DSDR_BUNDLE_RTLSDR=ON` (impostazione
predefinita su macOS). Il percorso del plugin viene impostato dall'app verso
`Contents/PlugIns/SoapySDR/modules0.8`, quindi il DMG non dipende da Homebrew.

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

Il deploy di Qt porta con sé le librerie Qt. FFTW e SoapySDR vengono raccolti
dal deploy macOS perché sono collegati dall'eseguibile; il modulo SoapyRTLSDR
e le sue dipendenze USB vengono copiati esplicitamente da
`src/app/CMakeLists.txt`. L'installazione richiede quindi, per una build macOS
completa:

```sh
brew install qt@6 fftw ninja soapysdr soapyrtlsdr
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DDSDR_QT_CMAKE_DEPLOY=OFF
cmake --build build
cmake --install build --prefix staging
macdeployqt staging/decodium-sdr.app -qmldir="$PWD/src/app/qml" -no-codesign -always-overwrite
cmake -DCMAKE_INSTALL_PREFIX="$PWD/staging" -P build/BundleMacOSSoapyRtlSdr.cmake
find staging/decodium-sdr.app/Contents -type f \
  \( -name '*.dylib' -o -name '*.so' -o -path '*/MacOS/*' \) \
  -exec codesign --remove-signature {} \; 2>/dev/null || true
```

Prima di creare il DMG, verificare la presenza di
`staging/decodium-sdr.app/Contents/PlugIns/SoapySDR/modules0.8/librtlsdrSupport.so`.

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
