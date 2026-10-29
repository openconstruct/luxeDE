# Copyright (C) 2026 Jerry Howell
# SPDX-License-Identifier: GPL-2.0-or-later
# Regression guard for PAM conversation handling (greeter + lock).
# Follows check-service.cmake pattern: static checks that the handlers keep
# validating input, reporting text/info safely, and clearing passwords.
file(READ "${SRC}/src/app.cpp" app)
file(READ "${SRC}/src/lock.cpp" lock)

# Greeter conversation (app.cpp conv) must reject bad input before touching Panel.
if(NOT app MATCHES "num_msg <= 0 \\|\\| msg == NULL \\|\\| resp == NULL")
  message(FATAL_ERROR "greeter PAM conversation must validate num_msg/msg/resp")
endif()
if(NOT app MATCHES "panel == NULL")
  message(FATAL_ERROR "greeter PAM conversation must handle a null panel")
endif()
if(NOT app MATCHES "PAM_CONV_ERR" OR NOT app MATCHES "PAM_BUF_ERR")
  message(FATAL_ERROR "greeter PAM conversation must report CONV/BUF errors")
endif()
if(NOT app MATCHES "PAM_ERROR_MSG" OR NOT app MATCHES "PAM_TEXT_INFO")
  message(FATAL_ERROR "greeter PAM conversation must handle info/error messages")
endif()
if(NOT app MATCHES "ResetPasswd")
  message(FATAL_ERROR "greeter PAM conversation must clear the password buffer")
endif()

# Locker conversation (lock.cpp ConvCallback) must do the same.
if(NOT lock MATCHES "num_msgs <= 0")
  message(FATAL_ERROR "locker PAM conversation must validate message count")
endif()
if(NOT lock MATCHES "LoginPanel == NULL")
  message(FATAL_ERROR "locker PAM conversation must handle a null panel")
endif()
if(NOT lock MATCHES "msg\\[i\\] == NULL")
  message(FATAL_ERROR "locker PAM conversation must validate each message")
endif()
if(NOT lock MATCHES "PAM_PROMPT_ECHO_OFF" OR NOT lock MATCHES "PAM_PROMPT_ECHO_ON")
  message(FATAL_ERROR "locker PAM conversation must distinguish echo on/off prompts")
endif()
if(NOT lock MATCHES "ResetPasswd")
  message(FATAL_ERROR "locker PAM conversation must clear the password after use")
endif()
