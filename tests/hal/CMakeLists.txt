# Conformance suite HAL (§8): ogni backend passa la STESSA batteria di test.
# È il contratto che tiene onesto il seam — e il modo in cui un backend nuovo
# viene validato senza hardware, sostituendo il transport con un mock.

dsdr_add_test(tst_hal_conformance
    SOURCES
        tst_hal_conformance.cpp
        RtlTcpMockServer.h
        RtlTcpMockServer.cpp
    LIBRARIES dsdr::hal Qt6::Network)

if(DSDR_BACKEND_SOAPY)
    # La traduzione profilo → capability è l'unica parte del backend soapy
    # verificabile senza hardware, ed è quella che guida la UI.
    dsdr_add_test(tst_soapy_profile
        SOURCES tst_soapy_profile.cpp
        LIBRARIES dsdr::hal)
endif()

if(DSDR_BACKEND_NETTCP)
    dsdr_add_test(tst_nettcp
        SOURCES
            tst_nettcp.cpp
            RtlTcpMockServer.h
            RtlTcpMockServer.cpp
            SpyServerMockServer.h
            SpyServerMockServer.cpp
        LIBRARIES dsdr::hal Qt6::Network)
endif()
