# Copyright (C) 2026 Jerry Howell
# SPDX-License-Identifier: GPL-2.0-or-later

cmake_minimum_required(VERSION 3.25)
project(luxurystore VERSION 0.1.0 LANGUAGES C CXX)
set(LUXURYSTORE_REVISION 33)
set(LUXURYSTORE_RELEASE "${PROJECT_VERSION}-${LUXURYSTORE_REVISION}")
include(../cmake/LuxePackage.cmake)

set(LUXURYSTORE_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/synaptic-0.91.7")
set(LUXURYSTORE_GTK4 "${LUXURYSTORE_SOURCE}/gtk4")
set(LUXURYSTORE_APT6_ROOT "" CACHE PATH "Optional extracted Ubuntu libapt-pkg 6.0 development prefix")
set(LUXURYSTORE_APT7_ROOT "" CACHE PATH "Optional extracted Debian libapt-pkg 7.0 development prefix")
pkg_check_modules(GTK4 REQUIRED IMPORTED_TARGET gtk4)
luxe_plain_gtk_ui()
pkg_check_modules(VTE REQUIRED IMPORTED_TARGET vte-2.91-gtk4)
pkg_check_modules(GLIB REQUIRED IMPORTED_TARGET glib-2.0)
pkg_check_modules(GIO_UNIX REQUIRED IMPORTED_TARGET gio-unix-2.0)
function(luxurystore_find_apt_abi abi root out_include out_library)
  unset(apt_include CACHE)
  unset(apt_include)
  unset(apt_library CACHE)
  unset(apt_library)
  if(NOT "${root}" STREQUAL "")
    find_path(apt_include apt-pkg/init.h
      HINTS "${root}/usr/include" REQUIRED NO_DEFAULT_PATH)
  else()
    find_path(apt_include apt-pkg/init.h)
  endif()
  if(NOT apt_include)
    message(FATAL_ERROR "APT ${abi}.0 headers are unavailable; set LUXURYSTORE_APT${abi}_ROOT")
  endif()
  find_file(apt_library NAMES "libapt-pkg.so.${abi}.0"
    PATHS "${root}/usr/lib/${CMAKE_LIBRARY_ARCHITECTURE}"
          "/usr/lib/${CMAKE_LIBRARY_ARCHITECTURE}" REQUIRED NO_DEFAULT_PATH)
  set(APT_ABI_CHECK_SOURCE "${CMAKE_BINARY_DIR}/luxurystore-apt-abi-${abi}.cpp")
  file(WRITE "${APT_ABI_CHECK_SOURCE}"
    "#include <apt-pkg/macros.h>\n#if APT_PKG_MAJOR != ${abi}\n#error wrong APT header ABI\n#endif\nint main(void) { return 0; }\n")
  try_compile(APT${abi}_HEADER_LIBRARY_MATCH "${CMAKE_BINARY_DIR}/luxurystore-apt-abi-${abi}-build"
    SOURCES "${APT_ABI_CHECK_SOURCE}"
    CMAKE_FLAGS "-DINCLUDE_DIRECTORIES=${apt_include}"
    LINK_LIBRARIES "${apt_library}"
    OUTPUT_VARIABLE APT${abi}_TRY_COMPILE_OUTPUT)
  if(NOT APT${abi}_HEADER_LIBRARY_MATCH)
    message(FATAL_ERROR "APT ${abi}.0 headers/library do not form a usable ABI pair: ${APT${abi}_TRY_COMPILE_OUTPUT}")
  endif()
  set(${out_include} "${apt_include}" PARENT_SCOPE)
  set(${out_library} "${apt_library}" PARENT_SCOPE)
endfunction()

luxurystore_find_apt_abi(6 "${LUXURYSTORE_APT6_ROOT}" APT6_INCLUDE_DIR APT6_PKG_LIBRARY)
luxurystore_find_apt_abi(7 "${LUXURYSTORE_APT7_ROOT}" APT7_INCLUDE_DIR APT7_PKG_LIBRARY)

set(SYNAPTIC_COMMON_SOURCES
  pkg_acqfile.cc rconfiguration.cc rinstallprogress.cc rpackage.cc
  rpackagestatus.cc rpackagecache.cc rpackagefilter.cc rpackagelister.cc
  rpackageview.cc rcdscanner.cc rpmindexcopy.cc raptoptions.cc rsources.cc
  rcacheactor.cc rpackagelistactor.cc rtagcollbuilder.cc rtagcollfilter.cc
  ruserdialog.cc sections_trans.cc)
list(TRANSFORM SYNAPTIC_COMMON_SOURCES PREPEND "${LUXURYSTORE_SOURCE}/common/")
function(add_luxurystore_abi abi apt_include apt_library)
  set(backend "luxurystore-apt-backend${abi}")
  set(ui "luxurystore-ui${abi}")
  set(helper "luxurystore-apt${abi}")
  add_library(${backend} STATIC ${SYNAPTIC_COMMON_SOURCES})
  target_compile_features(${backend} PUBLIC cxx_std_17)
  target_compile_definitions(${backend} PRIVATE HAVE_CONFIG_H)
  target_include_directories(${backend} PUBLIC "${LUXURYSTORE_GTK4}"
    "${LUXURYSTORE_SOURCE}/common" "${LUXURYSTORE_SOURCE}" "${apt_include}")
  target_link_libraries(${backend} PUBLIC "${apt_library}" PkgConfig::GLIB)
  add_executable(${ui} "${LUXURYSTORE_GTK4}/main.cc")
  target_compile_features(${ui} PRIVATE cxx_std_17)
  target_compile_definitions(${ui} PRIVATE HAVE_CONFIG_H
    LUXURYSTORE_HELPER_PATH="${CMAKE_INSTALL_FULL_LIBEXECDIR}/luxurystore-apt"
    LUXURYSTORE_VERSION="${LUXURYSTORE_RELEASE}")
  target_link_libraries(${ui} PRIVATE ${backend} PkgConfig::GTK4 luxe-gtk-ui PkgConfig::VTE PkgConfig::GIO_UNIX)
  add_executable(${helper} "${LUXURYSTORE_GTK4}/helper.cc")
  target_compile_features(${helper} PRIVATE cxx_std_17)
  target_compile_definitions(${helper} PRIVATE HAVE_CONFIG_H)
  target_link_libraries(${helper} PRIVATE ${backend} PkgConfig::GLIB)
endfunction()

add_luxurystore_abi(6 "${APT6_INCLUDE_DIR}" "${APT6_PKG_LIBRARY}")
add_luxurystore_abi(7 "${APT7_INCLUDE_DIR}" "${APT7_PKG_LIBRARY}")
configure_file(data/luxurystore-launcher.in luxurystore-launcher @ONLY)
configure_file(data/luxurystore-helper-launcher.in luxurystore-helper-launcher @ONLY)
set(LUXURYSTORE_POLICY_PATH "${CMAKE_INSTALL_FULL_LIBEXECDIR}/luxurystore-apt")
configure_file(synaptic-0.91.7/gtk4/data/org.luxe.LuxuryStore.policy.in
  org.luxe.LuxuryStore.policy @ONLY)

include(CTest)
if(BUILD_TESTING)
  add_executable(luxurystore-repository-rewrite-test tests/repository-rewrite-test.cc)
  target_compile_features(luxurystore-repository-rewrite-test PRIVATE cxx_std_17)
  target_compile_definitions(luxurystore-repository-rewrite-test PRIVATE LUXURYSTORE_TESTING)
  target_link_libraries(luxurystore-repository-rewrite-test PRIVATE PkgConfig::GLIB)
  add_test(NAME luxurystore-repository-rewrite COMMAND luxurystore-repository-rewrite-test)
  add_test(NAME luxurystore-state-directory
    COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/tests/check-state-directory.sh"
      ${CMAKE_CURRENT_BINARY_DIR})
  add_test(NAME luxurystore-apt-abi
    COMMAND ${CMAKE_COMMAND}
      -DPROGRAM=$<TARGET_FILE:luxurystore-ui6>
      -DHELPER=$<TARGET_FILE:luxurystore-apt6>
      -DEXPECTED_SONAME=libapt-pkg.so.6.0
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/check-apt-abi.cmake)
  add_test(NAME luxurystore-apt7-abi
    COMMAND ${CMAKE_COMMAND}
      -DPROGRAM=$<TARGET_FILE:luxurystore-ui7>
      -DHELPER=$<TARGET_FILE:luxurystore-apt7>
      -DEXPECTED_SONAME=libapt-pkg.so.7.0
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/check-apt-abi.cmake)
  add_executable(luxurystore-tests "${LUXURYSTORE_GTK4}/tests.cc")
  target_compile_features(luxurystore-tests PRIVATE cxx_std_17)
  target_compile_definitions(luxurystore-tests PRIVATE HAVE_CONFIG_H
    LUXURYSTORE_VERSION="${LUXURYSTORE_RELEASE}" LUXURYSTORE_TESTING
    LUXURYSTORE_TEST_STDOUT_LIMIT=65536 LUXURYSTORE_TEST_COMMAND_TIMEOUT_SECONDS=1)
  target_link_libraries(luxurystore-tests PRIVATE luxurystore-apt-backend7 PkgConfig::GTK4 luxe-gtk-ui PkgConfig::VTE PkgConfig::GIO_UNIX)
  add_test(NAME backend
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/tests/run-gui-test.sh $<TARGET_FILE:luxurystore-tests>)
  set_tests_properties(backend PROPERTIES
    SKIP_RETURN_CODE 77
    ENVIRONMENT "LUXURYSTORE_TEST_HELPER=$<TARGET_FILE:luxurystore-apt7>")
  add_executable(luxurystore-tests6 "${LUXURYSTORE_GTK4}/tests.cc")
  target_compile_features(luxurystore-tests6 PRIVATE cxx_std_17)
  target_compile_definitions(luxurystore-tests6 PRIVATE HAVE_CONFIG_H
    LUXURYSTORE_VERSION="${LUXURYSTORE_RELEASE}" LUXURYSTORE_TESTING
    LUXURYSTORE_TEST_STDOUT_LIMIT=65536 LUXURYSTORE_TEST_COMMAND_TIMEOUT_SECONDS=1)
  target_link_libraries(luxurystore-tests6 PRIVATE luxurystore-apt-backend6 PkgConfig::GTK4 luxe-gtk-ui PkgConfig::VTE PkgConfig::GIO_UNIX)
  add_test(NAME backend-abi6
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/tests/run-gui-test.sh $<TARGET_FILE:luxurystore-tests6>)
  set_tests_properties(backend-abi6 PROPERTIES
    SKIP_RETURN_CODE 77
    ENVIRONMENT "LUXURYSTORE_TEST_HELPER=$<TARGET_FILE:luxurystore-apt6>")
  foreach(abi 6 7)
    if(abi EQUAL 6)
      set(source_test luxurystore-tests6)
    else()
      set(source_test luxurystore-tests)
    endif()
    add_test(NAME luxurystore-flatpak-default-${abi}
      COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/tests/handoff-fixture.sh"
        sh ${CMAKE_CURRENT_SOURCE_DIR}/tests/run-gui-test.sh $<TARGET_FILE:${source_test}>)
    set_tests_properties(luxurystore-flatpak-default-${abi} PROPERTIES
      SKIP_RETURN_CODE 77 ENVIRONMENT "LUXURYSTORE_TEST_FLATPAK=1")
  endforeach()
  add_test(NAME luxurystore-p1-workflows
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/tests/check-p1-workflows.sh
      ${LUXURYSTORE_GTK4}/main.cc
      ${LUXURYSTORE_GTK4}/helper.cc)
endif()

install(TARGETS luxurystore-ui6 luxurystore-ui7 luxurystore-apt6 luxurystore-apt7
  RUNTIME DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/luxurystore)
install(PROGRAMS "${CMAKE_CURRENT_BINARY_DIR}/luxurystore-launcher"
  DESTINATION ${CMAKE_INSTALL_BINDIR} RENAME luxurystore)
install(PROGRAMS "${CMAKE_CURRENT_BINARY_DIR}/luxurystore-helper-launcher"
  DESTINATION ${CMAKE_INSTALL_LIBEXECDIR} RENAME luxurystore-apt)
install(FILES "${LUXURYSTORE_GTK4}/data/luxurystore.desktop" DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES "${LUXURYSTORE_GTK4}/data/luxurystore.svg" DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/org.luxe.LuxuryStore.policy" DESTINATION ${CMAKE_INSTALL_DATADIR}/polkit-1/actions)
# /var is system state, not ${CMAKE_INSTALL_PREFIX}/var. With a /usr prefix,
# the relative GNUInstallDirs value packaged this under /usr/var instead.
install(DIRECTORY DESTINATION /var/lib/synaptic)

luxe_configure_package(
  NAME luxurystore VERSION ${PROJECT_VERSION} DEB_VERSION ${LUXURYSTORE_RELEASE} RPM_RELEASE ${LUXURYSTORE_REVISION}
  DESCRIPTION "GTK4 package manager for the Luxe desktop" SECTION admin LICENSE GPL-2.0-or-later
  DEB_SHLIBDEPS OFF
  DEB_DEPENDS pkexec coreutils "libapt-pkg6.0 (>= 2.7.14build2) | libapt-pkg6.0t64 (>= 2.7.14build2) | libapt-pkg7.0" "libc6 (>= 2.34)" "libgcc-s1 (>= 3.0)"
              "libglib2.0-0t64 (>= 2.80.0) | libglib2.0-0 (>= 2.80.0)" "libgtk-4-1 (>= 4.12.0)"
              "libstdc++6 (>= 13.1)" "libvte-2.91-gtk4-0 (>= 0.47.90)"
  DEB_RECOMMENDS luxe-icons DEB_SUGGESTS flatpak snapd
  RPM_REQUIRES gtk4  vte291 polkit coreutils)
