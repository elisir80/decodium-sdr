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

if(DSDR_BACKEND_FLEX_PROBE)
    # Metà del protocollo SmartSDR: il canale di comando. Non è un backend —
    # non riceve campioni — ma dice se la radio risponde e chi è.
    list(APPEND DSDR_HAL_SOURCES
        backends/flex/FlexProtocol.h
        backends/flex/FlexProtocol.cpp
        backends/flex/FlexClient.h
        backends/flex/FlexClient.cpp
        backends/flex/FlexVita.h
        backends/flex/FlexVita.cpp
    )
    list(APPEND DSDR_HAL_DEFINES DSDR_BACKEND_FLEX_PROBE)
    set(DSDR_HAL_NEEDS_NETWORK ON)
endif()

if(DSDR_BACKEND_HERMES)
    # Solo UDP: il protocollo è aperto e non serve nessuna libreria.
    list(APPEND DSDR_HAL_SOURCES
        backends/hermes/HermesProtocol.h
        backends/hermes/HermesProtocol.cpp
        backends/hermes/HermesWorker.h
        backends/hermes/HermesWorker.cpp
        backends/hermes/HermesBackend.h
        backends/hermes/HermesBackend.cpp
    )
    list(APPEND DSDR_HAL_DEFINES DSDR_BACKEND_HERMES)
    set(DSDR_HAL_NEEDS_NETWORK ON)
endif()

# Quello che si e' compilato davvero, non quello che si e' chiesto. Le due
# cose divergono quando un modulo Qt manca, e chi sta fuori di qui — i test —
# deve poter leggere la prima. `INTERNAL` perche' non e' una scelta di chi
# compila: si ricalcola a ogni configurazione, e installando il modulo mancante
# torna da sé al valore giusto.
set(DSDR_BUILT_AUDIORIG OFF CACHE INTERNAL "audiorig compilato davvero")

if(DSDR_BACKEND_AUDIORIG)
    # Serve la seriale (piano di controllo) e il multimedia (piano dati). Se
    # Qt non li porta, il backend si spegne da sé con una spiegazione invece di
    # far fallire tutta la configurazione: chi non ha una radio tradizionale
    # non deve installare due moduli Qt per compilare.
    find_package(Qt6 QUIET COMPONENTS SerialPort Multimedia)
    if(Qt6SerialPort_FOUND AND Qt6Multimedia_FOUND)
        list(APPEND DSDR_HAL_SOURCES
            backends/audiorig/ICatDriver.h
            backends/audiorig/NewcatDriver.h
            backends/audiorig/NewcatDriver.cpp
            backends/audiorig/CivDriver.h
            backends/audiorig/CivDriver.cpp
            backends/audiorig/CatController.h
            backends/audiorig/CatController.cpp
            backends/audiorig/AudiorigBackend.h
            backends/audiorig/AudiorigBackend.cpp
        )
        list(APPEND DSDR_HAL_DEFINES DSDR_BACKEND_AUDIORIG)
        set(DSDR_HAL_NEEDS_SERIAL ON)
        set(DSDR_BUILT_AUDIORIG ON CACHE INTERNAL "audiorig compilato davvero")
    else()
        # Chi compila per sé può farne a meno, e lo si dice e basta. Chi
        # costruisce un pacchetto per altri no: un backend che sparisce in
        # silenzio produce un programma che non vede una radio che c'è, e chi
        # lo scopre è l'operatore, davanti a un elenco di device vuoto. È già
        # successo — la 1.1.3 è uscita senza audiorig perché questo messaggio
        # stava in mezzo a trecento righe di log e non l'ha letto nessuno.
        set(dsdr_audiorig_why
            "audiorig disattivato: mancano Qt6::SerialPort o Qt6::Multimedia\n"
            "  MSYS2 : pacman -S mingw-w64-x86_64-qt6-serialport\n"
            "  Debian: apt install qt6-serialport-dev")
        if(DSDR_STRICT_BACKENDS)
            message(FATAL_ERROR ${dsdr_audiorig_why}
                    "\nDSDR_STRICT_BACKENDS è attivo: un pacchetto senza questo "
                    "backend non parlerebbe con nessuna radio via CAT.")
        endif()
        message(STATUS ${dsdr_audiorig_why})
        set(DSDR_BACKEND_AUDIORIG OFF)
    endif()
endif()

