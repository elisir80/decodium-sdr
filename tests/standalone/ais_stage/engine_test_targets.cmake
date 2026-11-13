if(NOT TARGET dsdr_ais_engine_probe)
    add_library(dsdr_ais_engine_probe MODULE
        "${CMAKE_SOURCE_DIR}/tests/standalone/ais_stage/ais_engine_probe.cpp"
        "${CMAKE_SOURCE_DIR}/src/core/AsyncIqDecoder.cpp"
        "${CMAKE_SOURCE_DIR}/src/core/AisReceiver.cpp"
        "${CMAKE_SOURCE_DIR}/src/dsp/AisDecoder.cpp")
    target_include_directories(dsdr_ais_engine_probe PRIVATE "${CMAKE_SOURCE_DIR}/src")
    target_link_libraries(dsdr_ais_engine_probe PRIVATE Qt6::Core)
    set_target_properties(dsdr_ais_engine_probe PROPERTIES PREFIX "")
    qt_add_executable(tst_ais_real_engine
        "${CMAKE_SOURCE_DIR}/tests/standalone/ais_stage/tst_ais_real_engine.cpp")
    target_include_directories(tst_ais_real_engine PRIVATE
        "${CMAKE_SOURCE_DIR}/src" "${CMAKE_SOURCE_DIR}/tests/standalone")
    target_link_libraries(tst_ais_real_engine PRIVATE dsdr::core Qt6::Test)
    target_compile_definitions(tst_ais_real_engine PRIVATE
        DSDR_AIS_PROBE_PATH="$<TARGET_FILE:dsdr_ais_engine_probe>")
    add_dependencies(tst_ais_real_engine dsdr_ais_engine_probe)
    add_test(NAME tst_ais_real_engine COMMAND tst_ais_real_engine)
    set_tests_properties(tst_ais_real_engine PROPERTIES
        ENVIRONMENT "DSDR_TEST_SILENT_AUDIO=1;QT_QPA_PLATFORM=offscreen" TIMEOUT 60)
endif()
