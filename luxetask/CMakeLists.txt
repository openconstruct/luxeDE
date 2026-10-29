cmake_minimum_required(VERSION 3.25)
project(luxetask VERSION 0.1.9 LANGUAGES C)
set(LUXETASK_REVISION 18)
set(LUXETASK_PACKAGE_VERSION "${PROJECT_VERSION}+gtk4-1+luxe${LUXETASK_REVISION}")
include(../cmake/LuxePackage.cmake)
luxe_use_shared_headers()

pkg_check_modules(GTK4 REQUIRED IMPORTED_TARGET gtk4)
luxe_plain_gtk_ui()
add_executable(luxetask src/main.c src/callbacks.c src/functions.c src/interface.c
  src/xfce-taskmanager-linux.c src/utils.c)
target_compile_features(luxetask PRIVATE c_std_11)
target_compile_options(luxetask PRIVATE -Wall -Wextra)
# No autotools config.h remains; the sources only need ENABLE_NLS (NLS
# gate) plus the package macros below, all provided here via CMake.
target_compile_definitions(luxetask PRIVATE ENABLE_GTK4 ENABLE_NLS
  GETTEXT_PACKAGE="luxetask" VERSION="${LUXETASK_PACKAGE_VERSION}"
  PACKAGE_LOCALE_DIR="${CMAKE_INSTALL_FULL_LOCALEDIR}" G_LOG_DOMAIN="luxetask")
target_include_directories(luxetask PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" src)
target_link_libraries(luxetask PRIVATE PkgConfig::GTK4 luxe-gtk-ui m)
luxe_compile_translations(luxetask)

include(CTest)
if(BUILD_TESTING)
  add_executable(test-pid-protection tests/test-pid-protection.c src/xfce-taskmanager-linux.c)
  target_compile_features(test-pid-protection PRIVATE c_std_11)
  target_compile_definitions(test-pid-protection PRIVATE ENABLE_GTK4 ENABLE_NLS
    GETTEXT_PACKAGE="luxetask" VERSION="${LUXETASK_PACKAGE_VERSION}"
    PACKAGE_LOCALE_DIR="${CMAKE_INSTALL_FULL_LOCALEDIR}" G_LOG_DOMAIN="luxetask")
  target_include_directories(test-pid-protection PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" src)
  target_link_libraries(test-pid-protection PRIVATE PkgConfig::GTK4 m)
  add_test(NAME pid-protection COMMAND test-pid-protection)
  add_executable(task-behavior tests/behavior.c)
  target_compile_definitions(task-behavior PRIVATE GETTEXT_PACKAGE="luxetask"
    VERSION="${LUXETASK_PACKAGE_VERSION}")
  target_include_directories(task-behavior PRIVATE src)
  target_link_libraries(task-behavior PRIVATE PkgConfig::GTK4 luxe-gtk-ui m)
  add_test(NAME task-behavior COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/RunNestedX.sh" $<TARGET_FILE:task-behavior>)
endif()

install(TARGETS luxetask RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES data/luxetask.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
luxe_install_symlink(luxetask ${CMAKE_INSTALL_BINDIR}/lxtask)

luxe_configure_package(
  NAME luxetask VERSION ${PROJECT_VERSION} DEB_VERSION ${LUXETASK_PACKAGE_VERSION} RPM_RELEASE 1.luxe${LUXETASK_REVISION}
  DESCRIPTION "GTK4 process manager for the Luxe desktop" SECTION utils LICENSE GPL-2.0-or-later
  DEB_RECOMMENDS luxe-icons
  DEB_PROVIDES lxtask lxtask-gtk4 DEB_CONFLICTS lxtask lxtask-gtk4
  DEB_REPLACES lxtask lxtask-gtk4 RPM_REQUIRES gtk4)
