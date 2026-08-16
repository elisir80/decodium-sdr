# SPDX-License-Identifier: GPL-3.0-or-later
# Verifica la parte importante senza dipendere dai moduli del computer che
# esegue i test: un packager dichiara due plugin e il manifest deve contenerli
# entrambi, nell'ordine richiesto.

if(NOT DSDR_SOURCE_DIR)
    message(FATAL_ERROR "DSDR_SOURCE_DIR non passato al test")
endif()

set(dsdr_fixture "${CMAKE_CURRENT_BINARY_DIR}/soapy-bundle-fixture")
set(dsdr_modules "${dsdr_fixture}/modules0.8")
file(REMOVE_RECURSE "${dsdr_fixture}")
file(MAKE_DIRECTORY "${dsdr_modules}")
file(WRITE "${dsdr_modules}/librtlsdrSupport.so" "fixture rtl")
file(WRITE "${dsdr_modules}/libHackRFSupport.so" "fixture hackrf")

set(DSDR_BUNDLE_SOAPY ON)
set(DSDR_BACKEND_SOAPY ON)
set(DSDR_SOAPY_BUNDLE_MODULES "librtlsdrSupport;libHackRFSupport")
set(DSDR_SOAPY_MODULE_PATHS "${dsdr_modules}")
include("${DSDR_SOURCE_DIR}/cmake/DsdrSoapyBundle.cmake")
dsdr_prepare_soapy_bundle()

list(LENGTH DSDR_SOAPY_BUNDLED_MODULE_FILES dsdr_module_count)
if(NOT dsdr_module_count EQUAL 2)
    message(FATAL_ERROR "Attesi due moduli Soapy, trovati: ${DSDR_SOAPY_BUNDLED_MODULE_FILES}")
endif()
list(GET DSDR_SOAPY_BUNDLED_MODULE_FILES 0 dsdr_first)
list(GET DSDR_SOAPY_BUNDLED_MODULE_FILES 1 dsdr_second)
if(NOT dsdr_first STREQUAL "${dsdr_modules}/librtlsdrSupport.so"
   OR NOT dsdr_second STREQUAL "${dsdr_modules}/libHackRFSupport.so")
    message(FATAL_ERROR "Ordine o percorso moduli inatteso: ${DSDR_SOAPY_BUNDLED_MODULE_FILES}")
endif()

file(READ "${CMAKE_BINARY_DIR}/dsdr-soapy-modules.txt" dsdr_manifest)
string(FIND "${dsdr_manifest}" "librtlsdrSupport.so" dsdr_rtl_offset)
string(FIND "${dsdr_manifest}" "libHackRFSupport.so" dsdr_hackrf_offset)
if(dsdr_rtl_offset LESS 0 OR dsdr_hackrf_offset LESS 0)
    message(FATAL_ERROR "Manifest Soapy incompleto: ${dsdr_manifest}")
endif()

file(REMOVE_RECURSE "${dsdr_fixture}")
