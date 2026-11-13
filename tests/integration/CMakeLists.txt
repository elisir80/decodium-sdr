# RNF-07: l'applicazione completa (meno la UI) contro il backend demo,
# eseguibile headless in CI.

dsdr_add_test(tst_session_demo
    SOURCES tst_session_demo.cpp
    LIBRARIES dsdr::core)

# Fake radio only: compare normal shutdown and an initially refused PTT-off.
dsdr_add_test(tst_shutdown_fake_radio
    SOURCES tst_shutdown_fake_radio_standalone.cpp
    LIBRARIES dsdr::core dsdr::hal Qt6::Gui)
add_test(NAME tst_shutdown_ptt_retry COMMAND tst_shutdown_fake_radio)
add_test(NAME tst_shutdown_slow_backend COMMAND tst_shutdown_fake_radio)
add_test(NAME tst_shutdown_close_retry COMMAND tst_shutdown_fake_radio)
add_test(NAME tst_shutdown_external_gate COMMAND tst_shutdown_fake_radio)
add_test(NAME tst_shutdown_adsb_held COMMAND tst_shutdown_fake_radio)
add_test(NAME tst_shutdown_aprs_held COMMAND tst_shutdown_fake_radio)
add_test(NAME tst_shutdown_multiple_decoders COMMAND tst_shutdown_fake_radio)
dsdr_add_test(tst_shutdown_qml_retry
    SOURCES tst_shutdown_qml_retry_standalone.cpp
    LIBRARIES dsdr::core dsdr::hal Qt6::Gui Qt6::Quick Qt6::Qml)
