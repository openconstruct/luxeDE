# Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later
cmake_minimum_required(VERSION 3.25)
if(NOT DEFINED ICON_ROOT)
  message(FATAL_ERROR "Pass -DICON_ROOT=<generated share/icons>")
endif()
if(NOT DEFINED ICON_DEDUPLICATOR OR NOT EXISTS "${ICON_DEDUPLICATOR}")
  message(FATAL_ERROR "Pass -DICON_DEDUPLICATOR=<configured host executable>")
endif()
execute_process(COMMAND "${ICON_DEDUPLICATOR}" "${ICON_ROOT}" COMMAND_ERROR_IS_FATAL ANY)
