add_library(dsdr_core STATIC
    SpectrumFeed.h
    SpectrumFeed.cpp
    DspEngine.h
    DspEngine.cpp
    ChannelModel.h
    ChannelModel.cpp
    DeviceListModel.h
    DeviceListModel.cpp
    CapabilitiesInfo.h
    SessionManager.h
    SessionManager.cpp
)

target_link_libraries(dsdr_core
    PUBLIC dsdr::common dsdr::dsp dsdr::hal dsdr::audio Qt6::Core Qt6::Gui)

dsdr_apply_target_defaults(dsdr_core)
add_library(dsdr::core ALIAS dsdr_core)
