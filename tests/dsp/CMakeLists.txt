dsdr_add_test(tst_dsp_primitives
    SOURCES tst_dsp_primitives.cpp
    LIBRARIES dsdr::dsp)

add_executable(tst_aprs_afsk ../standalone/tst_aprs_afsk.cpp)
target_link_libraries(tst_aprs_afsk PRIVATE dsdr::dsp)
dsdr_apply_target_defaults(tst_aprs_afsk)
add_test(NAME tst_aprs_afsk COMMAND tst_aprs_afsk)

add_executable(tst_aprs_position_robust ../standalone/test_aprs_position_robust.cpp)
target_include_directories(tst_aprs_position_robust PRIVATE ../../src)
target_link_libraries(tst_aprs_position_robust PRIVATE Qt6::Core)
target_compile_definitions(tst_aprs_position_robust PRIVATE QT_FORCE_ASSERTS)
dsdr_apply_target_defaults(tst_aprs_position_robust)
add_test(NAME tst_aprs_position_robust COMMAND tst_aprs_position_robust)
set_tests_properties(tst_aprs_position_robust PROPERTIES TIMEOUT 60)

# Isolated POCSAG bitstream decoder; not yet an application receiver.
add_executable(tst_pocsag_bitstream ../standalone/pocsag/test.cpp ../../src/dsp/PocsagDecoder.cpp)
target_include_directories(tst_pocsag_bitstream PRIVATE ../../src)
dsdr_apply_target_defaults(tst_pocsag_bitstream)
add_test(NAME tst_pocsag_bitstream COMMAND tst_pocsag_bitstream)
add_executable(tst_pocsag_audio ../standalone/pocsag/test_audio.cpp
    ../../src/dsp/PocsagDecoder.cpp ../../src/dsp/PocsagAudioDecoder.cpp)
target_include_directories(tst_pocsag_audio PRIVATE ../../src)
dsdr_apply_target_defaults(tst_pocsag_audio)
add_test(NAME tst_pocsag_audio COMMAND tst_pocsag_audio)
set_tests_properties(tst_pocsag_audio PROPERTIES TIMEOUT 120)
add_executable(tst_pocsag_iq ../standalone/pocsag/test_iq.cpp
    ../../src/dsp/PocsagDecoder.cpp ../../src/dsp/PocsagAudioDecoder.cpp
    ../../src/dsp/PocsagIqReceiver.cpp)
target_include_directories(tst_pocsag_iq PRIVATE ../../src)
dsdr_apply_target_defaults(tst_pocsag_iq)
add_test(NAME tst_pocsag_iq COMMAND tst_pocsag_iq)
set_tests_properties(tst_pocsag_iq PROPERTIES TIMEOUT 120)
add_executable(tst_selcal_audio ../standalone/test_selcal_audio.cpp ../../src/dsp/SelcalAudioDecoder.cpp)
target_include_directories(tst_selcal_audio PRIVATE ../../src)
dsdr_apply_target_defaults(tst_selcal_audio)
add_test(NAME tst_selcal_audio COMMAND tst_selcal_audio)
set_tests_properties(tst_selcal_audio PROPERTIES ENVIRONMENT "DSDR_TEST_SILENT_AUDIO=1" TIMEOUT 60)
add_executable(tst_selcal_worker ../standalone/test_selcal_worker.cpp ../../src/core/SelcalReceiver.cpp
    ../../src/core/AsyncIqDecoder.cpp ../../src/dsp/SelcalAudioDecoder.cpp)
target_include_directories(tst_selcal_worker PRIVATE ../../src)
dsdr_apply_target_defaults(tst_selcal_worker)
add_test(NAME tst_selcal_worker COMMAND tst_selcal_worker)
set_tests_properties(tst_selcal_worker PROPERTIES ENVIRONMENT "DSDR_TEST_SILENT_AUDIO=1" TIMEOUT 60)
add_executable(tst_pocsag_ingress ../standalone/pocsag/test_engine_ingress.cpp
    ../../src/core/PocsagReceiver.cpp ../../src/core/AsyncIqDecoder.cpp ../../src/dsp/PocsagIqReceiver.cpp
    ../../src/dsp/PocsagAudioDecoder.cpp ../../src/dsp/PocsagDecoder.cpp)
target_include_directories(tst_pocsag_ingress PRIVATE ../../src)
dsdr_apply_target_defaults(tst_pocsag_ingress)
add_test(NAME tst_pocsag_ingress COMMAND tst_pocsag_ingress)
set_tests_properties(tst_pocsag_ingress PROPERTIES TIMEOUT 120)
add_executable(tst_pocsag_gap ../standalone/pocsag/test_gap_barrier.cpp)
target_include_directories(tst_pocsag_gap PRIVATE ../../src)
dsdr_apply_target_defaults(tst_pocsag_gap)
add_test(NAME tst_pocsag_gap COMMAND tst_pocsag_gap)

