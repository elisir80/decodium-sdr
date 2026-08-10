# RNF-07: l'applicazione completa (meno la UI) contro il backend demo,
# eseguibile headless in CI.

dsdr_add_test(tst_session_demo
    SOURCES tst_session_demo.cpp
    LIBRARIES dsdr::core)

# Il percorso di trasmissione senza microfono: in CW la sorgente è il tasto,
# e tutto il resto della catena è lo stesso.
dsdr_add_test(tst_tx_engine
    SOURCES tst_tx_engine.cpp
    LIBRARIES dsdr::core)

dsdr_add_test(tst_iq_recorder
    SOURCES tst_iq_recorder.cpp
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
