# Test dei modelli e dei ponti di core/ che si verificano senza una radio e
# senza una GPU.

dsdr_add_test(tst_spectrum_feed
    SOURCES tst_spectrum_feed.cpp
    LIBRARIES dsdr::core
)

# La linea grigia. Una mappa di propagazione sbagliata non da' un errore: da'
# una mappa, e manda a chiamare mezz'ora prima o dopo il momento buono.
dsdr_add_test(tst_greyline
    SOURCES tst_greyline.cpp
    LIBRARIES dsdr::core)

# Il rotore. Un motore in cima a una torre non si prova sul banco: quel che si
# prova e' che non parta da solo, che non lo si martelli, e che FERMA fermi.
dsdr_add_test(tst_rotor
    SOURCES tst_rotor.cpp RotctldMockServer.cpp RotctldMockServer.h
    LIBRARIES dsdr::core Qt6::Network)

# Il registro delle condizioni. Un registro che sbaglia non da' un errore: da'
# un grafico, e chi lo guarda conclude qualcosa di sbagliato sulla banda.
dsdr_add_test(tst_band_conditions
    SOURCES tst_band_conditions.cpp
    LIBRARIES dsdr::core)
