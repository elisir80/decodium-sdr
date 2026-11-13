add_library(dsdr_core STATIC
    SpectrumFeed.h
    SpectrumFeed.cpp
    AdsbMessage.h
    AdsbMessage.cpp
    AdsbReceiver.h
    AdsbReceiver.cpp
    AsyncIqDecoder.h
    AsyncIqDecoder.cpp
    DecoderShutdown.h
    DecoderShutdown.cpp
    BackendShutdown.h
    BackendShutdown.cpp
    BackendShutdownViewModel.h
    BackendShutdownViewModel.cpp
    AdsbController.h
    AdsbController.cpp
    AprsReceiver.h
    AprsReceiver.cpp
    AprsController.h
    AprsController.cpp
    AisReceiver.h
    AisReceiver.cpp
    AisController.h
    AisController.cpp
    AisEngineTap.h
    AisEngineTap.cpp
    AisIqGapFence.h
    AcarsReceiver.h
    AcarsReceiver.cpp
    AcarsController.h
    AcarsController.cpp
    ../dsp/acars/acars_chan.cpp
    VorAsyncReceiver.h
    VorAsyncReceiver.cpp
    VorController.h
    VorController.cpp
    VorEngineTap.h
    VorEngineTap.cpp
    ../dsp/VorReceiver.cpp
    ../dsp/VorIdentDecoder.cpp
    SelcalReceiver.h
    SelcalReceiver.cpp
    SelcalController.h
    SelcalController.cpp
    ../dsp/SelcalAudioDecoder.cpp
    DscReceiver.h
    DscReceiver.cpp
    DscController.h
    DscController.cpp
    NavtexReceiver.h
    NavtexReceiver.cpp
    NavtexController.h
    NavtexController.cpp
    ../dsp/dsc/DscRx.cpp
    ../dsp/dsc/FskAudioRx.cpp
    PocsagReceiver.h
    PocsagReceiver.cpp
    PocsagController.h
    PocsagController.cpp
    ../dsp/PocsagDecoder.cpp
    ../dsp/PocsagAudioDecoder.cpp
    ../dsp/PocsagIqReceiver.cpp
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
    BandConditions.h
    BandConditions.cpp
    IqModuleApi.h
    IqModuleManager.h
    IqModuleManager.cpp
    OperationScheduler.h
    OperationScheduler.cpp
    ChannelModel.h
    ChannelModel.cpp
    DeviceListModel.h
    DeviceListModel.cpp
    CapabilitiesInfo.h
    IqAnalysisLab.h
    IqAnalysisLab.cpp
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

if(DSDR_BUILD_VDL2_PROTOCOL_EXPERIMENT)
    # Kept separate so switching the experiment off cannot leave its Qt
    # metaobjects in the regular core target's generated autogen sources.
    add_library(dsdr_vdl2_async STATIC Vdl2AsyncStream.cpp Vdl2AsyncStream.h Vdl2IqPipeline.cpp Vdl2IqPipeline.h Vdl2ResultQueue.cpp Vdl2ResultQueue.h Vdl2Session.cpp Vdl2Session.h Vdl2IqIngress.cpp Vdl2IqIngress.h Vdl2Resources.h Vdl2MessageModel.cpp Vdl2MessageModel.h Vdl2Controller.cpp Vdl2Controller.h Vdl2Lifecycle.cpp Vdl2Lifecycle.h Vdl2SessionBinding.cpp Vdl2SessionBinding.h)
    target_link_libraries(dsdr_vdl2_async PUBLIC dsdr::core dsdr_vdl2_adapter)
    target_sources(dsdr_vdl2_async PRIVATE Vdl2ResourcePreparation.cpp Vdl2ResourcePreparation.h Vdl2AppManager.cpp Vdl2AppManager.h)
    dsdr_apply_target_defaults(dsdr_vdl2_async)
endif()

install(FILES IqModuleApi.h DESTINATION include/decodium)
