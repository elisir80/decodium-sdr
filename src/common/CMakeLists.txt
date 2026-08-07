add_library(dsdr_common STATIC
    Types.h
    Types.cpp
)

target_link_libraries(dsdr_common PUBLIC Qt6::Core)
dsdr_apply_target_defaults(dsdr_common)
add_library(dsdr::common ALIAS dsdr_common)
