dsdr_add_test(tst_dsp_primitives
    SOURCES tst_dsp_primitives.cpp
    LIBRARIES dsdr::dsp)

dsdr_add_test(tst_dsp_chain
    SOURCES tst_dsp_chain.cpp
    LIBRARIES dsdr::dsp)

dsdr_add_test(tst_time_shift
    SOURCES tst_time_shift.cpp
    LIBRARIES dsdr::dsp)

dsdr_add_test(tst_noise_filters
    SOURCES tst_noise_filters.cpp
    LIBRARIES dsdr::dsp)

# L'equalizzatore parametrico: un errore qui non fallisce, suona — e la curva
# che si guarda mentre si regola e' la prima cosa che puo' mentire.
dsdr_add_test(tst_parametric_eq
    SOURCES tst_parametric_eq.cpp
    LIBRARIES dsdr::dsp)

dsdr_add_test(tst_overload_guard
    SOURCES tst_overload_guard.cpp
    LIBRARIES dsdr::dsp)

dsdr_add_test(tst_spectral_denoiser
    SOURCES tst_spectral_denoiser.cpp
    LIBRARIES dsdr::dsp)

dsdr_add_test(tst_neural_denoiser
    SOURCES tst_neural_denoiser.cpp
    LIBRARIES dsdr::dsp)

dsdr_add_test(tst_tx_chain
    SOURCES tst_tx_chain.cpp
    LIBRARIES dsdr::dsp)

# Lo stadio neurale: bypass identico, zero allocazioni nel percorso caldo,
# latenza dichiarata e degrado onesto (DSDR-IMPL-001 §7.3).
dsdr_add_test(tst_neural_stage
    SOURCES tst_neural_stage.cpp
    LIBRARIES dsdr::dsp)

# L'interlock digitale: l'audio passato dalla rete non deve mai raggiungere un
# decodificatore, e il rifiuto avviene costruendo il collegamento.
dsdr_add_test(tst_audio_graph
    SOURCES tst_audio_graph.cpp
    LIBRARIES dsdr::audio)

# La soglia AGC-T automatica: due righe di aritmetica che si sbagliano in
# silenzio, e il difetto si attribuisce all'AGC.
dsdr_add_test(tst_agc_threshold
    SOURCES tst_agc_threshold.cpp
    LIBRARIES dsdr::dsp)
