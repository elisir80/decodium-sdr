# DsdrOptions.cmake — opzioni di build di DECODIUM SDR.
#
# RNF-06: ogni backend è compilabile/escludibile a build-time. Nessun backend
# è dipendenza obbligatoria del core: il seam HAL resta l'unico contratto.

option(DSDR_BACKEND_DEMO    "Backend sintetico (RF-09) — sempre consigliato ON" ON)
option(DSDR_BACKEND_SOAPY   "Backend SoapySDR (RF-01)"                          OFF)
option(DSDR_BACKEND_NETTCP  "Backend rtl_tcp / SpyServer (RF-07)"               ON)
option(DSDR_BACKEND_HPSDR   "Backend OpenHPSDR P1/P2 (RF-02)"                   OFF)
option(DSDR_BACKEND_DLINK   "Backend DLINK / DECODIUM SDR One (RF-03)"          OFF)
option(DSDR_BACKEND_FLEX    "Backend FlexRadio SmartSDR (RF-04)"                OFF)
option(DSDR_BACKEND_TCI     "Backend TCI / SunSDR (RF-05)"                      OFF)
option(DSDR_BACKEND_KIWI    "Backend KiwiSDR (RF-06)"                           OFF)
option(DSDR_BACKEND_HAMLIB  "Bridge CAT Hamlib (RF-08)"                         OFF)

# §6.1: il path CPU dello spettro è una decisione di build, non un fallback runtime.
option(DSDR_GPU_SPECTRUM    "Spettro/waterfall su GPU via QQuickRhiItem"         ON)

option(DSDR_BUILD_TESTS        "Compila la suite di test (RNF-07)"              ON)
option(DSDR_WARNINGS_AS_ERRORS "Tratta i warning del compilatore come errori"   OFF)

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
endfunction()

function(dsdr_print_configuration)
    message(STATUS "")
    message(STATUS "── DECODIUM SDR ${PROJECT_VERSION} ──────────────────────")
    message(STATUS "  Qt              : ${Qt6_VERSION}")
    message(STATUS "  Build type      : ${CMAKE_BUILD_TYPE}")
    message(STATUS "  Spettro GPU     : ${DSDR_GPU_SPECTRUM}")
    message(STATUS "  Backend attivi  :")
    foreach(be DEMO SOAPY NETTCP HPSDR DLINK FLEX TCI KIWI HAMLIB)
        if(DSDR_BACKEND_${be})
            message(STATUS "      • ${be}")
        endif()
    endforeach()
    message(STATUS "─────────────────────────────────────────────────────────")
    message(STATUS "")
endfunction()
