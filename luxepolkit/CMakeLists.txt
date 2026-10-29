cmake_minimum_required(VERSION 3.25)
project(luxepolkit VERSION 0.1.0 LANGUAGES C)
set(LUXEPOLKIT_REVISION 23)
set(LUXEPOLKIT_RELEASE "0.1.0+gtk4-5+luxe${LUXEPOLKIT_REVISION}")
set(LUXEPOLKIT_RPM_RELEASE "12.luxe${LUXEPOLKIT_REVISION}")
include(../cmake/LuxePackage.cmake)
luxe_use_shared_headers()
include(CTest)

pkg_check_modules(GTK4 REQUIRED IMPORTED_TARGET gtk4)
pkg_check_modules(POLKIT REQUIRED IMPORTED_TARGET polkit-agent-1 polkit-gobject-1)
pkg_check_modules(X11 REQUIRED IMPORTED_TARGET x11)
pkg_check_modules(GIO REQUIRED IMPORTED_TARGET gio-2.0 gio-unix-2.0)
add_executable(luxepolkit src/luxepolkit.c src/broker-listener.c)
target_compile_features(luxepolkit PRIVATE c_std_11)
target_compile_definitions(luxepolkit PRIVATE ENABLE_GTK4 ENABLE_NLS
  GETTEXT_PACKAGE="luxepolkit" VERSION="${LUXEPOLKIT_RELEASE}"
  POLKIT_DIALOG_PATH="${CMAKE_INSTALL_FULL_LIBEXECDIR}/luxepolkit-dialog"
  PACKAGE_DATA_DIR="${CMAKE_INSTALL_FULL_DATADIR}/luxepolkit"
  PACKAGE_UI_DIR="${CMAKE_INSTALL_FULL_DATADIR}/luxepolkit/ui"
  PACKAGE_LOCALE_DIR="${CMAKE_INSTALL_FULL_LOCALEDIR}")