if(DSDR_BACKEND_SOAPY)
    # SoapySDR è opzionale: se manca si spiega come installarlo invece di
    # lasciare all'utente un errore di CMake da decifrare.
    # La build locale ha la precedenza su quella di sistema.
    #
    # Su MSYS2 il pacchetto di SoapySDR manda l'applicazione in crash, e per
    # questo lo si compila a parte in `third_party/soapysdr`. Finora il
    # percorso andava passato a mano con `-DSoapySDR_DIR=...`: chi se ne
    # dimenticava otteneva una build che *sembrava* buona e moriva all'avvio
    # con `0xC0000139`, perché al link e all'esecuzione rispondevano due
    # librerie diverse. La cartella se la cerca CMake, come fa già per la
    # libreria del ColibriNANO e per RNNoise.
    if(NOT SoapySDR_DIR AND EXISTS "${CMAKE_SOURCE_DIR}/third_party/soapysdr/cmake")
        set(SoapySDR_DIR "${CMAKE_SOURCE_DIR}/third_party/soapysdr/cmake")
        message(STATUS "SoapySDR: build locale in third_party/soapysdr")
    endif()

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

# Il rilevamento in rete non è un backend e non dipende da nessuno di essi:
# dice quali radio sono raggiungibili, comprese quelle che non sappiamo ancora
# aprire. Ha bisogno della rete, sempre.
list(APPEND DSDR_HAL_SOURCES RadioScout.h RadioScout.cpp)
set(DSDR_HAL_NEEDS_NETWORK ON)

add_library(dsdr_hal STATIC ${DSDR_HAL_SOURCES})

target_compile_definitions(dsdr_hal PUBLIC ${DSDR_HAL_DEFINES})
target_link_libraries(dsdr_hal PUBLIC dsdr::common dsdr::dsp Qt6::Core)

if(DSDR_HAL_NEEDS_NETWORK)
    target_link_libraries(dsdr_hal PUBLIC Qt6::Network)
endif()

if(DSDR_HAL_NEEDS_SERIAL)
    # Il piano dati di `audiorig` è una scheda audio, e per leggerla si passa
    # da dsdr::audio: è l'unico backend che dipende dallo strato audio, e la
    # dipendenza va in quel verso solo — audio non conosce la HAL.
    target_link_libraries(dsdr_hal PUBLIC Qt6::SerialPort dsdr::audio)
endif()

if(DSDR_BACKEND_SOAPY)
    target_link_libraries(dsdr_hal PRIVATE SoapySDR)

    # Su Windows la libreria dinamica va messa accanto agli eseguibili.
    #
    # Non è un vezzo: se SoapySDR viene da `third_party/` — la build locale,
    # perché quella dei pacchetti MSYS2 va in crash — ma la sua DLL non è lì,
    # Windows va a pescarne un'altra dal PATH. È una copia diversa, con gli
    # stessi nomi e simboli diversi, e il programma muore all'avvio con
    # `0xC0000139`, «entry point non trovato»: un messaggio che non nomina né
    # la libreria né il simbolo, e manda a cercare il guasto ovunque tranne
    # che qui. È già costato una release.
    if(WIN32)
        get_target_property(dsdr_soapy_config SoapySDR IMPORTED_CONFIGURATIONS)
        if(dsdr_soapy_config)
            list(GET dsdr_soapy_config 0 dsdr_soapy_first)
            get_target_property(dsdr_soapy_lib SoapySDR
                                IMPORTED_LOCATION_${dsdr_soapy_first})
        endif()

        if(dsdr_soapy_lib)
            get_filename_component(dsdr_soapy_dir "${dsdr_soapy_lib}" DIRECTORY)
            get_filename_component(dsdr_soapy_root "${dsdr_soapy_dir}" DIRECTORY)
            find_file(DSDR_SOAPY_DLL
                NAMES libSoapySDR.dll SoapySDR.dll
                HINTS "${dsdr_soapy_root}/bin" "${dsdr_soapy_dir}"
                NO_DEFAULT_PATH)

            if(DSDR_SOAPY_DLL)
                add_custom_command(TARGET dsdr_hal POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                            "${DSDR_SOAPY_DLL}" "${CMAKE_BINARY_DIR}/bin"
                    COMMENT "SoapySDR: libreria accanto agli eseguibili")
                install(FILES "${DSDR_SOAPY_DLL}" DESTINATION bin)
            endif()
        endif()
    endif()
endif()

dsdr_apply_target_defaults(dsdr_hal)
add_library(dsdr::hal ALIAS dsdr_hal)
