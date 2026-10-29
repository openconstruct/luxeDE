# Copyright (C) 2026 Jerry Howell
# SPDX-License-Identifier: GPL-2.0-or-later

cmake_minimum_required(VERSION 3.25)
project(luxeappearance-obconf VERSION 0.2.3 LANGUAGES C CXX)
include(../cmake/LuxePackage.cmake)
include(CTest)

# Generated build configuration header (replaces the former autotools
# config.h): main.c/archive.c include <config.h> under HAVE_CONFIG_H.
# (ENABLE_GTK4/ENABLE_NLS stay command-line-only definitions, as in the
# sibling luxepanel package, so the header does not redefine them.)
set(GETTEXT_PACKAGE "luxeappearance-obconf")
set(PACKAGE "luxeappearance-obconf")
# Compiled-in About version must track the Luxe DEB_VERSION (bible rule);
# both the config.h header and the VERSION compile definition use it.
set(LUXE_RELEASE "luxe16")
set(LUXE_FULL_VERSION "0.2.3+gtk4-4+${LUXE_RELEASE}")
set(LUXE_ABOUT_VERSION "${LUXE_FULL_VERSION}")
configure_file(config.h.cmake config.h @ONLY)

set(OPENBOX_ROOT "" CACHE PATH "Optional Openbox development-files prefix")
pkg_check_modules(GTK4 REQUIRED IMPORTED_TARGET gtk4)
luxe_plain_gtk_ui()
pkg_check_modules(XML REQUIRED IMPORTED_TARGET libxml-2.0)
pkg_check_modules(PANGOXFT REQUIRED IMPORTED_TARGET pangoxft)
# Prefer the pkg-config imported target. The staging librsvg-2.0.pc is not
# always self-contained (a Requires.private entry may lack its own .pc, which
# makes pkg-config --cflags fail), so fall back to a manually defined
# PkgConfig::RSVG backed by the shared library instead of raw link paths.
pkg_check_modules(RSVG IMPORTED_TARGET librsvg-2.0)
if(NOT RSVG_FOUND)
  find_path(RSVG_INCLUDE_DIR librsvg/rsvg.h PATH_SUFFIXES librsvg-2.0 REQUIRED)
  find_library(RSVG_LIBRARY NAMES rsvg-2 REQUIRED)
  if(RSVG_LIBRARY MATCHES "\\.a$")
    message(FATAL_ERROR "Refusing static RSVG library ${RSVG_LIBRARY}; shared librsvg required")
  endif()
  add_library(PkgConfig::RSVG UNKNOWN IMPORTED)
  set_target_properties(PkgConfig::RSVG PROPERTIES
    IMPORTED_LOCATION "${RSVG_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${RSVG_INCLUDE_DIR}")
endif()
find_package(X11 REQUIRED)
find_package(ZLIB REQUIRED)
find_package(BZip2 REQUIRED)
find_path(OPENBOX_INCLUDE_DIR obrender/render.h
  HINTS "${OPENBOX_ROOT}/usr/include" PATH_SUFFIXES openbox/3.6 REQUIRED)
if(OPENBOX_ROOT)
  find_file(OBRENDER_LIBRARY NAMES libobrender.so.32.0.0 libobrender.so.32
    PATHS "${OPENBOX_ROOT}/usr/lib/${CMAKE_LIBRARY_ARCHITECTURE}"
          "/usr/lib/${CMAKE_LIBRARY_ARCHITECTURE}" REQUIRED NO_DEFAULT_PATH)
  find_file(OBT_LIBRARY NAMES libobt.so.2.0.2 libobt.so.2
    PATHS "${OPENBOX_ROOT}/usr/lib/${CMAKE_LIBRARY_ARCHITECTURE}"
          "/usr/lib/${CMAKE_LIBRARY_ARCHITECTURE}" REQUIRED NO_DEFAULT_PATH)
else()
  find_library(OBRENDER_LIBRARY NAMES obrender REQUIRED)
  find_library(OBT_LIBRARY NAMES obt REQUIRED)
