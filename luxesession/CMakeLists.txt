cmake_minimum_required(VERSION 3.25)
project(luxesession VERSION 0.5.5 LANGUAGES C)
set(LUXESESSION_REVISION 50)
set(LUXESESSION_RELEASE "${PROJECT_VERSION}+gtk4-4+luxe${LUXESESSION_REVISION}")
include(../cmake/LuxePackage.cmake)
include(CTest)

pkg_check_modules(GLIB REQUIRED IMPORTED_TARGET glib-2.0 gio-2.0 gio-unix-2.0 gobject-2.0)
pkg_check_modules(GTK4 REQUIRED IMPORTED_TARGET gtk4)
luxe_plain_gtk_ui()
pkg_check_modules(SM REQUIRED IMPORTED_TARGET sm ice)
find_package(X11 REQUIRED)

set(session_definitions HAVE_STRING_H ENABLE_NLS
  GETTEXT_PACKAGE="luxesession" VERSION="${LUXESESSION_RELEASE}"
  PACKAGE_BIN_DIR="${CMAKE_INSTALL_FULL_BINDIR}"
  PACKAGE_DATA_DIR="${CMAKE_INSTALL_FULL_DATADIR}"
  PACKAGE_LOCALE_DIR="${CMAKE_INSTALL_FULL_LOCALEDIR}"
  PACKAGE_UI_DIR="${CMAKE_INSTALL_FULL_DATADIR}/luxesession/ui")
set(session_includes "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/luxesettings-daemon"
  "${CMAKE_CURRENT_SOURCE_DIR}/xdg-autostart")
set(settings_daemon_sources
  luxesettings-daemon/settings-daemon.c luxesettings-daemon/xevent.c
  luxesettings-daemon/xsettings-common.c luxesettings-daemon/xsettings-manager.c
  luxesettings-daemon/xutils.c)

function(add_luxesession_program target)
  add_executable(${target} ${ARGN})
  target_compile_features(${target} PRIVATE c_std_11)
  target_compile_definitions(${target} PRIVATE ${session_definitions})
  target_include_directories(${target} PRIVATE ${session_includes})
  target_link_libraries(${target} PRIVATE PkgConfig::GLIB X11::X11)
endfunction()

add_luxesession_program(luxesettings-daemon
  luxesettings-daemon/main.c ${settings_daemon_sources}
  luxesession/profile.c safe-file.c)
add_luxesession_program(luxesession-xdg-autostart
  xdg-autostart/main.c xdg-autostart/xdg-autostart.c luxesession/profile.c)
