add_library(dsdr_audio STATIC
    AudioRouter.h
    AudioRouter.cpp
)

target_link_libraries(dsdr_audio PUBLIC dsdr::common dsdr::dsp Qt6::Core Qt6::Multimedia)

dsdr_apply_target_defaults(dsdr_audio)
add_library(dsdr::audio ALIAS dsdr_audio)
