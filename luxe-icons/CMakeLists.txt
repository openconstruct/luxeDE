cmake_minimum_required(VERSION 3.25)
project(luxe-icons VERSION 2026.09.09 LANGUAGES NONE)
set(ICON_LUXE_REVISION 12)
include(../cmake/LuxePackage.cmake)
include(CTest)
find_program(XEPHYR_BIN Xephyr)
set(icon_lookup_tests OFF)
if(BUILD_TESTING AND XEPHYR_BIN)
  set(icon_lookup_tests ON)
endif()

set(LUXE_HOST_CXX_COMPILER "" CACHE FILEPATH "Host C++17 compiler used for Luxe icon tools")
if(CMAKE_CROSSCOMPILING AND NOT LUXE_HOST_CXX_COMPILER)
  message(FATAL_ERROR "Cross builds must set LUXE_HOST_CXX_COMPILER to a runnable host C++17 compiler")
elseif(NOT LUXE_HOST_CXX_COMPILER)
  find_program(detected_host_cxx NAMES c++ g++ clang++)
  set(LUXE_HOST_CXX_COMPILER "${detected_host_cxx}")
endif()
if(NOT LUXE_HOST_CXX_COMPILER OR NOT EXISTS "${LUXE_HOST_CXX_COMPILER}")
  message(FATAL_ERROR "A host C++17 compiler is required to build the icon tools")
endif()

set(icon_host_build "${CMAKE_CURRENT_BINARY_DIR}/native-tools-host")
set(icon_host_configure_args
  "-S" "${CMAKE_CURRENT_SOURCE_DIR}/../tools/native/icons"
  "-B" "${icon_host_build}"
  "-DCMAKE_CXX_COMPILER=${LUXE_HOST_CXX_COMPILER}"
  "-DBUILD_TESTING=${BUILD_TESTING}"
  "-DBUILD_LOOKUP_TESTS=${icon_lookup_tests}")
set(icon_native_deps "${CMAKE_CURRENT_SOURCE_DIR}/../build/native-deps/usr")
if(EXISTS "${icon_native_deps}/share/cmake/nlohmann_json/nlohmann_jsonConfig.cmake")
  list(APPEND icon_host_configure_args "-DCMAKE_PREFIX_PATH=${icon_native_deps}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" ${icon_host_configure_args}
  RESULT_VARIABLE icon_host_configure_result)
if(NOT icon_host_configure_result EQUAL 0)
  message(FATAL_ERROR "Could not configure Luxe icon host tools")
endif()

set(icon_deduplicator "${icon_host_build}/deduplicate-icons")
set(icon_host_targets deduplicate-icons)
set(icon_host_outputs "${icon_deduplicator}")
if(BUILD_TESTING)
  list(APPEND icon_host_targets icon-fixtures-gtk4)
  list(APPEND icon_host_outputs "${icon_host_build}/icon-fixtures-gtk4")
endif()
if(icon_lookup_tests)
  list(APPEND icon_host_targets run-nested-x)
  list(APPEND icon_host_outputs "${icon_host_build}/run-nested-x")
endif()
add_custom_command(OUTPUT ${icon_host_outputs}
  COMMAND "${CMAKE_COMMAND}" --build "${icon_host_build}" --target ${icon_host_targets}
    --parallel 2
  DEPENDS ../tools/native/icons/CMakeLists.txt ../tools/native/deduplicate-icons.cc
    ../tools/native/icon-fixtures.cc ../tools/native/diagnostic-common.hh
  VERBATIM)
add_custom_target(luxe-icon-host-tools DEPENDS ${icon_host_outputs})

set(icon_root "${CMAKE_CURRENT_BINARY_DIR}/theme/usr/share/icons")
set(icon_stamp "${CMAKE_CURRENT_BINARY_DIR}/theme/.built")
add_custom_command(
  OUTPUT "${icon_stamp}"
  COMMAND "${CMAKE_COMMAND}"
    "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
    "-DBUILD_ROOT=${CMAKE_CURRENT_BINARY_DIR}"
    "-DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/theme"
    "-DICON_DEDUPLICATOR=${icon_deduplicator}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/BuildIcons.cmake"
  DEPENDS cmake/BuildIcons.cmake cmake/Deduplicate.cmake
    ../tools/native/icons/CMakeLists.txt ../tools/native/deduplicate-icons.cc sources.lock.json aliases.json coverage.json icon-ownership.json
    artwork/lucid.svg artwork/pcmanager.svg artwork/shotty.svg artwork/frame.svg artwork/matinee.svg
    upstream/Papirus.tar.gz upstream/Numix-circle.tar.gz
  VERBATIM)
