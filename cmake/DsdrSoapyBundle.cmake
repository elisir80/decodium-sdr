# DsdrSoapyBundle.cmake — raccoglie moduli SoapySDR che non sono dipendenze
# del binario principale e quindi sfuggono ai deploy automatici di Qt.
#
# Un modulo Soapy è un plugin caricato a runtime: non compare nella catena
# delle dipendenze dell'eseguibile. Senza questo passaggio l'app può mostrare
# il backend Soapy ma non il ricevitore che l'utente ha collegato, che è una
# forma particolarmente ingannevole di pacchetto incompleto.

include_guard(GLOBAL)

macro(dsdr_prepare_soapy_bundle)
    set(DSDR_SOAPY_BUNDLED_MODULE_FILES)
    # Chi prepara un pacchetto con un driver fuori dai prefissi standard deve
    # poterlo scegliere con certezza: i path espliciti precedono quelli
    # dedotti da CMake e dal sistema.
    set(DSDR_SOAPY_BUNDLE_SEARCH_PATHS ${DSDR_SOAPY_MODULE_PATHS})
    set(DSDR_SOAPY_BUNDLE_RUNTIME_DIRS)

    if(DSDR_BUNDLE_SOAPY AND NOT DSDR_BACKEND_SOAPY)
        message(FATAL_ERROR
            "DSDR_BUNDLE_SOAPY=ON ma DSDR_BACKEND_SOAPY=OFF: non c'è alcun "
            "loader SoapySDR nel pacchetto.")
    endif()

    if(DSDR_BUNDLE_SOAPY AND NOT DSDR_SOAPY_BUNDLE_MODULES)
        message(FATAL_ERROR
            "DSDR_BUNDLE_SOAPY=ON richiede almeno un nome in "
            "DSDR_SOAPY_BUNDLE_MODULES.")
    endif()

    if(DSDR_BUNDLE_SOAPY)

    # Il package config di Soapy vive in posti diversi: Homebrew usa
    # lib/cmake, Debian lib/<arch>/cmake, MSYS2 share/cmake. Risalire alcuni
    # livelli e provare tutte le directory modulo note evita path fissi e
    # funziona sia sui runner sia per chi prepara un pacchetto locale.
    set(dsdr_soapy_roots ${CMAKE_PREFIX_PATH}
        /opt/homebrew /usr/local /usr /opt/local)
    if(WIN32)
        list(APPEND dsdr_soapy_roots C:/msys64/mingw64 C:/msys64/ucrt64)
    endif()

    if(SoapySDR_DIR)
        set(dsdr_soapy_cursor "${SoapySDR_DIR}")
        foreach(dsdr_soapy_level RANGE 1 4)
            get_filename_component(dsdr_soapy_cursor "${dsdr_soapy_cursor}" DIRECTORY)
            list(APPEND dsdr_soapy_roots "${dsdr_soapy_cursor}")
        endforeach()
    endif()

    foreach(dsdr_soapy_root IN LISTS dsdr_soapy_roots)
        if(NOT dsdr_soapy_root)
            continue()
        endif()
        list(APPEND DSDR_SOAPY_BUNDLE_SEARCH_PATHS
            "${dsdr_soapy_root}/SoapySDR/modules0.8"
            "${dsdr_soapy_root}/lib/SoapySDR/modules0.8"
            "${dsdr_soapy_root}/lib64/SoapySDR/modules0.8"
            "${dsdr_soapy_root}/lib/${CMAKE_LIBRARY_ARCHITECTURE}/SoapySDR/modules0.8")
        list(APPEND DSDR_SOAPY_BUNDLE_RUNTIME_DIRS
            "${dsdr_soapy_root}"
            "${dsdr_soapy_root}/lib"
            "${dsdr_soapy_root}/lib64"
            "${dsdr_soapy_root}/bin")
    endforeach()
    list(REMOVE_DUPLICATES DSDR_SOAPY_BUNDLE_SEARCH_PATHS)
    list(REMOVE_DUPLICATES DSDR_SOAPY_BUNDLE_RUNTIME_DIRS)

    foreach(dsdr_soapy_module IN LISTS DSDR_SOAPY_BUNDLE_MODULES)
        unset(dsdr_soapy_module_file)
        unset(dsdr_soapy_module_file CACHE)
        if(IS_ABSOLUTE "${dsdr_soapy_module}")
            set(dsdr_soapy_module_file "${dsdr_soapy_module}")
        else()
            find_file(dsdr_soapy_module_file
                NAMES "${dsdr_soapy_module}${CMAKE_SHARED_MODULE_SUFFIX}"
                      "${dsdr_soapy_module}.so"
                      "${dsdr_soapy_module}.dll"
                      "${dsdr_soapy_module}.dylib"
                PATHS ${DSDR_SOAPY_BUNDLE_SEARCH_PATHS}
                NO_DEFAULT_PATH)
        endif()

        if(NOT dsdr_soapy_module_file OR NOT EXISTS "${dsdr_soapy_module_file}")
            message(FATAL_ERROR
                "Modulo SoapySDR richiesto ma non trovato: ${dsdr_soapy_module}\n"
                "  cercato in: ${DSDR_SOAPY_BUNDLE_SEARCH_PATHS}\n"
                "  installare il driver oppure passare "
                "-DDSDR_SOAPY_MODULE_PATHS=<directory-moduli>.")
        endif()

        list(APPEND DSDR_SOAPY_BUNDLED_MODULE_FILES "${dsdr_soapy_module_file}")
        unset(dsdr_soapy_module_file)
        unset(dsdr_soapy_module_file CACHE)
    endforeach()

    list(REMOVE_DUPLICATES DSDR_SOAPY_BUNDLED_MODULE_FILES)
    string(REPLACE ";" "\n" dsdr_soapy_manifest "${DSDR_SOAPY_BUNDLED_MODULE_FILES}")
    file(WRITE "${CMAKE_BINARY_DIR}/dsdr-soapy-modules.txt" "${dsdr_soapy_manifest}\n")

    message(STATUS "SoapySDR: moduli da includere")
    foreach(dsdr_soapy_module_file IN LISTS DSDR_SOAPY_BUNDLED_MODULE_FILES)
        message(STATUS "      • ${dsdr_soapy_module_file}")
    endforeach()
    endif()
endmacro()
