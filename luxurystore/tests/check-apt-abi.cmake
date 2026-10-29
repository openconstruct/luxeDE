# Copyright (C) 2026 Jerry Howell
# SPDX-License-Identifier: GPL-2.0-or-later

if(NOT EXISTS "${PROGRAM}")
  message(FATAL_ERROR "LuxuryStore executable is missing: ${PROGRAM}")
endif()
if("${EXPECTED_SONAME}" STREQUAL "")
  message(FATAL_ERROR "Expected APT SONAME was not configured")
endif()

find_program(READELF_EXECUTABLE readelf REQUIRED)
execute_process(
  COMMAND "${READELF_EXECUTABLE}" -d "${PROGRAM}"
  RESULT_VARIABLE readelf_result
  OUTPUT_VARIABLE dynamic_section
  ERROR_VARIABLE readelf_error)
if(NOT readelf_result EQUAL 0)
  message(FATAL_ERROR "Cannot inspect ${PROGRAM}: ${readelf_error}")
endif()
if(NOT dynamic_section MATCHES "Shared library: \\[${EXPECTED_SONAME}\\]")
  message(FATAL_ERROR
    "${PROGRAM} does not require the configured APT ABI ${EXPECTED_SONAME}")
endif()

if(DEFINED HELPER AND NOT HELPER STREQUAL "")
  if(NOT EXISTS "${HELPER}")
    message(FATAL_ERROR "LuxuryStore helper is missing: ${HELPER}")
  endif()
  execute_process(
    COMMAND "${READELF_EXECUTABLE}" -d "${HELPER}"
    RESULT_VARIABLE helper_readelf_result
    OUTPUT_VARIABLE helper_dynamic_section
    ERROR_VARIABLE helper_readelf_error)
  if(NOT helper_readelf_result EQUAL 0)
    message(FATAL_ERROR "Cannot inspect ${HELPER}: ${helper_readelf_error}")
  endif()
  if(NOT helper_dynamic_section MATCHES "Shared library: \\[${EXPECTED_SONAME}\\]")
    message(FATAL_ERROR
      "${HELPER} does not require the configured APT ABI ${EXPECTED_SONAME}")
  endif()
endif()
