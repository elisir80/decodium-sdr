# DsdrOptions.cmake — opzioni di build di DECODIUM SDR.
#
# RNF-06: ogni backend è compilabile/escludibile a build-time. Nessun backend
# è dipendenza obbligatoria del core: il seam HAL resta l'unico contratto.

option(DSDR_BACKEND_DEMO    "Backend sintetico (RF-09) — sempre consigliato ON" ON)
option(DSDR_BACKEND_SOAPY   "Backend SoapySDR (RF-01)"                          ON)
option(DSDR_BACKEND_NETTCP  "Backend rtl_tcp / SpyServer (RF-07)"               ON)
option(DSDR_BACKEND_HERMES  "Backend OpenHPSDR protocollo 1 / Hermes-Lite 2"     ON)
option(DSDR_BACKEND_HPSDR   "Backend OpenHPSDR P2 (RF-02)"                       OFF)
option(DSDR_BACKEND_DLINK   "Backend DLINK / DECODIUM SDR One (RF-03)"          OFF)
option(DSDR_BACKEND_COLIBRI "Backend ColibriNANO (Expert Electronics)"          ON)
option(DSDR_BACKEND_IQFILE  "Riproduzione di registrazioni IQ (RF-17)"          ON)
option(DSDR_BACKEND_AUDIORIG "Radio tradizionali via audio + CAT (SPEC-004)"       ON)
option(DSDR_BACKEND_FLEX_PROBE "Canale di comando SmartSDR (metà di RF-04)"      ON)
# Il backend FlexRadio e' scritto ma non e' mai stato provato su una radio
# vera: la sequenza che apre il flusso DAX IQ cambia fra le versioni maggiori
# del firmware, e le fonti pubbliche ne descrivono due forme.
#
# Acceso lo stesso, e non e' una svista. La regola «un backend che non consegna
# campioni non si mostra» qui si rispetta in un altro modo: se la sequenza non
# passa, il backend dice su quale comando si e' fermato e con che codice; se
# passa e non arrivano pacchetti, lo dice pure, nominando le due cause
# possibili. Non tace, non finge, e chi ha un Flex davanti ha nel diario tutto
# quello che serve a chiudere la questione in dieci minuti.
option(DSDR_BACKEND_FLEX    "Backend FlexRadio SmartSDR (RF-04)"                ON)
option(DSDR_BACKEND_TCI     "Backend TCI / SunSDR (RF-05)"                      OFF)
option(DSDR_BACKEND_KIWI    "Backend KiwiSDR (RF-06)"                           OFF)
option(DSDR_BACKEND_HAMLIB  "Bridge CAT Hamlib (RF-08)"                         OFF)

# Il DMG macOS deve portarsi dietro il modulo SoapyRTLSDR: senza di esso
# SoapySDR è presente ma una RTL-SDR V4 non può comparire né funzionare su una
# macchina che non ha Homebrew. È attivo di default su macOS e può essere
# disattivato per una build tecnica con `-DDSDR_BUNDLE_RTLSDR=OFF`.
option(DSDR_BUNDLE_RTLSDR   "Includi SoapyRTLSDR e runtime nel bundle macOS"   ${APPLE})
option(DSDR_QT_CMAKE_DEPLOY "Usa il deploy Qt generato da CMake in install"  ON)

# §6.1: il path CPU dello spettro è una decisione di build, non un fallback runtime.
option(DSDR_GPU_SPECTRUM    "Spettro/waterfall su GPU via QQuickRhiItem"         ON)

# La libreria del ColibriNANO è del costruttore e si carica a runtime: non è
# linkata, quindi `file(GET_RUNTIME_DEPENDENCIES)` non la vede e il pacchetto
# esce senza. Il risultato è un backend che compare nell'elenco e poi non trova
# nessun device — che è esattamente il modo peggiore di fallire.
#
# Non la si include d'ufficio perché non è nostra da ridistribuire: chi
# confeziona un pacchetto per sé indica qui il percorso, chi pubblica una
# release lo lascia vuoto e la libreria resta a carico di chi ha la radio.
set(DSDR_COLIBRI_LIB "" CACHE FILEPATH
    "Percorso di colibrinano_lib da includere nel pacchetto (vuoto: non inclusa)")

