# Conformance suite HAL (§8): ogni backend passa la STESSA batteria di test.
# È il contratto che tiene onesto il seam — e il modo in cui un backend nuovo
# viene validato senza hardware, sostituendo il transport con un mock.

dsdr_add_test(tst_hal_conformance
    SOURCES
        tst_hal_conformance.cpp
        RtlTcpMockServer.h
        RtlTcpMockServer.cpp
    LIBRARIES dsdr::hal Qt6::Network)

# La suite non sonda le porte seriali. Due motivi, e il primo basta da solo:
# aprire una porta manda un impulso su RTS e DTR, e su una radio con «CAT RTS»
# attivo quello è il PTT — nessuno deve poter mandare in aria una portante
# lanciando i test. Il secondo è che una sonda per ogni prova faceva durare la
# conformance due minuti.
set_tests_properties(tst_hal_conformance PROPERTIES
    ENVIRONMENT "DSDR_AUDIORIG_NO_PROBE=1")

# Il rilevamento in rete: i pacchetti si costruiscono a mano, e nessuna radio
# deve essere accesa perche' il test dica qualcosa.
dsdr_add_test(tst_radio_scout
    SOURCES tst_radio_scout.cpp
    LIBRARIES dsdr::hal Qt6::Network)

if(DSDR_BACKEND_SOAPY)
    # La traduzione profilo → capability è l'unica parte del backend soapy
    # verificabile senza hardware, ed è quella che guida la UI.
    dsdr_add_test(tst_soapy_profile
        SOURCES tst_soapy_profile.cpp
        LIBRARIES dsdr::hal)
endif()

if(DSDR_BACKEND_RTLSDR)
    dsdr_add_test(tst_rtlsdr_profile
        SOURCES tst_rtlsdr_profile.cpp
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

# `DSDR_BUILT_AUDIORIG`, non `DSDR_BACKEND_AUDIORIG`: la seconda dice cosa si
# e' chiesto, la prima cosa si e' ottenuto. Dove manca Qt6::SerialPort le due
# divergono, e compilare questi test sulla richiesta invece che sul risultato
# fa fallire il collegamento parlando di simboli mancanti invece che del
# modulo Qt che manca davvero.
if(DSDR_BUILT_AUDIORIG)
    # Formato della porta CAT: i controlli QML devono arrivare ai valori Qt
    # esatti, e i valori corrotti non devono diventare una radio muta.
    dsdr_add_test(tst_cat_serial_config
        SOURCES tst_cat_serial_config.cpp
        LIBRARIES dsdr::hal)

    # La tabella dei modi newcat è l'unica parte del backend verificabile
    # senza radio, ed è quella in cui un errore manda in aria il modo
    # sbagliato invece di far fallire qualcosa.
    dsdr_add_test(tst_newcat
        SOURCES tst_newcat.cpp
        LIBRARIES dsdr::hal)

    # Il BCD del CI-V: invertire i byte non produce un errore, produce una
    # frequenza plausibile.
    dsdr_add_test(tst_civ
        SOURCES tst_civ.cpp
        LIBRARIES dsdr::hal)

    # rigctld: l'indirizzo, i campi delle risposte estese e la conversione
    # dell'S-meter. In coda un finto demone di quindici righe, che presidia
    # l'unica cosa che le funzioni pure non vedono — che la sonda non si
    # attacchi al server rigctl di DECODIUM SDR, che sta sulla stessa porta.
    dsdr_add_test(tst_rigctld
        SOURCES tst_rigctld.cpp
        LIBRARIES dsdr::hal Qt6::Network)
endif()

if(DSDR_BACKEND_COLIBRI)
    # Il ripiegamento nelle zone di Nyquist e' aritmetica pura: si verifica
    # senza device, ed e' quella che sbaglia in silenzio.
    dsdr_add_test(tst_colibri_nyquist
        SOURCES tst_colibri_nyquist.cpp
        LIBRARIES dsdr::hal)
endif()

if(DSDR_BACKEND_HERMES)
    # Gli offset del protocollo 1: un byte contato male non fa fallire niente,
    # produce campioni plausibili e sbagliati.
    dsdr_add_test(tst_hermes_protocol
        SOURCES tst_hermes_protocol.cpp
        LIBRARIES dsdr::hal)
endif()

if(DSDR_BACKEND_FLEX_PROBE)
    # Le righe di SmartSDR: il codice d'errore e' esadecimale e i campi
    # arrivano in ordine libero. Sono le due trappole di un protocollo di
    # testo che sembra impossibile da sbagliare.
    dsdr_add_test(tst_flex_protocol
        SOURCES tst_flex_protocol.cpp
        LIBRARIES dsdr::hal)
endif()
