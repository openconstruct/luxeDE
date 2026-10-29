# Copyright (C) 2026 Jerry Howell
# SPDX-License-Identifier: GPL-2.0-or-later

cmake_minimum_required(VERSION 3.25)
project(luxerandr VERSION 0.3.2 LANGUAGES C)
set(LUXE_RANDR_REVISION 22)
set(LUXE_RANDR_RELEASE "${PROJECT_VERSION}+gtk4-2+luxe${LUXE_RANDR_REVISION}")
include(../cmake/LuxePackage.cmake)
luxe_use_shared_headers()
include(CTest)

pkg_check_modules(GTK4 REQUIRED IMPORTED_TARGET gtk4)
pkg_check_modules(GLIB REQUIRED IMPORTED_TARGET glib-2.0)
pkg_check_modules(GIO_UNIX REQUIRED IMPORTED_TARGET gio-unix-2.0)
pkg_check_modules(XRANDR REQUIRED IMPORTED_TARGET xrandr)
luxe_plain_gtk_ui()
find_package(X11 REQUIRED)
add_executable(luxerandr-guard src/display-guard.c)
target_compile_features(luxerandr-guard PRIVATE c_std_11)
target_link_libraries(luxerandr-guard PRIVATE PkgConfig::GLIB)
add_executable(luxerandr src/luxerandr.c src/command.c)
target_compile_features(luxerandr PRIVATE c_std_11)
target_compile_options(luxerandr PRIVATE -Wall -Wextra)
# No autotools config.h remains; the sources only need ENABLE_NLS (NLS
# gate) plus the package macros below, all provided here via CMake.
target_compile_definitions(luxerandr PRIVATE ENABLE_GTK4 ENABLE_NLS
  GETTEXT_PACKAGE="luxerandr" LUXE_VERSION="${LUXE_RANDR_RELEASE}"
  LUXERANDR_GUARD_PATH="${CMAKE_INSTALL_FULL_LIBEXECDIR}/luxerandr-guard"
  PACKAGE_DATA_DIR="${CMAKE_INSTALL_FULL_DATADIR}"
  PACKAGE_LOCALE_DIR="${CMAKE_INSTALL_FULL_LOCALEDIR}")
