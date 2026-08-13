add_library(dsdr_core STATIC
    SpectrumFeed.h
    SpectrumFeed.cpp
    DspEngine.h
    DspEngine.cpp
    TxEngine.h
    TxEngine.cpp
    TxProfiles.h
    TxProfiles.cpp
    Greyline.h
    Greyline.cpp
    RotorController.h
    RotorController.cpp
    IqModuleApi.h
    ChannelModel.h
    ChannelModel.cpp
    DeviceListModel.h
    DeviceListModel.cpp
    CapabilitiesInfo.h
    IqRecorder.h
    IqRecorder.cpp
    IqRecorderWriter.h
    IqRecorderWriter.cpp
    LanguageManager.h
    LanguageManager.cpp
    SessionManager.h
    SessionManager.cpp
)

# Qt6::Qml serve solo a LanguageManager, per rivalutare i binding qsTr() al
# cambio di lingua senza riavviare l'applicazione.
target_link_libraries(dsdr_core
    PUBLIC dsdr::common dsdr::dsp dsdr::hal dsdr::audio
    dsdr::plugins
           Qt6::Core Qt6::Gui Qt6::Network Qt6::Qml)

dsdr_apply_target_defaults(dsdr_core)
add_library(dsdr::core ALIAS dsdr_core)

install(FILES IqModuleApi.h DESTINATION include/decodium)
