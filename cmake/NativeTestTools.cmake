# Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later
# Compile host-side helpers during the build, before CTest's runtime deadlines.
function(luxe_prepare_native_test_tools)
  if(NOT BUILD_TESTING)
    return()
  endif()
  # Luxe Icons configures isolated host tools in its own build tree, with exact
  # executable paths for generation and CTest; the shared cache is not needed.
  if(PROJECT_NAME STREQUAL "luxe-icons")
    return()
  endif()
  get_property(tests DIRECTORY PROPERTY TESTS)
  if(NOT tests)
    return()
  endif()
  set(native_targets run-nested-x)
  if(PROJECT_NAME MATCHES "^(luxesession|luxepanel|luxenotify|luxepolkit|frame|luxeappearance|luxup|luxeterm)$")
    list(APPEND native_targets behavior-fixtures)
  elseif(PROJECT_NAME STREQUAL "luxe-icons")
    list(APPEND native_targets icon-fixtures-gtk4)
  elseif(PROJECT_NAME STREQUAL "luxedm")
    list(APPEND native_targets diagnostics)
  endif()
  add_custom_target(${PROJECT_NAME}-native-test-tools ALL
    COMMAND "${LUXE_REPOSITORY_ROOT}/tools/build-native.sh" ${native_targets}
    COMMENT "Preparing native test tools for ${PROJECT_NAME}"
    VERBATIM)
endfunction()
