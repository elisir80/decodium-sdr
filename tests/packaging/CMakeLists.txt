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
