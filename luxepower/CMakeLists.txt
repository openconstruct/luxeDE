cmake_minimum_required(VERSION 3.25)
project(luxepower VERSION 0.2.0 LANGUAGES C)
include(../cmake/LuxePackage.cmake)
include(CTest)
set(LUXEPOWER_REVISION 26)
set(LUXEPOWER_VERSION "${PROJECT_VERSION}-${LUXEPOWER_REVISION}")

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK4 REQUIRED IMPORTED_TARGET gtk4)
luxe_plain_gtk_ui()
pkg_check_modules(GIO REQUIRED IMPORTED_TARGET gio-2.0)
pkg_check_modules(X11 REQUIRED IMPORTED_TARGET x11)
pkg_check_modules(VTE REQUIRED IMPORTED_TARGET vte-2.91-gtk4)

add_library(luxepower-common STATIC EXCLUDE_FROM_ALL
  ../shared/luxe-lock-request.c
  src/tlp.c
  src/system.c)
target_compile_features(luxepower-common PUBLIC c_std_11)
target_compile_options(luxepower-common PRIVATE -Wall -Wextra)
target_link_libraries(luxepower-common PUBLIC PkgConfig::GIO)
target_compile_definitions(luxepower-common PUBLIC
  LUXEPOWER_HELPER_PATH="${CMAKE_INSTALL_FULL_LIBEXECDIR}/luxepower-apply"
  LUXELOCK_PATH="${CMAKE_INSTALL_FULL_BINDIR}/luxelock")

add_executable(luxepower
  src/app.c
  src/util.c
  src/power-page.c
  src/sleep-page.c
  src/display-page.c
  src/status-page.c
  src/activity.c)
