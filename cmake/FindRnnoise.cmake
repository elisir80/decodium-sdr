# FindRnnoise.cmake — costruisce RNNoise dal sorgente vendorizzato.
#
# RNNoise (Xiph, BSD-3) è il motore di riduzione di rumore neurale leggero
# dello stadio [E] (DSDR-SPEC-003 §8): poche centinaia di kilobyte di codice e
# meno del cinque per cento di un core, il che lo rende l'unico candidato
# ragionevole su un CM5 o su un portatile di dieci anni fa.
#
# Il sorgente non sta nel repository — non è nostro da ridistribuire, e i
# pesi del modello arrivano da un tarball a parte:
#
#   git clone --depth 1 https://github.com/xiph/rnnoise third_party/rnnoise
#   cd third_party/rnnoise && ./download_model.sh
#
# Senza, `DSDR_NEURAL_NR` resta spento e lo stadio si dichiara non
# disponibile: la UI lo dice invece di offrire un interruttore inerte
# (CONSTITUTION §7).

set(RNNOISE_ROOT "${CMAKE_SOURCE_DIR}/third_party/rnnoise")

if(NOT EXISTS "${RNNOISE_ROOT}/src/denoise.c")
    set(RNNOISE_FOUND FALSE)
    return()
endif()

if(NOT EXISTS "${RNNOISE_ROOT}/src/rnnoise_data.c")
    message(STATUS "RNNoise: sorgente presente ma senza pesi "
                   "(eseguire third_party/rnnoise/download_model.sh)")
    set(RNNOISE_FOUND FALSE)
    return()
endif()

add_library(rnnoise STATIC
    "${RNNOISE_ROOT}/src/denoise.c"
    "${RNNOISE_ROOT}/src/rnn.c"
    "${RNNOISE_ROOT}/src/pitch.c"
    "${RNNOISE_ROOT}/src/kiss_fft.c"
    "${RNNOISE_ROOT}/src/celt_lpc.c"
    "${RNNOISE_ROOT}/src/nnet.c"
    "${RNNOISE_ROOT}/src/nnet_default.c"
    "${RNNOISE_ROOT}/src/parse_lpcnet_weights.c"
    "${RNNOISE_ROOT}/src/rnnoise_data.c"
    "${RNNOISE_ROOT}/src/rnnoise_tables.c"
)

target_include_directories(rnnoise
    PUBLIC "${RNNOISE_ROOT}/include"
    PRIVATE "${RNNOISE_ROOT}/src" "${RNNOISE_ROOT}")

# `COMPILE_OPUS` non serve: usiamo solo il denoiser. I warning del codice
# altrui non sono nostri da correggere, e trattarli come errori bloccherebbe
# la build a ogni aggiornamento del sorgente a monte.
target_compile_definitions(rnnoise PRIVATE RNNOISE_BUILD DISABLE_DOT_PROD)
if(NOT MSVC)
    target_compile_options(rnnoise PRIVATE -w -O2)
endif()

# Niente moc su codice C altrui.
set_target_properties(rnnoise PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

set(RNNOISE_FOUND TRUE)
message(STATUS "RNNoise: compilato dal sorgente in third_party/rnnoise")
