# Copyright (C) 2026 Jerry Howell
# SPDX-License-Identifier: GPL-2.0-or-later
# Regression guard for preview-window argument bounds.  The parser must reject
# malformed, overflowing, and non-positive dimensions before opening X.
if(NOT DEFINED LUXEDM)
  message(FATAL_ERROR "LUXEDM is required")
endif()

foreach(size IN ITEMS "1x1junk" "0x1" "1x" "1x999999999999999999999" "1x32768")
  execute_process(
    COMMAND "${LUXEDM}" -p /tmp/luxedm-preview-theme -w "${size}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error)
  if(result EQUAL 0)
    message(FATAL_ERROR "accepted malformed preview size: ${size}")
  endif()
endforeach()
