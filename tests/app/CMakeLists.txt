# Test dei componenti di presentazione che hanno una regola verificabile senza
# una GPU: tabelle di colore e scala automatica del waterfall.

dsdr_add_test(tst_waterfall
    SOURCES tst_waterfall.cpp
    LIBRARIES dsdr::qml
)
