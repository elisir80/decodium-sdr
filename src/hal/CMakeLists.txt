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
        backends/nettcp/NetTcpBackend.h
        backends/nettcp/NetTcpBackend.cpp
    )
    list(APPEND DSDR_HAL_DEFINES DSDR_BACKEND_NETTCP)
    set(DSDR_HAL_NEEDS_NETWORK ON)
endif()

add_library(dsdr_hal STATIC ${DSDR_HAL_SOURCES})

target_compile_definitions(dsdr_hal PUBLIC ${DSDR_HAL_DEFINES})
target_link_libraries(dsdr_hal PUBLIC dsdr::common dsdr::dsp Qt6::Core)

if(DSDR_HAL_NEEDS_NETWORK)
    target_link_libraries(dsdr_hal PUBLIC Qt6::Network)
endif()

dsdr_apply_target_defaults(dsdr_hal)
add_library(dsdr::hal ALIAS dsdr_hal)
