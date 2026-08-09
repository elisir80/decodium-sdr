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

dsdr_add_test(tst_overload_guard
    SOURCES tst_overload_guard.cpp
    LIBRARIES dsdr::dsp)
