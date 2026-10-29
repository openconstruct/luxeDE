cmake_minimum_required(VERSION 3.25)
project(luxeappearance VERSION 0.3.0 LANGUAGES C CXX)
include(../cmake/LuxePackage.cmake)

# Luxe release suffix, shared by the compiled-in VERSION (shown in the
# About dialog) and the DEB/RPM release fields.
set(LUXE_RELEASE "luxe37")
set(LUXE_FULL_VERSION "0.3.0+gtk4-2+${LUXE_RELEASE}")

# Generated config.h replaces the old checked-in autotools header. The
# build dir comes first on the include path so sources pick up this
# generated header (CURSOR_THEME enabled: the Cursor page is live).
set(CURSOR_THEME 1)
configure_file(config.h.cmake "${CMAKE_CURRENT_BINARY_DIR}/config.h" @ONLY)

pkg_check_modules(GTK4 REQUIRED IMPORTED_TARGET gtk4>=4.12)
pkg_check_modules(LIBXML2 REQUIRED IMPORTED_TARGET libxml-2.0)
luxe_plain_gtk_ui()
find_package(X11 REQUIRED)
set(LUXEAPPEARANCE_DATA_DIR "${CMAKE_INSTALL_FULL_DATADIR}")
include(../luxesession/data/palettes/BuildThemes.cmake)
luxe_build_themes("${CMAKE_CURRENT_BINARY_DIR}/theme-data")
set(preset_sources src/theme-presets.c ../luxesession/safe-file.c)
add_executable(luxe-theme-publish src/publish-icon-theme.c)
target_compile_features(luxe-theme-publish PRIVATE c_std_11)
find_package(ZLIB REQUIRED)
find_package(BZip2 REQUIRED)
add_executable(luxe-theme-extract src/extract-icon-archive.cpp)
target_compile_features(luxe-theme-extract PRIVATE cxx_std_17)
target_link_libraries(luxe-theme-extract PRIVATE ZLIB::ZLIB BZip2::BZip2)
add_executable(luxeappearance src/main.c src/glade-support.c src/main-dlg-ui.c
  src/main-dlg.c src/demo-ui.c src/demo.c ${preset_sources})
target_compile_definitions(luxeappearance PRIVATE
  LUXE_THEME_BRIDGE_DIR="${CMAKE_INSTALL_FULL_DATADIR}/luxe/themes"
  LUXE_THEME_WALLPAPER_DIR="${CMAKE_INSTALL_FULL_DATADIR}/backgrounds/luxe")
target_compile_features(luxeappearance PRIVATE c_std_11)
target_compile_definitions(luxeappearance PRIVATE HAVE_CONFIG_H ENABLE_GTK4
  GETTEXT_PACKAGE="luxeappearance" VERSION="${LUXE_FULL_VERSION}"
  PACKAGE_DATA_DIR="${LUXEAPPEARANCE_DATA_DIR}"
  PACKAGE_LOCALE_DIR="${CMAKE_INSTALL_FULL_LOCALEDIR}"
  OPENBOX_DEFAULT_CONFIG="${CMAKE_INSTALL_FULL_DATADIR}/luxesession/defaults/openbox/rc.xml")
target_include_directories(luxeappearance PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}" src "${CMAKE_CURRENT_BINARY_DIR}/theme-data")
target_link_libraries(luxeappearance PRIVATE PkgConfig::GTK4 PkgConfig::LIBXML2 luxe-gtk-ui X11::X11)
# gettext is a documented build requirement, so a missing msgfmt is a
# configure error rather than a silently untranslated package.
find_program(MSGFMT_EXECUTABLE msgfmt REQUIRED)
luxe_compile_translations(luxeappearance)

