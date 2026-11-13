# Test-only top-level injection; no shared CMake file is edited.
cmake_language(DEFER CALL include "${CMAKE_SOURCE_DIR}/tests/standalone/ais_stage/engine_test_targets.cmake")
