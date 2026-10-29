cmake_minimum_required(VERSION 3.25)
project(luxeinput VERSION 0.3.5 LANGUAGES C)
include(../cmake/LuxePackage.cmake)
set(LUXEINPUT_REVISION 13)
set(LUXEINPUT_DEB_VERSION "0.3.5-1+luxe${LUXEINPUT_REVISION}")

pkg_check_modules(GTK4 REQUIRED IMPORTED_TARGET gtk4)
luxe_plain_gtk_ui()
find_package(X11 REQUIRED)
add_executable(luxeinput src/luxeinput.c)
target_compile_features(luxeinput PRIVATE c_std_11)
target_compile_definitions(luxeinput PRIVATE ENABLE_GTK4 ENABLE_NLS
  GETTEXT_PACKAGE="luxeinput" VERSION="${LUXEINPUT_DEB_VERSION}"
  PACKAGE_DATA_DIR="${CMAKE_INSTALL_FULL_DATADIR}/luxeinput"
  PACKAGE_LOCALE_DIR="${CMAKE_INSTALL_FULL_LOCALEDIR}")
target_include_directories(luxeinput PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" src)
target_link_libraries(luxeinput PRIVATE PkgConfig::GTK4 luxe-gtk-ui X11::X11)
luxe_compile_translations(luxeinput)

include(CTest)
if(BUILD_TESTING)
  find_program(XEPHYR_BIN Xephyr)
  find_program(DBUS_RUN_SESSION_BIN dbus-run-session)
  if(XEPHYR_BIN AND DBUS_RUN_SESSION_BIN)
    add_executable(luxeinput-test tests/test-input.c)
    target_compile_features(luxeinput-test PRIVATE c_std_11)
    target_compile_definitions(luxeinput-test PRIVATE ENABLE_GTK4 LUXEINPUT_TEST GETTEXT_PACKAGE="luxeinput"
      VERSION="0.3.5-1+luxe11" PACKAGE_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
    target_link_libraries(luxeinput-test PRIVATE PkgConfig::GTK4 luxe-gtk-ui X11::X11)
    add_test(NAME luxeinput-behavior COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/RunNestedX.sh" $<TARGET_FILE:luxeinput-test>)
    set_tests_properties(luxeinput-behavior PROPERTIES TIMEOUT 120)
  endif()
endif()

install(TARGETS luxeinput RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES data/luxeinput.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES data/luxeinput.ui data/input-mouse.png data/input-keyboard.png
  DESTINATION ${CMAKE_INSTALL_DATADIR}/luxeinput)
luxe_install_symlink(luxeinput ${CMAKE_INSTALL_BINDIR}/lxinput)

luxe_configure_package(
  NAME luxeinput VERSION ${PROJECT_VERSION} DEB_VERSION ${LUXEINPUT_DEB_VERSION} RPM_RELEASE 1.luxe${LUXEINPUT_REVISION}
  DESCRIPTION "GTK4 keyboard and pointer settings for Luxe" SECTION x11 LICENSE GPL-2.0-or-later
  DEB_DEPENDS "libxkbfile1 | x11-xkb-utils" DEB_RECOMMENDS luxe-icons
  DEB_PROVIDES lxinput-gtk4 DEB_CONFLICTS lxinput-gtk4 DEB_REPLACES lxinput-gtk4
  RPM_REQUIRES gtk4  libX11)