target_compile_features(luxepower PRIVATE c_std_11)
target_compile_options(luxepower PRIVATE -Wall -Wextra)
target_compile_definitions(luxepower PRIVATE LUXEPOWER_VERSION="${LUXEPOWER_VERSION}")
target_link_libraries(luxepower PRIVATE luxepower-common PkgConfig::GTK4 luxe-gtk-ui PkgConfig::X11 PkgConfig::VTE)
install(TARGETS luxepower RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

add_executable(luxepower-apply src/apply.c)
target_compile_features(luxepower-apply PRIVATE c_std_11)
target_compile_options(luxepower-apply PRIVATE -Wall -Wextra)
target_link_libraries(luxepower-apply PRIVATE luxepower-common ${CMAKE_DL_LIBS})
install(TARGETS luxepower-apply RUNTIME DESTINATION ${CMAKE_INSTALL_LIBEXECDIR})

add_executable(luxepower-idle src/idle.c)
target_compile_features(luxepower-idle PRIVATE c_std_11)
target_compile_options(luxepower-idle PRIVATE -Wall -Wextra)
target_link_libraries(luxepower-idle PRIVATE luxepower-common ${CMAKE_DL_LIBS})
install(TARGETS luxepower-idle RUNTIME DESTINATION ${CMAKE_INSTALL_LIBEXECDIR})

if(BUILD_TESTING)
  add_executable(tlp-test tests/test_tlp.c)
  target_compile_features(tlp-test PRIVATE c_std_11)
  target_compile_options(tlp-test PRIVATE -Wall -Wextra)
  target_include_directories(tlp-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_link_libraries(tlp-test PRIVATE luxepower-common)
  add_test(NAME luxepower-backend COMMAND tlp-test)

  add_executable(boundary-test tests/test_boundary.c)
  target_link_libraries(boundary-test PRIVATE luxepower-common)
  target_compile_options(boundary-test PRIVATE -Wall -Wextra)
  target_compile_definitions(boundary-test PRIVATE LUXEPOWER_TEST_FILE_OWNER)
  add_test(NAME luxepower-boundary COMMAND boundary-test)
  set_tests_properties(luxepower-boundary PROPERTIES TIMEOUT 15)
  add_executable(ui-test tests/test_ui.c src/util.c src/activity.c)
  target_link_libraries(ui-test PRIVATE luxepower-common luxe-gtk-ui PkgConfig::VTE)
  target_compile_options(ui-test PRIVATE -Wall -Wextra)
  target_link_options(ui-test PRIVATE -Wl,--wrap=luxe_lock_request_async)
  find_program(GLIB_COMPILE_SCHEMAS_EXECUTABLE glib-compile-schemas REQUIRED)
  set(LUXEPOWER_TEST_SCHEMA_DIR "${CMAKE_CURRENT_BINARY_DIR}/test-schemas")
  add_custom_command(OUTPUT "${LUXEPOWER_TEST_SCHEMA_DIR}/gschemas.compiled"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${LUXEPOWER_TEST_SCHEMA_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
      "${CMAKE_CURRENT_SOURCE_DIR}/data/org.luxe.LuxePower.gschema.xml"
      "${LUXEPOWER_TEST_SCHEMA_DIR}/org.luxe.LuxePower.gschema.xml"
    COMMAND "${GLIB_COMPILE_SCHEMAS_EXECUTABLE}" --strict "${LUXEPOWER_TEST_SCHEMA_DIR}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/data/org.luxe.LuxePower.gschema.xml"
    VERBATIM)
  add_custom_target(luxepower-test-schemas DEPENDS "${LUXEPOWER_TEST_SCHEMA_DIR}/gschemas.compiled")
  add_dependencies(ui-test luxepower-test-schemas)
  add_test(NAME luxepower-ui COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/RunNestedX.sh" $<TARGET_FILE:ui-test>)
  set_tests_properties(luxepower-ui PROPERTIES TIMEOUT 100 ENVIRONMENT
    "GSETTINGS_SCHEMA_DIR=${LUXEPOWER_TEST_SCHEMA_DIR};GSETTINGS_BACKEND=memory")
  add_executable(idle-test tests/test_idle.c)
  target_link_libraries(idle-test PRIVATE luxepower-common ${CMAKE_DL_LIBS})
  target_compile_options(idle-test PRIVATE -Wall -Wextra)
  add_test(NAME luxepower-idle COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/RunNestedX.sh" $<TARGET_FILE:idle-test>)
  set_tests_properties(luxepower-idle PROPERTIES TIMEOUT 100)

  add_executable(fake-systemctl-test tests/fake_systemctl.c)
  target_compile_options(fake-systemctl-test PRIVATE -Wall -Wextra)
  add_executable(sleep-transaction-test tests/test_apply_sleep.c)
  target_compile_features(sleep-transaction-test PRIVATE c_std_11)
  target_compile_options(sleep-transaction-test PRIVATE -Wall -Wextra)
  target_compile_definitions(sleep-transaction-test PRIVATE
    LUXEPOWER_TESTING SYSTEMCTL_PATH="$<TARGET_FILE:fake-systemctl-test>")
  target_link_libraries(sleep-transaction-test PRIVATE luxepower-common)
  add_dependencies(sleep-transaction-test fake-systemctl-test)
  add_test(NAME luxepower-sleep-transaction COMMAND $<TARGET_FILE:sleep-transaction-test>)
  set_tests_properties(luxepower-sleep-transaction PROPERTIES TIMEOUT 15)

  add_executable(system-timeout-test tests/test_system_timeout.c src/system.c)
  target_compile_features(system-timeout-test PRIVATE c_std_11)
  target_compile_options(system-timeout-test PRIVATE -Wall -Wextra)
  target_compile_definitions(system-timeout-test PRIVATE SPAWN_CAPTURE_TIMEOUT_US=500000)
  target_link_libraries(system-timeout-test PRIVATE PkgConfig::GIO)
  add_test(NAME luxepower-system-timeout COMMAND $<TARGET_FILE:system-timeout-test>)
  set_tests_properties(luxepower-system-timeout PROPERTIES TIMEOUT 5)
endif()

install(FILES data/luxepower.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES data/org.luxe.LuxePower.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
configure_file(data/org.luxe.LuxePower.policy ${CMAKE_CURRENT_BINARY_DIR}/org.luxe.LuxePower.policy @ONLY)
configure_file(data/luxepower-idle.desktop ${CMAKE_CURRENT_BINARY_DIR}/luxepower-idle.desktop @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/org.luxe.LuxePower.policy DESTINATION ${CMAKE_INSTALL_DATADIR}/polkit-1/actions)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/luxepower-idle.desktop DESTINATION /etc/xdg/autostart)
install(FILES data/copyright DESTINATION ${CMAKE_INSTALL_DOCDIR})

luxe_install_gschema(data/org.luxe.LuxePower.gschema.xml)

luxe_configure_package(
  NAME luxepower VERSION ${PROJECT_VERSION} DEB_VERSION ${LUXEPOWER_VERSION} RPM_RELEASE ${LUXEPOWER_REVISION}
  DESCRIPTION "GTK4 power manager for TLP, sleep and lid actions, brightness, and screen locking"
  SECTION admin LICENSE GPL-2.0-or-later
  DEB_DEPENDS "tlp (>= 1.3)" pkexec systemd libxss1
  DEB_RECOMMENDS luxe-icons tlp-rdw "luxesession (>= 0.5.5+gtk4-4+luxe43)" "luxedm (>= 1.4.1-1+luxe16)"
  DEB_PROVIDES tlpui DEB_CONFLICTS tlpui DEB_REPLACES tlpui
  RPM_REQUIRES gtk4  tlp polkit systemd)