target_include_directories(luxerandr PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" src)
target_link_libraries(luxerandr PRIVATE PkgConfig::GTK4 PkgConfig::GIO_UNIX
  PkgConfig::XRANDR luxe-gtk-ui X11::X11)
luxe_compile_translations(luxerandr)

if(BUILD_TESTING)
  add_executable(luxerandr-guard-test src/display-guard.c)
  target_compile_definitions(luxerandr-guard-test PRIVATE DISPLAY_GUARD_APPLY_MS=300 DISPLAY_GUARD_CONFIRM_MS=600 DISPLAY_GUARD_RESTORE_MS=300)
  target_link_libraries(luxerandr-guard-test PRIVATE PkgConfig::GLIB)
  add_executable(luxerandr-guard-no-group src/display-guard.c)
  target_compile_definitions(luxerandr-guard-no-group PRIVATE DISPLAY_GUARD_TEST_FAIL_SETPGID
    DISPLAY_GUARD_APPLY_MS=300 DISPLAY_GUARD_CONFIRM_MS=600 DISPLAY_GUARD_RESTORE_MS=300)
  target_link_libraries(luxerandr-guard-no-group PRIVATE PkgConfig::GLIB)
  add_executable(test_luxerandr_guard tests/display-guard.c)
  target_link_libraries(test_luxerandr_guard PRIVATE PkgConfig::GTK4)
  add_test(NAME luxerandr-guard COMMAND test_luxerandr_guard $<TARGET_FILE:luxerandr-guard-test>)
  set_tests_properties(luxerandr-guard PROPERTIES TIMEOUT 10)
  add_executable(test_luxerandr_guard_group_failure tests/display-guard-group-failure.c)
  target_link_libraries(test_luxerandr_guard_group_failure PRIVATE PkgConfig::GIO_UNIX)
  add_test(NAME luxerandr-guard-process-group-failure COMMAND test_luxerandr_guard_group_failure $<TARGET_FILE:luxerandr-guard-no-group>)
  set_tests_properties(luxerandr-guard-process-group-failure PROPERTIES TIMEOUT 10)
  add_executable(test_luxerandr_command tests/test_command.c src/command.c)
  target_compile_features(test_luxerandr_command PRIVATE c_std_11)
  target_compile_definitions(test_luxerandr_command PRIVATE ENABLE_GTK4 ENABLE_NLS
    GETTEXT_PACKAGE="luxerandr" LUXE_VERSION="${LUXE_RANDR_RELEASE}")
  target_include_directories(test_luxerandr_command PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" src)
  target_link_libraries(test_luxerandr_command PRIVATE PkgConfig::GTK4)
  add_test(NAME luxerandr-command COMMAND test_luxerandr_command)
  add_executable(luxerandr-window-placement tests/window-placement.c)
  target_link_libraries(luxerandr-window-placement PRIVATE PkgConfig::GLIB)
  add_test(NAME luxerandr-window-placement COMMAND luxerandr-window-placement)
  add_executable(luxerandr-behavior tests/behavior.c src/command.c)
  target_compile_definitions(luxerandr-behavior PRIVATE ENABLE_GTK4
    GETTEXT_PACKAGE="luxerandr" LUXE_VERSION="${LUXE_RANDR_RELEASE}"
    LUXERANDR_GUARD_PATH="$<TARGET_FILE:luxerandr-guard-test>")
  target_include_directories(luxerandr-behavior PRIVATE src)
  target_link_libraries(luxerandr-behavior PRIVATE PkgConfig::GTK4 PkgConfig::GIO_UNIX PkgConfig::XRANDR luxe-gtk-ui X11::X11)
  add_test(NAME luxerandr-behavior COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/RunNestedX.sh" $<TARGET_FILE:luxerandr-behavior>)
  add_executable(luxerandr-query-timeout tests/query-timeout.c src/command.c)
  target_compile_definitions(luxerandr-query-timeout PRIVATE ENABLE_GTK4
    GETTEXT_PACKAGE="luxerandr" LUXE_VERSION="${LUXE_RANDR_RELEASE}")
  target_include_directories(luxerandr-query-timeout PRIVATE src)
  target_link_libraries(luxerandr-query-timeout PRIVATE PkgConfig::GTK4 PkgConfig::GIO_UNIX PkgConfig::XRANDR luxe-gtk-ui X11::X11)
  add_test(NAME luxerandr-query-timeout COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/RunNestedX.sh" $<TARGET_FILE:luxerandr-query-timeout>)
  set_tests_properties(luxerandr-query-timeout PROPERTIES TIMEOUT 10)
endif()

install(TARGETS luxerandr RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(TARGETS luxerandr-guard RUNTIME DESTINATION ${CMAKE_INSTALL_LIBEXECDIR})
install(FILES data/luxerandr.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
luxe_install_symlink(luxerandr ${CMAKE_INSTALL_BINDIR}/lxrandr)

luxe_configure_package(
  NAME luxerandr VERSION ${PROJECT_VERSION} DEB_VERSION ${LUXE_RANDR_RELEASE} RPM_RELEASE 3.luxe${LUXE_RANDR_REVISION}
  DESCRIPTION "GTK4 display configuration for the Luxe desktop" SECTION x11 LICENSE GPL-2.0-or-later
  DEB_DEPENDS x11-xserver-utils libxrandr2 DEB_RECOMMENDS luxe-icons
  DEB_PROVIDES lxrandr lxrandr-gtk4 DEB_CONFLICTS lxrandr lxrandr-gtk4
  DEB_REPLACES lxrandr lxrandr-gtk4 RPM_REQUIRES gtk4 libX11 libXrandr xrandr)
