cmake_minimum_required(VERSION 3.21)
project(AcarsStandaloneUiTest LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick Test)
find_package(Threads REQUIRED)
get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
add_executable(tst_acars_qml
    ../test_acars_qml.cpp
    ${REPO_ROOT}/src/core/AcarsController.h
    ${REPO_ROOT}/src/core/AcarsController.cpp
    ${REPO_ROOT}/src/core/AcarsReceiver.cpp
    ${REPO_ROOT}/src/core/AsyncIqDecoder.cpp
    ${REPO_ROOT}/src/dsp/acars/acars_chan.cpp)
target_include_directories(tst_acars_qml PRIVATE ${REPO_ROOT}/src)
target_link_libraries(tst_acars_qml PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Test Threads::Threads)
# Assertions in this standalone harness are checks, including in release builds.
if(MSVC)
  target_compile_options(tst_acars_qml PRIVATE /UNDEBUG)
else()
  target_compile_options(tst_acars_qml PRIVATE -UNDEBUG -Wall -Wextra -Werror)
endif()
enable_testing()
add_test(NAME tst_acars_qml COMMAND tst_acars_qml ${CMAKE_BINARY_DIR}/qml ${REPO_ROOT}/src/app/qml)
set_tests_properties(tst_acars_qml PROPERTIES TIMEOUT 30 ENVIRONMENT
  "QT_QPA_PLATFORM=offscreen;DSDR_TEST_SILENT_AUDIO=1;TMPDIR=${CMAKE_BINARY_DIR};XDG_CONFIG_HOME=${CMAKE_BINARY_DIR};XDG_CACHE_HOME=${CMAKE_BINARY_DIR}")
