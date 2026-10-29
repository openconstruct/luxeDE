# Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later
cmake_minimum_required(VERSION 3.25)

# The Ninja stamp lives outside the generated tree. If that tree is removed,
# regenerate it, and let BuildIcons enforce its ownership and path checks.
if(NOT EXISTS "${OUTPUT_DIR}/.built")
  include("${SOURCE_DIR}/cmake/BuildIcons.cmake")
else()
  set(marker "${OUTPUT_DIR}/.luxe-icons-generated")
  if(IS_SYMLINK "${OUTPUT_DIR}" OR IS_SYMLINK "${marker}" OR
     NOT EXISTS "${marker}" OR IS_DIRECTORY "${marker}")
    message(FATAL_ERROR "Refusing unowned icon output: ${OUTPUT_DIR}")
  endif()
  file(READ "${marker}" marker_text LIMIT 64)
  if(NOT marker_text STREQUAL "luxe-icons-generated-v1\n")
    message(FATAL_ERROR "Invalid icon output ownership marker: ${marker}")
  endif()
endif()
