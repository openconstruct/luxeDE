cmake_minimum_required(VERSION 3.25)
project(luxe_icon_host_tools LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
add_compile_options(-Wall -Wextra -Wpedantic)

add_executable(deduplicate-icons ../deduplicate-icons.cc)
if(BUILD_LOOKUP_TESTS)
  find_package(X11 REQUIRED)
  add_executable(run-nested-x ../run-nested-x.cc)
  target_link_libraries(run-nested-x PRIVATE X11::X11)
endif()

if(BUILD_TESTING)
  find_package(PkgConfig REQUIRED)
  find_package(nlohmann_json CONFIG REQUIRED)
  pkg_check_modules(GTK4 REQUIRED IMPORTED_TARGET gtk4)
  add_executable(icon-fixtures-gtk4 ../icon-fixtures.cc)
  target_include_directories(icon-fixtures-gtk4 PRIVATE "${CMAKE_CURRENT_LIST_DIR}/..")
  target_link_libraries(icon-fixtures-gtk4 PRIVATE
    nlohmann_json::nlohmann_json PkgConfig::GTK4)
endif()
