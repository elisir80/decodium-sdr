cmake_minimum_required(VERSION 3.21)
project(dsdr_ais_stage LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
enable_testing()
find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick Test)
find_package(Threads REQUIRED)
get_filename_component(DSDR_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
add_library(ais_stage_core STATIC
    "${DSDR_ROOT}/src/dsp/AisDecoder.cpp"
    "${DSDR_ROOT}/src/dsp/AisIqFrontend.cpp"
    "${DSDR_ROOT}/src/core/AsyncIqDecoder.cpp"
    "${DSDR_ROOT}/src/core/AisReceiver.cpp"
    "${DSDR_ROOT}/src/core/AisEngineTap.cpp"
    "${DSDR_ROOT}/src/core/AisController.cpp"
    "${DSDR_ROOT}/src/core/AisController.h")
target_include_directories(ais_stage_core PUBLIC "${DSDR_ROOT}/src")
target_link_libraries(ais_stage_core PUBLIC Qt6::Core Threads::Threads)
add_executable(tst_ais_end_to_end tst_ais_end_to_end.cpp)
target_include_directories(tst_ais_end_to_end PRIVATE "${DSDR_ROOT}/tests/standalone")
target_compile_definitions(tst_ais_end_to_end PRIVATE DSDR_AIS_QML_DIR="${DSDR_ROOT}/src/app/qml")
target_link_libraries(tst_ais_end_to_end PRIVATE ais_stage_core Qt6::Gui Qt6::Quick Qt6::Qml Qt6::Test)
add_test(NAME tst_ais_end_to_end COMMAND tst_ais_end_to_end)
set_tests_properties(tst_ais_end_to_end PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;DSDR_TEST_SILENT_AUDIO=1" TIMEOUT 30)
add_executable(tst_ais_frontend tst_ais_frontend.cpp)
target_link_libraries(tst_ais_frontend PRIVATE ais_stage_core)
add_test(NAME tst_ais_frontend COMMAND tst_ais_frontend)
set_tests_properties(tst_ais_frontend PROPERTIES ENVIRONMENT "DSDR_TEST_SILENT_AUDIO=1" TIMEOUT 30)
add_executable(tst_ais_gap_fence tst_ais_gap_fence.cpp)
target_include_directories(tst_ais_gap_fence PRIVATE "${DSDR_ROOT}/tests/standalone")
target_link_libraries(tst_ais_gap_fence PRIVATE ais_stage_core)
add_test(NAME tst_ais_gap_fence COMMAND tst_ais_gap_fence)
set_tests_properties(tst_ais_gap_fence PROPERTIES ENVIRONMENT "DSDR_TEST_SILENT_AUDIO=1" TIMEOUT 30)
