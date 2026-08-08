# HAL — il seam. Nessun target sopra questo livello può includere un header
# sotto backends/: la conformance suite e il check di CI lo verificano.

set(DSDR_HAL_SOURCES
    HalLog.h
    Frames.h
    DeviceDescriptor.h
    BackendCapabilities.h
    IRadioBackend.h
    IRadioBackend.cpp
    BackendRegistry.h
    BackendRegistry.cpp
)

set(DSDR_HAL_DEFINES)

if(DSDR_BACKEND_DEMO)
    list(APPEND DSDR_HAL_SOURCES
        backends/demo/MorseKeyer.h
        backends/demo/MorseKeyer.cpp
        backends/demo/SyntheticBand.h
        backends/demo/SyntheticBand.cpp
        backends/demo/DemoWorker.h
        backends/demo/DemoWorker.cpp
        backends/demo/DemoBackend.h
        backends/demo/DemoBackend.cpp
    )
    list(APPEND DSDR_HAL_DEFINES DSDR_BACKEND_DEMO)
endif()

if(DSDR_BACKEND_NETTCP)
    list(APPEND DSDR_HAL_SOURCES
        backends/nettcp/RtlTcpProtocol.h
        backends/nettcp/EndpointProbe.h
        backends/nettcp/EndpointProbe.cpp
        backends/nettcp/RtlTcpClient.h
        backends/nettcp/RtlTcpClient.cpp
        backends/nettcp/SpyServerProtocol.h
        backends/nettcp/SpyServerClient.h
        backends/nettcp/SpyServerClient.cpp
        backends/nettcp/NetTcpBackend.h
        backends/nettcp/NetTcpBackend.cpp
    )
    list(APPEND DSDR_HAL_DEFINES DSDR_BACKEND_NETTCP)
    set(DSDR_HAL_NEEDS_NETWORK ON)
endif()

if(DSDR_BACKEND_COLIBRI)
    # Nessuna dipendenza da trovare: colibrinano_lib si carica a runtime, e se
    # manca il backend semplicemente non trova device.
    list(APPEND DSDR_HAL_SOURCES
        backends/colibri/ColibriLibrary.h
        backends/colibri/ColibriLibrary.cpp
        backends/colibri/ColibriBackend.h
        backends/colibri/ColibriBackend.cpp
    )
    list(APPEND DSDR_HAL_DEFINES DSDR_BACKEND_COLIBRI)
endif()

if(DSDR_BACKEND_IQFILE)
    # Nessuna dipendenza esterna: il formato lo scriviamo noi e si legge con
    # QFile. Il backend resta compilabile anche dove non c'è alcun hardware.
    list(APPEND DSDR_HAL_SOURCES
        backends/iqfile/IqFileReader.h
        backends/iqfile/IqFileReader.cpp
        backends/iqfile/IqFileWorker.h
        backends/iqfile/IqFileWorker.cpp
        backends/iqfile/IqFileBackend.h
        backends/iqfile/IqFileBackend.cpp
    )
    list(APPEND DSDR_HAL_DEFINES DSDR_BACKEND_IQFILE)
endif()

if(DSDR_BACKEND_SOAPY)
    # SoapySDR è opzionale: se manca si spiega come installarlo invece di
    # lasciare all'utente un errore di CMake da decifrare.
    find_package(SoapySDR CONFIG QUIET)
    if(NOT SoapySDR_FOUND)
        message(FATAL_ERROR
            "SoapySDR non trovato ma DSDR_BACKEND_SOAPY=ON.\n"
            "  MSYS2 : pacman -S mingw-w64-x86_64-soapysdr\n"
            "  Debian: apt install libsoapysdr-dev\n"
            "  macOS : brew install soapysdr\n"
            "Oppure disattiva il backend con -DDSDR_BACKEND_SOAPY=OFF.")
    endif()

    list(APPEND DSDR_HAL_SOURCES
        backends/soapy/SoapyProfile.h
        backends/soapy/SoapyProfile.cpp
        backends/soapy/SoapyWorker.h
        backends/soapy/SoapyWorker.cpp
        backends/soapy/SoapyBackend.h
        backends/soapy/SoapyBackend.cpp
    )
    list(APPEND DSDR_HAL_DEFINES DSDR_BACKEND_SOAPY)
endif()

add_library(dsdr_hal STATIC ${DSDR_HAL_SOURCES})

target_compile_definitions(dsdr_hal PUBLIC ${DSDR_HAL_DEFINES})
target_link_libraries(dsdr_hal PUBLIC dsdr::common dsdr::dsp Qt6::Core)

if(DSDR_HAL_NEEDS_NETWORK)
    target_link_libraries(dsdr_hal PUBLIC Qt6::Network)
endif()

if(DSDR_BACKEND_SOAPY)
    target_link_libraries(dsdr_hal PRIVATE SoapySDR)
endif()

dsdr_apply_target_defaults(dsdr_hal)
add_library(dsdr::hal ALIAS dsdr_hal)
