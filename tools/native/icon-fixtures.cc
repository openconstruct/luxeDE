// Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later
#include "diagnostic-common.hh"
#include <gtk/gtk.h>
#include <set>
static bool inside(const fs::path &p, const fs::path &root) {
  auto relative = p.lexically_relative(root);
  return !relative.empty() && *relative.begin() != ".." &&
         !relative.is_absolute();
}
static std::set<std::string> svg_names(const fs::path &root,
                                       bool recursive = true) {
  std::set<std::string> names;
  if (recursive) {
    for (const auto &e : fs::recursive_directory_iterator(root))
      if (e.path().extension() == ".svg")
        names.insert(e.path().stem().string());
  } else {
    for (const auto &e : fs::directory_iterator(root))
      if (e.path().extension() == ".svg")
        names.insert(e.path().stem().string());
  }
  return names;
}
static void coverage(const fs::path &root, const fs::path &source,
                     const fs::path &deduplicator) {
  auto icons = root / "usr/share/icons";
  require(fs::is_regular_file(root / ".built"),
          "Build did not validate successfully");
  for (const auto &name : {"Luxe", "Luxe-Light", "Luxe-Dark"}) {
    auto theme = icons / name;
    auto index = g_key_file_new();
    require(g_key_file_load_from_file(index, (theme / "index.theme").c_str(),
                                      G_KEY_FILE_NONE, nullptr),
            "Invalid index.theme");
    auto value =
        g_key_file_get_string(index, "Icon Theme", "Directories", nullptr);
    require(value, "Missing Directories");
    std::istringstream dirs(value);
    std::string directory;
    while (std::getline(dirs, directory, ','))
      require(g_key_file_has_group(index, directory.c_str()) &&
                  fs::is_directory(theme / directory),
              "Missing directory: " + directory);
    g_free(value);
    g_key_file_unref(index);
    for (auto &e : fs::recursive_directory_iterator(theme))
      if (e.is_symlink())
        require(fs::exists(e.path()) &&
                    inside(fs::canonical(e.path()), fs::canonical(icons)),
                "Unsafe alias: " + e.path().string());
    require(!fs::exists(theme / "icon-theme.cache"),
            "Build-host cache shipped");
  }
  auto base = icons / "Luxe", light = icons / "Luxe-Light";
  for (auto &e : fs::recursive_directory_iterator(base))
    if (e.path().extension() == ".svg") {
      auto other = light / e.path().lexically_relative(base);
      require(fs::exists(other) && read_file(e.path()) == read_file(other),
              "Light artwork differs: " + other.string());
    }
  auto provided = svg_names(icons);
  auto ownership = Json::parse(read_file(source / "icon-ownership.json"));
  for (auto &[name, owner] : ownership["fallback_owners"].items()) {
    auto owner_name = owner.get<std::string>();
    require(!owner_name.empty(), "Empty fallback owner: " + name);
    auto fallback =
        icons / "hicolor/scalable/apps" / (name + ".svg");
    if (owner_name == "luxe-icons") {
      require(fs::is_regular_file(fallback) || fs::is_symlink(fallback),
              "Missing Luxe Icons fallback: " + fallback.string());
    } else {
      require(!fs::exists(fallback) && !fs::is_symlink(fallback),
              "Non-Luxe package fallback duplicated: " + fallback.string());
    }
    require(fs::is_regular_file(base / "scalable/luxe-apps" /
                                (name + ".svg")),
            "Missing app artwork");
  }
  auto papirus =
      fs::directory_iterator(root / "sources/Papirus")->path() / "Papirus";
  auto numix = fs::directory_iterator(root / "sources/Numix-circle")->path() /
               "Numix-Circle/48/apps";
  auto required = svg_names(papirus);
  auto numix_names = svg_names(numix, false);
  required.insert(numix_names.begin(), numix_names.end());
  for (auto &name : required)
    require(provided.count(name), "Missing upstream icon: " + name);
  auto aliases = Json::parse(read_file(source / "aliases.json"));
  for (auto &[alias, target] : aliases.items()) {
    auto built = base / "scalable/luxe-apps" / (alias + ".svg");
    require(fs::is_regular_file(built), "Missing alias: " + alias);
    std::string dest = target;
    if (dest.rfind("luxe:", 0) == 0) {
      auto original = read_file(source / "artwork" / (dest.substr(5) + ".svg"));
      require(read_file(built) == original, "Changed custom artwork: " + alias);
      auto fallback = icons / "hicolor/scalable/apps" / (alias + ".svg");
      if (fs::exists(fallback))
        require(read_file(fallback) == original, "Changed fallback: " + alias);
    }
  }
  bool shared = false;
  for (auto &e : fs::recursive_directory_iterator(light))
    if (e.path().extension() == ".svg" && e.is_symlink())
      shared = true;
  require(shared, "No shared light artwork");
  Scratch scratch("luxe-icons-guards-");
  write_file(scratch.path / "keep", "preserve");
  auto script = source / "cmake/BuildIcons.cmake";
  Child guard({"cmake", "-DSOURCE_DIR=" + source.string(), "-DBUILD_ROOT=" + scratch.path.string(),
               "-DOUTPUT_DIR=" + scratch.path.string(), "-DICON_DEDUPLICATOR=" + deduplicator.string(),
               "-P", script.string()},
              {}, scratch.path / "guard.log");
  require(guard.wait(20) != 0 && read_file(scratch.path / "keep") == "preserve",
          "Destructive output guard failed");
  auto alias_root = scratch.path / "alias-root";
  fs::create_directory(alias_root);
  fs::create_directory_symlink(source, alias_root / "theme");
  Child alias({"cmake", "-DSOURCE_DIR=" + source.string(), "-DBUILD_ROOT=" + alias_root.string(),
               "-DOUTPUT_DIR=" + (alias_root / "theme").string(),
               "-DICON_DEDUPLICATOR=" + deduplicator.string(), "-P", script.string()},
              {}, scratch.path / "alias.log");
  require(alias.wait(20) != 0 && fs::is_symlink(alias_root / "theme") &&
          fs::exists(source / "CMakeLists.txt"), "Source alias was not rejected");
  auto output = scratch.path / "theme";
  fs::create_directory(output);
  write_file(output / "keep", "unowned");
  Child unowned({"cmake", "-DSOURCE_DIR=" + source.string(), "-DBUILD_ROOT=" + scratch.path.string(),
                 "-DOUTPUT_DIR=" + output.string(), "-DICON_DEDUPLICATOR=" + deduplicator.string(),
                 "-P", script.string()}, {}, scratch.path / "unowned.log");
  require(unowned.wait(20) != 0 && read_file(output / "keep") == "unowned",
          "Unowned theme was not preserved");
  auto dedup_output = scratch.path / "dedup";
  auto icon_root = dedup_output / "usr/share/icons";
  for (auto name : {"Luxe", "Luxe-Light", "Luxe-Dark"}) {
    fs::create_directories(icon_root / name);
    write_file(icon_root / name / "index.theme", "[Icon Theme]\n");
  }
  write_file(icon_root / "Luxe/a.svg", "same");
  write_file(icon_root / "Luxe/b.svg", "same");
  Child unsafe_dedup({deduplicator.string(), icon_root.string()},
                     {}, scratch.path / "dedup-unowned.log");
  require(unsafe_dedup.wait(60) != 0 && !fs::is_symlink(icon_root / "Luxe/b.svg") &&
          read_file(icon_root / "Luxe/b.svg") == "same", "Unowned dedup tree changed");
  write_file(dedup_output / ".luxe-icons-generated", "luxe-icons-generated-v1\n");
  Child safe_dedup({deduplicator.string(), icon_root.string()},
                   {}, scratch.path / "dedup-owned.log");
  require(safe_dedup.wait(60) == 0 && fs::is_symlink(icon_root / "Luxe/b.svg") &&
          read_file(icon_root / "Luxe/b.svg") == "same", "Owned dedup failed");
  write_file(output / ".luxe-icons-generated", "luxe-icons-generated-v1\n");
  write_file(output / ".built", "previous output");
  auto fake = scratch.path / "source";
  fs::create_directory(fake);
  auto lock = Json::parse(read_file(source / "sources.lock.json"));
  lock[0]["sha256"] = std::string(64, '0');
  write_file(fake / "sources.lock.json", lock.dump());
  fs::create_directory_symlink(source / "upstream", fake / "upstream");
  Child checksum({"cmake", "-DSOURCE_DIR=" + fake.string(), "-DBUILD_ROOT=" + scratch.path.string(),
                  "-DOUTPUT_DIR=" + (scratch.path / "theme").string(),
                  "-DICON_DEDUPLICATOR=" + deduplicator.string(), "-P",
                  script.string()},
                 {}, scratch.path / "checksum.log");
  require(
      checksum.wait(20) != 0 &&
          read_file(scratch.path / "checksum.log").find("Checksum mismatch") !=
              std::string::npos &&
          read_file(output / ".built") == "previous output" &&
          read_file(output / "keep") == "unowned",
      "Checksum rejection failed");
  std::cout << "PASS: upstream names, aliases, indexes, light artwork, safe "
               "links, ownership and output/checksum guards\n";
}
static void lookup(const fs::path &root, const fs::path &source,
                   const fs::path &deduplicator) {
  (void)deduplicator;
#if GTK_MAJOR_VERSION == 3
  gtk_init(nullptr, nullptr);
#else
  gtk_init();
#endif
  auto aliases = Json::parse(read_file(source / "aliases.json"));
  for (auto name : {"Luxe", "Luxe-Light", "Luxe-Dark"}) {
    auto theme = gtk_icon_theme_new();
    std::string search = (root / "usr/share/icons").string();
    const char *paths[] = {search.c_str(), nullptr};
#if GTK_MAJOR_VERSION == 3
    gtk_icon_theme_set_search_path(theme, paths, 1);
    gtk_icon_theme_set_custom_theme(theme, name);
#else
    gtk_icon_theme_set_search_path(theme, paths);
    gtk_icon_theme_set_theme_name(theme, name);
#endif
    for (auto &[alias, target] : aliases.items()) {
      auto expected = read_file(
          root / "usr/share/icons/Luxe/scalable/luxe-apps" / (alias + ".svg"));
      for (int size : {16, 24, 32, 48, 128})
        for (int scale : {1, 2}) {
          fs::path path;
#if GTK_MAJOR_VERSION == 3
          auto icon = gtk_icon_theme_lookup_icon_for_scale(
              theme, alias.c_str(), size, scale,
              static_cast<GtkIconLookupFlags>(0));
          require(icon, "Missing lookup: " + alias);
          auto filename = gtk_icon_info_get_filename(icon);
          require(filename, "Missing filename: " + alias);
          path = filename;
          auto pixbuf = gtk_icon_info_load_icon(icon, nullptr);
          require(pixbuf, "Cannot decode " + alias);
          g_object_unref(pixbuf);
#else
          auto icon = gtk_icon_theme_lookup_icon(
              theme, alias.c_str(), nullptr, size, scale, GTK_TEXT_DIR_LTR,
              static_cast<GtkIconLookupFlags>(0));
          require(icon, "Missing lookup: " + alias);
          auto file = gtk_icon_paintable_get_file(icon);
          require(file, "No lookup file: " + alias);
          auto filename = g_file_get_path(file);
          require(filename, "Nonlocal icon: " + alias);
          path = filename;
          g_free(filename);
#endif
          require(inside(path, root), "Lookup escaped theme: " + path.string());
          std::string dest = target;
          if (dest.rfind("luxe:", 0) == 0 || alias == "org.luxe.LuxUp")
            require(read_file(path) == expected,
                    "Wrong lookup artwork: " + alias);
          g_object_unref(icon);
        }
    }
    g_object_unref(theme);
  }
  std::cout
      << "PASS: GTK" << GTK_MAJOR_VERSION
      << " aliases resolve in all three themes, five sizes and two scales\n";
}
int main(int argc, char **argv) {
  try {
    require(argc == 5, "Usage: icon-fixtures coverage|lookup ROOT SOURCE DEDUPLICATOR");
    auto root = fs::absolute(argv[2]), source = fs::absolute(argv[3]);
    if (std::string(argv[1]) == "coverage")
      coverage(root, source, fs::absolute(argv[4]));
    else if (std::string(argv[1]) == "lookup")
      lookup(root, source, fs::absolute(argv[4]));
    else
      throw std::runtime_error("Unknown icon fixture");
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
