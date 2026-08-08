# DsdrOptions.cmake — opzioni di build di DECODIUM SDR.
#
# RNF-06: ogni backend è compilabile/escludibile a build-time. Nessun backend
# è dipendenza obbligatoria del core: il seam HAL resta l'unico contratto.

option(DSDR_BACKEND_DEMO    "Backend sintetico (RF-09) — sempre consigliato ON" ON)
option(DSDR_BACKEND_SOAPY   "Backend SoapySDR (RF-01)"                          ON)
option(DSDR_BACKEND_NETTCP  "Backend rtl_tcp / SpyServer (RF-07)"               ON)
option(DSDR_BACKEND_HPSDR   "Backend OpenHPSDR P1/P2 (RF-02)"                   OFF)
option(DSDR_BACKEND_DLINK   "Backend DLINK / DECODIUM SDR One (RF-03)"          OFF)
option(DSDR_BACKEND_COLIBRI "Backend ColibriNANO (Expert Electronics)"          ON)
option(DSDR_BACKEND_IQFILE  "Riproduzione di registrazioni IQ (RF-17)"          ON)
option(DSDR_BACKEND_FLEX    "Backend FlexRadio SmartSDR (RF-04)"                OFF)
option(DSDR_BACKEND_TCI     "Backend TCI / SunSDR (RF-05)"                      OFF)
option(DSDR_BACKEND_KIWI    "Backend KiwiSDR (RF-06)"                           OFF)
option(DSDR_BACKEND_HAMLIB  "Bridge CAT Hamlib (RF-08)"                         OFF)

# §6.1: il path CPU dello spettro è una decisione di build, non un fallback runtime.
option(DSDR_GPU_SPECTRUM    "Spettro/waterfall su GPU via QQuickRhiItem"         ON)

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
    message(STATUS "  Backend attivi  :")
    foreach(be DEMO SOAPY NETTCP COLIBRI IQFILE HPSDR DLINK FLEX TCI KIWI HAMLIB)
        if(DSDR_BACKEND_${be})
            message(STATUS "      • ${be}")
        endif()
    endforeach()
    message(STATUS "─────────────────────────────────────────────────────────")
    message(STATUS "")
endfunction()