add_custom_target(luxe-icon-themes ALL DEPENDS "${icon_stamp}")
add_dependencies(luxe-icon-themes luxe-icon-host-tools)
if(BUILD_TESTING)
  add_test(NAME luxe-icons-cache-path COMMAND sh
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/check-cache-path.sh" "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_COMMAND}")
  add_test(NAME luxe-icons-preserve-output COMMAND sh
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/check-output-preserved.sh" "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_COMMAND}")
  add_test(NAME luxe-icons-coverage COMMAND "${icon_host_build}/icon-fixtures-gtk4"
    coverage ${CMAKE_CURRENT_BINARY_DIR}/theme ${CMAKE_CURRENT_SOURCE_DIR} "${icon_deduplicator}")
  if(icon_lookup_tests)
    add_test(NAME luxe-icons-lookup COMMAND "${icon_host_build}/run-nested-x"
      "${icon_host_build}/icon-fixtures-gtk4" lookup ${CMAKE_CURRENT_BINARY_DIR}/theme
      ${CMAKE_CURRENT_SOURCE_DIR} "${icon_deduplicator}")
  endif()
endif()
install(DIRECTORY "${icon_root}/" DESTINATION ${CMAKE_INSTALL_DATADIR}/icons)
install(FILES aliases.json icon-ownership.json coverage.json sources.lock.json theme-comparison.json
  AUTHORS-Papirus COPYRIGHT LICENSE-Numix-circle LICENSE-Papirus
  DESTINATION ${CMAKE_INSTALL_DOCDIR})
install(FILES artwork/README.md DESTINATION ${CMAKE_INSTALL_DOCDIR} RENAME ARTWORK.md)

set(ICON_CACHE_DIR_SHELL "${CMAKE_INSTALL_FULL_DATADIR}/icons")
string(REPLACE "\\" "\\\\" ICON_CACHE_DIR_SHELL "${ICON_CACHE_DIR_SHELL}")
string(REPLACE "$" "\\$" ICON_CACHE_DIR_SHELL "${ICON_CACHE_DIR_SHELL}")
string(REPLACE "`" "\\`" ICON_CACHE_DIR_SHELL "${ICON_CACHE_DIR_SHELL}")
string(REPLACE "\"" "\\\"" ICON_CACHE_DIR_SHELL "${ICON_CACHE_DIR_SHELL}")
configure_file(cmake/update-icon-cache.sh ${CMAKE_CURRENT_BINARY_DIR}/postinst @ONLY)
configure_file(cmake/update-icon-cache.sh ${CMAKE_CURRENT_BINARY_DIR}/postrm @ONLY)
set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
  "${CMAKE_CURRENT_BINARY_DIR}/postinst;${CMAKE_CURRENT_BINARY_DIR}/postrm")
set(CPACK_RPM_POST_INSTALL_SCRIPT_FILE "${CMAKE_CURRENT_BINARY_DIR}/postinst")
set(CPACK_RPM_POST_UNINSTALL_SCRIPT_FILE "${CMAKE_CURRENT_BINARY_DIR}/postrm")
# SVG-heavy themes compress substantially better as one XZ stream than with
# CPack's default gzip payload.
set(CPACK_DEBIAN_COMPRESSION_TYPE xz)
luxe_configure_package(
  NAME luxe-icons VERSION ${PROJECT_VERSION} DEB_VERSION ${PROJECT_VERSION}-${ICON_LUXE_REVISION}
  RPM_VERSION ${PROJECT_VERSION} RPM_RELEASE ${ICON_LUXE_REVISION} NOARCH
  DESCRIPTION "Comprehensive flat icon themes for the Luxe desktop" SECTION x11 LICENSE GPL-3.0-or-later
  DEB_DEPENDS hicolor-icon-theme DEB_RECOMMENDS adwaita-icon-theme
  RPM_REQUIRES hicolor-icon-theme adwaita-icon-theme)
