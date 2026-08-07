# DSP Engine — codice originale (§5, §11). Nessuna dipendenza da Qt Gui/Quick:
# la libreria deve restare testabile headless e riusabile fuori dalla UI.

find_path(FFTW3_INCLUDE_DIR NAMES fftw3.h)
find_library(FFTW3F_LIBRARY NAMES fftw3f libfftw3f fftw3f-3)

if(NOT FFTW3_INCLUDE_DIR OR NOT FFTW3F_LIBRARY)
    message(FATAL_ERROR
        "FFTW3 (single precision) non trovato.\n"
        "  MSYS2 : pacman -S mingw-w64-x86_64-fftw\n"
        "  Debian: apt install libfftw3-dev\n"
        "  macOS : brew install fftw")
endif()

add_library(dsdr_dsp STATIC
    DspTypes.h
    DspTypes.cpp
    SpscRing.h
    Nco.h
    FirDesign.h
    FirDesign.cpp
    FirDecimator.h
    FirDecimator.cpp
    ComplexFir.h
    ComplexFir.cpp
    DecimatorChain.h
    DecimatorChain.cpp
    Agc.h
    Agc.cpp
    Demodulator.h
    Demodulator.cpp
    SpectrumAnalyzer.h
    SpectrumAnalyzer.cpp
    ChannelProcessor.h
    ChannelProcessor.cpp
)

target_include_directories(dsdr_dsp PRIVATE ${FFTW3_INCLUDE_DIR})
target_link_libraries(dsdr_dsp PUBLIC dsdr::common PRIVATE ${FFTW3F_LIBRARY})

dsdr_apply_target_defaults(dsdr_dsp)
add_library(dsdr::dsp ALIAS dsdr_dsp)
