# Copyright (C) 2026 Jerry Howell
# SPDX-License-Identifier: GPL-2.0-or-later

include(CMakeParseArguments)

function(luxe_add_internal_desktop_libraries prefix libfm_source menu_cache_source)
  file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/${prefix}-include")
  file(CREATE_LINK "${libfm_source}/src"
    "${CMAKE_CURRENT_BINARY_DIR}/${prefix}-include/libfm" SYMBOLIC)
  pkg_check_modules(${prefix}_GLIB REQUIRED IMPORTED_TARGET
    glib-2.0 gio-2.0 gio-unix-2.0 gobject-2.0 gmodule-2.0 gthread-2.0)

  add_library(${prefix}-menu-cache STATIC
    "${menu_cache_source}/libmenu-cache/menu-cache.c")
  set_target_properties(${prefix}-menu-cache PROPERTIES POSITION_INDEPENDENT_CODE ON)
  target_compile_definitions(${prefix}-menu-cache PRIVATE
    MENUCACHE_LIBEXECDIR="/usr/${CMAKE_INSTALL_LIBEXECDIR}/${prefix}/menu-cache"
    G_LOG_DOMAIN="Menu-Cache")
  target_include_directories(${prefix}-menu-cache PUBLIC
    "${menu_cache_source}" "${menu_cache_source}/libmenu-cache")
  target_link_libraries(${prefix}-menu-cache PUBLIC PkgConfig::${prefix}_GLIB)

  add_library(${prefix}-fm-extra STATIC "${libfm_source}/src/extra/fm-xml-file.c")
  set_target_properties(${prefix}-fm-extra PROPERTIES POSITION_INDEPENDENT_CODE ON)
  target_compile_definitions(${prefix}-fm-extra PRIVATE GETTEXT_PACKAGE="libfm")
  target_include_directories(${prefix}-fm-extra PUBLIC
    "${libfm_source}/src" "${libfm_source}/src/extra")
  target_link_libraries(${prefix}-fm-extra PUBLIC PkgConfig::${prefix}_GLIB)

  set(libfm_sources
    fm.c glib-compat.c
    base/fm-action.c base/fm-app-info.c base/fm-archiver.c base/fm-bookmarks.c
    base/fm-config.c base/fm-dummy-monitor.c base/fm-file.c base/fm-file-info.c
    base/fm-file-launcher.c base/fm-folder.c base/fm-folder-config.c base/fm-icon.c
    base/fm-list.c base/fm-marshal.c base/fm-mime-type.c base/fm-module.c
    base/fm-monitor.c base/fm-nav-history.c base/fm-path.c base/fm-templates.c
    base/fm-terminal.c base/fm-thumbnail-loader.c base/fm-thumbnailer.c base/fm-utils.c
    job/fm-deep-count-job.c job/fm-dir-list-job.c job/fm-file-info-job.c
    job/fm-file-ops-job.c job/fm-file-ops-job-change-attr.c
    job/fm-file-ops-job-delete.c job/fm-file-ops-job-xfer.c job/fm-job.c
    job/fm-simple-job.c)
  list(TRANSFORM libfm_sources PREPEND "${libfm_source}/src/")
  add_library(${prefix}-fm STATIC ${libfm_sources})
  set_target_properties(${prefix}-fm PROPERTIES POSITION_INDEPENDENT_CODE ON)
  target_compile_definitions(${prefix}-fm PRIVATE
    HAVE_CONFIG_H GETTEXT_PACKAGE="libfm"
    PACKAGE_LOCALE_DIR="${CMAKE_INSTALL_FULL_LOCALEDIR}"
    PACKAGE_DATA_DIR="${CMAKE_INSTALL_FULL_DATADIR}/libfm"
    PACKAGE_MODULES_DIR="${CMAKE_INSTALL_FULL_LIBDIR}/${prefix}/modules"
    PACKAGE_THEME_DIR="${CMAKE_INSTALL_FULL_DATADIR}/libfm/ui"
    PACKAGE_VERSION="1.4.1")
  target_include_directories(${prefix}-fm PUBLIC
    "${CMAKE_CURRENT_BINARY_DIR}/${prefix}-include"
    "${libfm_source}" "${libfm_source}/src" "${libfm_source}/src/base"
    "${libfm_source}/src/job" "${menu_cache_source}/libmenu-cache")
  target_link_libraries(${prefix}-fm PUBLIC PkgConfig::${prefix}_GLIB ${prefix}-menu-cache ${CMAKE_DL_LIBS} m)

  add_executable(${prefix}-menu-cache-gen
    "${menu_cache_source}/menu-cache-gen/main.c"
    "${menu_cache_source}/menu-cache-gen/menu-merge.c"
    "${menu_cache_source}/menu-cache-gen/menu-compose.c")
  target_compile_options(${prefix}-menu-cache-gen PRIVATE -fcommon)
  target_include_directories(${prefix}-menu-cache-gen PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/${prefix}-include"
    "${menu_cache_source}" "${menu_cache_source}/libmenu-cache"
    "${menu_cache_source}/menu-cache-gen")
  target_link_libraries(${prefix}-menu-cache-gen PRIVATE
    PkgConfig::${prefix}_GLIB ${prefix}-fm-extra)
  set_target_properties(${prefix}-menu-cache-gen PROPERTIES OUTPUT_NAME menu-cache-gen)
  add_executable(${prefix}-menu-cached "${menu_cache_source}/menu-cache-daemon/menu-cached.c")
  target_compile_definitions(${prefix}-menu-cached PRIVATE
    MENUCACHE_LIBEXECDIR="/usr/${CMAKE_INSTALL_LIBEXECDIR}/${prefix}/menu-cache")
  target_include_directories(${prefix}-menu-cached PRIVATE
    "${menu_cache_source}" "${menu_cache_source}/libmenu-cache")
  target_link_libraries(${prefix}-menu-cached PRIVATE PkgConfig::${prefix}_GLIB)
  set_target_properties(${prefix}-menu-cached PROPERTIES OUTPUT_NAME menu-cached)
endfunction()

function(luxe_install_internal_menu_helpers prefix)
  install(TARGETS ${prefix}-menu-cache-gen ${prefix}-menu-cached
    RUNTIME DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/${prefix}/menu-cache)
endfunction()

function(luxe_install_internal_desktop_licenses libfm_source menu_cache_source)
  if(EXISTS "${libfm_source}/COPYING")
    install(FILES "${libfm_source}/COPYING"
      DESTINATION "${CMAKE_INSTALL_DOCDIR}" RENAME libfm-COPYING)
  endif()
  if(EXISTS "${menu_cache_source}/COPYING")
    install(FILES "${menu_cache_source}/COPYING"
      DESTINATION "${CMAKE_INSTALL_DOCDIR}" RENAME menu-cache-COPYING)
  endif()
endfunction()
