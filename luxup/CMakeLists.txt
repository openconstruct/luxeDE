cmake_minimum_required(VERSION 3.25)
project(luxup VERSION 0.1.1 LANGUAGES C)
set(LUXUP_REVISION 33)
set(LUXUP_RELEASE "${PROJECT_VERSION}-1+luxe${LUXUP_REVISION}")
include(../cmake/LuxePackage.cmake)
luxe_use_shared_headers()
include(CTest)
set(LUXUP_SYSTEMD_UNITDIR "${CMAKE_INSTALL_PREFIX}/lib/systemd/system" CACHE PATH "systemd system unit directory")
configure_file(data/luxup-snap-policy.service luxup-snap-policy.service @ONLY)
foreach(script postinst prerm postrm rpm-pre-uninstall)
  configure_file(packaging/${script} packaging/${script} @ONLY)
endforeach()

pkg_check_modules(GTK4 REQUIRED IMPORTED_TARGET gtk4)
luxe_plain_gtk_ui()
pkg_check_modules(CAIRO REQUIRED IMPORTED_TARGET cairo)
find_package(X11 REQUIRED)
pkg_check_modules(GIO REQUIRED IMPORTED_TARGET gio-2.0 gio-unix-2.0)

add_executable(luxup main.c check.c)
target_compile_features(luxup PRIVATE c_std_11)
target_compile_definitions(luxup PRIVATE GETTEXT_PACKAGE="luxup"
  PACKAGE_LOCALE_DIR="${CMAKE_INSTALL_FULL_LOCALEDIR}" VERSION="${LUXUP_RELEASE}"
  LUXUP_HELPER="${CMAKE_INSTALL_FULL_LIBEXECDIR}/luxurystore-apt")
target_compile_definitions(luxup PRIVATE LUXUP_SERVICE="${CMAKE_INSTALL_FULL_LIBEXECDIR}/luxup-service")
target_compile_options(luxup PRIVATE -Wall -Wextra)
target_link_libraries(luxup PRIVATE PkgConfig::GTK4 luxe-gtk-ui PkgConfig::CAIRO PkgConfig::GIO X11::X11)
add_executable(luxup-service service.c check.c tray.c)
target_compile_features(luxup-service PRIVATE c_std_11)
target_compile_options(luxup-service PRIVATE -Wall -Wextra)
target_compile_definitions(luxup-service PRIVATE LUXUP_HEADLESS_TRAY
  LUXUP_THEME_DIR="${CMAKE_INSTALL_FULL_DATADIR}/luxe/themes"
  LUXUP_UI="${CMAKE_INSTALL_FULL_BINDIR}/luxup"
  LUXUP_HELPER="${CMAKE_INSTALL_FULL_LIBEXECDIR}/luxurystore-apt")
target_link_libraries(luxup-service PRIVATE PkgConfig::GIO PkgConfig::CAIRO X11::X11)
install(TARGETS luxup-service RUNTIME DESTINATION ${CMAKE_INSTALL_LIBEXECDIR})
configure_file(data/org.luxe.LuxUp.Service.service.in org.luxe.LuxUp.Service.service @ONLY)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/org.luxe.LuxUp.Service.service" DESTINATION ${CMAKE_INSTALL_DATADIR}/dbus-1/services)

install(TARGETS luxup RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES data/copyright DESTINATION ${CMAKE_INSTALL_DOCDIR})
install(FILES data/luxup.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES data/luxup-autostart.desktop DESTINATION /etc/xdg/autostart RENAME luxup.desktop)
install(FILES data/org.luxe.LuxUp.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
install(PROGRAMS data/luxup-snap-policy DESTINATION ${CMAKE_INSTALL_LIBEXECDIR})
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/luxup-snap-policy.service" data/luxup-snap-policy.path
  DESTINATION ${LUXUP_SYSTEMD_UNITDIR})

if(BUILD_TESTING)
add_executable(luxup-service-fixture tests/service-fixture.c tray.c)
target_compile_definitions(luxup-service-fixture PRIVATE LUXUP_HEADLESS_TRAY
  LUXUP_THEME_DIR="${CMAKE_INSTALL_FULL_DATADIR}/luxe/themes" LUXUP_UI="$<TARGET_FILE:luxup>")
target_link_libraries(luxup-service-fixture PRIVATE PkgConfig::GIO PkgConfig::CAIRO X11::X11)
add_test(NAME luxup-service COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/RunNestedX.sh" "${CMAKE_CURRENT_SOURCE_DIR}/tests/service.sh" "$<TARGET_FILE:luxup-service-fixture>" "$<TARGET_FILE:luxup>")
set_tests_properties(luxup-service PROPERTIES TIMEOUT 25)
add_test(NAME luxup-snap-policy
  COMMAND /bin/sh ${CMAKE_CURRENT_SOURCE_DIR}/tests/snap-policy.sh)
add_test(NAME luxup-package-hook-syntax
  COMMAND /bin/sh -c "for script in \"${CMAKE_CURRENT_BINARY_DIR}/packaging/postinst\" \"${CMAKE_CURRENT_BINARY_DIR}/packaging/prerm\" \"${CMAKE_CURRENT_BINARY_DIR}/packaging/rpm-pre-uninstall\"; do /bin/sh -n \"\$script\" || exit; done")
add_executable(luxup-behavior tests/behavior.c)
target_compile_definitions(luxup-behavior PRIVATE VERSION="${LUXUP_RELEASE}" LUXUP_BEHAVIOR_TEST)
target_link_libraries(luxup-behavior PRIVATE PkgConfig::GTK4 luxe-gtk-ui PkgConfig::CAIRO PkgConfig::GIO X11::X11)
add_test(NAME luxup-behavior COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/RunNestedX.sh" $<TARGET_FILE:luxup-behavior>)
endif()

set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
  "${CMAKE_CURRENT_BINARY_DIR}/packaging/postinst;${CMAKE_CURRENT_BINARY_DIR}/packaging/prerm;${CMAKE_CURRENT_BINARY_DIR}/packaging/postrm")
set(CPACK_RPM_POST_INSTALL_SCRIPT_FILE "${CMAKE_CURRENT_BINARY_DIR}/packaging/postinst")
set(CPACK_RPM_PRE_UNINSTALL_SCRIPT_FILE "${CMAKE_CURRENT_BINARY_DIR}/packaging/rpm-pre-uninstall")

luxe_configure_package(
  NAME luxup VERSION ${PROJECT_VERSION} DEB_VERSION ${LUXUP_RELEASE} RPM_RELEASE 1.luxe${LUXUP_REVISION}
  DESCRIPTION "Update notifier for APT, Flatpak, and Snap" SECTION admin LICENSE GPL-2.0-or-later
  DEB_DEPENDS apt util-linux DEB_RECOMMENDS luxurystore luxe-icons DEB_SUGGESTS flatpak snapd
  RPM_REQUIRES gtk4 cairo libX11 util-linux)
