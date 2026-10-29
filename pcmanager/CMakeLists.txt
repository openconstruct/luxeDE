cmake_minimum_required(VERSION 3.25)
project(pcmanager VERSION 1.4.0 LANGUAGES C)
include(../cmake/LuxePackage.cmake)
luxe_use_shared_headers()
include(../cmake/LuxeInternalDesktopLibraries.cmake)
include(CTest)

set(PCMANAGER_REVISION 44)
set(PCMANAGER_DESKTOP_CAIRO_REVISION 3)
set(PCMANAGER_DEB_VERSION "${PROJECT_VERSION}+gtk4-4+luxe${PCMANAGER_REVISION}+desktop-cairo${PCMANAGER_DESKTOP_CAIRO_REVISION}")
set(PCMANAGER_RPM_RELEASE "4.luxe${PCMANAGER_REVISION}")

set(LIBFM_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/../internal/libfm" CACHE PATH "Internal libfm source tree")
set(MENU_CACHE_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/../internal/menu-cache" CACHE PATH "Internal menu-cache source tree")

pkg_check_modules(GTK4 REQUIRED IMPORTED_TARGET "gtk4 >= 4.12")
luxe_plain_gtk_ui()
pkg_check_modules(GLIB REQUIRED IMPORTED_TARGET "glib-2.0 >= 2.80")
find_package(X11 REQUIRED)
luxe_add_internal_desktop_libraries(pcmanager "${LIBFM_SOURCE}" "${MENU_CACHE_SOURCE}")
# P2: pcmanager never installs menu-cache-gen/menu-cached and no compiled
# source references menu_cache_* — keep the helper targets defined (shared
# helper module) but exclude them from this package's default build.
set_target_properties(pcmanager-menu-cache-gen pcmanager-menu-cached
  PROPERTIES EXCLUDE_FROM_ALL TRUE)

add_executable(pcmanager
  src/pcmanager-gtk4.c src/browser-gtk4.c src/file-ops-gtk4.c
  src/desktop-forward.c src/single-inst.c src/permissions-gtk4.c)
target_compile_features(pcmanager PRIVATE c_std_11)
target_compile_definitions(pcmanager PRIVATE ENABLE_GTK4 ENABLE_NLS
  GETTEXT_PACKAGE="pcmanager" VERSION="${PCMANAGER_DEB_VERSION}"
  PACKAGE_DATA_DIR="${CMAKE_INSTALL_FULL_DATADIR}/pcmanager"
  PACKAGE_UI_DIR="${CMAKE_INSTALL_FULL_DATADIR}/pcmanager/ui"
  PACKAGE_MODULES_DIR="${CMAKE_INSTALL_FULL_LIBDIR}/pcmanager"
  PACKAGE_LOCALE_DIR="${CMAKE_INSTALL_FULL_LOCALEDIR}")
target_include_directories(pcmanager PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" src
  "${CMAKE_CURRENT_BINARY_DIR}/pcmanager-include")
target_link_libraries(pcmanager PRIVATE PkgConfig::GTK4 luxe-gtk-ui PkgConfig::GLIB X11::X11 pcmanager-fm)
luxe_compile_translations(pcmanager)
luxe_install_internal_desktop_licenses("${LIBFM_SOURCE}" "${MENU_CACHE_SOURCE}")

if(BUILD_TESTING)
  add_executable(pcmanager-permissions tests/permissions.c src/permissions-gtk4.c)
  target_compile_features(pcmanager-permissions PRIVATE c_std_11)
  target_compile_definitions(pcmanager-permissions PRIVATE GETTEXT_PACKAGE="pcmanager")
  target_link_libraries(pcmanager-permissions PRIVATE PkgConfig::GTK4)
  add_test(NAME pcmanager-permissions COMMAND pcmanager-permissions)
  add_test(NAME pcmanager-wallpaper-forward COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/tests/wallpaper-forward.sh" $<TARGET_FILE:pcmanager>)
  add_executable(pcmanager-single-inst-socket tests/single-inst-socket.c)
  target_compile_features(pcmanager-single-inst-socket PRIVATE c_std_11)
  target_include_directories(pcmanager-single-inst-socket PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" src)
  target_link_libraries(pcmanager-single-inst-socket PRIVATE PkgConfig::GTK4 PkgConfig::GLIB)
  add_test(NAME pcmanager-single-inst-socket COMMAND pcmanager-single-inst-socket)
  add_executable(pcmanager-single-inst-io tests/single-inst-io.c)
  target_compile_features(pcmanager-single-inst-io PRIVATE c_std_11)
  target_link_libraries(pcmanager-single-inst-io PRIVATE PkgConfig::GLIB)
  add_test(NAME pcmanager-single-inst-io COMMAND pcmanager-single-inst-io)
  set_tests_properties(pcmanager-single-inst-io PROPERTIES TIMEOUT 5)
  add_executable(pcmanager-single-inst-send tests/single-inst-send.c)
  target_compile_features(pcmanager-single-inst-send PRIVATE c_std_11)
  target_link_libraries(pcmanager-single-inst-send PRIVATE PkgConfig::GLIB)
  add_test(NAME pcmanager-single-inst-send COMMAND pcmanager-single-inst-send)
  set_tests_properties(pcmanager-single-inst-send PROPERTIES TIMEOUT 5)
  add_test(NAME pcmanager-single-inst-safety
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/tests/check-single-inst-safety.sh
      ${CMAKE_CURRENT_SOURCE_DIR}/src/single-inst.c)
  add_test(NAME pcmanager-drag-move-completion
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/tests/check-drag-move-completion.sh
      ${CMAKE_CURRENT_SOURCE_DIR}/src/browser-gtk4.c)
  add_executable(pcmanager-file-ops-conflicts tests/file-ops-conflicts.c)
  target_compile_features(pcmanager-file-ops-conflicts PRIVATE c_std_11)
  target_link_libraries(pcmanager-file-ops-conflicts PRIVATE PkgConfig::GTK4)
  add_test(NAME pcmanager-file-ops-conflicts
    COMMAND pcmanager-file-ops-conflicts)
  add_executable(pcmanager-browser-monitor tests/browser-monitor.c src/file-ops-gtk4.c src/permissions-gtk4.c)
  target_compile_features(pcmanager-browser-monitor PRIVATE c_std_11)
  target_compile_definitions(pcmanager-browser-monitor PRIVATE ENABLE_GTK4 ENABLE_NLS
    PCMANAGER_TESTING
    GETTEXT_PACKAGE="pcmanager" VERSION="${PCMANAGER_DEB_VERSION}"
    PACKAGE_DATA_DIR="${CMAKE_INSTALL_FULL_DATADIR}/pcmanager"
    PACKAGE_UI_DIR="${CMAKE_INSTALL_FULL_DATADIR}/pcmanager/ui"
    PACKAGE_MODULES_DIR="${CMAKE_INSTALL_FULL_LIBDIR}/pcmanager"
    PACKAGE_LOCALE_DIR="${CMAKE_INSTALL_FULL_LOCALEDIR}")
  target_include_directories(pcmanager-browser-monitor PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" src
    "${CMAKE_CURRENT_BINARY_DIR}/pcmanager-include")
  target_link_libraries(pcmanager-browser-monitor PRIVATE PkgConfig::GTK4 luxe-gtk-ui PkgConfig::GLIB X11::X11 pcmanager-fm)
  add_test(NAME pcmanager-browser-monitor COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/RunNestedX.sh" $<TARGET_FILE:pcmanager-browser-monitor>)
  set_tests_properties(pcmanager-browser-monitor PROPERTIES TIMEOUT 20 ENVIRONMENT "G_DEBUG=fatal-criticals")
  # Xephyr drag-and-drop integration test (internal/external MOVE and COPY,
  # conflict refusal, tab/places drops). Registered only when the display
  # test requirements are installed; the wrapper owns fixtures and cleanup.
  find_program(XEPHYR_BIN Xephyr)
  find_program(XDOTOOL_BIN xdotool)
  find_program(DBUS_RUN_SESSION_BIN dbus-run-session)
  if(XEPHYR_BIN AND XDOTOOL_BIN AND DBUS_RUN_SESSION_BIN)
    add_executable(pcmanager-drag-drop
      tests/drag-drop-gtk4.c src/file-ops-gtk4.c src/permissions-gtk4.c)
    target_compile_features(pcmanager-drag-drop PRIVATE c_std_11)
    target_compile_definitions(pcmanager-drag-drop PRIVATE ENABLE_GTK4 ENABLE_NLS
      GETTEXT_PACKAGE="pcmanager" VERSION="${PCMANAGER_DEB_VERSION}"
      PACKAGE_DATA_DIR="${CMAKE_INSTALL_FULL_DATADIR}/pcmanager"
      PACKAGE_UI_DIR="${CMAKE_INSTALL_FULL_DATADIR}/pcmanager/ui"
      PACKAGE_MODULES_DIR="${CMAKE_INSTALL_FULL_LIBDIR}/pcmanager"
      PACKAGE_LOCALE_DIR="${CMAKE_INSTALL_FULL_LOCALEDIR}")
    target_link_libraries(pcmanager-drag-drop PRIVATE
      pcmanager-fm PkgConfig::GTK4 luxe-gtk-ui X11::X11)
    add_test(NAME pcmanager-drag-drop-xephyr
      COMMAND ${CMAKE_COMMAND} -E env LUXE_TEST_SCREEN=1500x950
        "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/RunNestedX.sh"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/private-drag-drop.sh"
        $<TARGET_FILE:pcmanager-drag-drop>)
    set_tests_properties(pcmanager-drag-drop-xephyr PROPERTIES
      TIMEOUT 100)
  endif()
endif()

# Generate both launchers from tracked intltool templates, including any
# available translations. The generated .desktop files are not source files.
find_program(MSGFMT_DESKTOP_EXECUTABLE msgfmt REQUIRED)
find_program(PCMANAGER_DESKTOP_VALIDATE desktop-file-validate)
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/data")
file(GLOB pcmanager_desktop_translations CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/po/*.po")
set(pcmanager_desktop_entries)
foreach(entry IN ITEMS pcmanager pcmanager-desktop-pref)
  set(template "${CMAKE_CURRENT_SOURCE_DIR}/data/${entry}.desktop.in")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${template}")
  file(READ "${template}" desktop_text)
  foreach(key IN ITEMS Name GenericName Comment)
    string(REPLACE "\n_${key}=" "\n${key}=" desktop_text "${desktop_text}")
  endforeach()
  set(converted "${CMAKE_CURRENT_BINARY_DIR}/data/${entry}.desktop.template")
  set(output "${CMAKE_CURRENT_BINARY_DIR}/data/${entry}.desktop")
  file(WRITE "${converted}" "${desktop_text}")
  if(PCMANAGER_DESKTOP_VALIDATE)
    set(validate_command COMMAND "${PCMANAGER_DESKTOP_VALIDATE}" "${output}")
  else()
    set(validate_command)
  endif()
  if(pcmanager_desktop_translations)
    set(generate_command "${MSGFMT_DESKTOP_EXECUTABLE}" --desktop --template "${converted}"
      -d "${CMAKE_CURRENT_SOURCE_DIR}/po" -o "${output}")
  else()
    set(generate_command "${CMAKE_COMMAND}" -E copy_if_different "${converted}" "${output}")
  endif()
  add_custom_command(OUTPUT "${output}"
    COMMAND ${generate_command}
    ${validate_command}
    DEPENDS "${template}" "${converted}" ${pcmanager_desktop_translations}
    VERBATIM)
  list(APPEND pcmanager_desktop_entries "${output}")
endforeach()
add_custom_target(pcmanager-desktop-entry ALL DEPENDS ${pcmanager_desktop_entries})
if(BUILD_TESTING)
  add_test(NAME pcmanager-desktop-install
    COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/desktop-install.sh" "${CMAKE_CURRENT_BINARY_DIR}")
endif()

install(TARGETS pcmanager RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES ${pcmanager_desktop_entries}
  DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES data/pcmanager.conf DESTINATION /etc/xdg/pcmanager/default)

# The legacy luxe-default.png wallpaper is no longer shipped. Nothing defaults
# to it: fresh profiles use the generated luxesession wallpaper, and Appearance
# hands off the palette's own PNG in stretch mode. The six generated wallpapers in
# luxesession/data/palettes/ are the only backgrounds this suite installs.

luxe_configure_package(
  NAME pcmanager VERSION ${PROJECT_VERSION} DEB_VERSION ${PCMANAGER_DEB_VERSION}
  RPM_VERSION 1.4.0 RPM_RELEASE ${PCMANAGER_RPM_RELEASE}
  DESCRIPTION "GTK4 file manager for the Luxe desktop" SECTION utils LICENSE GPL-2.0-or-later
  DEB_SHLIBDEPS OFF
  DEB_DEPENDS frame "libc6 (>= 2.34)" "libcairo2 (>= 1.2.4)"
              "libgdk-pixbuf-2.0-0 (>= 2.22.0)" "libglib2.0-0t64 (>= 2.80.0) | libglib2.0-0 (>= 2.80.0)"
              "libgtk-4-1 (>= 4.12.0)" "libx11-6"
  DEB_RECOMMENDS luxe-icons xarchiver udisks2
  DEB_PROVIDES pcmanfm pcmanfm-gtk4 DEB_CONFLICTS pcmanfm pcmanfm-gtk4
  DEB_REPLACES pcmanfm pcmanfm-gtk4 RPM_REQUIRES gtk4 libX11 frame)
