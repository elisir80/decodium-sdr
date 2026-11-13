# Il bundler Soapy si verifica senza un hardware né un package manager: una
# directory di moduli sintetica deve produrre una lista deterministica dei
# plugin da mettere nel pacchetto.
add_test(
    NAME tst_soapy_bundle_manifest
    COMMAND ${CMAKE_COMMAND}
        -DDSDR_SOURCE_DIR=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tst_soapy_bundle_manifest.cmake)
set_tests_properties(tst_soapy_bundle_manifest PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    TIMEOUT 30)

add_test(NAME tst_onair_install_notices
    COMMAND ${CMAKE_COMMAND}
        -DDSDR_SOURCE_DIR=${CMAKE_SOURCE_DIR}
        -DDSDR_BINARY_DIR=${CMAKE_BINARY_DIR}
        -DDSDR_APPLE=${APPLE}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tst_onair_install_notices.cmake)
set_tests_properties(tst_onair_install_notices PROPERTIES RUN_SERIAL TRUE TIMEOUT 30)

find_program(DSDR_TEST_BASH bash)
if(DSDR_TEST_BASH)
    add_test(NAME tst_windows_package_dependencies
        COMMAND ${DSDR_TEST_BASH} ${CMAKE_CURRENT_SOURCE_DIR}/test_windows_dependencies.sh ${CMAKE_SOURCE_DIR})
    set_tests_properties(tst_windows_package_dependencies PROPERTIES TIMEOUT 30)
endif()