# Se chi compila non l'ha indicata, la si cerca dove ha senso tenerla: fuori
# dalle cartelle di build, che si cancellano.
#
# Prima stava soltanto accanto all'eseguibile di una build, copiata a mano una
# volta; ogni nuova cartella di build nasceva senza, e il ColibriNANO
# «spariva» — l'elenco dei device restava vuoto e sembrava un guasto della
# radio o del driver. Il file resta fuori dal repository (`.gitignore`): non è
# nostro da ridistribuire, ma nemmeno da far ritrovare a mano ogni volta.
if(NOT DSDR_COLIBRI_LIB)
    if(WIN32)
        set(dsdr_colibri_name "colibrinano_lib.dll")
    else()
        set(dsdr_colibri_name "libcolibrinano_lib.so")
    endif()

    set(dsdr_colibri_guess
        "${CMAKE_SOURCE_DIR}/third_party/colibrinano/${dsdr_colibri_name}")
    if(EXISTS "${dsdr_colibri_guess}")
        set(DSDR_COLIBRI_LIB "${dsdr_colibri_guess}" CACHE FILEPATH
            "Percorso di colibrinano_lib" FORCE)
        message(STATUS "ColibriNANO: libreria trovata in third_party/colibrinano")
    endif()
    unset(dsdr_colibri_guess)
    unset(dsdr_colibri_name)
endif()

# Lo stadio di riduzione di rumore neurale (DSDR-SPEC-003 §8). Si accende da
# sé quando il sorgente di RNNoise è presente in third_party/: senza, la
# capability resta assente e la UI non mostra un interruttore che non
# potrebbe fare nulla.
option(DSDR_NEURAL_NR "Stadio di riduzione di rumore neurale (RNNoise)" ON)

# Chi costruisce un pacchetto per altri lo accende: una parte che si spegne
# perché una dipendenza manca diventa un errore invece di una riga di log. Chi
# compila per sé lo lascia spento e resta libero di non installare mezzo Qt per
# un backend che non userà mai.
#
# Vale per i backend e per lo stadio neurale, che sono le due famiglie di cose
# che sanno sparire da sole. È successo tre volte: `audiorig` senza
# Qt6::SerialPort, il ColibriNANO senza la sua libreria, la riduzione di rumore
# senza RNNoise — e ogni volta chi se n'è accorto è stato un operatore davanti
# a una funzione che non c'era, non chi ha costruito il pacchetto.
option(DSDR_STRICT_BACKENDS    "Una parte richiesta che non si compila è un errore" OFF)

option(DSDR_BUILD_TESTS        "Compila la suite di test (RNF-07)"              ON)
option(DSDR_WARNINGS_AS_ERRORS "Tratta i warning del compilatore come errori"   OFF)

# I difetti di concorrenza non si trovano rileggendo il codice: si trovano
# facendoli succedere sotto uno strumento che li vede. Vale la pena tenere
# l'interruttore in pianta stabile, invece di riscoprire ogni volta le opzioni.
#   address  — use-after-free, overflow, doppia free
#   thread   — accessi concorrenti non sincronizzati
#   undefined
set(DSDR_SANITIZE "" CACHE STRING "Sanitizer da attivare: address, thread, undefined (anche combinati con ;)")

if(NOT DSDR_BACKEND_DEMO)
    message(WARNING
        "DSDR_BACKEND_DEMO=OFF: la conformance suite HAL e gli integration test "
        "headless non potranno girare (CONSTITUTION §9).")
endif()

