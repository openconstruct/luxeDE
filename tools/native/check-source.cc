// Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <set>
#include <sstream>
namespace fs = std::filesystem;
int main(int argc, char** argv) {
  try {
    if (argc != 2) throw std::runtime_error("Usage: check-source REPOSITORY");
    const auto root = fs::canonical(argv[1]);
    std::ifstream input(root / "cmake/LuxeComponents.cmake");
    std::string text((std::istreambuf_iterator<char>(input)), {});
    text = std::regex_replace(text, std::regex("#[^\n]*"), "");
    std::smatch match;
    if (!std::regex_search(text, match, std::regex("set\\(LUXE_COMPONENTS\\s+([^)]*)\\)")))
      throw std::runtime_error("Missing LUXE_COMPONENTS declaration");
    std::istringstream names(match[1]);
    std::set<std::string> components;
    std::string name;
    while (names >> name)
      if (!std::regex_match(name, std::regex("[a-z][a-z0-9-]*")) || !components.insert(name).second)
        throw std::runtime_error("Invalid or duplicate component: " + name);
    if (components.empty()) throw std::runtime_error("Empty component list");
    int errors = 0;
    auto problem = [&](const fs::path& p, const char* why) {
      std::cerr << p.lexically_relative(root).string() << ": " << why << '\n'; ++errors;
    };
    const std::set<std::string> dirs = {".git", ".deps", "build", "build-cmake", "CMakeFiles", "__pycache__", ".pytest_cache", "autom4te.cache"};
    const std::set<std::string> suffixes = {".o", ".a", ".so", ".deb", ".rpm", ".pyc", ".bak", ".rej"};
    const std::set<std::string> files = {"CMakeCache.txt", "cmake_install.cmake", "CPackConfig.cmake", "build.ninja"};
    for (const auto& component : components) {
      auto dir = root / component;
      if (!fs::is_regular_file(dir / "CMakeLists.txt")) { problem(dir, "missing CMakeLists.txt"); continue; }
      for (const auto& entry : fs::recursive_directory_iterator(dir)) {
        const auto& p = entry.path();
        if (entry.is_symlink()) {
          if (!fs::exists(p)) problem(p, "broken symlink");
          else {
            auto relative = fs::canonical(p).lexically_relative(root);
            if (relative.empty() || *relative.begin() == "..") problem(p, "points outside the monorepo");
          }
        } else if (entry.is_directory()) {
          if (dirs.count(p.filename())) problem(p, "generated/cache directory");
        } else if (entry.is_regular_file()) {
          if (suffixes.count(p.extension()) || files.count(p.filename())) problem(p, "generated build artifact");
          std::ifstream file(p, std::ios::binary);
          if (!file) throw std::runtime_error("Cannot read " + p.string());
          char magic[4] = {}; file.read(magic, sizeof magic);
          if (std::string(magic, 4) == std::string("\177ELF", 4)) problem(p, "compiled ELF/crash dump");
        }
      }
    }
    if (errors) return 1;
    std::cout << "PASS: " << components.size() << " package source directories are clean\n";
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