# Experimental CVOR phase receiver; standalone, not yet connected to the app.
add_executable(tst_vor_ident ../standalone/vor/test_ident.cpp ../../src/dsp/VorIdentDecoder.cpp)
target_include_directories(tst_vor_ident PRIVATE ../../src)
dsdr_apply_target_defaults(tst_vor_ident)
add_test(NAME tst_vor_ident COMMAND tst_vor_ident)
set_tests_properties(tst_vor_ident PROPERTIES ENVIRONMENT "DSDR_TEST_SILENT_AUDIO=1" TIMEOUT 120)
add_executable(tst_vor_standalone ../standalone/vor/test_vor.cpp ../../src/dsp/VorReceiver.cpp ../../src/dsp/VorIdentDecoder.cpp)
target_include_directories(tst_vor_standalone PRIVATE ../../src)
dsdr_apply_target_defaults(tst_vor_standalone)
add_test(NAME tst_vor_standalone COMMAND tst_vor_standalone)
set_tests_properties(tst_vor_standalone PROPERTIES TIMEOUT 300)
add_executable(tst_vor_iq_tap ../standalone/vor_engine/test.cpp
    ../../src/core/VorEngineTap.cpp ../../src/core/VorAsyncReceiver.cpp
    ../../src/core/AsyncIqDecoder.cpp ../../src/dsp/VorReceiver.cpp ../../src/dsp/VorIdentDecoder.cpp
    ../../src/dsp/DecimatorChain.cpp ../../src/dsp/FirDecimator.cpp ../../src/dsp/FirDesign.cpp)
target_include_directories(tst_vor_iq_tap PRIVATE ../../src)
dsdr_apply_target_defaults(tst_vor_iq_tap)
add_test(NAME tst_vor_iq_tap COMMAND tst_vor_iq_tap)
set_tests_properties(tst_vor_iq_tap PROPERTIES ENVIRONMENT "DSDR_TEST_SILENT_AUDIO=1" TIMEOUT 120)

# Standalone AIS GMSK receiver with synthetic and documented-payload fixtures.
add_executable(tst_ais_frontend_ab ../standalone/tst_ais_frontend_ab.cpp ../../src/dsp/AisIqFrontend.cpp)
target_include_directories(tst_ais_frontend_ab PRIVATE ../../src)
dsdr_apply_target_defaults(tst_ais_frontend_ab)
add_test(NAME tst_ais_frontend_ab COMMAND tst_ais_frontend_ab)
set_tests_properties(tst_ais_frontend_ab PROPERTIES TIMEOUT 120)
add_executable(tst_ais_standalone ../standalone/tst_ais_decoder.cpp ../../src/dsp/AisDecoder.cpp)
target_include_directories(tst_ais_standalone PRIVATE ../../src ../standalone)
dsdr_apply_target_defaults(tst_ais_standalone)
add_test(NAME tst_ais_standalone COMMAND tst_ais_standalone)
set_tests_properties(tst_ais_standalone PROPERTIES TIMEOUT 120)

# Experimental OnAir-derived ACARS receive path; not yet registered in app.
find_package(Threads REQUIRED)
add_executable(tst_acars_standalone ../standalone/acars/test_acars.cpp
    ../../src/core/AsyncIqDecoder.cpp ../../src/core/AcarsReceiver.cpp
    ../../src/dsp/acars/acars_chan.cpp)
target_include_directories(tst_acars_standalone PRIVATE ../../src)
target_link_libraries(tst_acars_standalone PRIVATE Threads::Threads)
dsdr_apply_target_defaults(tst_acars_standalone)
if(MSVC)
    target_compile_options(tst_acars_standalone PRIVATE /UNDEBUG)
else()
    target_compile_options(tst_acars_standalone PRIVATE -UNDEBUG)
endif()
add_test(NAME tst_acars_standalone COMMAND tst_acars_standalone)
set_tests_properties(tst_acars_standalone PROPERTIES TIMEOUT 120)

dsdr_add_test(tst_dsp_chain
    SOURCES tst_dsp_chain.cpp
    LIBRARIES dsdr::dsp)

dsdr_add_test(tst_time_shift
    SOURCES tst_time_shift.cpp
    LIBRARIES dsdr::dsp)

dsdr_add_test(tst_noise_filters
    SOURCES tst_noise_filters.cpp
    LIBRARIES dsdr::dsp)

