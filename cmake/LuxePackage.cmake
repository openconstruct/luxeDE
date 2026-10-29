# Copyright (C) 2026 Jerry Howell
# SPDX-License-Identifier: GPL-2.0-or-later

if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  set(CMAKE_INSTALL_PREFIX "/usr" CACHE PATH "Install prefix" FORCE)
endif()
set(CMAKE_INSTALL_LIBDIR "lib" CACHE PATH "Object-code libraries")
set(CMAKE_INSTALL_LIBEXECDIR "libexec" CACHE PATH "Private executables")
include(GNUInstallDirs)
include(CMakeParseArguments)
find_package(PkgConfig REQUIRED)

# Keep package-created directories deterministic even when the builder uses a
# restrictive or collaborative umask.
set(CMAKE_INSTALL_DEFAULT_DIRECTORY_PERMISSIONS
  OWNER_READ OWNER_WRITE OWNER_EXECUTE
  GROUP_READ GROUP_EXECUTE
  WORLD_READ WORLD_EXECUTE)
set(CPACK_INSTALL_DEFAULT_DIRECTORY_PERMISSIONS
  OWNER_READ OWNER_WRITE OWNER_EXECUTE
  GROUP_READ GROUP_EXECUTE
  WORLD_READ WORLD_EXECUTE)
set(CPACK_PRE_BUILD_SCRIPTS "${CMAKE_CURRENT_LIST_DIR}/FixPackagePermissions.cmake")

