# SPDX-License-Identifier: GPL-3.0-or-later
# L'ospite dei plugin VST3 (DSDR-SPEC-005 §4.5).
#
# Due bersagli e non uno, ed e' il punto di tutto: `dsdr_plugins` sta dentro il
# programma e non tocca l'SDK; `decodium-vst-host` e' un eseguibile a parte, ed
# e' l'unico che carica codice di terze parti. Se un plugin lo porta giu', il
# programma vede una pipe che si chiude.

# Il lato nostro: parla con l'ospite e non sa nemmeno che esista un SDK.
add_library(dsdr_plugins STATIC
    PluginProtocol.h
    PluginBridge.h
    PluginBridge.cpp
)
add_library(dsdr::plugins ALIAS dsdr_plugins)

target_link_libraries(dsdr_plugins PUBLIC Qt6::Core)
dsdr_apply_target_defaults(dsdr_plugins)

if(NOT DSDR_VST3)
    return()
endif()

# ── L'ospite ────────────────────────────────────────────────────────────
#
# Si compila solo la parte di SDK che serve a ospitare. Il resto — vstgui, gli
# esempi, gli strumenti di validazione — sono altre centinaia di file che non
# entrano in nessun binario nostro.
set(vst3 "${DSDR_VST3_SDK}")

add_executable(decodium-vst-host
    host/main.cpp

    # base: le stringhe e gli oggetti su cui poggia tutto l'SDK
    ${vst3}/base/source/baseiids.cpp
    ${vst3}/base/source/fbuffer.cpp
    ${vst3}/base/source/fdebug.cpp
    ${vst3}/base/source/fdynlib.cpp
    ${vst3}/base/source/fobject.cpp
    ${vst3}/base/source/fstreamer.cpp
    ${vst3}/base/source/fstring.cpp
    ${vst3}/base/source/updatehandler.cpp
    ${vst3}/base/thread/source/flock.cpp

    # pluginterfaces: gli identificativi delle interfacce
    ${vst3}/pluginterfaces/base/coreiids.cpp
    ${vst3}/pluginterfaces/base/funknown.cpp
    ${vst3}/pluginterfaces/base/ustring.cpp
    ${vst3}/pluginterfaces/base/conststringtable.cpp

    # public.sdk: le classi di hosting vere e proprie
    ${vst3}/public.sdk/source/common/commonstringconvert.cpp
    ${vst3}/public.sdk/source/vst/vstinitiids.cpp
    ${vst3}/public.sdk/source/vst/hosting/connectionproxy.cpp
    ${vst3}/public.sdk/source/vst/hosting/hostclasses.cpp
    ${vst3}/public.sdk/source/vst/hosting/module.cpp
    ${vst3}/public.sdk/source/vst/hosting/parameterchanges.cpp
    ${vst3}/public.sdk/source/vst/hosting/pluginterfacesupport.cpp
    ${vst3}/public.sdk/source/vst/hosting/plugprovider.cpp
    ${vst3}/public.sdk/source/vst/utility/stringconvert.cpp
)

if(WIN32)
    target_sources(decodium-vst-host PRIVATE
        ${vst3}/public.sdk/source/vst/hosting/module_win32.cpp
        ${vst3}/public.sdk/source/common/threadchecker_win32.cpp)
elseif(APPLE)
    target_sources(decodium-vst-host PRIVATE
        ${vst3}/public.sdk/source/vst/hosting/module_mac.mm
        ${vst3}/public.sdk/source/common/threadchecker_mac.mm)
else()
    target_sources(decodium-vst-host PRIVATE
        ${vst3}/public.sdk/source/vst/hosting/module_linux.cpp
        ${vst3}/public.sdk/source/common/threadchecker_linux.cpp)
endif()

target_include_directories(decodium-vst-host PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${vst3}
)

target_link_libraries(decodium-vst-host PRIVATE Qt6::Core)

if(WIN32)
    # `shell32` per i percorsi di sistema, `ole32` perche' su Windows i moduli
    # VST3 sono COM sotto la vernice.
    target_link_libraries(decodium-vst-host PRIVATE shell32 ole32)
endif()

# I warning dell'SDK non sono nostri da correggere, e in CI i warning sono
# errori: si tratta come codice di terze parti, che e' quello che e'.
target_compile_definitions(decodium-vst-host PRIVATE
    $<$<CONFIG:Release>:RELEASE=1>
    $<$<CONFIG:Debug>:DEVELOPMENT=1>
)

# Va accanto al programma, altrimenti non lo trova: il ponte lo cerca nella
# cartella dell'eseguibile, e non altrove — una ricerca nel PATH farebbe
# lanciare l'ospite di un'altra installazione, che parla lo stesso protocollo
# ma non e' detto sia la stessa versione.
install(TARGETS decodium-vst-host
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION bin
)

set_target_properties(decodium-vst-host PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    # Niente finestra della console: l'ospite parla su una pipe, non con
    # l'operatore, e una finestra nera che si apre a ogni avvio e' un difetto
    # che si e' gia' dovuto togliere una volta da questo programma.
    WIN32_EXECUTABLE OFF
)
