# Copyright (C) 2026 Jerry Howell
# SPDX-License-Identifier: GPL-2.0-or-later

cmake_minimum_required(VERSION 3.25)
project(luxedm VERSION 1.4.1 LANGUAGES CXX)
include(../cmake/LuxePackage.cmake)
include(CTest)

set(LUXEDM_REVISION 25)
set(LUXEDM_VERSION "${PROJECT_VERSION}+luxe${LUXEDM_REVISION}")
set(LUXEDM_DEB_VERSION "${PROJECT_VERSION}-1+luxe${LUXEDM_REVISION}")
set(LUXEDM_RPM_RELEASE "1.luxe${LUXEDM_REVISION}")

find_package(X11 REQUIRED COMPONENTS Xmu)
find_package(Freetype REQUIRED)
find_package(JPEG REQUIRED)
find_package(PNG REQUIRED)
find_package(Threads REQUIRED)
pkg_check_modules(FONTCONFIG REQUIRED IMPORTED_TARGET fontconfig)
find_path(PAM_INCLUDE_DIR security/pam_appl.h REQUIRED)
find_library(PAM_LIBRARY pam REQUIRED)
find_library(CRYPT_LIBRARY crypt REQUIRED)

set(luxedm_definitions
  USE_PAM HAVE_SHADOW
  APPNAME="luxedm"
  VERSION="${LUXEDM_VERSION}"
  PKGDATADIR="${CMAKE_INSTALL_FULL_DATADIR}/luxedm"
  SYSCONFDIR="/etc")

set(common_sources
  src/PAM.cpp src/cfg.cpp src/image.cpp src/log.cpp src/panel.cpp src/util.cpp)
