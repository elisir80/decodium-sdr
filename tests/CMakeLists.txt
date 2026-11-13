# RNF-07: unit test DSP con vettori noti + conformance suite HAL, tutti
# eseguibili headless in CI.

# `TIMEOUT` è opzionale e vale centoventi secondi. Chi ne chiede di più deve
# avere una ragione fisica: la conformance apre i backend veri, e su una
# macchina con delle porte seriali la sonda di `audiorig` le prova tutte —
# centoquattordici secondi misurati, su un limite di centoventi. Sei secondi di
# margine non sono un margine: sono un fallimento in attesa di una giornata
# storta, e un test che cade da solo insegna a ignorare i fallimenti veri.
function(dsdr_add_test name)
    cmake_parse_arguments(ARG "" "TIMEOUT" "SOURCES;LIBRARIES" ${ARGN})
    if(NOT ARG_TIMEOUT)
        set(ARG_TIMEOUT 120)
    endif()

    qt_add_executable(${name} ${ARG_SOURCES})
    target_link_libraries(${name} PRIVATE Qt6::Test ${ARG_LIBRARIES})
    dsdr_apply_target_defaults(${name})

    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        TIMEOUT ${ARG_TIMEOUT})
endfunction()

add_subdirectory(app)
add_subdirectory(core)
add_subdirectory(dsp)
add_subdirectory(plugins)
add_subdirectory(hal)
add_subdirectory(integration)
add_subdirectory(qml)
add_subdirectory(packaging)

find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_test(NAME tst_onair_mode_inventory
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/standalone/test_onair_mode_inventory.py)
    find_program(DSDR_TEST_POSIX_SHELL NAMES sh)
    if(DSDR_TEST_POSIX_SHELL)
        add_test(NAME tst_onair_package_terms
                 COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/standalone/test_onair_terms.py)
        set_tests_properties(tst_onair_package_terms PROPERTIES
            ENVIRONMENT "DSDR_TEST_BUILD_DIR=${PROJECT_BINARY_DIR};DSDR_TEST_SHELL=${DSDR_TEST_POSIX_SHELL}"
            TIMEOUT 30)
    endif()
endif()
