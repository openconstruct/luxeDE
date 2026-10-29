# Copyright (C) 2026 Jerry Howell
# SPDX-License-Identifier: GPL-2.0-or-later
# Regression guard for issue (1)+(3): root code must not use a shell and the
# F11 screenshot path must stay removed. Follows check-service.cmake pattern.
file(READ "${SRC}/src/app.cpp" app)
file(READ "${SRC}/src/panel.cpp" panel)
file(READ "${SRC}/data/luxedm.conf" packaged_config)
if(app MATCHES "system[ \t]*\\(")
  message(FATAL_ERROR "src/app.cpp still uses system(); use Util::runCommand/spawnAndWait")
endif()
if(panel MATCHES "system[ \t]*\\(")
  message(FATAL_ERROR "src/panel.cpp still uses system()")
endif()
if(app MATCHES "popen[ \t]*\\(" OR panel MATCHES "popen[ \t]*\\(")
  message(FATAL_ERROR "popen() must not be used in root greeter code")
endif()
if(panel MATCHES "XK_F11")
  message(FATAL_ERROR "F11 screenshot binding must stay removed")
endif()
if(app MATCHES "getOption\\(\"screenshot_cmd\"\\)" OR panel MATCHES "getOption\\(\"screenshot_cmd\"\\)")
  message(FATAL_ERROR "screenshot_cmd must not be executed from root code")
endif()
if(NOT app MATCHES "Util::runCommand" OR NOT app MATCHES "Util::spawnAndWait")
  message(FATAL_ERROR "root helpers must use the argv splitter + direct spawn")
endif()
if(NOT app MATCHES "splitCommand" OR NOT app MATCHES "expandEnvVars")
  message(FATAL_ERROR "root command path must split argv and expand env without a shell")
endif()
if(packaged_config MATCHES "(^|\n)[ \t]*console_cmd[ \t]+[^#\n]")
  message(FATAL_ERROR "packaged configuration must not enable a root console command by default")
endif()

# P1 #6: the console/exit magic usernames must not bypass authentication.
# The old code returned true for Console/Exit on the PAM auth-failure path
# ("This is simply fake!") and aborted their PAM password prompt so the
# bypass would trigger; App::Console then ran console_cmd as root.
if(app MATCHES "simply fake")
  message(FATAL_ERROR "src/app.cpp still contains the Console/Exit fake-auth bypass")
endif()
# Line scan: a `return true` (or PAM_CONV_ERR) governed by a Console/Exit
# case label is an unauthenticated magic-username path. Legitimate
# `return true` statements (successful auth, empty-password accounts) are
# never directly governed by such labels, while the removed bypass was.
string(REPLACE ";" "<semicolon>" app_scan "${app}")
string(REPLACE "\n" ";" app_lines "${app_scan}")
set(console_exit_armed FALSE)
foreach(raw_line IN LISTS app_lines)
  string(REPLACE "<semicolon>" ";" line "${raw_line}")
  string(STRIP "${line}" stripped)
  if(stripped MATCHES "^case Panel::(Console|Exit):$")
    set(console_exit_armed TRUE)
  elseif(stripped STREQUAL "" OR stripped MATCHES "^case Panel::[A-Za-z]+:$")
    if(stripped MATCHES "^case Panel::[A-Za-z]+:$" AND NOT stripped MATCHES "^case Panel::(Console|Exit):$")
      set(console_exit_armed FALSE)
    endif()
  elseif(stripped MATCHES "^return true")
    if(console_exit_armed)
      message(FATAL_ERROR "src/app.cpp lets Console/Exit through without authentication")
    endif()
    set(console_exit_armed FALSE)
  elseif(console_exit_armed AND stripped MATCHES "PAM_CONV_ERR")
    message(FATAL_ERROR "src/app.cpp aborts the Console/Exit password prompt instead of authenticating it")
  elseif(NOT stripped MATCHES "^(/|\\*|#)")
    set(console_exit_armed FALSE)
  endif()
endforeach()

# P1 #6: console_cmd must default to empty so an authenticated Console
# action spawns nothing unless the admin configures a command.
file(READ "${SRC}/src/cfg.cpp" cfg)
if(NOT cfg MATCHES "option\\(\"console_cmd\",\"\"\\)")
  message(FATAL_ERROR "console_cmd must default to empty in src/cfg.cpp")
endif()
# The Console action must be a silent no-op on an empty command and must
# run any configured command through the no-shell runner only.
if(NOT app MATCHES "command\\.empty\\(\\)")
  message(FATAL_ERROR "App::Console must no-op when console_cmd is empty")
endif()
if(app MATCHES "void App::Console\\(\\)[^}]*system[ \t]*\\(" OR app MATCHES "void App::Console\\(\\)[^}]*popen[ \t]*\\(")
  message(FATAL_ERROR "App::Console must not use a shell")
endif()
if(NOT app MATCHES "void App::Console\\(\\)[^}]*Util::runCommand")
  message(FATAL_ERROR "App::Console must run the configured command via Util::runCommand")
endif()
