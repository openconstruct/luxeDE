cmake_minimum_required(VERSION 3.25)
project(debinstall VERSION 0.1.0 LANGUAGES NONE)
include(../cmake/LuxePackage.cmake)
include(CTest)

set(DEBINSTALL_VERSION "${PROJECT_VERSION}-17")
configure_file(src/paths.py.in ${CMAKE_CURRENT_BINARY_DIR}/generated/paths.py @ONLY)
configure_file(src/installer.py ${CMAKE_CURRENT_BINARY_DIR}/generated/debinstall-install @ONLY)
configure_file(data/debinstall ${CMAKE_CURRENT_BINARY_DIR}/generated/debinstall @ONLY)
configure_file(data/org.lumen.DebInstall.policy ${CMAKE_CURRENT_BINARY_DIR}/generated/org.lumen.DebInstall.policy @ONLY)

if(BUILD_TESTING)
  foreach(original unset empty ngl flags all except)
    add_test(NAME debinstall-renderer-${original}
      COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/RunNestedX.sh"
        /usr/bin/python3 ${CMAKE_CURRENT_SOURCE_DIR}/tests/renderer.py ${original})
  endforeach()
  add_test(NAME debinstall-backend
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_backend.py)
  add_test(NAME debinstall-process-runner
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_process_runner.py)
  add_test(NAME debinstall-boundary
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_boundary.py
      ${CMAKE_CURRENT_BINARY_DIR}/generated)
  find_program(XEPHYR_BIN Xephyr)
  find_program(OPENBOX_BIN openbox)
  find_program(DBUS_RUN_SESSION_BIN dbus-run-session)
  if(XEPHYR_BIN AND OPENBOX_BIN AND DBUS_RUN_SESSION_BIN)
    # Isolated Xephyr/openbox session (self-managed by tests/run-ui.py,
    # same pattern as luxepanel/shotty); builds its own fixture .deb.
    add_test(NAME debinstall-ui
      COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/tests/run-ui.py)
    set_tests_properties(debinstall-ui PROPERTIES TIMEOUT 300)
  endif()
endif()

install(FILES src/app.py src/backend.py src/process_runner.py src/snapshot.py src/snapshot_copy.py ${CMAKE_CURRENT_BINARY_DIR}/generated/paths.py DESTINATION ${CMAKE_INSTALL_LIBDIR}/debinstall)
install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/generated/debinstall-install DESTINATION ${CMAKE_INSTALL_LIBEXECDIR})
install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/generated/debinstall DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES data/debinstall.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES data/debinstall.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/generated/org.lumen.DebInstall.policy DESTINATION ${CMAKE_INSTALL_DATADIR}/polkit-1/actions)
install(FILES data/copyright DESTINATION ${CMAKE_INSTALL_DOCDIR})

luxe_configure_package(
  NAME debinstall VERSION ${PROJECT_VERSION} DEB_VERSION ${DEBINSTALL_VERSION} NOARCH NO_RPM
  DESCRIPTION "GTK4 frontend for installing Debian packages with gdebi-core"
  SECTION admin LICENSE GPL-2.0-or-later
  DEB_DEPENDS python3 python3-gi python3-apt python3-debian gdebi-core gir1.2-gtk-4.0 gir1.2-vte-3.91 pkexec
  DEB_RECOMMENDS luxe-icons)
