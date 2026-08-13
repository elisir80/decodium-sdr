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
