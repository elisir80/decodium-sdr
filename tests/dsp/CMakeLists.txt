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

# Gate, leveller e limiter: i tre stadi che sbagliano in silenzio — non
# producono un errore, producono un segnale.
dsdr_add_test(tst_tx_dynamics
    SOURCES tst_tx_dynamics.cpp
    LIBRARIES dsdr::dsp)

# Il registratore della propria voce. Un buffer circolare non sbaglia mai con un
# errore: sbaglia con dell'audio, e chi lo sente va a cercare il difetto nella
# catena invece che nell'indice.
dsdr_add_test(tst_voice_recorder
    SOURCES tst_voice_recorder.cpp
    LIBRARIES dsdr::dsp)

# Il multibanda: la prova che conta e' che a riposo non si senta. Se le
# separazioni non sommano piatto, colora la voce prima ancora di comprimere.
dsdr_add_test(tst_multiband
    SOURCES tst_multiband.cpp
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

# PCM di rete: il test ascolta davvero un datagramma localhost e presidia
# formato, frequenza e assenza di intestazioni proprietarie.
dsdr_add_test(tst_network_audio
    SOURCES tst_network_audio.cpp
    LIBRARIES dsdr::audio Qt6::Network)

# La soglia AGC-T automatica: due righe di aritmetica che si sbagliano in
# silenzio, e il difetto si attribuisce all'AGC.
dsdr_add_test(tst_agc_threshold
    SOURCES tst_agc_threshold.cpp
    LIBRARIES dsdr::dsp)