target_compile_definitions(tst_shutdown_qml_retry PRIVATE DSDR_SHUTDOWN_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
set(_shutdown_test_root "${CMAKE_BINARY_DIR}/shutdown-fake-test")
file(MAKE_DIRECTORY "${_shutdown_test_root}/tmp" "${_shutdown_test_root}/config" "${_shutdown_test_root}/xdg-data")
set(_shutdown_test_env "QT_QPA_PLATFORM=offscreen;DSDR_TEST_SILENT_AUDIO=1;DSDR_TEST_ASYNC_SHUTDOWN=1;DSDR_TEST_BUILD_ROOT=${_shutdown_test_root};DSDR_TEST_SETTINGS_DIR=${_shutdown_test_root}/settings;DSDR_TEST_APPDATA_DIR=${_shutdown_test_root}/data;TMPDIR=${_shutdown_test_root}/tmp;XDG_CONFIG_HOME=${_shutdown_test_root}/config;XDG_DATA_HOME=${_shutdown_test_root}/xdg-data")
set_tests_properties(tst_shutdown_fake_radio PROPERTIES ENVIRONMENT "${_shutdown_test_env}" RUN_SERIAL TRUE)
set_tests_properties(tst_shutdown_ptt_retry PROPERTIES ENVIRONMENT "${_shutdown_test_env};DSDR_TEST_REFUSE_PTT=1" RUN_SERIAL TRUE TIMEOUT 120)
set_tests_properties(tst_shutdown_slow_backend PROPERTIES ENVIRONMENT "${_shutdown_test_env};DSDR_TEST_SLOW_BACKEND=1" RUN_SERIAL TRUE TIMEOUT 120)
set_tests_properties(tst_shutdown_close_retry PROPERTIES ENVIRONMENT "${_shutdown_test_env};DSDR_TEST_REFUSE_BACKEND=1" RUN_SERIAL TRUE TIMEOUT 120)
set_tests_properties(tst_shutdown_external_gate PROPERTIES ENVIRONMENT "${_shutdown_test_env};DSDR_TEST_EXTERNAL_GATE=1" RUN_SERIAL TRUE TIMEOUT 120)
set_tests_properties(tst_shutdown_adsb_held PROPERTIES ENVIRONMENT "${_shutdown_test_env};DSDR_TEST_ADSB_HELD=1" RUN_SERIAL TRUE TIMEOUT 120)
set_tests_properties(tst_shutdown_aprs_held PROPERTIES ENVIRONMENT "${_shutdown_test_env};DSDR_TEST_APRS_HELD=1" RUN_SERIAL TRUE TIMEOUT 120)
set_tests_properties(tst_shutdown_multiple_decoders PROPERTIES ENVIRONMENT "${_shutdown_test_env};DSDR_TEST_APRS_HELD=1;DSDR_TEST_ALL_DECODERS=1" RUN_SERIAL TRUE TIMEOUT 120)
add_executable(tst_adsb_full_qml_shutdown tst_adsb_full_qml_shutdown.cpp)
target_link_libraries(tst_adsb_full_qml_shutdown PRIVATE dsdr::core dsdr::hal dsdr::qml dsdr_qmlplugin Qt6::Gui Qt6::Quick Qt6::Qml Qt6::Test)
add_test(NAME tst_adsb_full_qml_close COMMAND tst_adsb_full_qml_shutdown)
add_test(NAME tst_adsb_full_qml_menu COMMAND tst_adsb_full_qml_shutdown)
add_test(NAME tst_adsb_full_qml_ptt_retry COMMAND tst_adsb_full_qml_shutdown)
dsdr_add_test(tst_adsb_iq_session SOURCES tst_adsb_iq_session.cpp LIBRARIES dsdr::core Qt6::Gui Qt6::Qml Qt6::Quick)
target_compile_definitions(tst_adsb_iq_session PRIVATE DSDR_ADSB_QML_DIR="${CMAKE_SOURCE_DIR}/src/app/qml")
set_tests_properties(tst_adsb_iq_session PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;DSDR_TEST_SILENT_AUDIO=1" TIMEOUT 60 RUN_SERIAL TRUE)
add_test(NAME tst_aprs_full_qml_close COMMAND tst_adsb_full_qml_shutdown)
add_test(NAME tst_aprs_full_qml_menu COMMAND tst_adsb_full_qml_shutdown)
add_test(NAME tst_aprs_full_qml_ptt_retry COMMAND tst_adsb_full_qml_shutdown)
set_tests_properties(tst_adsb_full_qml_close PROPERTIES ENVIRONMENT "${_shutdown_test_env};QT_QUICK_BACKEND=software" RUN_SERIAL TRUE TIMEOUT 120)
set_tests_properties(tst_aprs_full_qml_close PROPERTIES ENVIRONMENT "${_shutdown_test_env};QT_QUICK_BACKEND=software;DSDR_TEST_APRS_HELD=1" RUN_SERIAL TRUE TIMEOUT 120)
set_tests_properties(tst_aprs_full_qml_menu PROPERTIES ENVIRONMENT "${_shutdown_test_env};QT_QUICK_BACKEND=software;DSDR_TEST_APRS_HELD=1;DSDR_TEST_QUIT_MENU=1" RUN_SERIAL TRUE TIMEOUT 120)
set_tests_properties(tst_aprs_full_qml_ptt_retry PROPERTIES ENVIRONMENT "${_shutdown_test_env};QT_QUICK_BACKEND=software;DSDR_TEST_APRS_HELD=1;DSDR_TEST_REFUSE_PTT=1" RUN_SERIAL TRUE TIMEOUT 120)
set_tests_properties(tst_adsb_full_qml_menu PROPERTIES ENVIRONMENT "${_shutdown_test_env};QT_QUICK_BACKEND=software;DSDR_TEST_QUIT_MENU=1" RUN_SERIAL TRUE TIMEOUT 120)
set_tests_properties(tst_adsb_full_qml_ptt_retry PROPERTIES ENVIRONMENT "${_shutdown_test_env};QT_QUICK_BACKEND=software;DSDR_TEST_REFUSE_PTT=1" RUN_SERIAL TRUE TIMEOUT 120)
set_tests_properties(tst_shutdown_qml_retry PROPERTIES ENVIRONMENT "${_shutdown_test_env};QT_QUICK_BACKEND=software" RUN_SERIAL TRUE TIMEOUT 120)
if(DSDR_BUILD_VDL2_PROTOCOL_EXPERIMENT)
    add_executable(tst_vdl2_session_binding tst_vdl2_session_binding.cpp)
    target_link_libraries(tst_vdl2_session_binding PRIVATE dsdr_vdl2_async dsdr::hal Qt6::Gui Threads::Threads ${CMAKE_DL_LIBS})
    if(UNIX)
        target_link_libraries(tst_vdl2_session_binding PRIVATE m)
    endif()
    if(WIN32)
        target_link_libraries(tst_vdl2_session_binding PRIVATE ws2_32 userenv bcrypt ntdll)
    endif()
    add_test(NAME tst_vdl2_session_binding COMMAND tst_vdl2_session_binding)
    add_test(NAME tst_vdl2_session_binding_retry COMMAND tst_vdl2_session_binding)
    set_tests_properties(tst_vdl2_session_binding PROPERTIES ENVIRONMENT "${_shutdown_test_env}" RUN_SERIAL TRUE TIMEOUT 120)
    set_tests_properties(tst_vdl2_session_binding_retry PROPERTIES ENVIRONMENT "${_shutdown_test_env};DSDR_TEST_REFUSE_BACKEND=1" RUN_SERIAL TRUE TIMEOUT 120)
    add_executable(tst_vdl2_app_manager tst_vdl2_app_manager.cpp)
    target_link_libraries(tst_vdl2_app_manager PRIVATE dsdr_vdl2_async dsdr::hal Qt6::Gui Threads::Threads ${CMAKE_DL_LIBS})
    if(UNIX)
        target_link_libraries(tst_vdl2_app_manager PRIVATE m)
    endif()
    if(WIN32)
        target_link_libraries(tst_vdl2_app_manager PRIVATE ws2_32 userenv bcrypt ntdll)
    endif()
    add_test(NAME tst_vdl2_app_manager COMMAND tst_vdl2_app_manager)
    add_test(NAME tst_vdl2_app_manager_cancel COMMAND tst_vdl2_app_manager)
    add_test(NAME tst_vdl2_app_manager_source_change COMMAND tst_vdl2_app_manager)
    add_test(NAME tst_vdl2_app_manager_no_client_iq COMMAND tst_vdl2_app_manager)
    add_test(NAME tst_vdl2_app_manager_stop_restart COMMAND tst_vdl2_app_manager)
    add_test(NAME tst_vdl2_app_manager_stop_preparation COMMAND tst_vdl2_app_manager)
    set_tests_properties(tst_vdl2_app_manager PROPERTIES ENVIRONMENT "${_shutdown_test_env}" RUN_SERIAL TRUE TIMEOUT 120)
    set_tests_properties(tst_vdl2_app_manager_cancel PROPERTIES ENVIRONMENT "${_shutdown_test_env};DSDR_TEST_CANCEL_PREPARATION=1" RUN_SERIAL TRUE TIMEOUT 120)
    set_tests_properties(tst_vdl2_app_manager_source_change PROPERTIES ENVIRONMENT "${_shutdown_test_env};DSDR_TEST_CHANGE_SOURCE=1" RUN_SERIAL TRUE TIMEOUT 120)
    set_tests_properties(tst_vdl2_app_manager_no_client_iq PROPERTIES ENVIRONMENT "${_shutdown_test_env};DSDR_TEST_NO_CLIENT_IQ=1" RUN_SERIAL TRUE TIMEOUT 120)
    set_tests_properties(tst_vdl2_app_manager_stop_restart PROPERTIES ENVIRONMENT "${_shutdown_test_env};DSDR_TEST_STOP_RESTART=1" RUN_SERIAL TRUE TIMEOUT 120)
    set_tests_properties(tst_vdl2_app_manager_stop_preparation PROPERTIES ENVIRONMENT "${_shutdown_test_env};DSDR_TEST_STOP_PREPARATION=1" RUN_SERIAL TRUE TIMEOUT 120)
    add_executable(tst_vdl2_main_qml tst_vdl2_main_qml.cpp)
    target_link_libraries(tst_vdl2_main_qml PRIVATE dsdr_vdl2_async dsdr::hal dsdr::qml dsdr_qmlplugin Qt6::Quick Threads::Threads ${CMAKE_DL_LIBS})
    if(UNIX)
        target_link_libraries(tst_vdl2_main_qml PRIVATE m)
    endif()
    if(WIN32)
        target_link_libraries(tst_vdl2_main_qml PRIVATE ws2_32 userenv bcrypt ntdll)
    endif()
    add_test(NAME tst_vdl2_main_qml_off COMMAND tst_vdl2_main_qml)
    add_test(NAME tst_vdl2_main_qml_on COMMAND tst_vdl2_main_qml)
    add_test(NAME tst_vdl2_main_qml_no_iq COMMAND tst_vdl2_main_qml)
    add_test(NAME tst_vdl2_main_qml_restart COMMAND tst_vdl2_main_qml)
    set_tests_properties(tst_vdl2_main_qml_off PROPERTIES ENVIRONMENT "${_shutdown_test_env};QT_QUICK_BACKEND=software" RUN_SERIAL TRUE TIMEOUT 120)
    set_tests_properties(tst_vdl2_main_qml_on PROPERTIES ENVIRONMENT "${_shutdown_test_env};QT_QUICK_BACKEND=software;DSDR_TEST_VDL2_AVAILABLE=1" RUN_SERIAL TRUE TIMEOUT 120)
    set_tests_properties(tst_vdl2_main_qml_no_iq PROPERTIES ENVIRONMENT "${_shutdown_test_env};QT_QUICK_BACKEND=software;DSDR_TEST_VDL2_AVAILABLE=1;DSDR_TEST_VDL2_NO_IQ=1" RUN_SERIAL TRUE TIMEOUT 120)
    set_tests_properties(tst_vdl2_main_qml_restart PROPERTIES ENVIRONMENT "${_shutdown_test_env};QT_QUICK_BACKEND=software;DSDR_TEST_VDL2_AVAILABLE=1;DSDR_TEST_VDL2_UI_RESTART=1" RUN_SERIAL TRUE TIMEOUT 120)
    set(DSDR_VDL2_IQ_TEST_FIXTURE "" CACHE FILEPATH "Optional override for synthetic VDL2 IQ fixture")
    if(NOT DSDR_VDL2_IQ_TEST_FIXTURE)
        find_package(Python3 REQUIRED COMPONENTS Interpreter)
        set(DSDR_VDL2_IQ_TEST_FIXTURE "${PROJECT_BINARY_DIR}/test-fixtures/vdl2-synthetic-192.cf32")
        execute_process(COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/standalone/vdl2/unpack_fixture.py"
            "${PROJECT_SOURCE_DIR}/tests/standalone/vdl2/synthetic-192.cf32.gz" "${DSDR_VDL2_IQ_TEST_FIXTURE}"
            RESULT_VARIABLE _vdl2_unpack_result)
        if(NOT _vdl2_unpack_result EQUAL 0)
            message(FATAL_ERROR "Failed to prepare synthetic VDL2 IQ test fixture")
        endif()
    endif()
    if(EXISTS "${DSDR_VDL2_IQ_TEST_FIXTURE}")
        add_test(NAME tst_vdl2_main_qml_iq COMMAND tst_vdl2_main_qml)
        set_tests_properties(tst_vdl2_main_qml_iq PROPERTIES ENVIRONMENT "${_shutdown_test_env};QT_QUICK_BACKEND=software;DSDR_TEST_VDL2_AVAILABLE=1;DSDR_TEST_VDL2_IQ=${DSDR_VDL2_IQ_TEST_FIXTURE}" RUN_SERIAL TRUE TIMEOUT 120)
        add_test(NAME tst_vdl2_main_qml_iqfile COMMAND tst_vdl2_main_qml)
        add_test(NAME tst_vdl2_main_qml_iqfile_off COMMAND tst_vdl2_main_qml)
        add_test(NAME tst_vdl2_main_qml_iqfile_reconnect COMMAND tst_vdl2_main_qml)
        add_test(NAME tst_vdl2_main_qml_iqfile_stress COMMAND tst_vdl2_main_qml)
        add_test(NAME tst_vdl2_main_qml_iqfile_sustained COMMAND tst_vdl2_main_qml)
        set_tests_properties(tst_vdl2_main_qml_iqfile_sustained PROPERTIES ENVIRONMENT "${_shutdown_test_env};QT_QUICK_BACKEND=software;DSDR_TEST_VDL2_AVAILABLE=1;DSDR_TEST_VDL2_IQFILE=1;DSDR_TEST_VDL2_REPETITIONS=48;DSDR_TEST_VDL2_IQ=${DSDR_VDL2_IQ_TEST_FIXTURE}" RUN_SERIAL TRUE TIMEOUT 120)
        add_test(NAME tst_vdl2_main_qml_iqfile_retune COMMAND tst_vdl2_main_qml)
        set_tests_properties(tst_vdl2_main_qml_iqfile PROPERTIES ENVIRONMENT "${_shutdown_test_env};QT_QUICK_BACKEND=software;DSDR_TEST_VDL2_AVAILABLE=1;DSDR_TEST_VDL2_IQFILE=1;DSDR_TEST_VDL2_IQ=${DSDR_VDL2_IQ_TEST_FIXTURE}" RUN_SERIAL TRUE TIMEOUT 120)
        set_tests_properties(tst_vdl2_main_qml_iqfile_off PROPERTIES ENVIRONMENT "${_shutdown_test_env};QT_QUICK_BACKEND=software;DSDR_TEST_VDL2_IQFILE=1;DSDR_TEST_VDL2_IQ=${DSDR_VDL2_IQ_TEST_FIXTURE}" RUN_SERIAL TRUE TIMEOUT 120)
        set_tests_properties(tst_vdl2_main_qml_iqfile_reconnect PROPERTIES ENVIRONMENT "${_shutdown_test_env};QT_QUICK_BACKEND=software;DSDR_TEST_VDL2_AVAILABLE=1;DSDR_TEST_VDL2_IQFILE=1;DSDR_TEST_VDL2_RECONNECT=1;DSDR_TEST_VDL2_IQ=${DSDR_VDL2_IQ_TEST_FIXTURE}" RUN_SERIAL TRUE TIMEOUT 120)
        set_tests_properties(tst_vdl2_main_qml_iqfile_stress PROPERTIES ENVIRONMENT "${_shutdown_test_env};QT_QUICK_BACKEND=software;DSDR_TEST_VDL2_AVAILABLE=1;DSDR_TEST_VDL2_IQFILE=1;DSDR_TEST_VDL2_STRESS=1;DSDR_TEST_VDL2_IQ=${DSDR_VDL2_IQ_TEST_FIXTURE}" RUN_SERIAL TRUE TIMEOUT 120)
        set_tests_properties(tst_vdl2_main_qml_iqfile_retune PROPERTIES ENVIRONMENT "${_shutdown_test_env};QT_QUICK_BACKEND=software;DSDR_TEST_VDL2_AVAILABLE=1;DSDR_TEST_VDL2_IQFILE=1;DSDR_TEST_VDL2_RETUNE=1;DSDR_TEST_VDL2_IQ=${DSDR_VDL2_IQ_TEST_FIXTURE}" RUN_SERIAL TRUE TIMEOUT 120)
    else()
        message(FATAL_ERROR "VDL2 IQ test fixture does not exist: ${DSDR_VDL2_IQ_TEST_FIXTURE}")
    endif()
endif()

# Il percorso di trasmissione senza microfono: in CW la sorgente è il tasto,
# e tutto il resto della catena è lo stesso.
dsdr_add_test(tst_tx_engine
    SOURCES tst_tx_engine.cpp
    LIBRARIES dsdr::core)

# I profili per modo. Un profilo che cambia da se' e' comodo finche' non
# cancella mezz'ora di lavoro senza dirlo.
dsdr_add_test(tst_tx_profiles
    SOURCES tst_tx_profiles.cpp
    LIBRARIES dsdr::core)

# Il percorso di ricezione dei backend server-DSP: audio dentro, audio fuori.
dsdr_add_test(tst_audio_source
    SOURCES tst_audio_source.cpp
    LIBRARIES dsdr::core)

dsdr_add_test(tst_iq_recorder
    SOURCES tst_iq_recorder.cpp
    LIBRARIES dsdr::core)

# Il pianificatore deve rimanere deterministico: le scadenze sono valutate
# contro un orologio iniettato dal test, senza attese reali.
dsdr_add_test(tst_operation_scheduler
    SOURCES tst_operation_scheduler.cpp
    LIBRARIES dsdr::core)

# Il timer non basta da solo: questa prova percorre il boundary scheduler →
# sessione → backend demo e verifica una sintonia realmente applicata.
dsdr_add_test(tst_session_scheduler
    SOURCES tst_session_scheduler.cpp
    LIBRARIES dsdr::core)

add_library(dsdr_test_iq_module SHARED test_iq_module.cpp)
target_include_directories(dsdr_test_iq_module PRIVATE
    "${CMAKE_SOURCE_DIR}/src")
set_target_properties(dsdr_test_iq_module PROPERTIES
    PREFIX "")

dsdr_add_test(tst_iq_module
    SOURCES tst_iq_module.cpp
    LIBRARIES dsdr::core)
add_dependencies(tst_iq_module dsdr_test_iq_module)
set_tests_properties(tst_iq_module PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;DSDR_TEST_IQ_MODULE=$<TARGET_FILE:dsdr_test_iq_module>"
    TIMEOUT 120)

# Registratore e lettore stanno in strati diversi e sono scritti in momenti
# diversi: che combacino va verificato da fuori, contro un file vero.
dsdr_add_test(tst_iqfile_playback
    SOURCES tst_iqfile_playback.cpp
    LIBRARIES dsdr::hal)

# Le traduzioni sono incorporate nelle risorse dell'eseguibile dell'app: il
# test le raggiunge attraverso il target che le porta con sé.
dsdr_add_test(tst_language
    SOURCES tst_language.cpp
    LIBRARIES dsdr::core dsdr_qml dsdr_qmlplugin)

dsdr_add_test(tst_iq_analysis
    SOURCES tst_iq_analysis.cpp
    LIBRARIES dsdr::core)

# Tap ADS-B sul flusso IQ reale del motore, pilotato con campioni sintetici.
dsdr_add_test(tst_adsb_integration
    SOURCES tst_adsb_integration.cpp
    LIBRARIES dsdr::core)

dsdr_add_test(tst_adsb_message
    SOURCES tst_adsb_message.cpp
    LIBRARIES dsdr::core)

add_executable(tst_adsb_cpr tst_adsb_cpr_standalone.cpp)
target_link_libraries(tst_adsb_cpr PRIVATE dsdr::core)
dsdr_apply_target_defaults(tst_adsb_cpr)
add_test(NAME tst_adsb_cpr COMMAND tst_adsb_cpr)

add_executable(tst_async_iq_decoder tst_async_iq_decoder_standalone.cpp)
target_link_libraries(tst_async_iq_decoder PRIVATE dsdr::core)
dsdr_apply_target_defaults(tst_async_iq_decoder)
add_test(NAME tst_async_iq_decoder COMMAND tst_async_iq_decoder)

# APRS: A/B mono versus asynchronous stereo, queue loss and UI controller.
add_executable(tst_aprs_receiver tst_aprs_receiver_standalone.cpp)
target_link_libraries(tst_aprs_receiver PRIVATE dsdr::core)
dsdr_apply_target_defaults(tst_aprs_receiver)
if(MSVC)
    target_compile_options(tst_aprs_receiver PRIVATE /UNDEBUG)
else()
    target_compile_options(tst_aprs_receiver PRIVATE -UNDEBUG)
endif()
add_test(NAME tst_aprs_receiver COMMAND tst_aprs_receiver)
set_tests_properties(tst_aprs_receiver PROPERTIES
    ENVIRONMENT "DSDR_TEST_SILENT_AUDIO=1" TIMEOUT 30)

add_executable(tst_async_iq_tags tst_async_iq_tags_standalone.cpp)
target_link_libraries(tst_async_iq_tags PRIVATE dsdr::core)
dsdr_apply_target_defaults(tst_async_iq_tags)
if(MSVC)
    target_compile_options(tst_async_iq_tags PRIVATE /UNDEBUG)
else()
    target_compile_options(tst_async_iq_tags PRIVATE -UNDEBUG)
endif()
add_test(NAME tst_async_iq_tags COMMAND tst_async_iq_tags)
set_tests_properties(tst_async_iq_tags PROPERTIES ENVIRONMENT "DSDR_TEST_SILENT_AUDIO=1")

dsdr_add_test(tst_aprs_session
    SOURCES tst_aprs_session.cpp
    LIBRARIES dsdr::core)
set_tests_properties(tst_aprs_session PROPERTIES
    ENVIRONMENT "DSDR_TEST_SILENT_AUDIO=1" TIMEOUT 120)

dsdr_add_test(tst_aprs_iq_session SOURCES tst_aprs_iq_session.cpp LIBRARIES dsdr::core Qt6::Gui Qt6::Qml Qt6::Quick)
target_compile_definitions(tst_aprs_iq_session PRIVATE DSDR_APRS_QML_DIR="${CMAKE_SOURCE_DIR}/src/app/qml")
set_tests_properties(tst_aprs_iq_session PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;DSDR_TEST_SILENT_AUDIO=1" TIMEOUT 60 RUN_SERIAL TRUE)

dsdr_add_test(tst_aprs_engine
    SOURCES tst_aprs_engine.cpp
    LIBRARIES dsdr::core)
set_tests_properties(tst_aprs_engine PROPERTIES
    ENVIRONMENT "DSDR_TEST_SILENT_AUDIO=1" TIMEOUT 60)

# Same buffered IQ with and without a reported loss: decoder and audio A/B.
dsdr_add_test(tst_aprs_backlog
    SOURCES ../standalone/aprs_gap/tst_aprs_backlog.cpp
    LIBRARIES dsdr::core)
set_tests_properties(tst_aprs_backlog PROPERTIES
    ENVIRONMENT "DSDR_TEST_SILENT_AUDIO=1" TIMEOUT 60)

# Standalone synthetic IQ -> async POCSAG -> controller/QML; no engine tap.
add_executable(tst_pocsag_async ../standalone/pocsag/test_async.cpp)
target_link_libraries(tst_pocsag_async PRIVATE dsdr::core)
dsdr_apply_target_defaults(tst_pocsag_async)
add_test(NAME tst_pocsag_async COMMAND tst_pocsag_async)
set_tests_properties(tst_pocsag_async PROPERTIES ENVIRONMENT "DSDR_TEST_SILENT_AUDIO=1" TIMEOUT 60)

add_executable(tst_pocsag_managed ../standalone/pocsag/test_managed.cpp)
target_link_libraries(tst_pocsag_managed PRIVATE dsdr::core)
dsdr_apply_target_defaults(tst_pocsag_managed)
add_test(NAME tst_pocsag_managed COMMAND tst_pocsag_managed)
set_tests_properties(tst_pocsag_managed PROPERTIES ENVIRONMENT "DSDR_TEST_SILENT_AUDIO=1" TIMEOUT 60)

add_executable(tst_pocsag_engine_ingress ../standalone/pocsag/test_engine_ingress.cpp)
target_link_libraries(tst_pocsag_engine_ingress PRIVATE dsdr::core)
dsdr_apply_target_defaults(tst_pocsag_engine_ingress)
add_test(NAME tst_pocsag_engine_ingress COMMAND tst_pocsag_engine_ingress)
set_tests_properties(tst_pocsag_engine_ingress PROPERTIES ENVIRONMENT "DSDR_TEST_SILENT_AUDIO=1" TIMEOUT 60)

dsdr_add_test(tst_pocsag_engine SOURCES tst_pocsag_engine.cpp LIBRARIES dsdr::core)
set_tests_properties(tst_pocsag_engine PROPERTIES ENVIRONMENT "DSDR_TEST_SILENT_AUDIO=1" TIMEOUT 60)

dsdr_add_test(tst_acars_engine SOURCES tst_acars_engine.cpp LIBRARIES dsdr::core)
set_tests_properties(tst_acars_engine PROPERTIES ENVIRONMENT "DSDR_TEST_SILENT_AUDIO=1" TIMEOUT 60)
dsdr_add_test(tst_acars_session_live SOURCES tst_acars_session_live.cpp LIBRARIES dsdr::core Qt6::Gui Qt6::Qml Qt6::Quick)
target_compile_definitions(tst_acars_session_live PRIVATE DSDR_ACARS_QML_DIR="${CMAKE_SOURCE_DIR}/src/app/qml")
set_tests_properties(tst_acars_session_live PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;DSDR_TEST_SILENT_AUDIO=1" TIMEOUT 60 RUN_SERIAL TRUE)

# Exercise the ACARS dialog and synthetic receiver in regular CI builds too.
add_executable(tst_acars_qml ../standalone/acars/test_acars_qml.cpp)
target_link_libraries(tst_acars_qml PRIVATE dsdr::core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Test)
dsdr_apply_target_defaults(tst_acars_qml)
# The harness uses assert for its checks, including in Release builds.
if(MSVC)
    target_compile_options(tst_acars_qml PRIVATE /UNDEBUG)
else()
    target_compile_options(tst_acars_qml PRIVATE -UNDEBUG)
endif()
add_test(NAME tst_acars_qml COMMAND tst_acars_qml "${PROJECT_BINARY_DIR}/acars-qml-fixture" "${PROJECT_SOURCE_DIR}/src/app/qml")
set_tests_properties(tst_acars_qml PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;DSDR_TEST_SILENT_AUDIO=1" TIMEOUT 60)

add_executable(tst_vor_async ../standalone/vor_async/test.cpp
    ../../src/core/VorController.h ../../src/core/VorController.cpp
    ../../src/core/VorAsyncReceiver.cpp ../../src/core/AsyncIqDecoder.cpp ../../src/dsp/VorReceiver.cpp ../../src/dsp/VorIdentDecoder.cpp)
target_include_directories(tst_vor_async PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(tst_vor_async PRIVATE Qt6::Core)
dsdr_apply_target_defaults(tst_vor_async)
add_test(NAME tst_vor_async COMMAND tst_vor_async)
set_tests_properties(tst_vor_async PROPERTIES ENVIRONMENT "DSDR_TEST_SILENT_AUDIO=1" TIMEOUT 120)
add_executable(tst_vor_high_rate ../standalone/vor_engine/test_high_rate.cpp)
target_link_libraries(tst_vor_high_rate PRIVATE dsdr::core)
dsdr_apply_target_defaults(tst_vor_high_rate)
add_test(NAME tst_vor_high_rate COMMAND tst_vor_high_rate)
set_tests_properties(tst_vor_high_rate PROPERTIES ENVIRONMENT "DSDR_TEST_SILENT_AUDIO=1" TIMEOUT 120)
dsdr_add_test(tst_vor_engine SOURCES tst_vor_engine.cpp LIBRARIES dsdr::core)
set_tests_properties(tst_vor_engine PROPERTIES ENVIRONMENT "DSDR_TEST_SILENT_AUDIO=1" TIMEOUT 600)
dsdr_add_test(tst_selcal_engine SOURCES tst_selcal_engine.cpp LIBRARIES dsdr::core)
set_tests_properties(tst_selcal_engine PROPERTIES ENVIRONMENT "DSDR_TEST_SILENT_AUDIO=1" TIMEOUT 120)
dsdr_add_test(tst_dsc_engine SOURCES tst_dsc_engine.cpp LIBRARIES dsdr::core)
set_tests_properties(tst_dsc_engine PROPERTIES ENVIRONMENT "DSDR_TEST_SILENT_AUDIO=1" TIMEOUT 120)
dsdr_add_test(tst_navtex_engine SOURCES tst_navtex_engine.cpp LIBRARIES dsdr::core)
set_tests_properties(tst_navtex_engine PROPERTIES ENVIRONMENT "DSDR_TEST_SILENT_AUDIO=1" TIMEOUT 360)
dsdr_add_test(tst_navtex_session SOURCES tst_navtex_session.cpp LIBRARIES dsdr::core Qt6::Gui Qt6::Qml Qt6::Quick)
target_compile_definitions(tst_navtex_session PRIVATE DSDR_NAVTEX_QML_DIR="${CMAKE_SOURCE_DIR}/src/app/qml")
set_tests_properties(tst_navtex_session PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;DSDR_TEST_SILENT_AUDIO=1" TIMEOUT 120 RUN_SERIAL TRUE)
dsdr_add_test(tst_dsc_session SOURCES tst_dsc_session.cpp LIBRARIES dsdr::core Qt6::Gui Qt6::Qml Qt6::Quick)
target_compile_definitions(tst_dsc_session PRIVATE DSDR_DSC_QML_DIR="${CMAKE_SOURCE_DIR}/src/app/qml")
set_tests_properties(tst_dsc_session PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;DSDR_TEST_SILENT_AUDIO=1" TIMEOUT 420 RUN_SERIAL TRUE)
dsdr_add_test(tst_selcal_session_live SOURCES tst_selcal_session_live.cpp LIBRARIES dsdr::core Qt6::Gui Qt6::Qml Qt6::Quick)
target_compile_definitions(tst_selcal_session_live PRIVATE DSDR_SELCAL_QML_DIR="${CMAKE_SOURCE_DIR}/src/app/qml")
set_tests_properties(tst_selcal_session_live PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;DSDR_TEST_SILENT_AUDIO=1" TIMEOUT 60 RUN_SERIAL TRUE)
dsdr_add_test(tst_vor_session_live SOURCES tst_vor_session_live.cpp LIBRARIES dsdr::core Qt6::Gui Qt6::Qml Qt6::Quick)
target_compile_definitions(tst_vor_session_live PRIVATE DSDR_VOR_QML_DIR="${CMAKE_SOURCE_DIR}/src/app/qml")
set_tests_properties(tst_vor_session_live PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;DSDR_TEST_SILENT_AUDIO=1" TIMEOUT 240 RUN_SERIAL TRUE)

dsdr_add_test(tst_pocsag_session_live SOURCES tst_pocsag_session_live.cpp LIBRARIES dsdr::core Qt6::Gui Qt6::Qml Qt6::Quick)
target_compile_definitions(tst_pocsag_session_live PRIVATE DSDR_POCSAG_QML_DIR="${CMAKE_SOURCE_DIR}/src/app/qml")
set_tests_properties(tst_pocsag_session_live PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;DSDR_TEST_SILENT_AUDIO=1" TIMEOUT 180 RUN_SERIAL TRUE)

add_executable(tst_pocsag_ui ../standalone/pocsag/test_ui.cpp)
target_link_libraries(tst_pocsag_ui PRIVATE dsdr::core Qt6::Quick Qt6::Qml)
target_compile_definitions(tst_pocsag_ui PRIVATE DSDR_POCSAG_SOURCE_DIR="${PROJECT_SOURCE_DIR}" DSDR_POCSAG_BINARY_DIR="${PROJECT_BINARY_DIR}")
dsdr_apply_target_defaults(tst_pocsag_ui)
add_test(NAME tst_pocsag_ui COMMAND tst_pocsag_ui)
set_tests_properties(tst_pocsag_ui PROPERTIES ENVIRONMENT "DSDR_TEST_SILENT_AUDIO=1;QT_QPA_PLATFORM=offscreen" TIMEOUT 60)

add_executable(tst_pocsag_iq_source_ab ../standalone/pocsag/test_iq_source_ab.cpp)
target_link_libraries(tst_pocsag_iq_source_ab PRIVATE dsdr::core)
target_compile_definitions(tst_pocsag_iq_source_ab PRIVATE DSDR_POCSAG_BINARY_DIR="${PROJECT_BINARY_DIR}")
dsdr_apply_target_defaults(tst_pocsag_iq_source_ab)
add_test(NAME tst_pocsag_iq_source_ab COMMAND tst_pocsag_iq_source_ab)
set_tests_properties(tst_pocsag_iq_source_ab PROPERTIES ENVIRONMENT "DSDR_TEST_SILENT_AUDIO=1" TIMEOUT 60)

# AIS frontend, conservative input-loss fence and asynchronous receiver.
add_executable(tst_ais_frontend ../standalone/ais_stage/tst_ais_frontend.cpp)
target_link_libraries(tst_ais_frontend PRIVATE dsdr::dsp)
dsdr_apply_target_defaults(tst_ais_frontend)
add_test(NAME tst_ais_frontend COMMAND tst_ais_frontend)
set_tests_properties(tst_ais_frontend PROPERTIES ENVIRONMENT "DSDR_TEST_SILENT_AUDIO=1" TIMEOUT 30)

add_executable(tst_ais_gap_fence ../standalone/ais_stage/tst_ais_gap_fence.cpp)
target_include_directories(tst_ais_gap_fence PRIVATE "${CMAKE_SOURCE_DIR}/tests/standalone")
target_link_libraries(tst_ais_gap_fence PRIVATE dsdr::core)
dsdr_apply_target_defaults(tst_ais_gap_fence)
add_test(NAME tst_ais_gap_fence COMMAND tst_ais_gap_fence)
set_tests_properties(tst_ais_gap_fence PROPERTIES ENVIRONMENT "DSDR_TEST_SILENT_AUDIO=1" TIMEOUT 30)

add_executable(tst_ais_receiver ../standalone/tst_ais_receiver.cpp)
target_include_directories(tst_ais_receiver PRIVATE "${CMAKE_SOURCE_DIR}/tests/standalone")
target_link_libraries(tst_ais_receiver PRIVATE dsdr::core)
dsdr_apply_target_defaults(tst_ais_receiver)
add_test(NAME tst_ais_receiver COMMAND tst_ais_receiver)
set_tests_properties(tst_ais_receiver PROPERTIES ENVIRONMENT "DSDR_TEST_SILENT_AUDIO=1" TIMEOUT 30)

add_executable(tst_ais_qml ../standalone/tst_ais_qml.cpp)
target_include_directories(tst_ais_qml PRIVATE "${CMAKE_SOURCE_DIR}/tests/standalone")
target_link_libraries(tst_ais_qml PRIVATE dsdr::core Qt6::Gui Qt6::Qml Qt6::Quick)
dsdr_apply_target_defaults(tst_ais_qml)
add_test(NAME tst_ais_qml COMMAND tst_ais_qml "${PROJECT_BINARY_DIR}/ais-qml-fixture" "${PROJECT_SOURCE_DIR}/src/app/qml")
set_tests_properties(tst_ais_qml PROPERTIES ENVIRONMENT "DSDR_TEST_SILENT_AUDIO=1;QT_QPA_PLATFORM=offscreen" TIMEOUT 30)

# Synthetic IQ -> AIS frontend/tap -> asynchronous decoder -> real dialog.
dsdr_add_test(tst_ais_end_to_end
    SOURCES ../standalone/ais_stage/tst_ais_end_to_end.cpp
    LIBRARIES dsdr::core Qt6::Gui Qt6::Qml Qt6::Quick)
target_include_directories(tst_ais_end_to_end PRIVATE "${CMAKE_SOURCE_DIR}/tests/standalone")
target_compile_definitions(tst_ais_end_to_end PRIVATE DSDR_AIS_QML_DIR="${CMAKE_SOURCE_DIR}/src/app/qml")
set_tests_properties(tst_ais_end_to_end PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;DSDR_TEST_SILENT_AUDIO=1" TIMEOUT 30)

# Test-only module exercises the existing DspEngine filtered-IQ module ABI.
add_library(dsdr_ais_engine_probe MODULE
    ../standalone/ais_stage/ais_engine_probe.cpp
    ../../src/core/AsyncIqDecoder.cpp ../../src/core/AisReceiver.cpp ../../src/dsp/AisDecoder.cpp)
target_include_directories(dsdr_ais_engine_probe PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(dsdr_ais_engine_probe PRIVATE Qt6::Core)
set_target_properties(dsdr_ais_engine_probe PROPERTIES PREFIX "")
dsdr_add_test(tst_ais_real_engine
    SOURCES ../standalone/ais_stage/tst_ais_real_engine.cpp
    LIBRARIES dsdr::core)
target_include_directories(tst_ais_real_engine PRIVATE "${CMAKE_SOURCE_DIR}/tests/standalone")
target_compile_definitions(tst_ais_real_engine PRIVATE
    DSDR_AIS_PROBE_PATH="$<TARGET_FILE:dsdr_ais_engine_probe>")
add_dependencies(tst_ais_real_engine dsdr_ais_engine_probe)
set_tests_properties(tst_ais_real_engine PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;DSDR_TEST_SILENT_AUDIO=1" TIMEOUT 60)

dsdr_add_test(tst_ais_engine_live SOURCES tst_ais_engine_live.cpp LIBRARIES dsdr::core)
set_tests_properties(tst_ais_engine_live PROPERTIES ENVIRONMENT "DSDR_TEST_SILENT_AUDIO=1" TIMEOUT 60)

dsdr_add_test(tst_ais_session_live SOURCES tst_ais_session_live.cpp LIBRARIES dsdr::core Qt6::Gui Qt6::Qml Qt6::Quick)
target_compile_definitions(tst_ais_session_live PRIVATE DSDR_AIS_QML_DIR="${CMAKE_SOURCE_DIR}/src/app/qml")
set_tests_properties(tst_ais_session_live PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;DSDR_TEST_SILENT_AUDIO=1" TIMEOUT 180)