endif()
# Openbox is X11/shared-only here: never accept a static archive even if
# find_library happens to prefer one.
foreach(_ob_lib IN ITEMS OBRENDER_LIBRARY OBT_LIBRARY)
  if(${_ob_lib} MATCHES "\\.a$")
    message(FATAL_ERROR "Refusing static Openbox library ${${_ob_lib}}; shared library required")
  endif()
endforeach()

add_executable(luxeappearance-obconf src/archive.c src/main.c src/appearance.c
  src/preview.c src/preview_update.c src/theme.c src/tree.c src/desktops.c)
target_compile_features(luxeappearance-obconf PRIVATE c_std_11)
target_compile_definitions(luxeappearance-obconf PRIVATE HAVE_CONFIG_H ENABLE_GTK4 ENABLE_NLS
  GETTEXT_PACKAGE="luxeappearance-obconf" VERSION="${LUXE_ABOUT_VERSION}"
  LOCALEDIR="${CMAKE_INSTALL_FULL_LOCALEDIR}"
  PIXMAPDIR="${CMAKE_INSTALL_FULL_DATADIR}/pixmaps"
  GLADEDIR="${CMAKE_INSTALL_FULL_DATADIR}/luxeappearance-obconf"
  INSTALL_SCRIPT_DIR="${CMAKE_INSTALL_FULL_DATADIR}/luxeappearance-obconf"
  THEMEDIR="${CMAKE_INSTALL_FULL_DATADIR}/openbox/themes" G_LOG_DOMAIN="Obconf")
target_include_directories(luxeappearance-obconf PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}" src "${OPENBOX_INCLUDE_DIR}")
target_link_libraries(luxeappearance-obconf PRIVATE PkgConfig::GTK4 luxe-gtk-ui
  PkgConfig::XML PkgConfig::PANGOXFT PkgConfig::RSVG X11::X11 "${OBRENDER_LIBRARY}" "${OBT_LIBRARY}")
add_executable(obconf-theme-extract ../luxeappearance/src/extract-icon-archive.cpp)
target_compile_features(obconf-theme-extract PRIVATE cxx_std_17)
target_link_libraries(obconf-theme-extract PRIVATE ZLIB::ZLIB BZip2::BZip2)
add_executable(obconf-theme-publish ../luxeappearance/src/publish-icon-theme.c)
target_compile_features(obconf-theme-publish PRIVATE c_std_11)
# gettext is required; compile catalogs under the same domain bound by main.c.
find_program(MSGFMT_EXECUTABLE msgfmt REQUIRED)
luxe_compile_translations(luxeappearance-obconf)