add_library(luxedm-common STATIC ${common_sources})
target_compile_features(luxedm-common PUBLIC cxx_std_17)
target_compile_definitions(luxedm-common PUBLIC ${luxedm_definitions})
target_include_directories(luxedm-common PUBLIC src "${PAM_INCLUDE_DIR}")
target_compile_options(luxedm-common PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(luxedm-common PUBLIC
  X11::X11 X11::Xft X11::Xrandr X11::Xrender
  Freetype::Freetype JPEG::JPEG PNG::PNG PkgConfig::FONTCONFIG
  "${PAM_LIBRARY}" "${CRYPT_LIBRARY}")

add_executable(luxedm src/main.cpp src/app.cpp src/numlock.cpp src/switchuser.cpp)
target_compile_features(luxedm PRIVATE cxx_std_17)
target_compile_options(luxedm PRIVATE -Wall -Wextra -Wpedantic)
add_library(luxedm-session-process STATIC src/session-process.cpp)
target_compile_features(luxedm-session-process PUBLIC cxx_std_17)
target_compile_options(luxedm-session-process PRIVATE -Wall -Wextra -Wpedantic)
target_include_directories(luxedm-session-process PUBLIC src)
target_link_libraries(luxedm PRIVATE luxedm-common luxedm-session-process X11::Xmu)

add_library(luxedm-lock-guard STATIC src/lock-guard.cpp)
target_compile_features(luxedm-lock-guard PUBLIC cxx_std_17)
target_include_directories(luxedm-lock-guard PUBLIC src)
target_compile_options(luxedm-lock-guard PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(luxedm-lock-guard PUBLIC X11::X11 Threads::Threads)
add_executable(luxedm-lock src/lock.cpp)
target_compile_features(luxedm-lock PRIVATE cxx_std_17)
target_compile_options(luxedm-lock PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(luxedm-lock PRIVATE luxedm-common luxedm-lock-guard X11::Xext)

install(TARGETS luxedm RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(TARGETS luxedm-lock RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES data/luxedm.conf DESTINATION ${CMAKE_INSTALL_FULL_SYSCONFDIR})
install(FILES data/luxedm-lock.conf DESTINATION ${CMAKE_INSTALL_FULL_SYSCONFDIR})
install(PROGRAMS data/luxedm-display-setup DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/luxedm)
install(FILES data/pam/luxedm DESTINATION ${CMAKE_INSTALL_FULL_SYSCONFDIR}/pam.d)
install(FILES data/pam/luxedm-lock DESTINATION ${CMAKE_INSTALL_FULL_SYSCONFDIR}/pam.d)
install(FILES data/systemd/luxedm.service DESTINATION ${CMAKE_INSTALL_LIBDIR}/systemd/system)
install(PROGRAMS data/luxedm-migrate-slim-config DESTINATION ${CMAKE_INSTALL_LIBEXECDIR})
install(PROGRAMS data/luxedm-set-default DESTINATION ${CMAKE_INSTALL_SBINDIR})
install(DIRECTORY data/themes/luxe DESTINATION ${CMAKE_INSTALL_DATADIR}/luxedm/themes
  PATTERN "*.svg" EXCLUDE)
install(FILES man/luxedm.1 DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)
install(FILES man/luxedm-lock.1 DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)
install(FILES man/luxedm-set-default.8 DESTINATION ${CMAKE_INSTALL_MANDIR}/man8)

if(BUILD_TESTING)
  add_executable(luxedm-lock-guard-test tests/lock-guard.cpp)
  target_link_libraries(luxedm-lock-guard-test PRIVATE luxedm-lock-guard X11::Xrandr)
  add_test(NAME luxedm-lock-protection COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/RunNestedX.sh" $<TARGET_FILE:luxedm-lock-guard-test>)
  set_tests_properties(luxedm-lock-protection PROPERTIES TIMEOUT 20)
  add_executable(luxedm-pam-account tests/pam-account.cpp)
  target_link_libraries(luxedm-pam-account PRIVATE luxedm-common)
  target_link_options(luxedm-pam-account PRIVATE
    -Wl,--wrap=pam_start,--wrap=pam_end,--wrap=pam_acct_mgmt,--wrap=pam_strerror)
  add_test(NAME luxedm-pam-account COMMAND luxedm-pam-account)
  add_test(NAME luxedm-version COMMAND luxedm -v)
  add_executable(luxedm-core-tests tests/core-tests.cpp)
  target_compile_features(luxedm-core-tests PRIVATE cxx_std_17)
  target_compile_definitions(luxedm-core-tests PRIVATE
    LUXEDM_TEST_THEME="${CMAKE_CURRENT_SOURCE_DIR}/data/themes/luxe/panel.png")
  target_link_libraries(luxedm-core-tests PRIVATE luxedm-common)
  add_test(NAME luxedm-core COMMAND luxedm-core-tests)
  add_executable(luxedm-session-process-test tests/session-process.cpp)
  target_link_libraries(luxedm-session-process-test PRIVATE luxedm-session-process)
  add_test(NAME luxedm-session-process COMMAND luxedm-session-process-test)
  add_executable(luxedm-panel-resources tests/panel-resources.cpp)
  target_link_libraries(luxedm-panel-resources PRIVATE luxedm-common)
  add_test(NAME luxedm-restart-limit COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/tests/restart-limit.sh"
    "${CMAKE_CURRENT_SOURCE_DIR}/data/systemd/luxedm.service")
  set_tests_properties(luxedm-restart-limit PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 30)
  add_test(NAME luxedm-greeter-resource-lifetime COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/RunNestedX.sh" "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/ProfileDMResources.sh"
    $<TARGET_FILE:luxedm-panel-resources> "${CMAKE_CURRENT_SOURCE_DIR}/data/themes/luxe"
    "${CMAKE_CURRENT_BINARY_DIR}/resources-test.json" --release)
  add_executable(luxedm-pam-lifetime tests/pam-lifetime.cpp src/app.cpp src/numlock.cpp src/switchuser.cpp)
  target_link_libraries(luxedm-pam-lifetime PRIVATE luxedm-common luxedm-session-process X11::Xmu)
  add_test(NAME luxedm-pam-resource-lifetime COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/RunNestedX.sh" $<TARGET_FILE:luxedm-pam-lifetime>
    "${CMAKE_CURRENT_SOURCE_DIR}/data/themes/luxe")
  set_tests_properties(luxedm-pam-resource-lifetime PROPERTIES ENVIRONMENT "LUXE_TEST_SCREEN=1366x768")
  set_tests_properties(luxedm-pam-resource-lifetime PROPERTIES TIMEOUT 30)
  add_test(NAME luxedm-preview-size
    COMMAND ${CMAKE_COMMAND}
      -DLUXEDM=$<TARGET_FILE:luxedm>
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/check-preview-size.cmake)
  add_test(NAME luxedm-service-command
    COMMAND ${CMAKE_COMMAND}
      -DSERVICE=${CMAKE_CURRENT_SOURCE_DIR}/data/systemd/luxedm.service
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/check-service.cmake)
  add_test(NAME luxedm-migration-security
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/tests/check-migration.sh
      ${CMAKE_CURRENT_SOURCE_DIR}/data/luxedm-migrate-slim-config
      ${CMAKE_CURRENT_SOURCE_DIR}/data/luxedm.conf)
  add_test(NAME luxedm-root-shell
    COMMAND ${CMAKE_COMMAND}
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/check-root-shell.cmake)
  add_test(NAME luxedm-pam-conversation
    COMMAND ${CMAKE_COMMAND}
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/check-pam-conversation.cmake)
  add_test(NAME luxedm-grabs
    COMMAND ${CMAKE_COMMAND}
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/check-grabs.cmake)
  add_test(NAME luxedm-lock-signals
    COMMAND ${CMAKE_COMMAND}
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/check-lock-signals.cmake)
endif()

set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
  "${CMAKE_CURRENT_SOURCE_DIR}/packaging/conffiles;${CMAKE_CURRENT_SOURCE_DIR}/packaging/config;${CMAKE_CURRENT_SOURCE_DIR}/packaging/templates;${CMAKE_CURRENT_SOURCE_DIR}/packaging/postinst;${CMAKE_CURRENT_SOURCE_DIR}/packaging/prerm;${CMAKE_CURRENT_SOURCE_DIR}/packaging/postrm")
luxe_configure_package(
  NAME luxedm VERSION ${PROJECT_VERSION} DEB_VERSION ${LUXEDM_DEB_VERSION}
  RPM_VERSION ${PROJECT_VERSION} RPM_RELEASE ${LUXEDM_RPM_RELEASE}
  DESCRIPTION "Lightweight X11 display manager and screen locker for Luxe"
  SECTION x11 LICENSE GPL-2.0-or-later
  # Native distro cohorts must depend on the actual linked JPEG/PNG ABI.
  # Ubuntu uses libjpeg-turbo8, not Debian's libjpeg62-turbo. Let the native
  # symbols metadata describe all ELF dependencies rather than guessing them.
  DEB_SHLIBDEPS ON
  # xserver-xorg-core can render the greeter without loading any input driver.
  # A minimal install then ignores every keyboard/touchpad event in Xorg.
  DEB_DEPENDS dbus debconf "default-logind | logind" libpam-runtime pkexec xauth
              "xserver-xorg-core | xserver" xserver-xorg-input-libinput
              x11-xserver-utils
  DEB_RECOMMENDS xterm
  DEB_PROVIDES x-display-manager DEB_CONFLICTS slim DEB_REPLACES slim
  RPM_REQUIRES dbus pam polkit systemd xauth xrandr
              xorg-x11-server-Xorg xorg-x11-drv-libinput)
