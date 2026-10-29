cmake_minimum_required(VERSION 3.25)
project(luxenotify VERSION 0.1.0 LANGUAGES C)
include(../cmake/LuxePackage.cmake)
luxe_use_shared_headers()
set(LUXENOTIFY_REVISION 15)
set(LUXENOTIFY_VERSION "0.1.0-${LUXENOTIFY_REVISION}")

pkg_check_modules(GTK4 REQUIRED IMPORTED_TARGET gtk4)
find_package(X11 REQUIRED COMPONENTS Xrandr)

add_executable(luxenotify-renderer
  src/luxenotify.c
  src/notification-window.c)
target_compile_features(luxenotify-renderer PRIVATE c_std_11)
target_compile_definitions(luxenotify-renderer PRIVATE
  VERSION="${LUXENOTIFY_VERSION}"
  PACKAGE_NAME="luxenotify")
target_compile_options(luxenotify-renderer PRIVATE -Wall -Wextra -Wpedantic)
target_include_directories(luxenotify-renderer PRIVATE src)
target_link_libraries(luxenotify-renderer PRIVATE
  PkgConfig::GTK4
  X11::X11
  X11::Xrandr)
pkg_check_modules(GIO REQUIRED IMPORTED_TARGET gio-2.0)
add_executable(luxenotify src/broker.c)
target_compile_features(luxenotify PRIVATE c_std_11)
target_compile_options(luxenotify PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(luxenotify PRIVATE VERSION="${LUXENOTIFY_VERSION}"
  RENDERER_PATH="${CMAKE_INSTALL_FULL_LIBEXECDIR}/luxenotify-renderer")
target_link_libraries(luxenotify PRIVATE PkgConfig::GIO)

install(TARGETS luxenotify RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(TARGETS luxenotify-renderer RUNTIME DESTINATION ${CMAKE_INSTALL_LIBEXECDIR})
install(FILES data/luxenotify.desktop DESTINATION /etc/xdg/autostart)
install(FILES data/org.freedesktop.Notifications.service
  DESTINATION ${CMAKE_INSTALL_DATADIR}/dbus-1/services)

set(CPACK_RPM_PACKAGE_PROVIDES notification-daemon)

include(CTest)
if(BUILD_TESTING)
  find_program(DBUS_RUN_SESSION_EXECUTABLE dbus-run-session)
  find_program(GDBUS_EXECUTABLE gdbus)
  find_program(XEPHYR_EXECUTABLE Xephyr)
  add_executable(luxenotify-behavior tests/behavior.c)
  add_executable(luxenotify-hanging-renderer tests/hanging-renderer.c)
  target_link_libraries(luxenotify-hanging-renderer PRIVATE PkgConfig::GIO)
  target_compile_options(luxenotify-hanging-renderer PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_definitions(luxenotify-behavior PRIVATE VERSION="${LUXENOTIFY_VERSION}")
  target_compile_options(luxenotify-behavior PRIVATE -Wall -Wextra -Wpedantic)
  target_link_libraries(luxenotify-behavior PRIVATE PkgConfig::GTK4 X11::X11 X11::Xrandr)
  if(DBUS_RUN_SESSION_EXECUTABLE AND GDBUS_EXECUTABLE AND XEPHYR_EXECUTABLE)
    add_test(NAME luxenotify-broker-timeout COMMAND "${DBUS_RUN_SESSION_EXECUTABLE}" -- sh
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/broker-timeout.sh" $<TARGET_FILE:luxenotify> $<TARGET_FILE:luxenotify-hanging-renderer>)
    set_tests_properties(luxenotify-broker-timeout PROPERTIES TIMEOUT 25)
    add_test(NAME luxenotify-broker
      COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/RunNestedX.sh"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/broker.sh"
        "$<TARGET_FILE:luxenotify>" "$<TARGET_FILE:luxenotify-renderer>")
    set_tests_properties(luxenotify-broker PROPERTIES TIMEOUT 35)
    add_test(NAME luxenotify-dbus
      COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/RunNestedX.sh"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/dbus-smoke.sh" "$<TARGET_FILE:luxenotify-renderer>")
    add_test(NAME luxenotify-behavior
      COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/RunNestedX.sh"
        "$<TARGET_FILE:luxenotify-behavior>")
    add_test(NAME luxenotify-headless
      COMMAND dbus-run-session -- ${CMAKE_CURRENT_SOURCE_DIR}/tests/headless.sh $<TARGET_FILE:luxenotify-renderer>)
    add_test(NAME luxenotify-broker-headless
      COMMAND dbus-run-session -- ${CMAKE_CURRENT_SOURCE_DIR}/tests/headless.sh $<TARGET_FILE:luxenotify> $<TARGET_FILE:luxenotify-renderer>)
  else()
    message(STATUS "Notification UI tests require Xephyr, dbus-run-session and gdbus")
  endif()
endif()

luxe_configure_package(
  NAME luxenotify VERSION ${PROJECT_VERSION} DEB_VERSION ${LUXENOTIFY_VERSION} RPM_RELEASE ${LUXENOTIFY_REVISION}
  DESCRIPTION "GTK4 notification service for the Luxe desktop"
  SECTION x11 LICENSE GPL-2.0-or-later
  DEB_PROVIDES notification-daemon
  DEB_RECOMMENDS luxe-icons
  RPM_REQUIRES gtk4 libX11 libXrandr)
