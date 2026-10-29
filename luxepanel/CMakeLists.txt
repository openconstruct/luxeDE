cmake_minimum_required(VERSION 3.25)
project(luxepanel VERSION 0.10.1 LANGUAGES C)
set(LUXEPANEL_REVISION 65)
set(LUXEPANEL_RELEASE "0.10.1+gtk4-8+luxe${LUXEPANEL_REVISION}")
set(LUXEPANEL_RPM_RELEASE "8.luxe${LUXEPANEL_REVISION}")
include(../cmake/LuxePackage.cmake)
luxe_use_shared_headers()
include(../cmake/LuxeInternalDesktopLibraries.cmake)

set(LIBFM_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/../internal/libfm" CACHE PATH "Internal libfm source tree")
set(MENU_CACHE_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/../internal/menu-cache" CACHE PATH "Internal menu-cache source tree")

pkg_check_modules(GTK4 REQUIRED IMPORTED_TARGET gtk4)
pkg_check_modules(ALSA REQUIRED IMPORTED_TARGET alsa)
find_package(X11 REQUIRED)
luxe_add_internal_desktop_libraries(luxepanel "${LIBFM_SOURCE}" "${MENU_CACHE_SOURCE}")

set(GETTEXT_PACKAGE "luxepanel")
configure_file(config.h.cmake config.h @ONLY)

set(panel_definitions HAVE_CONFIG_H ENABLE_GTK4 ENABLE_NLS
  GETTEXT_PACKAGE="luxepanel" VERSION="${LUXEPANEL_RELEASE}"
  PACKAGE_LIB_DIR="/usr/${CMAKE_INSTALL_LIBDIR}"
  PACKAGE_DATA_DIR="/usr/${CMAKE_INSTALL_DATADIR}/luxepanel"
  PACKAGE_UI_DIR="/usr/${CMAKE_INSTALL_DATADIR}/luxepanel/ui"
  PACKAGE_BIN_DIR="/usr/${CMAKE_INSTALL_BINDIR}"
  PACKAGE_LOCALE_DIR="/usr/${CMAKE_INSTALL_LOCALEDIR}")
set(panel_includes "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/src"
  "${CMAKE_CURRENT_BINARY_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/luxepanel-include" "${LIBFM_SOURCE}/src"
  "${LIBFM_SOURCE}/src/base" "${LIBFM_SOURCE}/src/job"
  "${MENU_CACHE_SOURCE}/libmenu-cache")

add_library(luxepanel-core STATIC
  src/misc.c src/line-parser.c src/configurator.c src/dbg.c src/ev.c src/icon-grid.c src/panel.c
  src/panel-plugin-move.c src/plugin.c src/conf.c src/space.c src/input-button.c
  src/x11-hotkey.c)
set_target_properties(luxepanel-core PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_definitions(luxepanel-core PRIVATE ${panel_definitions})
target_include_directories(luxepanel-core PUBLIC ${panel_includes})
target_link_libraries(luxepanel-core PUBLIC PkgConfig::GTK4 X11::X11 luxepanel-fm luxepanel-menu-cache)

set(builtin_plugins
  plugins/dclock.c plugins/dirmenu.c plugins/launchtaskbar.c
  plugins/task-button.c plugins/launch-button.c plugins/separator.c
  plugins/tray.c plugins/wincmd.c plugins/menu.c plugins/notifications.c)
add_executable(luxepanel src/bg.c src/gtk-run.c src/main.c src/renderer-init.c src/menu-policy.c
  src/menu-section-state.c ${builtin_plugins})
# Loadable plugins resolve panel symbols (luxepanel_plugin_qdata, ...);
# without -rdynamic dlopen fails with undefined symbols.
set_target_properties(luxepanel PROPERTIES ENABLE_EXPORTS TRUE)
set_target_properties(luxepanel PROPERTIES ENABLE_EXPORTS ON)
target_compile_features(luxepanel PRIVATE c_std_11)
target_compile_definitions(luxepanel PRIVATE ${panel_definitions})
target_include_directories(luxepanel PRIVATE ${panel_includes})
# Loadable plugins resolve the whole plugin API (panel_config_click_parse,
# ...) at dlopen; nothing inside the panel itself calls some of those
# functions, so the static archive would GC them. Pull the core whole.
target_link_libraries(luxepanel PRIVATE
  -Wl,--whole-archive luxepanel-core -Wl,--no-whole-archive
  PkgConfig::GTK4 X11::X11)

add_executable(luxepanelctl src/luxepanelctl.c)
target_compile_features(luxepanelctl PRIVATE c_std_11)
target_compile_definitions(luxepanelctl PRIVATE HAVE_CONFIG_H ENABLE_GTK4)
target_include_directories(luxepanelctl PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" src)
target_link_libraries(luxepanelctl PRIVATE X11::X11)

function(add_luxepanel_plugin name)
  add_library(luxepanel-plugin-${name} MODULE ${ARGN})
  set_target_properties(luxepanel-plugin-${name} PROPERTIES PREFIX "" OUTPUT_NAME "${name}")
  target_compile_features(luxepanel-plugin-${name} PRIVATE c_std_11)
  target_compile_definitions(luxepanel-plugin-${name} PRIVATE ${panel_definitions})
  target_include_directories(luxepanel-plugin-${name} PRIVATE ${panel_includes})
  target_link_libraries(luxepanel-plugin-${name} PRIVATE PkgConfig::GTK4 X11::X11)
  install(TARGETS luxepanel-plugin-${name} LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/luxepanel/plugins)
endfunction()

add_luxepanel_plugin(volume plugins/volumealsa/volumealsa.c)
target_link_directories(luxepanel-plugin-volume PRIVATE ${ALSA_LIBRARY_DIRS})
target_link_libraries(luxepanel-plugin-volume PRIVATE PkgConfig::ALSA)
add_luxepanel_plugin(cpu plugins/cpu/cpu.c)
add_luxepanel_plugin(deskno plugins/deskno/deskno.c)
add_luxepanel_plugin(batt plugins/batt/batt.c plugins/batt/batt_sys.c)
target_include_directories(luxepanel-plugin-batt PRIVATE plugins/batt)
add_luxepanel_plugin(kbled plugins/kbled/kbled.c)
add_luxepanel_plugin(xkb plugins/xkb/xkb-plugin.c plugins/xkb/xkb.c)
target_include_directories(luxepanel-plugin-xkb PRIVATE plugins/xkb)
target_compile_definitions(luxepanel-plugin-xkb PRIVATE
  FLAGSDIR="/usr/${CMAKE_INSTALL_DATADIR}/luxepanel/images/xkb-flags"
  FLAGSCUSTDIR="/usr/${CMAKE_INSTALL_DATADIR}/luxepanel/images/xkb-flags-cust"
  XKBCONFDIR="/usr/${CMAKE_INSTALL_DATADIR}/luxepanel/xkeyboardconfig")
add_luxepanel_plugin(thermal plugins/thermal/thermal.c)
add_luxepanel_plugin(cpufreq plugins/cpufreq/cpufreq.c)
add_luxepanel_plugin(monitors plugins/monitors/monitors.c)

luxe_compile_translations(luxepanel)
luxe_install_internal_menu_helpers(luxepanel)
luxe_install_internal_desktop_licenses("${LIBFM_SOURCE}" "${MENU_CACHE_SOURCE}")

include(CTest)
if(BUILD_TESTING)
  add_executable(luxepanel-runtime-dir-test ../shared/tests/runtime-dir.c)
  target_link_libraries(luxepanel-runtime-dir-test PRIVATE PkgConfig::GTK4)
  add_test(NAME luxepanel-runtime-dir COMMAND luxepanel-runtime-dir-test)
  add_executable(luxepanel-init-profile tests/init-profile.c src/renderer-init.c)
  target_include_directories(luxepanel-init-profile PRIVATE src)
  target_link_libraries(luxepanel-init-profile PRIVATE PkgConfig::GTK4 luxepanel-fm)
  add_test(NAME luxepanel-default-panels
    COMMAND "${CMAKE_COMMAND}" "-DSOURCE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/DefaultPanels.cmake")
  set_tests_properties(luxepanel-default-panels PROPERTIES TIMEOUT 30)

  add_executable(luxepanel-menu-section-state
    tests/menu-section-state.c src/menu-section-state.c)
  target_compile_features(luxepanel-menu-section-state PRIVATE c_std_11)
  target_include_directories(luxepanel-menu-section-state PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_link_libraries(luxepanel-menu-section-state PRIVATE PkgConfig::GTK4)
  add_test(NAME luxepanel-menu-section-state COMMAND luxepanel-menu-section-state)
  add_executable(luxepanel-config-tests tests/config.c src/conf.c src/line-parser.c)
  target_compile_features(luxepanel-config-tests PRIVATE c_std_11)
  target_compile_definitions(luxepanel-config-tests PRIVATE HAVE_CONFIG_H ENABLE_GTK4)
  target_include_directories(luxepanel-config-tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src" "${CMAKE_CURRENT_BINARY_DIR}" ${panel_includes})
  target_link_libraries(luxepanel-config-tests PRIVATE PkgConfig::GTK4 X11::X11)
  add_test(NAME luxepanel-config-boundaries COMMAND luxepanel-config-tests)

  add_executable(luxepanel-line-parser-tests tests/line-parser.c src/line-parser.c)
  target_compile_features(luxepanel-line-parser-tests PRIVATE c_std_11)
  target_compile_definitions(luxepanel-line-parser-tests PRIVATE HAVE_CONFIG_H ENABLE_GTK4)
  target_include_directories(luxepanel-line-parser-tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src" "${CMAKE_CURRENT_BINARY_DIR}" ${panel_includes})
  target_link_libraries(luxepanel-line-parser-tests PRIVATE PkgConfig::GTK4 X11::X11)
  add_test(NAME luxepanel-line-parser COMMAND luxepanel-line-parser-tests)
  add_executable(luxepanel-dclock-schedule tests/dclock-schedule.c)
  target_link_libraries(luxepanel-dclock-schedule PRIVATE PkgConfig::GTK4)
  add_test(NAME luxepanel-dclock-schedule COMMAND luxepanel-dclock-schedule)
  add_executable(luxepanel-run-completion tests/run-completion.c)
  target_link_libraries(luxepanel-run-completion PRIVATE PkgConfig::GTK4)
  add_test(NAME luxepanel-run-completion COMMAND luxepanel-run-completion)

  find_program(XEPHYR_BIN Xephyr)
  find_program(OPENBOX_BIN openbox)
  find_program(XDOTOOL_BIN xdotool)
  if(XEPHYR_BIN)
    add_executable(luxepanel-notification-bus tests/notification-bus.c)
    target_compile_definitions(luxepanel-notification-bus PRIVATE ${panel_definitions})
    target_include_directories(luxepanel-notification-bus PRIVATE ${panel_includes})
    target_compile_options(luxepanel-notification-bus PRIVATE -ffunction-sections -fdata-sections)
    target_link_options(luxepanel-notification-bus PRIVATE -Wl,--gc-sections)
    target_link_libraries(luxepanel-notification-bus PRIVATE luxepanel-core PkgConfig::GTK4 X11::X11)
    add_test(NAME luxepanel-notification-bus COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/RunNestedX.sh" $<TARGET_FILE:luxepanel-notification-bus>)
    set_tests_properties(luxepanel-notification-bus PROPERTIES TIMEOUT 15)
    add_executable(luxepanel-notification-count tests/notification-count.c)
    target_compile_definitions(luxepanel-notification-count PRIVATE ${panel_definitions})
    target_include_directories(luxepanel-notification-count PRIVATE ${panel_includes})
    target_compile_options(luxepanel-notification-count PRIVATE -ffunction-sections -fdata-sections)
    target_link_options(luxepanel-notification-count PRIVATE -Wl,--gc-sections)
    target_link_libraries(luxepanel-notification-count PRIVATE luxepanel-core PkgConfig::GTK4 X11::X11)
    add_test(NAME luxepanel-notification-count COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/RunNestedX.sh" $<TARGET_FILE:luxepanel-notification-count>)
    add_executable(luxepanel-run-completion-jobs tests/run-completion-jobs.c src/menu-policy.c)
    target_compile_definitions(luxepanel-run-completion-jobs PRIVATE ${panel_definitions})
    target_include_directories(luxepanel-run-completion-jobs PRIVATE ${panel_includes})
    target_compile_options(luxepanel-run-completion-jobs PRIVATE -ffunction-sections -fdata-sections)
    target_link_options(luxepanel-run-completion-jobs PRIVATE -Wl,--gc-sections)
    target_link_libraries(luxepanel-run-completion-jobs PRIVATE luxepanel-core PkgConfig::GTK4 X11::X11)
    add_test(NAME luxepanel-run-completion-jobs COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/RunNestedX.sh" $<TARGET_FILE:luxepanel-run-completion-jobs>)
    add_executable(luxepanel-dirmenu-path tests/dirmenu-path.c)
    target_compile_definitions(luxepanel-dirmenu-path PRIVATE ${panel_definitions})
    target_include_directories(luxepanel-dirmenu-path PRIVATE ${panel_includes})
    target_compile_options(luxepanel-dirmenu-path PRIVATE -ffunction-sections -fdata-sections)
    target_link_options(luxepanel-dirmenu-path PRIVATE -Wl,--gc-sections)
    target_link_libraries(luxepanel-dirmenu-path PRIVATE luxepanel-core PkgConfig::GTK4 X11::X11)
    add_test(NAME luxepanel-dirmenu-path COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/RunNestedX.sh" $<TARGET_FILE:luxepanel-dirmenu-path>)
    add_executable(luxepanel-tray-message-limits tests/tray-message-limits.c)
    target_compile_definitions(luxepanel-tray-message-limits PRIVATE ${panel_definitions})
    target_include_directories(luxepanel-tray-message-limits PRIVATE ${panel_includes})
    target_compile_options(luxepanel-tray-message-limits PRIVATE -ffunction-sections -fdata-sections)
    target_link_options(luxepanel-tray-message-limits PRIVATE -Wl,--gc-sections)
    target_link_libraries(luxepanel-tray-message-limits PRIVATE luxepanel-core PkgConfig::GTK4 X11::X11)
    add_test(NAME luxepanel-tray-message-limits COMMAND luxepanel-tray-message-limits)
    add_executable(luxepanel-launcher-drop-lifetime tests/launcher-drop-lifetime.c plugins/launch-button.c plugins/task-button.c src/menu-policy.c)
    target_compile_definitions(luxepanel-launcher-drop-lifetime PRIVATE ${panel_definitions})
    target_include_directories(luxepanel-launcher-drop-lifetime PRIVATE ${panel_includes})
    target_compile_options(luxepanel-launcher-drop-lifetime PRIVATE -ffunction-sections -fdata-sections)
    target_link_options(luxepanel-launcher-drop-lifetime PRIVATE -Wl,--gc-sections)
    target_link_libraries(luxepanel-launcher-drop-lifetime PRIVATE luxepanel-core PkgConfig::GTK4 X11::X11)
    add_test(NAME luxepanel-launcher-drop-lifetime COMMAND luxepanel-launcher-drop-lifetime)
    add_executable(luxepanel-renderer-trial tests/renderer-trial.c src/renderer-init.c)
    target_compile_features(luxepanel-renderer-trial PRIVATE c_std_11)
    target_include_directories(luxepanel-renderer-trial PRIVATE src)
    target_link_libraries(luxepanel-renderer-trial PRIVATE PkgConfig::GTK4)
    foreach(original unset ngl empty flags all except)
      add_test(NAME luxepanel-renderer-trial-${original}
        COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/RunNestedX.sh"
          $<TARGET_FILE:luxepanel-renderer-trial> ${original})
      set_tests_properties(luxepanel-renderer-trial-${original} PROPERTIES TIMEOUT 20)
    endforeach()
  endif()
  if(XEPHYR_BIN AND OPENBOX_BIN AND XDOTOOL_BIN)
    add_executable(luxepanel-icon-images tests/icon-images.c)
    target_compile_definitions(luxepanel-icon-images PRIVATE ${panel_definitions})
    target_include_directories(luxepanel-icon-images PRIVATE ${panel_includes})
    target_compile_options(luxepanel-icon-images PRIVATE -ffunction-sections -fdata-sections)
    target_link_options(luxepanel-icon-images PRIVATE -Wl,--gc-sections)
    target_link_libraries(luxepanel-icon-images PRIVATE PkgConfig::GTK4 X11::X11 luxepanel-fm)
    add_test(NAME luxepanel-icon-images COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/RunNestedX.sh"
      $<TARGET_FILE:luxepanel-icon-images>)
    set_tests_properties(luxepanel-icon-images PROPERTIES TIMEOUT 20)
    include(../luxesession/data/palettes/BuildThemes.cmake)
    luxe_build_themes("${CMAKE_CURRENT_BINARY_DIR}/test-theme-data")
    add_executable(luxepanel-tray-palette-client tests/tray-palette-client.c)
    target_link_libraries(luxepanel-tray-palette-client PRIVATE X11::X11 PkgConfig::GTK4)
    add_test(NAME luxepanel-menu-effects-and-tray
      COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/RunNestedX.sh"
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/menu-effects.sh
        $<TARGET_FILE:luxepanel> $<TARGET_FILE:luxepanelctl>
        $<TARGET_FILE:luxepanel-tray-palette-client> ${CMAKE_CURRENT_BINARY_DIR}/test-theme-data)
    set_tests_properties(luxepanel-menu-effects-and-tray PROPERTIES TIMEOUT 90)
    add_test(NAME luxepanel-menu-keyboard-focus
      COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/RunNestedX.sh"
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/menu-focus.sh
        $<TARGET_FILE:luxepanel> $<TARGET_FILE:luxepanelctl>)
    set_tests_properties(luxepanel-menu-keyboard-focus PROPERTIES TIMEOUT 90)
    # Taskbar activation/minimize/restore over a real window manager.
    add_test(NAME luxepanel-taskbar-click
      COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/RunNestedX.sh"
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/taskbar-click.sh
        $<TARGET_FILE:luxepanel>)
    set_tests_properties(luxepanel-taskbar-click PROPERTIES TIMEOUT 300 ENVIRONMENT "LUXE_TEST_SCREEN=600x200")
    # luxepanelctl respawn/restart/exit against a dead and live panel.
    add_test(NAME luxepanel-ctl-respawn
      COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/RunNestedX.sh"
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/ctl-respawn.sh
        $<TARGET_FILE:luxepanelctl> $<TARGET_FILE:luxepanel>)
    set_tests_properties(luxepanel-ctl-respawn PROPERTIES TIMEOUT 300 ENVIRONMENT "LUXE_TEST_SCREEN=800x600")
    add_test(NAME luxepanel-run-dialog-lifecycle
      COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/RunNestedX.sh"
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/run-dialog.sh
        $<TARGET_FILE:luxepanelctl> $<TARGET_FILE:luxepanel>)
    set_tests_properties(luxepanel-run-dialog-lifecycle PROPERTIES TIMEOUT 300 ENVIRONMENT "LUXE_TEST_SCREEN=800x600")
  endif()
  if(XEPHYR_BIN)
    add_executable(luxepanel-x-property-tests tests/x-property.c)
    target_compile_features(luxepanel-x-property-tests PRIVATE c_std_11)
    target_compile_definitions(luxepanel-x-property-tests PRIVATE ${panel_definitions})
    target_compile_options(luxepanel-x-property-tests PRIVATE -ffunction-sections -fdata-sections)
    target_include_directories(luxepanel-x-property-tests PRIVATE
      "${CMAKE_CURRENT_SOURCE_DIR}/src" "${CMAKE_CURRENT_BINARY_DIR}" ${panel_includes})
    target_link_libraries(luxepanel-x-property-tests PRIVATE luxepanel-core PkgConfig::GTK4 X11::X11)
    target_link_options(luxepanel-x-property-tests PRIVATE -Wl,--gc-sections)
    add_test(NAME luxepanel-x-property-boundaries
      COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/tests/run-x-property.sh
        $<TARGET_FILE:luxepanel-x-property-tests>)
    set_tests_properties(luxepanel-x-property-boundaries PROPERTIES TIMEOUT 30)

    add_executable(luxepanel-taskbar-overflow tests/taskbar-overflow.c src/icon-grid.c)
    target_compile_definitions(luxepanel-taskbar-overflow PRIVATE ENABLE_GTK4)
    target_include_directories(luxepanel-taskbar-overflow PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src" "${CMAKE_CURRENT_BINARY_DIR}")
    target_link_libraries(luxepanel-taskbar-overflow PRIVATE PkgConfig::GTK4)
    add_test(NAME luxepanel-taskbar-overflow
      COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/tests/run-taskbar-overflow.sh
        $<TARGET_FILE:luxepanel-taskbar-overflow>)
    set_tests_properties(luxepanel-taskbar-overflow PROPERTIES TIMEOUT 30)
  endif()
  # Volume plugin: right-click opens Pavucontrol (bwrap-swapped marker).
  find_program(BWRAP_BIN bwrap)
  find_program(PULSEAUDIO_BIN pulseaudio)
  find_program(SCROT_BIN scrot)
  if(BWRAP_BIN AND XEPHYR_BIN AND OPENBOX_BIN AND XDOTOOL_BIN AND ALSA_LIBRARY_DIRS AND PULSEAUDIO_BIN AND SCROT_BIN)
    add_test(NAME luxepanel-volume-integration
      COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/RunNestedX.sh"
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/volume-integration.sh
        $<TARGET_FILE:luxepanel> $<TARGET_FILE:luxepanel-plugin-volume>)
    set_tests_properties(luxepanel-volume-integration PROPERTIES TIMEOUT 300 ENVIRONMENT "LUXE_TEST_SCREEN=600x240")
  else()
    message(STATUS "Panel volume integration requires bubblewrap, Xephyr, Openbox, xdotool, ALSA, PulseAudio and scrot (all supplied in CI)")
  endif()
endif()
install(TARGETS luxepanel luxepanelctl RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY data/images/ DESTINATION ${CMAKE_INSTALL_DATADIR}/luxepanel/images)
install(DIRECTORY plugins/xkb/flags/ DESTINATION ${CMAKE_INSTALL_DATADIR}/luxepanel/images/xkb-flags
  FILES_MATCHING PATTERN "*.png")
install(DIRECTORY plugins/xkb/xkeyboardconfig/ DESTINATION ${CMAKE_INSTALL_DATADIR}/luxepanel/xkeyboardconfig
  FILES_MATCHING PATTERN "*.cfg")
install(DIRECTORY data/ui/gtk4/ DESTINATION ${CMAKE_INSTALL_DATADIR}/luxepanel/ui
  FILES_MATCHING PATTERN "*.ui")
install(FILES data/default/config DESTINATION /etc/xdg/luxepanel/default)
install(FILES data/default/panels/panel DESTINATION /etc/xdg/luxepanel/default/panels)
install(FILES data/default/config DESTINATION /etc/xdg/luxepanel/LXDE)
install(FILES data/default/panels/panel DESTINATION /etc/xdg/luxepanel/LXDE/panels)
install(FILES data/two_panels/config DESTINATION /etc/xdg/luxepanel/two_panels)
install(FILES data/two_panels/panels/top data/two_panels/panels/bottom
  DESTINATION /etc/xdg/luxepanel/two_panels/panels)
luxe_install_symlink(luxepanel ${CMAKE_INSTALL_BINDIR}/lxpanel)
luxe_install_symlink(luxepanelctl ${CMAKE_INSTALL_BINDIR}/lxpanelctl)

luxe_configure_package(
  NAME luxepanel VERSION ${PROJECT_VERSION} DEB_VERSION ${LUXEPANEL_RELEASE}
  RPM_VERSION 0.10.1 RPM_RELEASE ${LUXEPANEL_RPM_RELEASE}
  DESCRIPTION "Lightweight GTK4 panel for the Luxe desktop" SECTION x11 LICENSE GPL-2.0-or-later
  DEB_DEPENDS luxemenu-data DEB_RECOMMENDS luxe-icons pavucontrol
  DEB_PROVIDES lxpanel lxpanel-gtk4 DEB_CONFLICTS lxpanel lxpanel-gtk4
  DEB_REPLACES lxpanel lxpanel-gtk4 RPM_REQUIRES gtk4 alsa-lib libX11)
