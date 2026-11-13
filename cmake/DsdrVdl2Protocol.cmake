# Experimental isolated VDL2 protocol library, not the VDL2 receiver.
# The caller includes this only when explicitly enabled. Cargo and its target
# must already be installed; no toolchain or crate is downloaded by the build.
find_program(DSDR_VDL2_CARGO NAMES cargo REQUIRED)
if(NOT EXISTS "${DSDR_VDL2_CARGO}")
    message(FATAL_ERROR "VDL2 protocol requires an existing Cargo executable: ${DSDR_VDL2_CARGO}")
endif()
set(DSDR_VDL2_RUST_TARGET "" CACHE STRING "Rust target triple for a cross-compiled VDL2 protocol library")
if(CMAKE_CROSSCOMPILING AND NOT DSDR_VDL2_RUST_TARGET)
    message(FATAL_ERROR "VDL2 protocol cross-compilation requires DSDR_VDL2_RUST_TARGET")
endif()
if(APPLE AND CMAKE_OSX_ARCHITECTURES)
    list(LENGTH CMAKE_OSX_ARCHITECTURES _dsdr_vdl_arch_count)
    if(_dsdr_vdl_arch_count GREATER 1)
        message(FATAL_ERROR "VDL2 protocol does not yet support universal macOS builds")
    endif()
    if(NOT DSDR_VDL2_RUST_TARGET AND NOT CMAKE_OSX_ARCHITECTURES STREQUAL CMAKE_HOST_SYSTEM_PROCESSOR)
        message(FATAL_ERROR "Set DSDR_VDL2_RUST_TARGET when the macOS target architecture differs from the host")
    endif()
endif()
set(_dsdr_vdl_root "${PROJECT_SOURCE_DIR}/src/dsp/vdl2/rust")
set(_dsdr_vdl_build "${PROJECT_BINARY_DIR}/vdl2-rust")
set(_dsdr_vdl_target_args)
set(_dsdr_vdl_release "${_dsdr_vdl_build}/release")
if(DSDR_VDL2_RUST_TARGET)
    list(APPEND _dsdr_vdl_target_args --target "${DSDR_VDL2_RUST_TARGET}")
    set(_dsdr_vdl_release "${_dsdr_vdl_build}/${DSDR_VDL2_RUST_TARGET}/release")
endif()
if(MSVC)
    set(_dsdr_vdl_library "${_dsdr_vdl_release}/dsdr_vdl2.lib")
else()
    set(_dsdr_vdl_library "${_dsdr_vdl_release}/libdsdr_vdl2.a")
endif()
file(GLOB_RECURSE _dsdr_vdl_inputs CONFIGURE_DEPENDS
    "${_dsdr_vdl_root}/*.rs" "${_dsdr_vdl_root}/Cargo.toml" "${_dsdr_vdl_root}/*/Cargo.toml")
add_custom_command(OUTPUT "${_dsdr_vdl_library}"
    COMMAND "${CMAKE_COMMAND}" -E env "CARGO_TARGET_DIR=${_dsdr_vdl_build}"
            "${DSDR_VDL2_CARGO}" build --offline --locked --release -p dsdr_vdl2 ${_dsdr_vdl_target_args}
    WORKING_DIRECTORY "${_dsdr_vdl_root}"
    DEPENDS ${_dsdr_vdl_inputs} "${_dsdr_vdl_root}/Cargo.lock"
    COMMENT "Building experimental isolated VDL2 protocol"
    VERBATIM)
add_custom_target(dsdr_vdl2_protocol_build DEPENDS "${_dsdr_vdl_library}")
add_library(dsdr_vdl2_protocol STATIC IMPORTED GLOBAL)
set_target_properties(dsdr_vdl2_protocol PROPERTIES IMPORTED_LOCATION "${_dsdr_vdl_library}")
if(MINGW)
    set_property(TARGET dsdr_vdl2_protocol APPEND PROPERTY INTERFACE_LINK_LIBRARIES synchronization ntdll ws2_32 userenv bcrypt)
endif()
add_dependencies(dsdr_vdl2_protocol dsdr_vdl2_protocol_build)
add_library(dsdr_vdl2_adapter STATIC "${PROJECT_SOURCE_DIR}/src/dsp/vdl2/Vdl2Frame.cpp"
    "${PROJECT_SOURCE_DIR}/src/dsp/vdl2/Vdl2Burst.cpp"
    "${PROJECT_SOURCE_DIR}/src/dsp/vdl2/Vdl2Symbols.cpp"
    "${PROJECT_SOURCE_DIR}/src/dsp/vdl2/Vdl2Acquisition.cpp"
    "${PROJECT_SOURCE_DIR}/src/dsp/vdl2/Vdl2Offline.cpp"
    "${PROJECT_SOURCE_DIR}/src/dsp/vdl2/Vdl2WindowStream.cpp"
    "${PROJECT_SOURCE_DIR}/src/dsp/vdl2/Vdl2Frontend.cpp")
target_include_directories(dsdr_vdl2_adapter PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(dsdr_vdl2_adapter PUBLIC dsdr_vdl2_protocol)
