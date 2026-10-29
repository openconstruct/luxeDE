# Copyright (C) 2026 Jerry Howell
# SPDX-License-Identifier: GPL-2.0-or-later

cmake_minimum_required(VERSION 3.25)
project(luxemenu-data VERSION 0.1.4 LANGUAGES NONE)
include(../cmake/LuxePackage.cmake)
include(CTest)
set(LUXEMENU_DATA_REVISION 10)
set(LUXEMENU_DATA_DEB_VERSION "0.1.4+luxe-2+luxe${LUXEMENU_DATA_REVISION}")

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/layout/settings-exclusions.txt"
  LUXE_SETTINGS_EXCLUDED_FILES)
set(LUXE_SETTINGS_EXCLUSIONS_XML "")
foreach(filename IN LISTS LUXE_SETTINGS_EXCLUDED_FILES)
  string(REPLACE "|" ";" entry_fields "${filename}")
  list(LENGTH entry_fields entry_field_count)
  if(NOT entry_field_count EQUAL 2)
    message(FATAL_ERROR "Invalid settings exclusion record: ${filename}")
  endif()
  list(GET entry_fields 0 desktop_filename)
  if(NOT desktop_filename MATCHES "^[A-Za-z0-9._+-]+\\.desktop$")
    message(FATAL_ERROR "Invalid settings desktop filename: ${filename}")
  endif()
  string(APPEND LUXE_SETTINGS_EXCLUSIONS_XML "\t\t\t<Filename>${desktop_filename}</Filename>\n")
endforeach()
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
configure_file(layout/lxde-applications.menu.in
  "${CMAKE_CURRENT_BINARY_DIR}/generated/luxe-applications.menu" @ONLY)

luxe_compile_translations(lxmenu-data)

if(BUILD_TESTING)
  # Do not silently remove the integration test from CTest. Only accept the
  # private Luxe generator; a generic host menu-cache-gen can differ in build.
  find_program(MENU_CACHE_GEN_EXECUTABLE menu-cache-gen
    PATHS /usr/libexec/luxepanel/menu-cache NO_DEFAULT_PATH)
  add_test(NAME luxe-settings-menu-layout
    COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/tests/menu-layout.sh"
      "${MENU_CACHE_GEN_EXECUTABLE}"
      "${CMAKE_CURRENT_BINARY_DIR}/generated/luxe-applications.menu"
      "${CMAKE_CURRENT_SOURCE_DIR}/layout/settings-exclusions.txt"
      "${CMAKE_CURRENT_SOURCE_DIR}/..")
  set_tests_properties(luxe-settings-menu-layout PROPERTIES SKIP_RETURN_CODE 77)
endif()
install(DIRECTORY desktop-directories/ DESTINATION ${CMAKE_INSTALL_DATADIR}/desktop-directories
  FILES_MATCHING PATTERN "*.directory" PATTERN "*.directory.in" EXCLUDE)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/generated/luxe-applications.menu"
  DESTINATION /etc/xdg/menus)
luxe_install_symlink(luxe-applications.menu /etc/xdg/menus/lxde-applications.menu)

# RPM counterpart of the DEB Provides/Replaces on lxmenu-data below
# (RPM has no Replaces field; Obsoletes covers it).
set(CPACK_RPM_PACKAGE_PROVIDES "lxmenu-data")
set(CPACK_RPM_PACKAGE_OBSOLETES "lxmenu-data")

luxe_configure_package(
  NAME luxemenu-data VERSION ${PROJECT_VERSION} DEB_VERSION ${LUXEMENU_DATA_DEB_VERSION}
  RPM_VERSION 0.1.4 RPM_RELEASE 2.luxe${LUXEMENU_DATA_REVISION} NOARCH
  DESCRIPTION "Freedesktop menu layout for the Luxe desktop" SECTION x11 LICENSE GPL-2.0-or-later
  DEB_DEPENDS luxesettings DEB_RECOMMENDS luxe-icons DEB_PROVIDES lxmenu-data DEB_CONFLICTS lxmenu-data
  DEB_REPLACES lxmenu-data RPM_REQUIRES shared-mime-info luxesettings)