target_include_directories(luxepolkit PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" src)
target_link_libraries(luxepolkit PRIVATE PkgConfig::GIO PkgConfig::POLKIT)
add_executable(luxepolkit-dialog src/dialog-process.c src/luxepolkit-listener.c)
target_compile_features(luxepolkit-dialog PRIVATE c_std_11)
target_compile_definitions(luxepolkit-dialog PRIVATE ENABLE_GTK4 ENABLE_NLS
  GETTEXT_PACKAGE="luxepolkit" VERSION="${LUXEPOLKIT_RELEASE}"
  PACKAGE_UI_DIR="${CMAKE_INSTALL_FULL_DATADIR}/luxepolkit/ui"
  PACKAGE_LOCALE_DIR="${CMAKE_INSTALL_FULL_LOCALEDIR}")
target_include_directories(luxepolkit-dialog PRIVATE src)
target_link_libraries(luxepolkit-dialog PRIVATE PkgConfig::GTK4 PkgConfig::POLKIT PkgConfig::X11)
luxe_compile_translations(luxepolkit)

if(BUILD_TESTING)
  add_executable(luxepolkit-idle-test tests/idle-agent.c src/broker-listener.c)
  target_compile_definitions(luxepolkit-idle-test PRIVATE VERSION="${LUXEPOLKIT_RELEASE}"
    POLKIT_DIALOG_PATH="$<TARGET_FILE:luxepolkit-dialog>"
    GETTEXT_PACKAGE="luxepolkit" PACKAGE_UI_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data/ui")
  target_include_directories(luxepolkit-idle-test PRIVATE src)
  target_link_libraries(luxepolkit-idle-test PRIVATE PkgConfig::GIO PkgConfig::POLKIT)
  add_test(NAME agent-windowless-registration COMMAND dbus-run-session -- $<TARGET_FILE:luxepolkit-idle-test>)
  # Dialog lifecycle test: macro-overridden session, retry, cancel.
  add_executable(luxepolkit-dialog-test tests/dialog-test.c)
  target_compile_features(luxepolkit-dialog-test PRIVATE c_std_11)
  target_compile_definitions(luxepolkit-dialog-test PRIVATE ENABLE_GTK4 G_ENABLE_DEBUG POLKIT_GRAB_TIMEOUT_MS=1000
    VERSION="${LUXEPOLKIT_RELEASE}" GETTEXT_PACKAGE="luxepolkit"
    PACKAGE_UI_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data/ui"
    PACKAGE_LOCALE_DIR="/usr/${CMAKE_INSTALL_LOCALEDIR}")
  target_include_directories(luxepolkit-dialog-test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" src)
  find_program(XDOTOOL_BIN xdotool)
  if(XDOTOOL_BIN)
    target_compile_definitions(luxepolkit-dialog-test PRIVATE XDOTOOL_PATH="${XDOTOOL_BIN}")
  endif()
  target_link_libraries(luxepolkit-dialog-test PRIVATE PkgConfig::POLKIT PkgConfig::GTK4 PkgConfig::X11)
  find_program(XEPHYR_BIN Xephyr)
  find_program(DBUS_RUN_SESSION_BIN dbus-run-session)
  if(XEPHYR_BIN AND DBUS_RUN_SESSION_BIN)
    add_executable(luxepolkit-process-fixture tests/dialog-process-fixture.c)
    target_compile_definitions(luxepolkit-process-fixture PRIVATE ENABLE_GTK4 POLKIT_GRAB_TIMEOUT_MS=1000
      VERSION="${LUXEPOLKIT_RELEASE}" GETTEXT_PACKAGE="luxepolkit"
      PACKAGE_UI_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data/ui")
    target_include_directories(luxepolkit-process-fixture PRIVATE src)
    target_link_libraries(luxepolkit-process-fixture PRIVATE PkgConfig::GTK4 PkgConfig::POLKIT PkgConfig::X11)
    add_executable(luxepolkit-broker-test tests/broker-process.c)
    target_compile_definitions(luxepolkit-broker-test PRIVATE
      POLKIT_DIALOG_PATH=dialog_path POLKIT_STARTUP_TIMEOUT_MS=5000 FIXTURE_DIALOG_PATH="$<TARGET_FILE:luxepolkit-process-fixture>")
    target_include_directories(luxepolkit-broker-test PRIVATE src)
    target_link_libraries(luxepolkit-broker-test PRIVATE PkgConfig::GIO PkgConfig::POLKIT)
    add_test(NAME broker-child-lifecycle COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/RunNestedX.sh" $<TARGET_FILE:luxepolkit-broker-test>)
    set_tests_properties(broker-child-lifecycle PROPERTIES TIMEOUT 60)
    add_test(NAME process-boundary COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/RunNestedX.sh"
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/process-boundary.sh
      $<TARGET_FILE:luxepolkit-dialog> $<TARGET_FILE:luxepolkit-process-fixture>)
    set_tests_properties(process-boundary PROPERTIES TIMEOUT 60)
    add_test(NAME dialog-lifecycle COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/RunNestedX.sh" $<TARGET_FILE:luxepolkit-dialog-test>)
    set_tests_properties(dialog-lifecycle PROPERTIES TIMEOUT 120)
  endif()
endif()

install(TARGETS luxepolkit RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(TARGETS luxepolkit-dialog RUNTIME DESTINATION ${CMAKE_INSTALL_LIBEXECDIR})
install(FILES data/luxepolkit.desktop DESTINATION /etc/xdg/autostart)
install(FILES data/ui/luxepolkit.ui DESTINATION ${CMAKE_INSTALL_DATADIR}/luxepolkit/ui)
luxe_install_symlink(luxepolkit ${CMAKE_INSTALL_BINDIR}/lxpolkit)
luxe_install_symlink(../bin/luxepolkit ${CMAKE_INSTALL_LIBEXECDIR}/luxepolkit)

luxe_configure_package(
  NAME luxepolkit VERSION ${PROJECT_VERSION} DEB_VERSION ${LUXEPOLKIT_RELEASE} RPM_RELEASE ${LUXEPOLKIT_RPM_RELEASE}
  DESCRIPTION "GTK4 PolicyKit authentication agent for Luxe" SECTION admin LICENSE GPL-2.0-or-later
  DEB_PROVIDES lxpolkit lxpolkit-gtk4 polkit-1-auth-agent
  DEB_CONFLICTS lxpolkit lxpolkit-gtk4 DEB_REPLACES lxpolkit lxpolkit-gtk4
  RPM_REQUIRES gtk4 polkit x11)