file(GLOB session_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/luxesession/*.c")
list(FILTER session_sources EXCLUDE REGEX "/autostart\\.c$")
add_luxesession_program(luxesession-bin ${session_sources} ${settings_daemon_sources} safe-file.c mime-defaults.c)
set_target_properties(luxesession-bin PROPERTIES OUTPUT_NAME luxesession)
target_link_libraries(luxesession-bin PRIVATE PkgConfig::GTK4)

add_luxesession_program(luxesession-db
  luxesession-db/main.c luxesession-db/desktop-files-backend.c safe-file.c)
target_link_libraries(luxesession-db PRIVATE PkgConfig::GTK4)
add_luxesession_program(luxesession-edit
  luxesession-edit/luxesession-edit.c luxesession-edit/luxesession-edit-common.c
  luxesession/profile.c safe-file.c)
target_link_libraries(luxesession-edit PRIVATE PkgConfig::GTK4 luxe-gtk-ui)
add_luxesession_program(luxesession-default-apps
  luxesession-default-apps/main.c
  luxesession-default-apps/combobox.c luxesession-default-apps/dbus-backend.c
  luxesession-default-apps/utils.c safe-file.c mime-defaults.c)
target_link_libraries(luxesession-default-apps PRIVATE PkgConfig::GTK4 m)
add_luxesession_program(luxesession-logout
  luxesession-logout/luxesession-logout-dbus-interface.c
  luxesession-logout/luxesession-logout.c luxesession/profile.c ../shared/luxe-lock-request.c)
target_link_libraries(luxesession-logout PRIVATE PkgConfig::GTK4 PkgConfig::SM)
add_luxesession_program(luxeclipboard luxeclipboard/main.c luxeclipboard/clipboard.c)

if(BUILD_TESTING)
  add_executable(luxesession-supervision-policy-tests tests/supervision-policy.c)
  target_compile_features(luxesession-supervision-policy-tests PRIVATE c_std_11)
  target_include_directories(luxesession-supervision-policy-tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/luxesession")
  target_link_libraries(luxesession-supervision-policy-tests PRIVATE PkgConfig::GLIB)
  add_test(NAME luxesession-supervision-policy COMMAND luxesession-supervision-policy-tests)
  set_tests_properties(luxesession-supervision-policy PROPERTIES TIMEOUT 10)
  add_test(NAME luxesession-default-terminal
    COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/test-default-terminal.sh"
      "${CMAKE_CURRENT_SOURCE_DIR}/luxesession-default/luxesession-default-terminal")
  add_executable(luxesession-lock-request-tests ../shared/tests/lock-request.c ../shared/luxe-lock-request.c)
  target_compile_features(luxesession-lock-request-tests PRIVATE c_std_11)
  target_link_libraries(luxesession-lock-request-tests PRIVATE PkgConfig::GLIB)
  add_test(NAME luxesession-lock-request COMMAND luxesession-lock-request-tests)
  set_tests_properties(luxesession-lock-request PROPERTIES TIMEOUT 10)
  add_test(NAME luxesession-lock-wrapper COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/test-lock-wrapper.sh"
    $<TARGET_FILE:luxesession-lock-request-tests> "${CMAKE_CURRENT_SOURCE_DIR}/luxelock/luxelock")
  set_tests_properties(luxesession-lock-wrapper PROPERTIES TIMEOUT 10)
  add_luxesession_program(luxesession-lock-gate-tests tests/test-lock-gate.c
    luxesession-logout/luxesession-logout-dbus-interface.c luxesession/profile.c ../shared/luxe-lock-request.c)
  target_link_libraries(luxesession-lock-gate-tests PRIVATE PkgConfig::GTK4 PkgConfig::SM)
  add_test(NAME luxesession-lock-gate COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/RunNestedX.sh" $<TARGET_FILE:luxesession-lock-gate-tests>)
  set_tests_properties(luxesession-lock-gate PROPERTIES TIMEOUT 20)
  find_program(XCOMPMGR_EXECUTABLE xcompmgr)
  if(XCOMPMGR_EXECUTABLE)
    add_test(NAME luxesession-compositor
      COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/RunNestedX.sh"
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test-compositor.sh
        $<TARGET_FILE:luxesession-xdg-autostart>
        ${CMAKE_CURRENT_SOURCE_DIR}/data/luxe-migrate-user-config
        ${CMAKE_CURRENT_SOURCE_DIR}/data/luxe-xcompmgr.desktop ${XCOMPMGR_EXECUTABLE})
    set_tests_properties(luxesession-compositor PROPERTIES TIMEOUT 30)
  else()
    message(STATUS "xcompmgr unavailable: real compositor startup test not registered")
  endif()
  add_executable(luxeclipboard-behavior tests/clipboard-behavior.c)
  add_executable(luxeclipboard-budget tests/clipboard-budget.c)
  target_link_libraries(luxeclipboard-budget PRIVATE PkgConfig::GLIB X11::X11)
  add_test(NAME luxesession-clipboard-budget COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/RunNestedX.sh" $<TARGET_FILE:luxeclipboard-budget>)
  target_link_libraries(luxeclipboard-behavior PRIVATE PkgConfig::GTK4 X11::X11)
  add_test(NAME luxesession-clipboard
    COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/RunNestedX.sh"
      $<TARGET_FILE:luxeclipboard-behavior> $<TARGET_FILE:luxeclipboard>)
  set_tests_properties(luxesession-clipboard PROPERTIES TIMEOUT 60)
  add_executable(luxesession-mime-driver tests/mime-driver.c mime-defaults.c safe-file.c)
  target_include_directories(luxesession-mime-driver PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(luxesession-mime-driver PRIVATE PkgConfig::GLIB)
  add_test(NAME luxesession-mime-defaults
    COMMAND dbus-run-session -- ${CMAKE_CURRENT_SOURCE_DIR}/tests/test-mime-defaults.sh
      $<TARGET_FILE:luxesession-mime-driver>)
  add_test(NAME luxesession-mime-dbus
    COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/RunNestedX.sh"
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/test-mime-defaults.sh
      $<TARGET_FILE:luxesession-mime-driver> $<TARGET_FILE:luxesession-bin>)
  set_tests_properties(luxesession-mime-defaults luxesession-mime-dbus
    PROPERTIES TIMEOUT 30)
  add_test(NAME luxesession-autostart-migration
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/test-migrate-user-config.sh
      ${CMAKE_CURRENT_SOURCE_DIR}/data/luxe-migrate-user-config)
  add_test(NAME luxesession-xdg-autostart
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/test-xdg-autostart.sh
      $<TARGET_FILE:luxesession-xdg-autostart>)
  add_test(NAME luxesession-profile-boundary
    COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/tests/test-profile-boundary.sh"
      $<TARGET_FILE:luxesession-bin>)
  add_executable(luxesession-profile-tests tests/test-profile.c luxesession/profile.c)
  target_compile_features(luxesession-profile-tests PRIVATE c_std_11)
  target_include_directories(luxesession-profile-tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/luxesession")
  target_link_libraries(luxesession-profile-tests PRIVATE PkgConfig::GLIB)
  add_test(NAME luxesession-profile COMMAND luxesession-profile-tests)

  add_executable(luxesession-xsettings-common-tests
    tests/test-xsettings-common.c luxesettings-daemon/xsettings-common.c)
  target_compile_features(luxesession-xsettings-common-tests PRIVATE c_std_11)
  target_include_directories(luxesession-xsettings-common-tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/luxesettings-daemon")
  target_link_libraries(luxesession-xsettings-common-tests PRIVATE PkgConfig::GLIB)
  add_test(NAME luxesession-xsettings-common COMMAND luxesession-xsettings-common-tests)

  add_executable(luxesession-safe-file-tests tests/test-safe-file.c safe-file.c)
  target_compile_features(luxesession-safe-file-tests PRIVATE c_std_11)
  target_include_directories(luxesession-safe-file-tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(luxesession-safe-file-tests PRIVATE PkgConfig::GLIB)
  add_test(NAME luxesession-safe-file COMMAND luxesession-safe-file-tests)
endif()

luxe_compile_translations(luxesession)
install(TARGETS luxesettings-daemon luxesession-xdg-autostart luxesession-db
  luxesession-edit luxesession-default-apps luxesession-logout luxeclipboard
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(TARGETS luxesession-bin RUNTIME DESTINATION ${CMAKE_INSTALL_LIBDIR}/luxesession)
configure_file(data/luxesession-wrapper
  ${CMAKE_CURRENT_BINARY_DIR}/luxesession-wrapper @ONLY)
configure_file(data/start-luxe
  ${CMAKE_CURRENT_BINARY_DIR}/start-luxe @ONLY)
install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/luxesession-wrapper
  DESTINATION ${CMAKE_INSTALL_BINDIR} RENAME luxesession)
install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/start-luxe luxelock/luxelock
  luxesession-default/luxesession-default
  luxesession-default/luxesession-default-terminal DESTINATION ${CMAKE_INSTALL_BINDIR})
configure_file(data/luxe-migrate-user-config
  ${CMAKE_CURRENT_BINARY_DIR}/luxe-migrate-user-config @ONLY)
install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/luxe-migrate-user-config
  DESTINATION ${CMAKE_INSTALL_LIBEXECDIR})
install(PROGRAMS luxesession-utils/luxesession-xsettings
  DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/luxesession)
install(FILES data/luxesession-default-apps.desktop data/luxesession-edit.desktop
  DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES data/luxe.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/xsessions)
install(FILES data/luxe-xcompmgr.desktop DESTINATION /etc/xdg/autostart)
install(DIRECTORY data/ui/ DESTINATION ${CMAKE_INSTALL_DATADIR}/luxesession/ui
  FILES_MATCHING PATTERN "*.ui")
install(DIRECTORY data/images/ DESTINATION ${CMAKE_INSTALL_DATADIR}/luxesession/images)
# The Afterpiece, Syscrash-Light and Syscrash-Luxe theme directories are no
# longer shipped. They were reachable only as manual choices; the six generated
# palettes carry the branding and each ships its own Openbox variant. The
# 6x6 control masks those themes provided now live in data/openbox-masks/ and
# are copied into every palette by BuildThemes.cmake.
install(DIRECTORY data/defaults/ DESTINATION ${CMAKE_INSTALL_DATADIR}/luxesession/defaults)
include(data/palettes/BuildThemes.cmake)
luxe_build_themes("${CMAKE_CURRENT_BINARY_DIR}/theme-data")
target_sources(luxesession-bin PRIVATE theme-watch.c ../luxeappearance/src/theme-presets.c)
target_include_directories(luxesession-bin PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/theme-data")
target_compile_definitions(luxesession-bin PRIVATE
  LUXE_THEME_BRIDGE_DIR="${CMAKE_INSTALL_FULL_DATADIR}/luxe/themes"
  LUXE_THEME_WALLPAPER_DIR="${CMAKE_INSTALL_FULL_DATADIR}/backgrounds/luxe")
if(BUILD_TESTING)
  add_executable(luxesession-theme-watch-tests tests/test-theme-watch.c theme-watch.c
    ../luxeappearance/src/theme-presets.c safe-file.c)
  target_include_directories(luxesession-theme-watch-tests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/theme-data")
  target_compile_definitions(luxesession-theme-watch-tests PRIVATE
    LUXE_THEME_BRIDGE_DIR="${CMAKE_CURRENT_BINARY_DIR}/theme-data/bridges"
    LUXE_THEME_WALLPAPER_DIR="${CMAKE_CURRENT_BINARY_DIR}/theme-data/backgrounds")
  target_link_libraries(luxesession-theme-watch-tests PRIVATE PkgConfig::GTK4)
  add_test(NAME luxesession-theme-watch COMMAND luxesession-theme-watch-tests)
  set_tests_properties(luxesession-theme-watch PROPERTIES TIMEOUT 10)
  find_program(OPENBOX_BIN openbox)
  find_program(XEPHYR_BIN Xephyr)
  find_program(XDOTOOL_BIN xdotool)
  find_program(XWININFO_BIN xwininfo)
  if(OPENBOX_BIN AND XEPHYR_BIN AND XDOTOOL_BIN AND XWININFO_BIN)
    add_test(NAME luxesession-native-headers
      COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/RunNestedX.sh"
        ${CMAKE_CURRENT_SOURCE_DIR}/../luxeappearance/tests/theme-widgets.sh
        ${CMAKE_CURRENT_BINARY_DIR}/theme-data)
    set_tests_properties(luxesession-native-headers PROPERTIES TIMEOUT 90)
    find_program(LUXETERM_TEST_BIN luxeterm)
    if(LUXETERM_TEST_BIN)
      add_test(NAME luxesession-real-header
        COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/RunNestedX.sh"
          ${CMAKE_CURRENT_SOURCE_DIR}/tests/test-real-header.sh
          ${LUXETERM_TEST_BIN} ${CMAKE_CURRENT_BINARY_DIR}/theme-data)
      set_tests_properties(luxesession-real-header PROPERTIES TIMEOUT 60)
    endif()
    # Session Settings loads its UI from the source tree in this test build.
    set(edit_test_definitions ${session_definitions})
    list(FILTER edit_test_definitions EXCLUDE REGEX "^PACKAGE_UI_DIR=")
    list(APPEND edit_test_definitions PACKAGE_UI_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data/ui")
    add_executable(luxesession-edit-test
      luxesession-edit/luxesession-edit.c luxesession-edit/luxesession-edit-common.c
      luxesession/profile.c safe-file.c)
    target_compile_features(luxesession-edit-test PRIVATE c_std_11)
    target_compile_definitions(luxesession-edit-test PRIVATE ${edit_test_definitions})
    target_include_directories(luxesession-edit-test PRIVATE ${session_includes})
    target_link_libraries(luxesession-edit-test PRIVATE PkgConfig::GLIB X11::X11 PkgConfig::GTK4 luxe-gtk-ui)
    add_test(NAME luxesession-session-edit
      COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/RunNestedX.sh"
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test-session-edit.sh
        $<TARGET_FILE:luxesession-edit-test>)
    set_tests_properties(luxesession-session-edit PROPERTIES TIMEOUT 60)
    add_test(NAME luxesession-openbox-compact
      COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/RunNestedX.sh"
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test-openbox-compact.sh
        ${CMAKE_CURRENT_BINARY_DIR}/theme-data/themes ${CMAKE_CURRENT_SOURCE_DIR}/data/themes)
    set_tests_properties(luxesession-openbox-compact PROPERTIES TIMEOUT 60)
  endif()
  add_executable(luxesession-autostart-state-tests
    tests/autostart-state.c luxesession-edit/luxesession-edit-common.c
    luxesession/profile.c safe-file.c)
  target_compile_features(luxesession-autostart-state-tests PRIVATE c_std_11)
  target_compile_definitions(luxesession-autostart-state-tests PRIVATE ${session_definitions})
  target_include_directories(luxesession-autostart-state-tests PRIVATE ${session_includes})
  target_link_libraries(luxesession-autostart-state-tests PRIVATE PkgConfig::GLIB PkgConfig::GTK4 X11::X11)
  add_test(NAME luxesession-autostart-state COMMAND luxesession-autostart-state-tests)
  set_tests_properties(luxesession-autostart-state PROPERTIES TIMEOUT 10)
  add_executable(luxesession-gnome-signals-tests tests/gnome-session-signals.c)
  target_compile_features(luxesession-gnome-signals-tests PRIVATE c_std_11)
  target_link_libraries(luxesession-gnome-signals-tests PRIVATE PkgConfig::GLIB)
  add_test(NAME luxesession-gnome-signals COMMAND luxesession-gnome-signals-tests)
  set_tests_properties(luxesession-gnome-signals PROPERTIES TIMEOUT 10)
endif()
install(DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/theme-data/themes/"
  DESTINATION ${CMAKE_INSTALL_DATADIR}/themes)
install(DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/theme-data/bridges/"
  DESTINATION ${CMAKE_INSTALL_DATADIR}/luxe/themes)
install(DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/theme-data/backgrounds/"
  DESTINATION ${CMAKE_INSTALL_DATADIR}/backgrounds/luxe)
# Optional supplied collections do not replace the six theme-follow defaults.
foreach(collection horizon river veil)
  install(DIRECTORY "data/wallpapers/${collection}"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/backgrounds/luxe
    COMPONENT WallpaperCollections FILES_MATCHING PATTERN "*.png")
endforeach()
install(FILES data/wallpapers/PROVENANCE.md
  DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/luxesession
  RENAME WALLPAPER-COLLECTIONS.md COMPONENT WallpaperCollections)
install(FILES data/wallpapers/SHA256SUMS
  DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/luxesession
  RENAME WALLPAPER-COLLECTIONS.sha256 COMPONENT WallpaperCollections)
if(BUILD_TESTING)
  add_test(NAME luxesession-wallpaper-collections
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/test-wallpaper-collections.sh
      ${CMAKE_CURRENT_SOURCE_DIR}/data/wallpapers ${CMAKE_CURRENT_BINARY_DIR}
      ${CMAKE_COMMAND} ${CMAKE_INSTALL_DATADIR})
  set_tests_properties(luxesession-wallpaper-collections PROPERTIES TIMEOUT 60)
endif()
install(FILES data/palettes/PROVENANCE.md DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/luxesession
  RENAME THEME-PROVENANCE.md)
install(FILES data/LXDE/desktop.conf
  DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/xdg/luxesession/LXDE)
foreach(alias lxclipboard lxlock lxsession lxsession-db lxsession-default
    lxsession-default-apps lxsession-default-terminal lxsession-edit
    lxsession-logout lxsession-xdg-autostart lxsettings-daemon)
  string(REGEX REPLACE "^lx" "luxe" target "${alias}")
  luxe_install_symlink("${target}" "${CMAKE_INSTALL_BINDIR}/${alias}")
endforeach()

set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
  "${CMAKE_CURRENT_SOURCE_DIR}/packaging/conffiles")
luxe_configure_package(
  NAME luxesession VERSION ${PROJECT_VERSION} DEB_VERSION ${LUXESESSION_RELEASE}
  RPM_VERSION ${PROJECT_VERSION} RPM_RELEASE 5.luxe${LUXESESSION_REVISION}
  DESCRIPTION "Session manager and desktop defaults for Luxe" SECTION x11 LICENSE GPL-2.0-or-later
  DEB_DEPENDS openbox python3 xcompmgr frame DEB_RECOMMENDS luxepanel pcmanager luxepolkit luxenotify luxeterm luxe-icons "luxedm (>= 1.4.1-1+luxe16)" pkexec
  DEB_PROVIDES lxsession lxsession-gtk4 DEB_CONFLICTS lxsession lxsession-gtk4
  DEB_REPLACES lxsession lxsession-gtk4 RPM_REQUIRES gtk4 libSM libX11 openbox python3 xcompmgr frame luxenotify "luxedm >= 1.4.1-1.luxe16")