# Gate, leveller e limiter: i tre stadi che sbagliano in silenzio — non
# producono un errore, producono un segnale.
dsdr_add_test(tst_tx_dynamics
    SOURCES tst_tx_dynamics.cpp
    LIBRARIES dsdr::dsp)

# Il registratore della propria voce. Un buffer circolare non sbaglia mai con un
# errore: sbaglia con dell'audio, e chi lo sente va a cercare il difetto nella
# catena invece che nell'indice.
dsdr_add_test(tst_voice_recorder
    SOURCES tst_voice_recorder.cpp
    LIBRARIES dsdr::dsp)

# Il multibanda: la prova che conta e' che a riposo non si senta. Se le
# separazioni non sommano piatto, colora la voce prima ancora di comprimere.
dsdr_add_test(tst_multiband
    SOURCES tst_multiband.cpp
    LIBRARIES dsdr::dsp)

# L'equalizzatore parametrico: un errore qui non fallisce, suona — e la curva
# che si guarda mentre si regola e' la prima cosa che puo' mentire.
dsdr_add_test(tst_parametric_eq
    SOURCES tst_parametric_eq.cpp
    LIBRARIES dsdr::dsp)

dsdr_add_test(tst_overload_guard
    SOURCES tst_overload_guard.cpp
    LIBRARIES dsdr::dsp)

dsdr_add_test(tst_spectral_denoiser
    SOURCES tst_spectral_denoiser.cpp
    LIBRARIES dsdr::dsp)

dsdr_add_test(tst_neural_denoiser
    SOURCES tst_neural_denoiser.cpp
    LIBRARIES dsdr::dsp)

dsdr_add_test(tst_tx_chain
    SOURCES tst_tx_chain.cpp
    LIBRARIES dsdr::dsp)

# Lo stadio neurale: bypass identico, zero allocazioni nel percorso caldo,
# latenza dichiarata e degrado onesto (DSDR-IMPL-001 §7.3).
dsdr_add_test(tst_neural_stage
    SOURCES tst_neural_stage.cpp
    LIBRARIES dsdr::dsp)

# L'interlock digitale: l'audio passato dalla rete non deve mai raggiungere un
# decodificatore, e il rifiuto avviene costruendo il collegamento.
dsdr_add_test(tst_audio_graph
    SOURCES tst_audio_graph.cpp
    LIBRARIES dsdr::audio)

# PCM di rete: il test ascolta davvero un datagramma localhost e presidia
# formato, frequenza e assenza di intestazioni proprietarie.
dsdr_add_test(tst_network_audio
    SOURCES tst_network_audio.cpp
    LIBRARIES dsdr::audio Qt6::Network)

# La soglia AGC-T automatica: due righe di aritmetica che si sbagliano in
# silenzio, e il difetto si attribuisce all'AGC.
dsdr_add_test(tst_agc_threshold
    SOURCES tst_agc_threshold.cpp
    LIBRARIES dsdr::dsp)

# Ricezione ADS-B: preambolo, PPM e CRC su IQ sintetico.
dsdr_add_test(tst_adsb_decoder
    SOURCES tst_adsb_decoder.cpp
    LIBRARIES dsdr::dsp)

# Fixture sintetica ripetibile e confronto A/B del blanker su ingresso identico.
dsdr_add_test(tst_synthetic_signal
    SOURCES tst_synthetic_signal.cpp
    LIBRARIES dsdr::dsp)
