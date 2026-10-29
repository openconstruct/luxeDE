cmake_minimum_required(VERSION 3.25)
project(matinee VERSION 0.1.0 LANGUAGES C)
set(MATINEE_LUXE_REVISION 32)
set(MATINEE_RELEASE "${PROJECT_VERSION}-1+luxe${MATINEE_LUXE_REVISION}")
include(CTest)
include(../cmake/LuxePackage.cmake)

pkg_check_modules(GTK4 REQUIRED IMPORTED_TARGET gtk4)
luxe_plain_gtk_ui()
# Playback, decode and conversion are driven directly through FFmpeg's
# libraries; GStreamer is deliberately not used.
pkg_check_modules(AVFORMAT REQUIRED IMPORTED_TARGET libavformat)
pkg_check_modules(AVCODEC REQUIRED IMPORTED_TARGET libavcodec)
pkg_check_modules(AVUTIL REQUIRED IMPORTED_TARGET libavutil)
pkg_check_modules(SWSCALE REQUIRED IMPORTED_TARGET libswscale)
pkg_check_modules(SWRESAMPLE REQUIRED IMPORTED_TARGET libswresample)
pkg_check_modules(ALSA REQUIRED IMPORTED_TARGET alsa)

luxe_compile_resources("${CMAKE_CURRENT_BINARY_DIR}/matinee-resources.c" matinee.gresource.xml)
luxe_install_gschema(org.luxe.Matinee.gschema.xml)

add_library(matinee-resources OBJECT "${CMAKE_CURRENT_BINARY_DIR}/matinee-resources.c")
target_link_libraries(matinee-resources PRIVATE PkgConfig::GTK4)

set(matinee_sources
  matinee-app.c
  matinee-window.c
  matinee-engine.c
  matinee-visual.c
  matinee-model.c)
add_executable(matinee matinee.c ${matinee_sources} $<TARGET_OBJECTS:matinee-resources>)
target_compile_features(matinee PRIVATE c_std_11)
target_compile_definitions(matinee PRIVATE
  VERSION="${MATINEE_RELEASE}"
  LOCALEDIR="${CMAKE_INSTALL_FULL_LOCALEDIR}")
target_compile_options(matinee PRIVATE -Wall -Wextra)
target_link_libraries(matinee PRIVATE
  PkgConfig::GTK4
  luxe-gtk-ui
  PkgConfig::AVFORMAT
  PkgConfig::AVCODEC
  PkgConfig::AVUTIL
  PkgConfig::SWSCALE
  PkgConfig::SWRESAMPLE
  PkgConfig::ALSA
  m)

if(BUILD_TESTING)
  add_executable(matinee-startup tests/startup.c ${matinee_sources} $<TARGET_OBJECTS:matinee-resources>)
  target_compile_features(matinee-startup PRIVATE c_std_11)
  target_compile_definitions(matinee-startup PRIVATE VERSION="${MATINEE_RELEASE}" LOCALEDIR="${CMAKE_INSTALL_FULL_LOCALEDIR}")
  target_link_libraries(matinee-startup PRIVATE luxe-gtk-ui PkgConfig::AVFORMAT PkgConfig::AVCODEC
    PkgConfig::AVUTIL PkgConfig::SWSCALE PkgConfig::SWRESAMPLE PkgConfig::ALSA m)
  foreach(mode IN ITEMS unset empty explicit)
    add_test(NAME matinee-startup-${mode} COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/RunNestedX.sh"
      "${CMAKE_COMMAND}" -E env "GSETTINGS_SCHEMA_DIR=${CMAKE_CURRENT_BINARY_DIR}/schemas" $<TARGET_FILE:matinee-startup> ${mode})
    set_tests_properties(matinee-startup-${mode} PROPERTIES TIMEOUT 20)
  endforeach()
  find_program(GLIB_COMPILE_SCHEMAS glib-compile-schemas REQUIRED)
  file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/schemas")
  configure_file(org.luxe.Matinee.gschema.xml schemas/org.luxe.Matinee.gschema.xml COPYONLY)
  add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/schemas/gschemas.compiled"
    COMMAND "${GLIB_COMPILE_SCHEMAS}" --strict "${CMAKE_CURRENT_BINARY_DIR}/schemas"
    DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/schemas/org.luxe.Matinee.gschema.xml")
  add_custom_target(matinee-test-schemas DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/schemas/gschemas.compiled")
  add_dependencies(matinee-startup matinee-test-schemas)
  # behavior.c includes the implementation files directly so the test
  # exercises the shipped code, exactly as the other Luxe packages do.
  add_executable(matinee-behavior tests/behavior.c
    $<TARGET_OBJECTS:matinee-resources>
    "${CMAKE_CURRENT_BINARY_DIR}/schemas/gschemas.compiled")
  target_compile_definitions(matinee-behavior PRIVATE
    VERSION="${MATINEE_RELEASE}"
    LOCALEDIR="${CMAKE_INSTALL_FULL_LOCALEDIR}")
  target_compile_options(matinee-behavior PRIVATE -Wall -Wextra)
  target_link_libraries(matinee-behavior PRIVATE
    PkgConfig::GTK4
    luxe-gtk-ui
    PkgConfig::AVFORMAT
    PkgConfig::AVCODEC
    PkgConfig::AVUTIL
    PkgConfig::SWSCALE
    PkgConfig::SWRESAMPLE
    PkgConfig::ALSA
    m)
  add_test(NAME matinee-behavior COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/RunNestedX.sh" "${CMAKE_COMMAND}" -E env
    "GSETTINGS_SCHEMA_DIR=${CMAKE_CURRENT_BINARY_DIR}/schemas" $<TARGET_FILE:matinee-behavior>)
endif()

install(TARGETS matinee RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES data/matinee.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES data/copyright DESTINATION ${CMAKE_INSTALL_DOCDIR})

luxe_configure_package(
  NAME matinee VERSION ${PROJECT_VERSION} DEB_VERSION ${MATINEE_RELEASE} RPM_RELEASE 1.luxe${MATINEE_LUXE_REVISION}
  DESCRIPTION "GTK4 media player for the Luxe desktop" SECTION sound
  LICENSE GPL-3.0-or-later
  DEB_DEPENDS "dconf-gsettings-backend | gsettings-backend"
  DEB_RECOMMENDS "pipewire-audio | pipewire"
  RPM_REQUIRES gtk4  alsa-libs)
