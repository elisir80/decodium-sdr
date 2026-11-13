# Test-only top-level injection; leave the application's shared CMake untouched.
cmake_language(DEFER CALL include "${CMAKE_SOURCE_DIR}/tests/standalone/aprs_gap/targets.cmake")
