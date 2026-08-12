# SPDX-License-Identifier: GPL-3.0-or-later
# L'ospite dei plugin, e un plugin vero con cui provarlo.
#
# **Perche' si compila un plugin dentro i test.** La macchina di sviluppo non
# ne ha nessuno installato, e senza un plugin vero l'unica cosa dimostrabile
# sarebbe che il protocollo parla — non che l'audio attraversi davvero una
# libreria di terze parti e ne esca cambiato. Il ritardo d'esempio dell'SDK e'
# un effetto completo, con un parametro e una linea di ritardo: quello che gli
# si manda in ingresso torna spostato nel tempo, e uno spostamento e' la cosa
# piu' facile da misurare senza ambiguita'.
#
# Non finisce in nessun pacchetto: e' un attrezzo del banco di prova.

if(NOT DSDR_VST3)
    return()
endif()

set(vst3 "${DSDR_VST3_SDK}")
set(adelay "${vst3}/public.sdk/samples/vst/adelay/source")

add_library(dsdr_test_plugin MODULE
    ${adelay}/adelayprocessor.cpp
    ${adelay}/adelaycontroller.cpp
    ${adelay}/factory.cpp
    ${adelay}/exampletest.cpp

    # Il lato plugin dell'SDK: la fabbrica, i bus, i parametri.
    ${vst3}/public.sdk/source/main/pluginfactory.cpp
    ${vst3}/public.sdk/source/main/moduleinit.cpp
    ${vst3}/public.sdk/source/vst/vstaudioeffect.cpp
    ${vst3}/public.sdk/source/vst/vstbus.cpp
    ${vst3}/public.sdk/source/vst/vstcomponent.cpp
    ${vst3}/public.sdk/source/vst/vstcomponentbase.cpp
    ${vst3}/public.sdk/source/vst/vsteditcontroller.cpp
    ${vst3}/public.sdk/source/vst/vstinitiids.cpp
    ${vst3}/public.sdk/source/vst/vstparameters.cpp
    ${vst3}/public.sdk/source/common/pluginview.cpp
    ${vst3}/public.sdk/source/common/commoniids.cpp
    ${vst3}/public.sdk/source/vst/utility/testing.cpp
    ${vst3}/public.sdk/source/vst/utility/stringconvert.cpp
    ${vst3}/public.sdk/source/common/commonstringconvert.cpp
    ${vst3}/base/source/baseiids.cpp
    ${vst3}/base/source/fbuffer.cpp
    ${vst3}/base/source/fdebug.cpp
    ${vst3}/base/source/fobject.cpp
    ${vst3}/base/source/fstreamer.cpp
    ${vst3}/base/source/fstring.cpp
    ${vst3}/base/source/updatehandler.cpp
    ${vst3}/base/thread/source/flock.cpp
    ${vst3}/pluginterfaces/base/coreiids.cpp
    ${vst3}/pluginterfaces/base/funknown.cpp
    ${vst3}/pluginterfaces/base/ustring.cpp
    ${vst3}/pluginterfaces/base/conststringtable.cpp
)

if(WIN32)
    target_sources(dsdr_test_plugin PRIVATE
        ${vst3}/public.sdk/source/main/dllmain.cpp)
elseif(APPLE)
    target_sources(dsdr_test_plugin PRIVATE
        ${vst3}/public.sdk/source/main/macmain.cpp)
else()
    target_sources(dsdr_test_plugin PRIVATE
        ${vst3}/public.sdk/source/main/linuxmain.cpp)
endif()

# Il ritardo d'esempio include `projectversion.h`, che normalmente glielo
# genera il suo progetto CMake. Qui lo si scrive a mano: e' meno di generare
# meta' dell'albero di build dell'SDK per tre numeri.
set(genver "${CMAKE_BINARY_DIR}/vst3-generated")
file(MAKE_DIRECTORY "${genver}")
file(WRITE "${genver}/projectversion.h" "#pragma once
#define MAJOR_VERSION_STR \"1\"
#define MAJOR_VERSION_INT 1
#define SUB_VERSION_STR \"0\"
#define SUB_VERSION_INT 0
#define RELEASE_NUMBER_STR \"0\"
#define RELEASE_NUMBER_INT 0
#define BUILD_NUMBER_STR \"1\"
#define BUILD_NUMBER_INT 1
#define FULL_VERSION_STR \"1.0.0.1\"
#define VERSION_STR \"1.0.0\"
")

target_include_directories(dsdr_test_plugin PRIVATE ${vst3} "${genver}")
target_compile_definitions(dsdr_test_plugin PRIVATE
    $<$<CONFIG:Debug>:DEVELOPMENT=1>
    $<$<NOT:$<CONFIG:Debug>>:RELEASE=1>
)

# Su Windows un VST3 e' una DLL con estensione .vst3: nessun bundle, nessuna
# cartella. E' la sola piattaforma su cui il caricamento si prova cosi' a buon
# mercato, ed e' quella su cui si sviluppa.
set_target_properties(dsdr_test_plugin PROPERTIES
    OUTPUT_NAME "dsdr-ritardo-di-prova"
    PREFIX ""
    SUFFIX ".vst3"
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

dsdr_add_test(tst_plugin_host
    SOURCES tst_plugin_host.cpp
    LIBRARIES dsdr::plugins)

# Il test carica il plugin che sta accanto a se': il percorso arriva da qui,
# perche' trovarlo a runtime vorrebbe dire indovinare la cartella di build.
target_compile_definitions(tst_plugin_host PRIVATE
    DSDR_TEST_PLUGIN="$<TARGET_FILE:dsdr_test_plugin>")
add_dependencies(tst_plugin_host dsdr_test_plugin decodium-vst-host)
