# Copyright (C) 2026 Jerry Howell
# SPDX-License-Identifier: GPL-2.0-or-later

cmake_minimum_required(VERSION 3.25)
project(luxesettings VERSION 0.1.0 LANGUAGES C)
set(LUXE_SETTINGS_REVISION 24)
set(LUXE_SETTINGS_RELEASE "${PROJECT_VERSION}+luxe${LUXE_SETTINGS_REVISION}")

include(../cmake/LuxePackage.cmake)
luxe_plain_gtk_ui()

pkg_check_modules(GTK4 REQUIRED IMPORTED_TARGET gtk4)

add_executable(luxesettings
  src/main.c
  src/settings-actions.c)
target_compile_features(luxesettings PRIVATE c_std_11)
target_compile_definitions(luxesettings PRIVATE
  GETTEXT_PACKAGE="luxesettings"
  PACKAGE_LOCALE_DIR="${CMAKE_INSTALL_FULL_LOCALEDIR}"
  VERSION="${LUXE_SETTINGS_RELEASE}"
  G_LOG_DOMAIN="luxesettings")
target_include_directories(luxesettings PRIVATE src)
target_link_libraries(luxesettings PRIVATE PkgConfig::GTK4 luxe-gtk-ui)

include(CTest)
if(BUILD_TESTING)
  add_executable(test-settings-actions
    tests/test-settings-actions.c
    src/settings-actions.c)
  target_compile_features(test-settings-actions PRIVATE c_std_11)
  target_compile_definitions(test-settings-actions PRIVATE GETTEXT_PACKAGE="luxesettings")
  target_include_directories(test-settings-actions PRIVATE src)
  target_link_libraries(test-settings-actions PRIVATE PkgConfig::GTK4)
  add_test(NAME settings-action-catalog COMMAND test-settings-actions)
  find_program(XEPHYR_BIN Xephyr)
  find_program(DBUS_RUN_SESSION_BIN dbus-run-session)
  if(XEPHYR_BIN AND DBUS_RUN_SESSION_BIN)
    add_executable(test-settings-ui tests/test-settings-ui.c src/settings-actions.c)
    target_compile_features(test-settings-ui PRIVATE c_std_11)
    target_compile_definitions(test-settings-ui PRIVATE
      GETTEXT_PACKAGE="luxesettings"
      PACKAGE_LOCALE_DIR="${CMAKE_CURRENT_BINARY_DIR}/locale"
      VERSION="${LUXE_SETTINGS_RELEASE}"
      G_LOG_DOMAIN="luxesettings")
    target_include_directories(test-settings-ui PRIVATE src)
    target_link_libraries(test-settings-ui PRIVATE PkgConfig::GTK4 luxe-gtk-ui)
    add_test(NAME settings-ui COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/RunNestedX.sh" $<TARGET_FILE:test-settings-ui>)
    set_tests_properties(settings-ui PROPERTIES TIMEOUT 120 ENVIRONMENT "GSETTINGS_BACKEND=memory")
  endif()
endif()

luxe_compile_translations(luxesettings)

install(TARGETS luxesettings RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES data/org.luxe.Settings.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)

# Companion controls are optional: DEB installs them as recommendations and
# RPM exposes them as weak suggestions; the hub remains useful when absent.
luxe_configure_package(
  NAME luxesettings VERSION ${PROJECT_VERSION}
  DEB_VERSION ${LUXE_SETTINGS_RELEASE}
  RPM_RELEASE 1.luxe${LUXE_SETTINGS_REVISION}
  DESCRIPTION "Native settings hub for the Luxe desktop"
  SECTION utils LICENSE GPL-2.0-or-later
  DEB_RECOMMENDS luxe-icons luxeappearance luxeappearance-obconf luxeinput luxepower luxerandr luxesession luxepanel luxehotkey pcmanager frame
  RPM_REQUIRES gtk4
  RPM_SUGGESTS luxe-icons luxeappearance luxeappearance-obconf luxeinput luxepower luxerandr luxesession luxepanel luxehotkey pcmanager frame)
