# RNF-07: unit test DSP con vettori noti + conformance suite HAL, tutti
# eseguibili headless in CI.

function(dsdr_add_test name)
    cmake_parse_arguments(ARG "" "" "SOURCES;LIBRARIES" ${ARGN})

    qt_add_executable(${name} ${ARG_SOURCES})
    target_link_libraries(${name} PRIVATE Qt6::Test ${ARG_LIBRARIES})
    dsdr_apply_target_defaults(${name})

    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        TIMEOUT 120)
endfunction()

add_subdirectory(app)
add_subdirectory(core)
add_subdirectory(dsp)
add_subdirectory(hal)
add_subdirectory(integration)
add_subdirectory(qml)
