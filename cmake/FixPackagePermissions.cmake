# Copyright (C) 2026 Jerry Howell
# SPDX-License-Identifier: GPL-2.0-or-later

# CPack creates parts of its staging tree before running install rules. Under a
# restrictive or collaborative developer umask those implicit entries can
# otherwise enter a DEB or RPM with modes such as 0750, 0775, or 0660.
if(NOT DEFINED CPACK_TEMPORARY_DIRECTORY OR NOT IS_DIRECTORY "${CPACK_TEMPORARY_DIRECTORY}")
  message(FATAL_ERROR "CPack did not provide a valid staging directory")
endif()

find_program(FIND_EXECUTABLE find REQUIRED)
find_program(CHMOD_EXECUTABLE chmod REQUIRED)

function(normalize_entries description)
  execute_process(
    COMMAND ${ARGN}
    RESULT_VARIABLE result
    ERROR_VARIABLE error)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "Could not normalize ${description}: ${error}")
  endif()
endfunction()

# Symlinks are intentionally untouched. Luxe packages never ship setuid or
# setgid executables: privileged operations belong to narrowly reviewed system
# services, not a desktop package payload. Fail before packaging if one is
# introduced.
execute_process(
  COMMAND "${FIND_EXECUTABLE}" "${CPACK_TEMPORARY_DIRECTORY}" -type f -perm /6000 -print -quit
  RESULT_VARIABLE setid_result
  OUTPUT_VARIABLE setid_files
  ERROR_VARIABLE setid_error)
if(NOT setid_result EQUAL 0)
  message(FATAL_ERROR "Could not inspect setid files: ${setid_error}")
endif()
if(NOT setid_files STREQUAL "")
  message(FATAL_ERROR "Setuid/setgid payload is forbidden: ${setid_files}")
endif()

# Packages do not contain private mutable state; that is created at runtime.
normalize_entries("directory permissions"
  "${FIND_EXECUTABLE}" "${CPACK_TEMPORARY_DIRECTORY}" -type d
  -exec "${CHMOD_EXECUTABLE}" 0755 "{}" +)
normalize_entries("non-executable file permissions"
  "${FIND_EXECUTABLE}" "${CPACK_TEMPORARY_DIRECTORY}" -type f
  ! -perm /0111 ! -perm /6000 -exec "${CHMOD_EXECUTABLE}" 0644 "{}" +)
normalize_entries("executable file permissions"
  "${FIND_EXECUTABLE}" "${CPACK_TEMPORARY_DIRECTORY}" -type f
  -perm /0111 ! -perm /6000 -exec "${CHMOD_EXECUTABLE}" 0755 "{}" +)
