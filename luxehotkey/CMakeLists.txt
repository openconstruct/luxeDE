# Copyright (C) 2026 Jerry Howell
# SPDX-License-Identifier: GPL-2.0-or-later

cmake_minimum_required(VERSION 3.25)
project(luxehotkey VERSION 0.1.0 LANGUAGES C)
set(LUXEHOTKEY_REVISION 17)
set(LUXEHOTKEY_VERSION "0.1.0+gtk4-2+luxe${LUXEHOTKEY_REVISION}")
include(../cmake/LuxePackage.cmake)
luxe_use_shared_headers()
include(../cmake/LuxeInternalDesktopLibraries.cmake)
include(CTest)

pkg_check_modules(GLIB REQUIRED IMPORTED_TARGET glib-2.0 gio-2.0 gio-unix-2.0 gmodule-2.0 gthread-2.0)
pkg_check_modules(GTK4 REQUIRED IMPORTED_TARGET gtk4)
luxe_plain_gtk_ui()
find_package(X11 REQUIRED)
set(LIBFM_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/../internal/libfm" CACHE PATH "Internal libfm source tree")
set(MENU_CACHE_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/../internal/menu-cache" CACHE PATH "Internal menu-cache source tree")
if(NOT EXISTS "${LIBFM_SOURCE}/src/extra/fm-xml-file.c")
  message(FATAL_ERROR "Internal libfm source not found: ${LIBFM_SOURCE}")
endif()
luxe_add_internal_desktop_libraries(luxehotkey "${LIBFM_SOURCE}" "${MENU_CACHE_SOURCE}")

add_executable(luxehotkey src/luxehotkey.c)
set_target_properties(luxehotkey PROPERTIES ENABLE_EXPORTS ON)
target_compile_features(luxehotkey PRIVATE c_std_11)
target_compile_definitions(luxehotkey PRIVATE ENABLE_GTK4 ENABLE_NLS
  GETTEXT_PACKAGE="luxehotkey" VERSION="${LUXEHOTKEY_VERSION}"
  PACKAGE_PLUGINS_DIR="${CMAKE_INSTALL_FULL_LIBDIR}/luxehotkey"
  PACKAGE_LOCALE_DIR="${CMAKE_INSTALL_FULL_LOCALEDIR}")
target_include_directories(luxehotkey PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" src)
target_link_libraries(luxehotkey PRIVATE PkgConfig::GLIB luxe-gtk-ui X11::X11 luxehotkey-fm)

add_library(luxehotkey-gtk MODULE plugins/gtk/gtk.c plugins/gtk/edit.c)
set_target_properties(luxehotkey-gtk PROPERTIES PREFIX "" OUTPUT_NAME gtk)
target_compile_features(luxehotkey-gtk PRIVATE c_std_11)
target_include_directories(luxehotkey-gtk PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" src)
target_compile_definitions(luxehotkey-gtk PRIVATE ENABLE_GTK4 GETTEXT_PACKAGE="luxehotkey"
  VERSION="${LUXEHOTKEY_VERSION}")
target_link_libraries(luxehotkey-gtk PRIVATE PkgConfig::GTK4 luxe-gtk-ui PkgConfig::GLIB luxehotkey-fm)

add_library(luxehotkey-ob MODULE plugins/openbox/openbox.c)
set_target_properties(luxehotkey-ob PROPERTIES PREFIX "" OUTPUT_NAME ob)
target_compile_features(luxehotkey-ob PRIVATE c_std_11)
target_include_directories(luxehotkey-ob PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" src)
target_compile_definitions(luxehotkey-ob PRIVATE GETTEXT_PACKAGE="luxehotkey")
target_link_libraries(luxehotkey-ob PRIVATE PkgConfig::GLIB luxehotkey-fm luxehotkey-fm-extra)
luxe_compile_translations(luxehotkey)
luxe_install_internal_desktop_licenses("${LIBFM_SOURCE}" "${MENU_CACHE_SOURCE}")

if(BUILD_TESTING)
  find_program(XEPHYR_BIN Xephyr)
  find_program(DBUS_RUN_SESSION_BIN dbus-run-session)
  if(XEPHYR_BIN AND DBUS_RUN_SESSION_BIN)
    add_executable(luxehotkey-edit-test tests/test-edit.c)
    target_compile_definitions(luxehotkey-edit-test PRIVATE $<TARGET_PROPERTY:luxehotkey-gtk,COMPILE_DEFINITIONS>)
    target_include_directories(luxehotkey-edit-test PRIVATE $<TARGET_PROPERTY:luxehotkey-gtk,INCLUDE_DIRECTORIES>)
    target_link_libraries(luxehotkey-edit-test PRIVATE $<TARGET_PROPERTY:luxehotkey-gtk,LINK_LIBRARIES>)
    add_test(NAME luxehotkey-edit-ui COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/RunNestedX.sh" $<TARGET_FILE:luxehotkey-edit-test>)
    set_tests_properties(luxehotkey-edit-ui PROPERTIES TIMEOUT 120)
  endif()
  add_executable(luxehotkey-openbox-test tests/test-openbox.c)
  target_compile_definitions(luxehotkey-openbox-test PRIVATE $<TARGET_PROPERTY:luxehotkey-ob,COMPILE_DEFINITIONS>)
  target_include_directories(luxehotkey-openbox-test PRIVATE $<TARGET_PROPERTY:luxehotkey-ob,INCLUDE_DIRECTORIES>)
  target_link_libraries(luxehotkey-openbox-test PRIVATE $<TARGET_PROPERTY:luxehotkey-ob,LINK_LIBRARIES> X11::X11)
  add_test(NAME luxehotkey-openbox-behavior COMMAND luxehotkey-openbox-test)
  add_executable(luxehotkey-roundtrip tests/test-roundtrip.c)
  set_target_properties(luxehotkey-roundtrip PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
  target_compile_definitions(luxehotkey-roundtrip PRIVATE $<TARGET_PROPERTY:luxehotkey,COMPILE_DEFINITIONS>)
  target_include_directories(luxehotkey-roundtrip PRIVATE $<TARGET_PROPERTY:luxehotkey,INCLUDE_DIRECTORIES>)
  target_link_libraries(luxehotkey-roundtrip PRIVATE $<TARGET_PROPERTY:luxehotkey,LINK_LIBRARIES>)
  add_test(NAME luxehotkey-roundtrip COMMAND luxehotkey-roundtrip)
  add_test(NAME luxehotkey-headless-help
    COMMAND ${CMAKE_COMMAND} -E env --unset=DISPLAY --unset=WAYLAND_DISPLAY $<TARGET_FILE:luxehotkey> --help)
endif()

install(TARGETS luxehotkey RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(TARGETS luxehotkey-gtk luxehotkey-ob LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/luxehotkey)
install(FILES plugins/gtk/luxehotkey-gtk.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
luxe_install_symlink(luxehotkey ${CMAKE_INSTALL_BINDIR}/lxhotkey)

luxe_configure_package(
  NAME luxehotkey VERSION ${PROJECT_VERSION} DEB_VERSION ${LUXEHOTKEY_VERSION} RPM_RELEASE 2.luxe${LUXEHOTKEY_REVISION}
  DESCRIPTION "GTK4 global shortcut settings for Luxe" SECTION x11 LICENSE GPL-2.0-or-later
  DEB_RECOMMENDS luxe-icons
  DEB_PROVIDES lxhotkey lxhotkey-gtk4 DEB_CONFLICTS lxhotkey lxhotkey-gtk4
  DEB_REPLACES lxhotkey lxhotkey-gtk4 RPM_REQUIRES gtk4 libX11)
