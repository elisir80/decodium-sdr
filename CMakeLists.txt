cmake_minimum_required(VERSION 3.24)

project(decodium-sdr
    VERSION 0.1.0
    DESCRIPTION "DECODIUM SDR — client SDR universale dell'ecosistema DECODIUM"
    HOMEPAGE_URL "https://decodium.it"
    LANGUAGES CXX)

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
    Core Gui Qml Quick QuickControls2 Multimedia Concurrent ShaderTools Test QuickTest
    GuiPrivate) # QRhi: API pubblica di Qt Quick ma header privati di Qt Gui

qt_standard_project_setup(REQUIRES 6.8)

enable_testing()

add_subdirectory(src)

if(DSDR_BUILD_TESTS)
    add_subdirectory(tests)
endif()

dsdr_print_configuration()
