cmake_minimum_required(VERSION 3.25)
project(lucid VERSION 0.1.0 LANGUAGES C)
include(../cmake/LuxePackage.cmake)
set(LUCID_RELEASE_NUMBER 22)
set(LUCID_VERSION "0.1.0-1+luxe${LUCID_RELEASE_NUMBER}")
set(LUCID_RPM_RELEASE "1.luxe${LUCID_RELEASE_NUMBER}")

pkg_check_modules(GTK4 REQUIRED IMPORTED_TARGET gtk4>=4.14)
pkg_check_modules(GIO REQUIRED IMPORTED_TARGET gio-2.0)
luxe_plain_gtk_ui()
pkg_check_modules(SOURCEVIEW REQUIRED IMPORTED_TARGET gtksourceview-5>=5.10)
pkg_check_modules(VTE REQUIRED IMPORTED_TARGET vte-2.91-gtk4>=0.76)
pkg_check_modules(SPELLING REQUIRED IMPORTED_TARGET libspelling-1>=0.2)
configure_file(config.h.cmake config.h @ONLY)

add_library(lucid-state STATIC src/state.c)
target_compile_features(lucid-state PUBLIC c_std_11)
target_compile_options(lucid-state PRIVATE -Wall -Wextra)
target_link_libraries(lucid-state PUBLIC PkgConfig::GIO)
target_include_directories(lucid-state PUBLIC src "${CMAKE_CURRENT_BINARY_DIR}")
add_executable(lucid src/main.c src/modeline-parser.c)
target_compile_features(lucid PRIVATE c_std_11)
target_include_directories(lucid PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(lucid PRIVATE lucid-state PkgConfig::GTK4 luxe-gtk-ui PkgConfig::SOURCEVIEW PkgConfig::VTE PkgConfig::SPELLING)

include(CTest)
if(BUILD_TESTING)
  add_executable(lucid-state-test tests/state-test.c)
  target_link_libraries(lucid-state-test PRIVATE lucid-state)
  add_test(NAME session-state COMMAND lucid-state-test)
  add_executable(lucid-editor-test tests/editor-test.c src/modeline-parser.c)
  target_compile_features(lucid-editor-test PRIVATE c_std_11)
  target_include_directories(lucid-editor-test PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(lucid-editor-test PRIVATE lucid-state luxe-gtk-ui PkgConfig::SOURCEVIEW PkgConfig::VTE PkgConfig::SPELLING)
  file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/schemas")
  include(../luxesession/data/palettes/BuildThemes.cmake)
  luxe_build_themes("${CMAKE_CURRENT_BINARY_DIR}/test-theme-data")
  configure_file(data/org.luxe.Lucid.gschema.xml schemas/org.luxe.Lucid.gschema.xml COPYONLY)
  find_program(GLIB_COMPILE_SCHEMAS glib-compile-schemas REQUIRED)
  execute_process(COMMAND "${GLIB_COMPILE_SCHEMAS}" "${CMAKE_CURRENT_BINARY_DIR}/schemas" COMMAND_ERROR_IS_FATAL ANY)
  add_test(NAME editor-integration COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/RunNestedX.sh" $<TARGET_FILE:lucid-editor-test>)
  set_tests_properties(editor-integration PROPERTIES TIMEOUT 100 ENVIRONMENT
    "GSETTINGS_SCHEMA_DIR=${CMAKE_CURRENT_BINARY_DIR}/schemas;LUCID_DATA_DIR=${CMAKE_CURRENT_SOURCE_DIR}/data;XDG_DATA_DIRS=${CMAKE_CURRENT_BINARY_DIR}/test-theme-data:/usr/share")

endif()

install(TARGETS lucid RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES data/org.luxe.Lucid.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES data/org.luxe.Lucid.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
install(FILES data/language-mappings DESTINATION ${CMAKE_INSTALL_DATADIR}/lucid)
install(DIRECTORY data/styles/ DESTINATION ${CMAKE_INSTALL_DATADIR}/lucid/styles)
install(FILES COPYING DESTINATION ${CMAKE_INSTALL_DOCDIR} RENAME gedit-COPYING)
install(FILES gedit-copyright DESTINATION ${CMAKE_INSTALL_DOCDIR} RENAME gedit-copyright)
install(FILES UPSTREAM-CREDITS.txt COPYRIGHT-DOCUMENTATION.txt
  DESTINATION ${CMAKE_INSTALL_DOCDIR})
luxe_install_gschema(data/org.luxe.Lucid.gschema.xml)

luxe_configure_package(
  NAME lucid VERSION ${PROJECT_VERSION} DEB_VERSION ${LUCID_VERSION} RPM_RELEASE ${LUCID_RPM_RELEASE}
  DESCRIPTION "Fast GTK4 text editor for the Luxe desktop" SECTION editors LICENSE GPL-2.0-or-later
  DEB_DEPENDS "dconf-gsettings-backend | gsettings-backend" DEB_RECOMMENDS aspell-en luxe-icons
  RPM_REQUIRES gtk4 gtksourceview5 vte291 libspelling)
