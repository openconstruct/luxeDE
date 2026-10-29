cmake_minimum_required(VERSION 3.25)
project(shotty VERSION 0.1.0 LANGUAGES C)
set(SHOTTY_REVISION 21)
set(SHOTTY_RELEASE "${PROJECT_VERSION}-1+luxe${SHOTTY_REVISION}")
set(SHOTTY_RPM_RELEASE "1.luxe${SHOTTY_REVISION}")
include(../cmake/LuxePackage.cmake)

pkg_check_modules(GTK4 REQUIRED IMPORTED_TARGET gtk4)
luxe_plain_gtk_ui()
find_package(X11 REQUIRED)
pkg_check_modules(XFIXES REQUIRED IMPORTED_TARGET xfixes)
find_library(SLOP_LIBRARY NAMES slopy slopy.so.7.6 libslopy.so.7.6 REQUIRED)
luxe_compile_resources("${CMAKE_CURRENT_BINARY_DIR}/shotty-resources.c" simpleshot.gresource.xml)
luxe_install_gschema(org.luxe.Shotty.gschema.xml)

add_library(shotty-resources OBJECT "${CMAKE_CURRENT_BINARY_DIR}/shotty-resources.c")
target_link_libraries(shotty-resources PRIVATE PkgConfig::GTK4)
add_executable(shotty shotty.c screenshot-app.c screenshot-menu.c screenshot-logic.c
  actions-menu.c $<TARGET_OBJECTS:shotty-resources>)
target_compile_features(shotty PRIVATE c_std_11)
target_compile_definitions(shotty PRIVATE VERSION="${SHOTTY_RELEASE}")
target_compile_options(shotty PRIVATE -Wall -Wextra)
target_include_directories(shotty PRIVATE deps)
target_link_libraries(shotty PRIVATE PkgConfig::GTK4 luxe-gtk-ui X11::X11 PkgConfig::XFIXES "${SLOP_LIBRARY}")

include(CTest)
if(BUILD_TESTING)
  find_program(XEPHYR_BIN Xephyr)
  find_program(DBUS_RUN_SESSION_BIN dbus-run-session)
  find_program(GLIB_COMPILE_SCHEMAS glib-compile-schemas REQUIRED)
  file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/schemas")
  configure_file(org.luxe.Shotty.gschema.xml schemas/org.luxe.Shotty.gschema.xml COPYONLY)
  add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/schemas/gschemas.compiled"
    COMMAND "${GLIB_COMPILE_SCHEMAS}" --strict "${CMAKE_CURRENT_BINARY_DIR}/schemas"
    DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/schemas/org.luxe.Shotty.gschema.xml")
  add_executable(shotty-behavior tests/behavior.c
    $<TARGET_OBJECTS:shotty-resources>
    "${CMAKE_CURRENT_BINARY_DIR}/schemas/gschemas.compiled")
  target_include_directories(shotty-behavior PRIVATE deps)
  target_compile_definitions(shotty-behavior PRIVATE VERSION="${SHOTTY_RELEASE}")
  target_compile_options(shotty-behavior PRIVATE -Wall -Wextra)
  target_link_libraries(shotty-behavior PRIVATE PkgConfig::GTK4 luxe-gtk-ui X11::X11 PkgConfig::XFIXES "${SLOP_LIBRARY}")
  if(XEPHYR_BIN AND DBUS_RUN_SESSION_BIN)
    add_test(NAME shotty-behavior
      COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/RunNestedX.sh"
        "${CMAKE_COMMAND}" -E env "GSETTINGS_BACKEND=memory" "GSETTINGS_SCHEMA_DIR=${CMAKE_CURRENT_BINARY_DIR}/schemas"
        $<TARGET_FILE:shotty-behavior>)
  endif()
endif()

install(TARGETS shotty RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES data/shotty.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)

luxe_configure_package(
  NAME shotty VERSION ${PROJECT_VERSION} DEB_VERSION ${SHOTTY_RELEASE} RPM_RELEASE ${SHOTTY_RPM_RELEASE}
  DESCRIPTION "GTK4 screenshot tool for the Luxe desktop" SECTION graphics LICENSE GPL-3.0-or-later
  DEB_DEPENDS "libslopy7.6" "dconf-gsettings-backend | gsettings-backend"
  DEB_RECOMMENDS luxe-icons
  RPM_REQUIRES gtk4 slop)
