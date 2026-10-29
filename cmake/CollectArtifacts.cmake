# Copyright (C) 2026 Jerry Howell
# SPDX-License-Identifier: GPL-2.0-or-later

cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED INPUT_ROOT OR NOT DEFINED ARTIFACT_ROOT)
  message(FATAL_ERROR
    "Usage: cmake -DINPUT_ROOT=<build-root> -DARTIFACT_ROOT=<repo>/artifacts -P cmake/CollectArtifacts.cmake")
endif()
if(NOT IS_DIRECTORY "${INPUT_ROOT}")
  message(FATAL_ERROR "INPUT_ROOT is not a directory: ${INPUT_ROOT}")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/LuxeComponents.cmake")
find_program(DPKG_DEB_EXECUTABLE dpkg-deb REQUIRED)

file(GLOB package_files LIST_DIRECTORIES FALSE "${INPUT_ROOT}/*/packages/*.deb")
list(SORT package_files)
set(package_names)
set(package_records)

foreach(package_file IN LISTS package_files)
  execute_process(
    COMMAND "${DPKG_DEB_EXECUTABLE}" -f "${package_file}" Package
    RESULT_VARIABLE result
    OUTPUT_VARIABLE package_name
    ERROR_VARIABLE error
    OUTPUT_STRIP_TRAILING_WHITESPACE)
  if(NOT result EQUAL 0 OR package_name STREQUAL "")
    message(FATAL_ERROR "Cannot read package name from ${package_file}: ${error}")
  endif()
  if(NOT package_name IN_LIST LUXE_COMPONENTS)
    message(FATAL_ERROR "Unexpected package '${package_name}' in ${package_file}")
  endif()
  if(package_name IN_LIST package_names)
    message(FATAL_ERROR "More than one DEB was supplied for package '${package_name}'")
  endif()

  execute_process(
    COMMAND "${CMAKE_CURRENT_LIST_DIR}/AuditDeb.sh"
            "${package_file}" "${package_name}"
    RESULT_VARIABLE audit_result
    OUTPUT_VARIABLE audit_output
    ERROR_VARIABLE audit_error)
  if(NOT audit_result EQUAL 0)
    message(FATAL_ERROR "${audit_error}")
  endif()
  string(STRIP "${audit_output}" audit_output)
  message(STATUS "${audit_output}")

  list(APPEND package_names "${package_name}")
  list(APPEND package_records "${package_name}|${package_file}")
endforeach()

foreach(expected_package IN LISTS LUXE_COMPONENTS)
  if(NOT expected_package IN_LIST package_names)
    message(FATAL_ERROR "Missing expected package '${expected_package}'")
  endif()
endforeach()
list(LENGTH package_names package_count)
list(LENGTH LUXE_COMPONENTS expected_count)
if(NOT package_count EQUAL expected_count)
  message(FATAL_ERROR "Expected ${expected_count} unique packages; found ${package_count}")
endif()
list(SORT package_records)

execute_process(
  COMMAND "${CMAKE_CURRENT_LIST_DIR}/OrganizeArtifacts.sh"
          --root "${ARTIFACT_ROOT}" --incoming ${package_files}
  RESULT_VARIABLE organize_result
  OUTPUT_VARIABLE organize_output
  ERROR_VARIABLE organize_error)
if(NOT organize_result EQUAL 0)
  message(FATAL_ERROR "Could not organize the audited DEBs: ${organize_error}")
endif()
string(STRIP "${organize_output}" organize_output)
message(STATUS "${organize_output}")
