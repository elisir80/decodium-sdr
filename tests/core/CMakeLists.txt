# Test dei modelli e dei ponti di core/ che si verificano senza una radio e
# senza una GPU.

dsdr_add_test(tst_spectrum_feed
    SOURCES tst_spectrum_feed.cpp
    LIBRARIES dsdr::core
)