# dsdr_apply_target_defaults(<target>)
#   Impostazioni comuni: warning, include root della sorgente.
function(dsdr_apply_target_defaults tgt)
    target_include_directories(${tgt} PUBLIC ${CMAKE_SOURCE_DIR}/src)

    if(MSVC)
        target_compile_options(${tgt} PRIVATE /W4 /permissive-)
        if(DSDR_WARNINGS_AS_ERRORS)
            target_compile_options(${tgt} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${tgt} PRIVATE -Wall -Wextra)
        if(DSDR_WARNINGS_AS_ERRORS)
            target_compile_options(${tgt} PRIVATE -Werror)
        endif()
    endif()

    if(DSDR_SANITIZE AND NOT MSVC)
        string(REPLACE ";" "," dsdr_san_list "${DSDR_SANITIZE}")
        # `-fno-omit-frame-pointer` non è un dettaglio: senza, lo stack che il
        # sanitizer stampa salta proprio i livelli che servono a capire chi ha
        # liberato la memoria.
        target_compile_options(${tgt} PRIVATE
            -fsanitize=${dsdr_san_list} -fno-omit-frame-pointer -g)
        target_link_options(${tgt} PRIVATE -fsanitize=${dsdr_san_list})
    endif()
endfunction()

# dsdr_link_qt_rhi(<target>)
#   Dà accesso a <rhi/qrhi.h>, necessario a QQuickRhiItem.
#
#   QRhi è documentata come parte dell'API di Qt Quick, ma i suoi header
#   stanno fra i privati di Qt Gui. Il componente CMake "GuiPrivate" è la via
#   pulita e c'è nella maggior parte delle distribuzioni; quando manca — capita
#   con alcuni pacchetti binari — si aggiunge a mano il percorso versionato,
#   che Qt organizza sempre allo stesso modo.
function(dsdr_link_qt_rhi tgt)
    if(TARGET Qt6::GuiPrivate)
        target_link_libraries(${tgt} PUBLIC Qt6::GuiPrivate)
        return()
    endif()

    get_target_property(qt_gui_includes Qt6::Gui INTERFACE_INCLUDE_DIRECTORIES)
    set(private_dirs)
    foreach(dir IN LISTS qt_gui_includes)
        if(EXISTS "${dir}/${Qt6_VERSION}/QtGui/rhi/qrhi.h")
            list(APPEND private_dirs "${dir}/${Qt6_VERSION}" "${dir}/${Qt6_VERSION}/QtGui")
        endif()
    endforeach()

    if(NOT private_dirs)
        message(FATAL_ERROR
            "Header privati di Qt Gui non trovati: QQuickRhiItem non può essere "
            "compilato.\n"
            "  Installare gli header privati di Qt (pacchetto qt6-base-private-dev "
            "su Debian/Ubuntu) oppure usare una distribuzione Qt che esponga il "
            "componente GuiPrivate.")
    endif()

    list(REMOVE_DUPLICATES private_dirs)
    target_include_directories(${tgt} PUBLIC ${private_dirs})
    message(STATUS "QRhi: header privati aggiunti a mano (${private_dirs})")
endfunction()

function(dsdr_print_configuration)
    message(STATUS "")
    message(STATUS "── DECODIUM SDR ${PROJECT_VERSION} ──────────────────────")
    message(STATUS "  Qt              : ${Qt6_VERSION}")
    message(STATUS "  Build type      : ${CMAKE_BUILD_TYPE}")
    message(STATUS "  Spettro GPU     : ${DSDR_GPU_SPECTRUM}")
    message(STATUS "  Bundle RTL-SDR  : ${DSDR_BUNDLE_RTLSDR}")
    # L'elenco è quello delle opzioni, non una lista scritta a mano: una lista
    # scritta a mano si dimentica di aggiornare, e `audiorig` e `hermes` sono
    # rimasti fuori per mesi. Chi guarda questa riga la guarda proprio per
    # sapere se un backend c'è — è successo con audiorig sparito dalla 1.1.3, e
    # questa riga diceva di sì tacendo.
    message(STATUS "  Backend attivi  :")
    foreach(be DEMO SOAPY NETTCP HERMES COLIBRI IQFILE AUDIORIG FLEX FLEX_PROBE
               HPSDR DLINK TCI KIWI HAMLIB)
        if(DSDR_BACKEND_${be})
            message(STATUS "      • ${be}")
        endif()
    endforeach()
    message(STATUS "─────────────────────────────────────────────────────────")
    message(STATUS "")
endfunction()
