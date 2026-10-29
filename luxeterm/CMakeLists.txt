cmake_minimum_required(VERSION 3.25)
project(luxeterm VERSION 0.3.2 LANGUAGES C)
include(../cmake/LuxePackage.cmake)
include(CTest)
set(LUXETERM_REVISION 20)
set(LUXETERM_VERSION "${PROJECT_VERSION}+gtk4-2+luxe${LUXETERM_REVISION}")

pkg_check_modules(GTK4 REQUIRED IMPORTED_TARGET gtk4)
luxe_plain_gtk_ui()
pkg_check_modules(VTE REQUIRED IMPORTED_TARGET vte-2.91-gtk4)
pkg_check_modules(PCRE2 REQUIRED IMPORTED_TARGET libpcre2-8)
add_executable(luxeterm src/setting.c src/luxeterm.c src/preferences.c)
target_compile_features(luxeterm PRIVATE c_std_11)
target_compile_options(luxeterm PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
target_compile_definitions(luxeterm PRIVATE ENABLE_GTK4 ENABLE_NLS
  GETTEXT_PACKAGE="luxeterm" VERSION="${LUXETERM_VERSION}"
  PACKAGE_STRING="LuxeTerm ${LUXETERM_VERSION}"
  PACKAGE_BIN_DIR="${CMAKE_INSTALL_FULL_BINDIR}"
  PACKAGE_DATA_DIR="${CMAKE_INSTALL_FULL_DATADIR}"
  PACKAGE_LOCALE_DIR="${CMAKE_INSTALL_FULL_LOCALEDIR}")
target_include_directories(luxeterm PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" src)
target_link_libraries(luxeterm PRIVATE PkgConfig::GTK4 luxe-gtk-ui PkgConfig::VTE PkgConfig::PCRE2)
luxe_compile_translations(luxeterm)

if(BUILD_TESTING)
  include(../luxesession/data/palettes/BuildThemes.cmake)
  luxe_build_themes("${CMAKE_CURRENT_BINARY_DIR}/test-theme-data")
  add_executable(term-test tests/test_term.c src/setting.c)
  get_target_property(term_definitions luxeterm COMPILE_DEFINITIONS)
  target_compile_definitions(term-test PRIVATE ${term_definitions})
  target_compile_definitions(term-test PRIVATE LUXE_TEST_THEME_DATA="${CMAKE_CURRENT_BINARY_DIR}/test-theme-data")
  target_compile_options(term-test PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
  target_link_libraries(term-test PRIVATE luxe-gtk-ui PkgConfig::VTE PkgConfig::PCRE2)
  add_test(NAME luxeterm-behavior COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/RunNestedX.sh" $<TARGET_FILE:term-test>)
  set_tests_properties(luxeterm-behavior PROPERTIES TIMEOUT 100)
  set_tests_properties(luxeterm-behavior PROPERTIES ENVIRONMENT
    "XDG_DATA_DIRS=${CMAKE_CURRENT_BINARY_DIR}/test-theme-data:/usr/share")
  add_test(NAME luxeterm-forwarding COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/RunNestedX.sh" ${CMAKE_CURRENT_SOURCE_DIR}/tests/test-forwarding.sh $<TARGET_FILE:luxeterm>)
  set_tests_properties(luxeterm-forwarding PROPERTIES TIMEOUT 100)
endif()

install(TARGETS luxeterm RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES data/luxeterm.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES data/luxeterm.conf DESTINATION ${CMAKE_INSTALL_DATADIR}/luxeterm)
install(FILES data/luxeterm.png DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/128x128/apps)
luxe_install_symlink(luxeterm ${CMAKE_INSTALL_BINDIR}/lxterminal)

luxe_configure_package(
  NAME luxeterm VERSION ${PROJECT_VERSION} DEB_VERSION ${LUXETERM_VERSION} RPM_RELEASE 2.luxe${LUXETERM_REVISION}
  DESCRIPTION "GTK4 VTE terminal for the Luxe desktop" SECTION x11 LICENSE GPL-2.0-or-later
  DEB_PROVIDES lxterminal lxterminal-gtk4 DEB_CONFLICTS lxterminal lxterminal-gtk4
  DEB_REPLACES lxterminal lxterminal-gtk4 RPM_REQUIRES gtk4  vte291-gtk4 pcre2)
