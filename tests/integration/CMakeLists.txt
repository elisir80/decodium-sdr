# RNF-07: l'applicazione completa (meno la UI) contro il backend demo,
# eseguibile headless in CI.

dsdr_add_test(tst_session_demo
    SOURCES tst_session_demo.cpp
    LIBRARIES dsdr::core)

dsdr_add_test(tst_iq_recorder
    SOURCES tst_iq_recorder.cpp
    LIBRARIES dsdr::core)

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
