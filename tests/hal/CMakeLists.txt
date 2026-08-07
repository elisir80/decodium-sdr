# Conformance suite HAL (§8): ogni backend passa la STESSA batteria di test.
# È il contratto che tiene onesto il seam — e il modo in cui un backend nuovo
# viene validato senza hardware, sostituendo il transport con un mock.

dsdr_add_test(tst_hal_conformance
    SOURCES tst_hal_conformance.cpp
    LIBRARIES dsdr::hal)
