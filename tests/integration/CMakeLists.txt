# RNF-07: l'applicazione completa (meno la UI) contro il backend demo,
# eseguibile headless in CI.

dsdr_add_test(tst_session_demo
    SOURCES tst_session_demo.cpp
    LIBRARIES dsdr::core)