if(DSDR_BUILD_VDL2_PROTOCOL_EXPERIMENT)
    add_executable(tst_vdl2_protocol_abi ../standalone/vdl2/abi_ab.cpp)
    target_include_directories(tst_vdl2_protocol_abi PRIVATE ../../src)
    target_link_libraries(tst_vdl2_protocol_abi PRIVATE dsdr_vdl2_protocol)
    find_package(Threads REQUIRED)
    target_link_libraries(tst_vdl2_protocol_abi PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
    if(UNIX)
        target_link_libraries(tst_vdl2_protocol_abi PRIVATE m)
    endif()
    if(WIN32)
        target_link_libraries(tst_vdl2_protocol_abi PRIVATE ws2_32 userenv bcrypt ntdll)
    endif()
    add_test(NAME tst_vdl2_protocol_abi COMMAND tst_vdl2_protocol_abi)
    set_tests_properties(tst_vdl2_protocol_abi PROPERTIES ENVIRONMENT "DSDR_TEST_SILENT_AUDIO=1" TIMEOUT 60)
    add_executable(tst_vdl2_frame_adapter ../standalone/vdl2/frame_adapter.cpp)
    target_link_libraries(tst_vdl2_frame_adapter PRIVATE dsdr_vdl2_adapter Threads::Threads ${CMAKE_DL_LIBS})
    if(UNIX)
        target_link_libraries(tst_vdl2_frame_adapter PRIVATE m)
    endif()
    if(WIN32)
        target_link_libraries(tst_vdl2_frame_adapter PRIVATE ws2_32 userenv bcrypt ntdll)
    endif()
    add_test(NAME tst_vdl2_frame_adapter COMMAND tst_vdl2_frame_adapter)
    set_tests_properties(tst_vdl2_frame_adapter PROPERTIES ENVIRONMENT "DSDR_TEST_SILENT_AUDIO=1" TIMEOUT 60)
    add_executable(tst_vdl2_burst_adapter ../standalone/vdl2/burst_adapter.cpp)
    target_link_libraries(tst_vdl2_burst_adapter PRIVATE dsdr_vdl2_adapter Threads::Threads ${CMAKE_DL_LIBS})
    if(UNIX)
        target_link_libraries(tst_vdl2_burst_adapter PRIVATE m)
    endif()
    if(WIN32)
        target_link_libraries(tst_vdl2_burst_adapter PRIVATE ws2_32 userenv bcrypt ntdll)
    endif()
    add_test(NAME tst_vdl2_burst_adapter COMMAND tst_vdl2_burst_adapter)
    set_tests_properties(tst_vdl2_burst_adapter PROPERTIES ENVIRONMENT "DSDR_TEST_SILENT_AUDIO=1" TIMEOUT 60)
    add_executable(tst_vdl2_message_model ../standalone/vdl2/message_model.cpp)
    target_link_libraries(tst_vdl2_message_model PRIVATE dsdr_vdl2_async Threads::Threads ${CMAKE_DL_LIBS})
    if(UNIX)
        target_link_libraries(tst_vdl2_message_model PRIVATE m)
    endif()
    if(WIN32)
        target_link_libraries(tst_vdl2_message_model PRIVATE ws2_32 userenv bcrypt ntdll)
    endif()
    add_test(NAME tst_vdl2_message_model COMMAND tst_vdl2_message_model)
    set_tests_properties(tst_vdl2_message_model PROPERTIES ENVIRONMENT "DSDR_TEST_SILENT_AUDIO=1" TIMEOUT 60)
    # Fixture generated on demand by OnAir reference test; not a default CTest.
    add_executable(tst_vdl2_controller_iq ../standalone/vdl2/controller_iq.cpp)
    target_link_libraries(tst_vdl2_controller_iq PRIVATE dsdr_vdl2_async dsdr::qml dsdr_qmlplugin Qt6::Quick Threads::Threads ${CMAKE_DL_LIBS})
    if(UNIX)
        target_link_libraries(tst_vdl2_controller_iq PRIVATE m)
    endif()
    if(WIN32)
        target_link_libraries(tst_vdl2_controller_iq PRIVATE ws2_32 userenv bcrypt ntdll)
    endif()
    add_executable(tst_vdl2_shutdown ../standalone/vdl2/shutdown.cpp)
    target_link_libraries(tst_vdl2_shutdown PRIVATE dsdr_vdl2_async Threads::Threads ${CMAKE_DL_LIBS})
    if(UNIX)
        target_link_libraries(tst_vdl2_shutdown PRIVATE m)
    endif()
    if(WIN32)
        target_link_libraries(tst_vdl2_shutdown PRIVATE ws2_32 userenv bcrypt ntdll)
    endif()
    add_test(NAME tst_vdl2_shutdown COMMAND tst_vdl2_shutdown)
    set_tests_properties(tst_vdl2_shutdown PROPERTIES ENVIRONMENT "DSDR_TEST_SILENT_AUDIO=1" TIMEOUT 60)
    add_executable(tst_vdl2_lifecycle ../standalone/vdl2/lifecycle.cpp)
    target_link_libraries(tst_vdl2_lifecycle PRIVATE dsdr_vdl2_async Threads::Threads ${CMAKE_DL_LIBS})
    if(UNIX)
        target_link_libraries(tst_vdl2_lifecycle PRIVATE m)
    endif()
    if(WIN32)
        target_link_libraries(tst_vdl2_lifecycle PRIVATE ws2_32 userenv bcrypt ntdll)
    endif()
    add_test(NAME tst_vdl2_lifecycle COMMAND tst_vdl2_lifecycle)
    set_tests_properties(tst_vdl2_lifecycle PROPERTIES ENVIRONMENT "DSDR_TEST_SILENT_AUDIO=1" TIMEOUT 60)
endif()
