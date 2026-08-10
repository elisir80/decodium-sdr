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
    FftwPlanning.h
    FftwPlanning.cpp
    FirDesign.h
    FirDesign.cpp
    FirDecimator.h
    FirDecimator.cpp
    ComplexFir.h
    ComplexFir.cpp
    DecimatorChain.h
    DecimatorChain.cpp
    FirInterpolator.h
    FirInterpolator.cpp
    InterpolatorChain.h
    InterpolatorChain.cpp
    Agc.h
    Agc.cpp
    NoiseBlanker.h
    NoiseBlanker.cpp
    OverloadGuard.h
    OverloadGuard.cpp
    LmsFilter.h
    LmsFilter.cpp
    NotchFilter.h
    NotchFilter.cpp
    PeakFilter.h
    PeakFilter.cpp
    SpectralDenoiser.h
    SpectralDenoiser.cpp
    NeuralDenoiser.h
    NeuralDenoiser.cpp
    neural/INrEngine.h
    neural/RnnoiseEngine.h
    neural/RnnoiseEngine.cpp
    neural/DfnEngine.h
    neural/DfnEngine.cpp
    neural/ModelStore.h
    neural/ModelStore.cpp
    neural/NeuralNrStage.h
    neural/NeuralNrStage.cpp
    Demodulator.h
    Demodulator.cpp
    Modulator.h
    Modulator.cpp
    SpeechProcessor.h
    SpeechProcessor.cpp
    CwKeyer.h
    CwKeyer.cpp
    AudioHighPass.h
    AudioHighPass.cpp
    BroadcastFmStereo.h
    BroadcastFmStereo.cpp
    RdsDecoder.h
    RdsDecoder.cpp
    CtcssDetector.h
    CtcssDetector.cpp
    NoiseBlanker.h
    NoiseBlanker.cpp
    FmIfNoiseReducer.h
    FmIfNoiseReducer.cpp
    SpectrumAnalyzer.h
    SpectrumAnalyzer.cpp
    TimeShiftBuffer.h
    TimeShiftBuffer.cpp
    ChannelProcessor.h
    ChannelProcessor.cpp
)

target_include_directories(dsdr_dsp PRIVATE ${FFTW3_INCLUDE_DIR})
# Lo stadio neurale vive su un thread proprio e ha una macchina a stati: gli
# serve il ciclo di eventi di Qt, non solo il vocabolario di dsdr::common.
target_link_libraries(dsdr_dsp PUBLIC dsdr::common Qt6::Core PRIVATE ${FFTW3F_LIBRARY})

# Il motore neurale, se chi compila ne ha messo il sorgente: la macro decide
# se lo stadio esiste, e da lì nasce la capability che la UI legge.
if(RNNOISE_FOUND)
    target_link_libraries(dsdr_dsp PRIVATE rnnoise)
    target_compile_definitions(dsdr_dsp PUBLIC DSDR_HAVE_RNNOISE)
endif()

set_target_properties(dsdr_dsp PROPERTIES AUTOMOC ON)
dsdr_apply_target_defaults(dsdr_dsp)
add_library(dsdr::dsp ALIAS dsdr_dsp)