get_filename_component(LUXE_REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

include("${CMAKE_CURRENT_LIST_DIR}/NativeTestTools.cmake")

# Header-only helpers shared verbatim by several packages live in shared/.
# Opt in per package so a package that keeps its own variant of a helper is
# never shadowed by the shared file.
function(luxe_use_shared_headers)
  include_directories("${LUXE_REPOSITORY_ROOT}/shared")
endfunction()

# Small statically linked native GTK presentation patterns; no installed ABI.
function(luxe_plain_gtk_ui)
  luxe_use_shared_headers()
  if(NOT TARGET luxe-gtk-ui)
    add_library(luxe-gtk-ui STATIC "${LUXE_REPOSITORY_ROOT}/shared/luxe-gtk-ui.c")
    target_include_directories(luxe-gtk-ui PUBLIC "${LUXE_REPOSITORY_ROOT}/shared")
    target_link_libraries(luxe-gtk-ui PUBLIC PkgConfig::GTK4)
    target_compile_features(luxe-gtk-ui PRIVATE c_std_11)
    target_compile_options(luxe-gtk-ui PRIVATE -Wall -Wextra)
  endif()
endfunction()

function(luxe_compile_translations domain)
  find_program(MSGFMT_EXECUTABLE msgfmt)
  if(NOT MSGFMT_EXECUTABLE OR NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/po")
    return()
  endif()
  file(GLOB po_files CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/po/*.po")
  foreach(po_file IN LISTS po_files)
    get_filename_component(locale "${po_file}" NAME_WE)
    set(mo_file "${CMAKE_CURRENT_BINARY_DIR}/locale/${locale}/LC_MESSAGES/${domain}.mo")
    add_custom_command(
      OUTPUT "${mo_file}"
      COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/locale/${locale}/LC_MESSAGES"
      COMMAND "${MSGFMT_EXECUTABLE}" -o "${mo_file}" "${po_file}"
      DEPENDS "${po_file}"
      VERBATIM)
    list(APPEND mo_files "${mo_file}")
    install(FILES "${mo_file}" DESTINATION "${CMAKE_INSTALL_LOCALEDIR}/${locale}/LC_MESSAGES")
  endforeach()
  if(mo_files)
    add_custom_target(translations ALL DEPENDS ${mo_files})
  endif()
endfunction()

function(luxe_compile_resources output xml)
  find_program(GLIB_COMPILE_RESOURCES_EXECUTABLE glib-compile-resources REQUIRED)
  execute_process(
    COMMAND "${GLIB_COMPILE_RESOURCES_EXECUTABLE}" --generate-dependencies
            "--sourcedir=${CMAKE_CURRENT_SOURCE_DIR}" "${xml}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    OUTPUT_VARIABLE resource_dependencies
    OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY)
  string(REPLACE "\n" ";" resource_dependencies "${resource_dependencies}")
  add_custom_command(
    OUTPUT "${output}"
    COMMAND "${GLIB_COMPILE_RESOURCES_EXECUTABLE}" "${xml}"
            "--target=${output}" "--sourcedir=${CMAKE_CURRENT_SOURCE_DIR}" --generate-source
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${xml}" ${resource_dependencies}
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    VERBATIM)
endfunction()

function(luxe_validate_schema schema)
  find_program(GLIB_COMPILE_SCHEMAS_EXECUTABLE glib-compile-schemas REQUIRED)
  add_custom_target(validate-schema ALL
    COMMAND "${GLIB_COMPILE_SCHEMAS_EXECUTABLE}" --strict --dry-run
            "--schema-file=${CMAKE_CURRENT_SOURCE_DIR}/${schema}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${schema}"
    VERBATIM)
endfunction()

# Validate and install a GSettings schema, and remember that this package
# needs the glib-2.0-schemas trigger (Debian) and a schema-compile RPM
# scriptlet, both applied by luxe_configure_package.
function(luxe_install_gschema schema)
  find_program(GLIB_COMPILE_SCHEMAS_EXECUTABLE glib-compile-schemas REQUIRED)
  add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/.schema-validated"
    COMMAND "${GLIB_COMPILE_SCHEMAS_EXECUTABLE}" --strict --dry-run
            "--schema-file=${CMAKE_CURRENT_SOURCE_DIR}/${schema}"
    COMMAND "${CMAKE_COMMAND}" -E touch "${CMAKE_CURRENT_BINARY_DIR}/.schema-validated"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${schema}"
    VERBATIM)
  add_custom_target(validate-schema ALL
    DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/.schema-validated")
  install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/${schema}"
          DESTINATION "${CMAKE_INSTALL_DATADIR}/glib-2.0/schemas")
  set_property(GLOBAL APPEND PROPERTY LUXE_SCHEMA_CARRIERS TRUE)
endfunction()

# Validate every desktop entry the package ships (hints are non-fatal;
# errors fail the build). Session entries under data/xsessions/ are excluded:
# they legitimately carry DesktopNames, which the application-entry schema
# desktop-file-validate checks against rejects.
function(luxe_validate_desktop_files)
  find_program(DESKTOP_FILE_VALIDATE_EXECUTABLE desktop-file-validate)
  if(NOT DESKTOP_FILE_VALIDATE_EXECUTABLE)
    message(STATUS "desktop-file-validate not found; skipping desktop entry validation")
    return()
  endif()
  set(entries)
  foreach(glob_expr
      "${CMAKE_CURRENT_SOURCE_DIR}/*.desktop"
      "${CMAKE_CURRENT_SOURCE_DIR}/data/*.desktop"
      "${CMAKE_CURRENT_SOURCE_DIR}/data/desktop-directories/*.directory")
    file(GLOB matches CONFIGURE_DEPENDS "${glob_expr}")
    foreach(match IN LISTS matches)
      get_filename_component(filename "${match}" NAME)
      if(NOT filename STREQUAL "luxe.desktop") # xsessions entry (see above)
        list(APPEND entries "${match}")
      endif()
    endforeach()
  endforeach()
  if(NOT entries)
    return()
  endif()
  add_custom_target(validate-desktop ALL
    COMMAND "${CMAKE_COMMAND}" -E echo "Validating desktop entries:"
    COMMAND "${DESKTOP_FILE_VALIDATE_EXECUTABLE}" ${entries}
    DEPENDS ${entries}
    VERBATIM)
endfunction()

function(luxe_install_symlink target link_path)
  get_filename_component(link_dir "${link_path}" DIRECTORY)
  if(IS_ABSOLUTE "${link_path}")
    install(CODE
      "file(MAKE_DIRECTORY \"\$ENV{DESTDIR}${link_dir}\")\nfile(CREATE_LINK \"${target}\" \"\$ENV{DESTDIR}${link_path}\" SYMBOLIC)")
  else()
    install(CODE
      "file(MAKE_DIRECTORY \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${link_dir}\")\nfile(CREATE_LINK \"${target}\" \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${link_path}\" SYMBOLIC)")
  endif()
endfunction()

function(luxe_install_documentation package)
  foreach(document COPYING COPYING.md LICENSE LICENSE.md copyright UPSTREAM-CREDITS.txt TRANSLATOR-CREDITS.txt COPYRIGHT-DOCUMENTATION.txt)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${document}")
      install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/${document}"
              DESTINATION "${CMAKE_INSTALL_DOCDIR}")
    endif()
  endforeach()
  if(EXISTS "${LUXE_REPOSITORY_ROOT}/LUXE-BIBLE.md")
    install(FILES "${LUXE_REPOSITORY_ROOT}/LUXE-BIBLE.md"
            DESTINATION "${CMAKE_INSTALL_DOCDIR}")
  endif()
  set(credits_file "${CMAKE_CURRENT_BINARY_DIR}/${package}-LUXE-CREDITS")
  file(WRITE "${credits_file}"
    "Luxe integration, GTK4 porting, packaging, and distribution work:\n"
    "Copyright (C) 2026 Jerry Howell\n\n"
    "All upstream copyright notices and licenses remain in this package's documentation and source tree.\n")
  install(FILES "${credits_file}" DESTINATION "${CMAKE_INSTALL_DOCDIR}"
          RENAME LUXE-CREDITS)
endfunction()

macro(luxe_configure_package)
  set(options NOARCH NO_RPM)
  set(one_value NAME VERSION DEB_VERSION RPM_VERSION RPM_RELEASE DESCRIPTION SECTION LICENSE DEB_SHLIBDEPS)
  set(multi_value DEB_DEPENDS DEB_RECOMMENDS DEB_SUGGESTS DEB_PROVIDES DEB_CONFLICTS DEB_REPLACES RPM_REQUIRES RPM_SUGGESTS)
  cmake_parse_arguments(LUXE_PKG "${options}" "${one_value}" "${multi_value}" ${ARGN})

  if(NOT LUXE_PKG_NAME OR NOT LUXE_PKG_VERSION)
    message(FATAL_ERROR "luxe_configure_package requires NAME and VERSION")
  endif()
  if(NOT LUXE_PKG_DEB_VERSION)
    set(LUXE_PKG_DEB_VERSION "${LUXE_PKG_VERSION}")
  endif()
  if(NOT LUXE_PKG_RPM_VERSION)
    set(LUXE_PKG_RPM_VERSION "${LUXE_PKG_VERSION}")
  endif()
  if(NOT LUXE_PKG_RPM_RELEASE)
    set(LUXE_PKG_RPM_RELEASE "1")
  endif()

  if(LUXE_PKG_NO_RPM)
    set(CPACK_GENERATOR "DEB")
  else()
    set(CPACK_GENERATOR "DEB;RPM")
  endif()
  set(CPACK_PACKAGE_NAME "${LUXE_PKG_NAME}")
  set(CPACK_PACKAGE_VENDOR "Luxe")
  set(CPACK_PACKAGE_CONTACT "Jerry Howell <jerr@luxe.desktop>")
  set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${LUXE_PKG_DESCRIPTION}")
  set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/jerrimus/luxe")
  set(CPACK_PACKAGE_VERSION "${LUXE_PKG_VERSION}")
  set(CPACK_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}/packages")
  set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
  set(CPACK_SET_DESTDIR ON)
  set(CPACK_STRIP_FILES TRUE)

  set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
  set(CPACK_DEBIAN_PACKAGE_VERSION "${LUXE_PKG_DEB_VERSION}")
  set(CPACK_DEBIAN_PACKAGE_SECTION "${LUXE_PKG_SECTION}")
  set(CPACK_DEBIAN_PACKAGE_PRIORITY optional)
  if(NOT "${LUXE_PKG_DEB_SHLIBDEPS}" STREQUAL "")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS "${LUXE_PKG_DEB_SHLIBDEPS}")
  else()
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
  endif()
  string(JOIN ", " CPACK_DEBIAN_PACKAGE_DEPENDS ${LUXE_PKG_DEB_DEPENDS})
  string(JOIN ", " CPACK_DEBIAN_PACKAGE_RECOMMENDS ${LUXE_PKG_DEB_RECOMMENDS})
  string(JOIN ", " CPACK_DEBIAN_PACKAGE_SUGGESTS ${LUXE_PKG_DEB_SUGGESTS})
  string(JOIN ", " CPACK_DEBIAN_PACKAGE_PROVIDES ${LUXE_PKG_DEB_PROVIDES})
  string(JOIN ", " CPACK_DEBIAN_PACKAGE_CONFLICTS ${LUXE_PKG_DEB_CONFLICTS})
  string(JOIN ", " CPACK_DEBIAN_PACKAGE_REPLACES ${LUXE_PKG_DEB_REPLACES})

  set(CPACK_RPM_FILE_NAME RPM-DEFAULT)
  set(CPACK_RPM_PACKAGE_VERSION "${LUXE_PKG_RPM_VERSION}")
  set(CPACK_RPM_PACKAGE_RELEASE "${LUXE_PKG_RPM_RELEASE}")
  set(CPACK_RPM_PACKAGE_LICENSE "${LUXE_PKG_LICENSE}")
  set(CPACK_RPM_PACKAGE_GROUP "User Interface/Desktops")
  set(CPACK_RPM_PACKAGE_AUTOREQ ON)
  string(JOIN ", " CPACK_RPM_PACKAGE_REQUIRES ${LUXE_PKG_RPM_REQUIRES})
  string(JOIN ", " CPACK_RPM_PACKAGE_SUGGESTS ${LUXE_PKG_RPM_SUGGESTS})

  if(LUXE_PKG_NOARCH)
    set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE all)
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS OFF)
    set(CPACK_RPM_PACKAGE_ARCHITECTURE noarch)
  endif()

  get_property(luxe_schema_carrier GLOBAL PROPERTY LUXE_SCHEMA_CARRIERS)
  if(luxe_schema_carrier)
    if(NOT CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA)
      set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
          "${LUXE_REPOSITORY_ROOT}/cmake/debian-glib-schemas.triggers")
    endif()
    set(CPACK_RPM_POST_INSTALL_SCRIPT_FILE
        "${LUXE_REPOSITORY_ROOT}/cmake/rpm-glib-compile-schemas.script")
    set(CPACK_RPM_POST_UNINSTALL_SCRIPT_FILE
        "${LUXE_REPOSITORY_ROOT}/cmake/rpm-glib-compile-schemas.script")
  endif()

  luxe_install_documentation("${LUXE_PKG_NAME}")
  luxe_validate_desktop_files()
  include(CPack)
  luxe_prepare_native_test_tools()
endmacro()
