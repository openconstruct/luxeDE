# Copyright (C) 2026 Jerry Howell
# SPDX-License-Identifier: GPL-2.0-or-later
# Regression guard for keyboard/pointer grabs: the greeter must refuse to show
# a false sense of security without the keyboard grab, and the locker must
# abort unless both keyboard and pointer grabs succeed.
file(READ "${SRC}/src/panel.cpp" panel)
file(READ "${SRC}/src/lock.cpp" lock)

if(NOT panel MATCHES "XGrabKeyboard")
  message(FATAL_ERROR "greeter panel must grab the keyboard")
endif()
if(NOT panel MATCHES "could not grab the keyboard")
  message(FATAL_ERROR "greeter panel must log a grab failure")
endif()
if(NOT panel MATCHES "exit\\(ERR_EXIT\\)")
  message(FATAL_ERROR "greeter panel must exit when the keyboard grab fails")
endif()
if(NOT panel MATCHES "XUngrabKeyboard")
  message(FATAL_ERROR "greeter panel must release the keyboard grab on close")
endif()

if(NOT lock MATCHES "XGrabKeyboard")
  message(FATAL_ERROR "locker must grab the keyboard")
endif()
if(NOT lock MATCHES "cannot secure the keyboard grab")
  message(FATAL_ERROR "locker must abort when the keyboard grab fails")
endif()
if(NOT lock MATCHES "XGrabPointer")
  message(FATAL_ERROR "locker must grab the pointer")
endif()
if(NOT lock MATCHES "cannot secure the pointer grab")
  message(FATAL_ERROR "locker must abort when the pointer grab fails")
endif()
