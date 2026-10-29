cmake_minimum_required(VERSION 3.25)
project(frame VERSION 0.1.0 LANGUAGES C)
include(../cmake/LuxePaletteCatalog.cmake)
set(FRAME_RELEASE_NUMBER 36)
set(FRAME_RELEASE "0.1.0-1+luxe${FRAME_RELEASE_NUMBER}")
set(FRAME_RPM_RELEASE "1.luxe${FRAME_RELEASE_NUMBER}")
include(CTest)
include(../cmake/LuxePackage.cmake)

pkg_check_modules(GTK4 REQUIRED IMPORTED_TARGET gtk4)
luxe_plain_gtk_ui()
pkg_check_modules(EXIF REQUIRED IMPORTED_TARGET libexif)
# Debian's gdk-pixbuf has no WebP loader, so Frame decodes WebP itself.
pkg_check_modules(WEBP REQUIRED IMPORTED_TARGET libwebp libwebpdecoder)
find_package(X11 REQUIRED COMPONENTS Xrandr)
pkg_check_modules(CAIRO REQUIRED IMPORTED_TARGET cairo)
luxe_compile_resources("${CMAKE_CURRENT_BINARY_DIR}/frame-resources.c" frame.gresource.xml)
luxe_install_gschema(org.luxe.Frame.gschema.xml)

add_library(frame-resources OBJECT "${CMAKE_CURRENT_BINARY_DIR}/frame-resources.c")
target_link_libraries(frame-resources PRIVATE PkgConfig::GTK4)
add_executable(frame frame.c frame-app.c frame-window.c frame-image.c frame-wallpaper.c frame-exif.c frame-webp.c frame-decode.c $<TARGET_OBJECTS:frame-resources>)
target_compile_features(frame PRIVATE c_std_11)
target_compile_definitions(frame PRIVATE VERSION="${FRAME_RELEASE}"
  LUXE_WALLPAPER_DIR="${CMAKE_INSTALL_FULL_DATADIR}/backgrounds/luxe"
  LUXE_DEFAULT_PALETTE="${LUXE_DEFAULT_PALETTE}")
target_compile_options(frame PRIVATE -Wall -Wextra)
target_link_libraries(frame PRIVATE PkgConfig::GTK4 luxe-gtk-ui PkgConfig::EXIF PkgConfig::WEBP PkgConfig::CAIRO X11::X11 X11::Xrandr)