find_program(BASH_EXECUTABLE bash)
find_program(XEPHYR_BIN Xephyr)
find_program(DBUS_RUN_SESSION_BIN dbus-run-session)
if(BUILD_TESTING AND BASH_EXECUTABLE)
  add_executable(luxeappearance-obconf-tree-memory tests/tree-memory.c src/tree.c)
  target_compile_features(luxeappearance-obconf-tree-memory PRIVATE c_std_11)
  target_compile_definitions(luxeappearance-obconf-tree-memory PRIVATE GETTEXT_PACKAGE="luxeappearance-obconf")
  target_compile_options(luxeappearance-obconf-tree-memory PRIVATE -ffunction-sections -fdata-sections)
  target_link_options(luxeappearance-obconf-tree-memory PRIVATE -Wl,--gc-sections)
  target_include_directories(luxeappearance-obconf-tree-memory PRIVATE src "${OPENBOX_INCLUDE_DIR}")
  target_link_libraries(luxeappearance-obconf-tree-memory PRIVATE PkgConfig::GTK4 PkgConfig::XML X11::X11 "${OBT_LIBRARY}")
  add_test(NAME obconf-tree-memory COMMAND luxeappearance-obconf-tree-memory)
  add_executable(luxeappearance-obconf-rcxml-lock-test tests/rcxml-lock.c)
  target_compile_features(luxeappearance-obconf-rcxml-lock-test PRIVATE c_std_11)
  target_link_libraries(luxeappearance-obconf-rcxml-lock-test PRIVATE PkgConfig::GTK4)
  add_test(NAME obconf-rcxml-lock COMMAND luxeappearance-obconf-rcxml-lock-test)
  add_test(NAME theme-installer
    COMMAND ${BASH_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/run-installer-test.sh
      ${CMAKE_CURRENT_SOURCE_DIR}/scripts/install-theme.sh
      $<TARGET_FILE:obconf-theme-extract> $<TARGET_FILE:obconf-theme-publish>)
  set_tests_properties(theme-installer PROPERTIES TIMEOUT 60)
  if(XEPHYR_BIN AND DBUS_RUN_SESSION_BIN)
    add_executable(luxeappearance-obconf-tree-test tests/tree-test.c
      src/archive.c src/appearance.c src/desktops.c src/preview.c
      src/preview_update.c src/tree.c)
    target_compile_features(luxeappearance-obconf-tree-test PRIVATE c_std_11)
    target_compile_definitions(luxeappearance-obconf-tree-test PRIVATE HAVE_CONFIG_H ENABLE_GTK4 ENABLE_NLS
      GETTEXT_PACKAGE="luxeappearance-obconf" VERSION="${LUXE_ABOUT_VERSION}"
      LOCALEDIR="${CMAKE_INSTALL_FULL_LOCALEDIR}"
      PIXMAPDIR="${CMAKE_INSTALL_FULL_DATADIR}/pixmaps"
      GLADEDIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
      INSTALL_SCRIPT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/scripts"
      THEMEDIR="${CMAKE_INSTALL_FULL_DATADIR}/openbox/themes" G_LOG_DOMAIN="Obconf")
    target_include_directories(luxeappearance-obconf-tree-test PRIVATE
      "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}" src "${OPENBOX_INCLUDE_DIR}")
    target_link_libraries(luxeappearance-obconf-tree-test PRIVATE PkgConfig::GTK4 luxe-gtk-ui
      PkgConfig::XML PkgConfig::PANGOXFT PkgConfig::RSVG X11::X11
      "${OBRENDER_LIBRARY}" "${OBT_LIBRARY}")
    add_test(NAME tree-apply
      COMMAND ${BASH_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/run-tree-test.sh
        $<TARGET_FILE:luxeappearance-obconf-tree-test>)
    set_tests_properties(tree-apply PROPERTIES
      TIMEOUT 120
      ENVIRONMENT "XEPHYR_BIN=${XEPHYR_BIN};DBUS_RUN_SESSION_BIN=${DBUS_RUN_SESSION_BIN};OBCONF_THEME_EXTRACTOR=$<TARGET_FILE:obconf-theme-extract>;OBCONF_THEME_PUBLISHER=$<TARGET_FILE:obconf-theme-publish>")
  endif()
endif()

install(TARGETS luxeappearance-obconf RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(TARGETS obconf-theme-extract obconf-theme-publish
  RUNTIME DESTINATION ${CMAKE_INSTALL_DATADIR}/luxeappearance-obconf)
install(FILES data/obconf.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
  RENAME luxeappearance-obconf.desktop)
install(FILES data/obconf.ui DESTINATION ${CMAKE_INSTALL_DATADIR}/luxeappearance-obconf)
install(PROGRAMS scripts/install-theme.sh DESTINATION ${CMAKE_INSTALL_DATADIR}/luxeappearance-obconf)
luxe_install_symlink(luxeappearance-obconf ${CMAKE_INSTALL_BINDIR}/obconf)

luxe_configure_package(
  NAME luxeappearance-obconf VERSION ${PROJECT_VERSION}
  DEB_VERSION 2:${LUXE_FULL_VERSION} RPM_VERSION 0.2.3 RPM_RELEASE 4.luxe16
  DESCRIPTION "GTK4 Openbox settings for the Luxe desktop" SECTION x11 LICENSE GPL-2.0-or-later
  DEB_DEPENDS openbox DEB_RECOMMENDS luxe-icons
  DEB_PROVIDES lxappearance-obconf lxappearance-obconf-gtk4 obconf
  DEB_CONFLICTS lxappearance-obconf lxappearance-obconf-gtk4 obconf
  DEB_REPLACES lxappearance-obconf lxappearance-obconf-gtk4 obconf
  RPM_REQUIRES gtk4  openbox libxml2 librsvg2)