include(CTest)
if(BUILD_TESTING)
  add_test(NAME theme-native-widgets COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/RunNestedX.sh"
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/theme-widgets.sh
    ${CMAKE_CURRENT_BINARY_DIR}/theme-data)
  set_tests_properties(theme-native-widgets PROPERTIES TIMEOUT 90
    ENVIRONMENT "LUXE_THEME_SCREENSHOTS=${CMAKE_CURRENT_BINARY_DIR}/theme-screenshots")
  # Settings-save test: an isolated Xephyr session asserts that applying
  # settings preserves unknown keys and comments in the user's settings.ini.
  find_program(XEPHYR_BIN Xephyr)
  find_program(DBUS_RUN_SESSION_BIN dbus-run-session)
  find_program(BASH_EXECUTABLE bash)
  if(XEPHYR_BIN AND DBUS_RUN_SESSION_BIN AND BASH_EXECUTABLE)
    # The test binary intentionally loads UI only from its installed data
    # directory. Stage the current UI beside the build so it cannot
    # accidentally exercise an older system-installed package.
    set(LUXEAPPEARANCE_TEST_DATA_DIR "${CMAKE_CURRENT_BINARY_DIR}/test-data")
    file(MAKE_DIRECTORY "${LUXEAPPEARANCE_TEST_DATA_DIR}/luxeappearance")
    configure_file(data/luxeappearance.ui
      "${LUXEAPPEARANCE_TEST_DATA_DIR}/luxeappearance/luxeappearance.ui" COPYONLY)
    configure_file(data/demo.ui
      "${LUXEAPPEARANCE_TEST_DATA_DIR}/luxeappearance/demo.ui" COPYONLY)
    add_executable(luxeappearance-save-test tests/save-settings.c
      src/glade-support.c src/main-dlg-ui.c src/demo-ui.c src/demo.c ${preset_sources})
    target_compile_features(luxeappearance-save-test PRIVATE c_std_11)
    target_compile_definitions(luxeappearance-save-test PRIVATE HAVE_CONFIG_H
      LUXE_TEST_SCREENSHOT="${CMAKE_CURRENT_BINARY_DIR}/appearance-chooser.png"
      GETTEXT_PACKAGE="luxeappearance" VERSION="${LUXE_FULL_VERSION}"
      LUXE_APPEARANCE_TESTING
      PACKAGE_DATA_DIR="${LUXEAPPEARANCE_TEST_DATA_DIR}"
      PACKAGE_LOCALE_DIR="${CMAKE_INSTALL_FULL_LOCALEDIR}"
      OPENBOX_DEFAULT_CONFIG="${CMAKE_CURRENT_SOURCE_DIR}/../luxesession/data/defaults/openbox/rc.xml")
    target_include_directories(luxeappearance-save-test PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}" src)
    # The save test builds the real dialog (AdwApplicationWindow from the
    # .ui), so it needs  for GtkBuilder type resolution; it is a
    # GUI test binary, not an unprivileged backend.
    target_link_libraries(luxeappearance-save-test PRIVATE PkgConfig::GTK4 PkgConfig::LIBXML2 luxe-gtk-ui X11::X11)
    add_test(NAME icon-chooser-ownership COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/RunNestedX.sh"
      $<TARGET_FILE:luxeappearance-save-test> --chooser-ownership)
    set_tests_properties(icon-chooser-ownership PROPERTIES TIMEOUT 20)
    # The runner uses bash arrays/pipefail, so it must be executed directly:
    # registering it as `sh script` made CMake bypass the shebang and run
    # the file under dash, which rejects `set -o pipefail` immediately.
    add_test(NAME save-settings-preserve
      COMMAND ${BASH_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/run-save-test.sh
        $<TARGET_FILE:luxeappearance-save-test> ${CMAKE_CURRENT_SOURCE_DIR}
        $<TARGET_FILE:luxeappearance-probe> $<TARGET_FILE:luxeappearance-lifecycle-test>)
    set_tests_properties(save-settings-preserve PROPERTIES
      TIMEOUT 300
      ENVIRONMENT "XEPHYR_BIN=${XEPHYR_BIN};DBUS_RUN_SESSION_BIN=${DBUS_RUN_SESSION_BIN}")
    add_test(NAME icon-theme-installer
      COMMAND ${BASH_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/run-installer-test.sh
        ${CMAKE_CURRENT_SOURCE_DIR}/scripts/install-icon-theme.sh $<TARGET_FILE:luxe-theme-publish> $<TARGET_FILE:luxe-theme-extract>)
    set_tests_properties(icon-theme-installer PROPERTIES TIMEOUT 60)
    add_executable(luxeappearance-probe tests/settings-probe.c)
    target_compile_features(luxeappearance-probe PRIVATE c_std_11)
    target_link_libraries(luxeappearance-probe PRIVATE PkgConfig::GTK4 X11::X11)
    # Activation/lifecycle test includes main.c so it exercises the same
    # private activation and destroy callbacks as the shipped application.
    add_executable(luxeappearance-lifecycle-test tests/lifecycle.c
      src/glade-support.c src/main-dlg-ui.c src/demo-ui.c src/demo.c src/main-dlg.c ${preset_sources})
    target_compile_features(luxeappearance-lifecycle-test PRIVATE c_std_11)
    target_compile_definitions(luxeappearance-lifecycle-test PRIVATE HAVE_CONFIG_H
      GETTEXT_PACKAGE="luxeappearance" VERSION="${LUXE_FULL_VERSION}"
      PACKAGE_DATA_DIR="${LUXEAPPEARANCE_TEST_DATA_DIR}"
      PACKAGE_LOCALE_DIR="${CMAKE_INSTALL_FULL_LOCALEDIR}"
      OPENBOX_DEFAULT_CONFIG="${CMAKE_INSTALL_FULL_DATADIR}/luxesession/defaults/openbox/rc.xml")
    target_include_directories(luxeappearance-lifecycle-test PRIVATE
      "${CMAKE_CURRENT_BINARY_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}" src)
    target_link_libraries(luxeappearance-lifecycle-test PRIVATE PkgConfig::GTK4 PkgConfig::LIBXML2 luxe-gtk-ui X11::X11)
    foreach(test luxeappearance-save-test luxeappearance-lifecycle-test)
      target_include_directories(${test} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/theme-data")
      target_compile_definitions(${test} PRIVATE
        LUXE_THEME_BRIDGE_DIR="${CMAKE_CURRENT_BINARY_DIR}/theme-data/bridges"
        LUXE_THEME_WALLPAPER_DIR="${CMAKE_CURRENT_BINARY_DIR}/theme-data/backgrounds")
    endforeach()
  endif()
endif()

install(TARGETS luxeappearance RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES luxeappearance.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES data/luxeappearance.ui data/demo.ui DESTINATION ${CMAKE_INSTALL_DATADIR}/luxeappearance)
install(PROGRAMS scripts/install-icon-theme.sh DESTINATION ${CMAKE_INSTALL_DATADIR}/luxeappearance)
install(TARGETS luxe-theme-publish RUNTIME DESTINATION ${CMAKE_INSTALL_DATADIR}/luxeappearance)
install(TARGETS luxe-theme-extract RUNTIME DESTINATION ${CMAKE_INSTALL_DATADIR}/luxeappearance)
luxe_install_symlink(luxeappearance ${CMAKE_INSTALL_BINDIR}/lxappearance)

luxe_configure_package(
  NAME luxeappearance VERSION ${PROJECT_VERSION} DEB_VERSION ${LUXE_FULL_VERSION} RPM_RELEASE 2.${LUXE_RELEASE}
  # The shipped COPYING is GPL-3 (the retained upstream license text), and the
  # About dialog credits GPL-3.0. Metadata must agree with what we ship.
  DESCRIPTION "GTK4 appearance settings for the Luxe desktop" SECTION x11 LICENSE GPL-3.0-or-later
  # Apply publishes org.gnome.desktop.interface; on minimal installations
  # that schema is not pulled in by GTK/ alone.
  DEB_DEPENDS gsettings-desktop-schemas frame
  DEB_RECOMMENDS luxe-icons "luxesession (>= 0.5.5+gtk4-4+luxe23)"
  DEB_PROVIDES lxappearance lxappearance-gtk4 DEB_CONFLICTS lxappearance lxappearance-gtk4
  DEB_REPLACES lxappearance lxappearance-gtk4
  RPM_REQUIRES gtk4 libX11 libxml2 frame luxesession gsettings-desktop-schemas)