if(BUILD_TESTING)
  add_executable(frame-decode-test tests/decode.c frame-decode.c frame-webp.c)
  target_compile_definitions(frame-decode-test PRIVATE FRAME_MAX_INPUT=131072u)
  target_link_libraries(frame-decode-test PRIVATE PkgConfig::GTK4 PkgConfig::EXIF PkgConfig::WEBP)
  add_test(NAME frame-decode-limits COMMAND frame-decode-test)
  add_test(NAME frame-wallpaper-missing-operand
    COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/wallpaper-missing-operand.sh" $<TARGET_FILE:frame>)
  add_test(NAME frame-wallpaper-save-failure
    COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/RunNestedX.sh"
      sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/wallpaper-save-failure.sh" $<TARGET_FILE:frame>)
  set_tests_properties(frame-wallpaper-save-failure PROPERTIES TIMEOUT 30)
  add_library(frame-wallpaper-gate SHARED tests/wallpaper-gate.c)
  target_link_libraries(frame-wallpaper-gate PRIVATE PkgConfig::GTK4 ${CMAKE_DL_LIBS})
  add_test(NAME frame-wallpaper-concurrent-login
    COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/RunNestedX.sh" dbus-run-session --
      sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/wallpaper-concurrent-login.sh"
      $<TARGET_FILE:frame> $<TARGET_FILE:frame-wallpaper-gate> $<TARGET_FILE:frame-randr-fixture>)
  set_tests_properties(frame-wallpaper-concurrent-login PROPERTIES TIMEOUT 45)
  pkg_check_modules(GIO_UNIX REQUIRED IMPORTED_TARGET gio-unix-2.0)
  add_executable(frame-desktop-launch tests/desktop-launch.c)
  target_link_libraries(frame-desktop-launch PRIVATE PkgConfig::GIO_UNIX)
  add_test(NAME frame-desktop-launch COMMAND frame-desktop-launch "${CMAKE_CURRENT_SOURCE_DIR}/data/frame.desktop")
  add_executable(frame-webp-memory tests/webp-memory.c frame-webp.c)
  target_link_libraries(frame-webp-memory PRIVATE PkgConfig::GTK4 PkgConfig::EXIF PkgConfig::WEBP)
    find_program(GLIB_COMPILE_SCHEMAS glib-compile-schemas REQUIRED)
  # The WebP tests mux an EXIF chunk onto a test image.
  pkg_check_modules(WEBMUX REQUIRED IMPORTED_TARGET libwebpmux)
  file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/schemas")
  configure_file(org.luxe.Frame.gschema.xml schemas/org.luxe.Frame.gschema.xml COPYONLY)
  add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/schemas/gschemas.compiled"
    COMMAND "${GLIB_COMPILE_SCHEMAS}" --strict "${CMAKE_CURRENT_BINARY_DIR}/schemas"
    DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/schemas/org.luxe.Frame.gschema.xml")
  add_executable(frame-behavior tests/behavior.c frame-image.c frame-exif.c frame-webp.c frame-decode.c
    $<TARGET_OBJECTS:frame-resources> "${CMAKE_CURRENT_BINARY_DIR}/schemas/gschemas.compiled")
  target_compile_definitions(frame-behavior PRIVATE VERSION="${FRAME_RELEASE}")
  target_link_libraries(frame-behavior PRIVATE PkgConfig::GTK4 luxe-gtk-ui PkgConfig::EXIF PkgConfig::WEBP PkgConfig::WEBMUX)
  add_test(NAME frame-behavior COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/RunNestedX.sh" "${CMAKE_COMMAND}" -E env
    "GSETTINGS_SCHEMA_DIR=${CMAKE_CURRENT_BINARY_DIR}/schemas" $<TARGET_FILE:frame-behavior>)
  add_test(NAME frame-wallpaper COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/RunNestedX.sh" "${CMAKE_CURRENT_SOURCE_DIR}/tests/wallpaper.sh" $<TARGET_FILE:frame>)
  set_tests_properties(frame-wallpaper PROPERTIES ENVIRONMENT "LUXE_TEST_RESIZEABLE=1")
  add_library(frame-randr-fixture SHARED tests/randr-fixture.c)
  target_compile_options(frame-randr-fixture PRIVATE -Wall -Wextra)
  target_link_libraries(frame-randr-fixture PRIVATE X11::X11 X11::Xrandr ${CMAKE_DL_LIBS})
  add_test(NAME frame-wallpaper-randr-login
    COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/RunNestedX.sh" dbus-run-session --
      sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/wallpaper-randr-login.sh"
      $<TARGET_FILE:frame> $<TARGET_FILE:frame-wallpaper-gate> $<TARGET_FILE:frame-randr-fixture>)
  set_tests_properties(frame-wallpaper-randr-login PROPERTIES TIMEOUT 45)
  add_test(NAME frame-wallpaper-randr-settle COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/RunNestedX.sh" "${CMAKE_COMMAND}" -E env
    "LD_PRELOAD=$<TARGET_FILE:frame-randr-fixture>" "${CMAKE_CURRENT_SOURCE_DIR}/tests/wallpaper.sh" $<TARGET_FILE:frame>)
  set_tests_properties(frame-wallpaper-randr-settle PROPERTIES ENVIRONMENT "LUXE_TEST_RESIZEABLE=1")
  add_executable(frame-wallpaper-behavior tests/wallpaper-behavior.c frame-webp.c frame-exif.c frame-decode.c)
  target_compile_definitions(frame-wallpaper-behavior PRIVATE
    LUXE_DEFAULT_PALETTE="${LUXE_DEFAULT_PALETTE}")
  target_link_libraries(frame-wallpaper-behavior PRIVATE PkgConfig::GTK4 luxe-gtk-ui PkgConfig::EXIF PkgConfig::WEBP PkgConfig::CAIRO X11::X11 X11::Xrandr)
  add_test(NAME frame-wallpaper-rotation-stretch COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/RunNestedX.sh" $<TARGET_FILE:frame-wallpaper-behavior>)
endif()

install(TARGETS frame RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES data/frame.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES data/copyright DESTINATION ${CMAKE_INSTALL_DOCDIR})

luxe_configure_package(
  NAME frame VERSION ${PROJECT_VERSION} DEB_VERSION ${FRAME_RELEASE} RPM_RELEASE ${FRAME_RPM_RELEASE}
  DESCRIPTION "Fast GTK4 image viewer for the Luxe desktop" SECTION graphics LICENSE GPL-3.0-or-later
  DEB_DEPENDS "dconf-gsettings-backend | gsettings-backend"
  DEB_RECOMMENDS luxe-icons
  RPM_REQUIRES gtk4 libexif libwebp libwebpdecoder libX11 libXrandr)
