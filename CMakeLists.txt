cmake_minimum_required(VERSION 3.24)

project(decodium-sdr
    VERSION 1.1.3
    DESCRIPTION "DECODIUM SDR — client SDR universale dell'ecosistema DECODIUM"
    HOMEPAGE_URL "https://decodium.it"
    # Il C serve a RNNoise, che è l'unico codice C del progetto: senza,
    # i suoi sorgenti verrebbero ignorati in silenzio e la libreria uscirebbe
    # vuota — è già successo.
    LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Tipo di build" FORCE)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
include(DsdrOptions)

find_package(Qt6 6.8 REQUIRED COMPONENTS
    Core Gui Network Qml Quick QuickControls2 Multimedia Concurrent
    ShaderTools Test QuickTest LinguistTools)

# QRhi è API pubblica di Qt Quick ma vive negli header privati di Qt Gui, e non
# tutte le distribuzioni espongono il componente CMake "GuiPrivate". Se manca,
# gli header si raggiungono per percorso: vedi dsdr_link_qt_rhi().
find_package(Qt6 6.8 QUIET OPTIONAL_COMPONENTS GuiPrivate)

qt_standard_project_setup(REQUIRES 6.8)

enable_testing()

# RNNoise, se chi compila ne ha messo il sorgente in third_party/. La
# capability dello stadio neurale nasce da qui: non c'è un fallback a runtime,
# perché una funzione che a volte c'è e a volte no è peggio di una che non c'è.
if(DSDR_NEURAL_NR)
    include(FindRnnoise)
endif()

add_subdirectory(src)

if(DSDR_BUILD_TESTS)
    add_subdirectory(tests)
endif()

# Il programma di installazione. Va per ultimo: CPack vuole sapere che cosa si
# installa, e lo sa solo dopo che i sottoprogetti hanno dichiarato i loro
# `install()`.
include(DsdrPackaging)

dsdr_print_configuration()
