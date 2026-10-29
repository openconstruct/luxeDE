// Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <set>
static void wallpaper_install(const std::vector<std::string> &args) {
  require(args.size() == 4,
          "Usage: wallpaper-install SOURCE BUILD CMAKE DATADIR");
  fs::path source = fs::absolute(args[0]);
  auto catalog = source.parent_path().parent_path().parent_path() /
                 "cmake/LuxePaletteCatalog.cmake";
  std::set<std::string> palettes;
  for (auto &line : lines(read_file(catalog))) {
    constexpr const char *marker = "set(LUXE_PALETTE_IDS ";
    constexpr size_t marker_size = 21;
    if (line.compare(0, marker_size, marker) != 0)
      continue;
    std::istringstream values(
        line.substr(marker_size, line.find(')', marker_size) - marker_size));
    for (std::string id; values >> id;)
      palettes.insert(id);
  }
  require(!palettes.empty(), "Palette catalog is missing LUXE_PALETTE_IDS");
  std::set<std::string> expected;
  for (auto c : {"horizon", "river", "veil"})
    for (auto &palette : palettes)
      expected.insert(std::string(c) + "/luxe-" + c + "-" + palette + ".png");
  std::map<std::string, std::string> manifest;
  for (auto &line : lines(read_file(source / "SHA256SUMS"))) {
    std::istringstream in(line);
    std::string hash, name;
    require(bool(in >> hash >> name), "Invalid manifest");
    require(manifest.emplace(name, hash).second, "Duplicate manifest name");
  }
  std::set<std::string> actual;
  for (auto &[name, hash] : manifest)
    actual.insert(name);
  require(actual == expected, "Unexpected manifest names");
  actual.clear();
  for (auto &e : fs::recursive_directory_iterator(source))
    if (e.path().extension() == ".png")
      actual.insert(e.path().lexically_relative(source).string());
  require(actual == expected, "Unexpected source images");
  Scratch dir("luxe-wallpaper-install-");
  command({args[2], "--install", args[1], "--prefix", "/usr", "--component",
           "WallpaperCollections"},
          {{"DESTDIR", dir.path.string()}}, 45);
  auto share = dir.path / "usr" / args[3], images = share / "backgrounds/luxe",
       docs = share / "doc/luxesession";
  actual.clear();
  for (auto &e : fs::recursive_directory_iterator(images))
    if (e.path().extension() == ".png")
      actual.insert(e.path().lexically_relative(images).string());
  require(actual == expected, "Unexpected installed images");
  require(read_file(docs / "WALLPAPER-COLLECTIONS.md") ==
              read_file(source / "PROVENANCE.md"),
          "Provenance differs");
  require(read_file(docs / "WALLPAPER-COLLECTIONS.sha256") ==
              read_file(source / "SHA256SUMS"),
          "Installed manifest differs");
  for (auto &[name, hash] : manifest) {
    auto target = images / name;
    require(read_file(target) == read_file(source / name) &&
                digest(target) == hash,
            "Changed image: " + name);
    require((fs::status(target).permissions() & fs::perms::mask) ==
                static_cast<fs::perms>(0644),
            "Incorrect permissions: " + name);
    auto pixbuf = gdk_pixbuf_new_from_file(target.c_str(), nullptr);
    require(pixbuf, "Image decode failed: " + name);
    bool valid = gdk_pixbuf_get_width(pixbuf) == 3840 &&
                 gdk_pixbuf_get_height(pixbuf) == 2160 &&
                 !gdk_pixbuf_get_has_alpha(pixbuf) &&
                 gdk_pixbuf_get_n_channels(pixbuf) == 3;
    g_object_unref(pixbuf);
    require(valid, "Unexpected image format: " + name);
  }
  std::cout
      << "PASS: 18 original 4K PNGs installed, decoded and hash-verified\n";
}
static void xdg_autostart(const fs::path &self,
                          const std::vector<std::string> &args) {
  require(args.size() == 1, "Usage: xdg-autostart PROGRAM");
  Scratch dir("luxe-xdg-autostart-");
  auto config = dir.path / "config", autostart = config / "autostart",
       marker = dir.path / "marker", code_marker = dir.path / "field-codes",
       path_marker = dir.path / "working-directory", system_config = dir.path / "system",
       system_autostart = system_config / "autostart";
  fs::create_directories(autostart);
  fs::create_directories(system_autostart);
  // Desktop Exec quoting uses double quotes and escapes, independent of a
  // shell.
  auto quote = [](const std::string &s) {
    std::string out = "\"";
    for (char c : s) {
      if (c == '"' || c == '\\' || c == '$' || c == '`')
        out += '\\';
      out += c;
    }
    return out + '"';
  };
  auto exec = quote(self.string()) + " capture-args " + quote(marker.string());
  write_file(autostart / "good.desktop",
             "[Desktop Entry]\nType=Application\nExec=" + exec +
                 " 'two words'\n");
  write_file(autostart / "hidden.desktop",
             "[Desktop Entry]\nType=Application\nHidden=true\nExec=" + exec +
                 " hidden\n");
  write_file(autostart / "wrong.desktop",
             "[Desktop Entry]\nType=Application\nOnlyShowIn=GNOME;\nExec=" +
                 exec + " wrong\n");
  write_file(autostart / "malformed.desktop",
             "[Desktop Entry]\nType=Application\nExec=\"unterminated\n");
  auto outside_marker = dir.path / "symlink-launched";
  write_file(dir.path / "outside.desktop",
             "[Desktop Entry]\nType=Application\nExec=" + quote(self.string()) +
                 " capture-args " + quote(outside_marker.string()) + " linked\n");
  fs::create_symlink(dir.path / "outside.desktop",
                     autostart / "linked.desktop");
  auto field_entry = autostart / "field-codes.desktop";
  write_file(field_entry,
             "[Desktop Entry]\nType=Application\nName=Field Code Fixture\n"
             "Icon=preferences-system\nTerminal=false\nTryExec=/usr/bin/env\n"
             "Path=" + dir.path.string() + "\nExec=" + quote(self.string()) +
                 " capture-args " + quote(code_marker.string()) +
                 " %i %c %k %%\n");
  write_file(autostart / "working-directory.desktop",
             "[Desktop Entry]\nType=Application\nName=Working Directory\n"
             "Path=" + dir.path.string() + "\nExec=" + quote(self.string()) +
                 " capture-cwd " + quote(path_marker.string()) + " %c\n");
  write_file(autostart / "tryexec.desktop",
             "[Desktop Entry]\nType=Application\nTryExec=luxe-no-such-test-program\n"
             "Exec=" + exec + " should-not-run\n");
  auto override_marker = dir.path / "system-override-launched";
  write_file(system_autostart / "override.desktop",
             "[Desktop Entry]\nType=Application\nExec=" + quote(self.string()) +
                 " capture-args " + quote(override_marker.string()) + " system\n");
  write_file(autostart / "override.desktop",
             "[Desktop Entry]\nType=Application\nHidden=true\nExec=" + exec + " hidden\n");
  require(mkfifo((autostart / "fifo.desktop").c_str(), 0600) == 0,
          "mkfifo failed");
  Child app({fs::absolute(args[0]).string(), "--desktop_environnement", "LXDE"},
            {{"XDG_CONFIG_HOME", config.string()},
             {"XDG_CONFIG_DIRS", system_config.string()},
             {"PATH", "/usr/bin:/bin"}},
            dir.path / "autostart.log");
  require(app.wait(10) == 0, read_file(dir.path / "autostart.log"));
  double deadline = now() + 1;
  while (!fs::exists(marker) && now() < deadline)
    pause_for(.02);
  require(read_file(marker) == "two words",
          "Incorrect autostart arguments/filtering");
  double code_deadline = now() + 2;
  while ((!fs::exists(code_marker) || !fs::exists(path_marker)) && now() < code_deadline)
    pause_for(.02);
  std::string expected_fields = "--icon\\0preferences-system\\0Field Code Fixture\\0" +
                                field_entry.string() + "\\0%";
  require(read_file(code_marker) == expected_fields,
          "Desktop Entry field-code expansion changed argv or file identity");
  require(read_file(path_marker) == dir.path.string() + "\\0Working Directory",
          "Desktop Entry Path working directory was not honored");
  require(!fs::exists(outside_marker), "Symlinked autostart entry escaped the bounded policy");
  require(!fs::exists(override_marker), "User Hidden override did not mask the system entry");
  std::cout << "PASS: bounded XDG autostart parsing and entry filtering\n";
}
static void terminal_forward(const fs::path &self,
                             const std::vector<std::string> &args) {
  require(args.size() == 1, "Usage: terminal-forward PROGRAM");
  Scratch dir("luxe-term-forward-");
  auto make = [&](const std::string &name, const std::string &delay,
                  std::vector<std::string> options = {}, std::string executable = {}) {
    std::vector<std::string> cmd = {fs::absolute(args[0]).string()};
    cmd.insert(cmd.end(), options.begin(), options.end());
    std::vector<std::string> tail = {"-e",
                                     executable.empty() ? self.string() : executable,
                                     "terminal-child",
                                     (dir.path / name).string(),
                                     "literal spaces; no shell expansion",
                                     delay};
    cmd.insert(cmd.end(), tail.begin(), tail.end());
    return cmd;
  };
  auto marker = [&](const std::string &name) {
    double deadline = now() + 3;
    while (now() < deadline) {
      if (fs::exists(dir.path / name)) {
        try {
          return Json::parse(read_file(dir.path / name));
        } catch (const Json::parse_error &) {
        }
      }
      pause_for(.01);
    }
    throw std::runtime_error("Child did not write " + name);
  };
  Child primary(make("primary", "6"), {{"LUXE_TEST_MARKER", "primary"}, {"VIRTUAL_ENV", "primary-env"}}, dir.path / "primary.log", false,
                dir.path);
  require(marker("primary")[0] == primary.pid, "Wrong primary parent");
  auto nested = dir.path / "nested";
  fs::create_directory(nested);
  auto bin = dir.path / "bin";
  fs::create_directory(bin);
  fs::create_symlink(self, bin / "fixture-child");
  const char *inherited_path = std::getenv("PATH");
  Env remote_env = {{"LUXE_TEST_MARKER", "remote"}, {"VIRTUAL_ENV", "remote-env"},
                    {"GSK_RENDERER", "gl"}, {"PATH", bin.string() + ":" + (inherited_path ? inherited_path : "/usr/bin:/bin")}};
  Child remote(make("remote", ".1", {"--title=Remote fixture"}, "fixture-child"), remote_env,
               dir.path / "remote.log", false, nested);
  require(remote.wait(5) == 0, "Remote launch failed");
  require(marker("remote") ==
              Json::array({primary.pid, nested.string(),
                           "literal spaces; no shell expansion", "remote", "remote-env", "gl"}),
          "Remote cwd/args/parent/environment differs");
  require(marker("primary")[3] == "primary" && marker("primary")[4] == "primary-env",
          "Original terminal environment changed");
  Child standalone(make("standalone", ".1", {"--no-remote"}), {{"LUXE_TEST_MARKER", "standalone"}, {"VIRTUAL_ENV", "standalone-env"}},
                   dir.path / "standalone.log", false, dir.path);
  require(marker("standalone")[0] == standalone.pid, "Wrong standalone parent");
  require(marker("standalone")[3] == "standalone" && marker("standalone")[4] == "standalone-env",
          "Standalone environment differs");
  require(standalone.wait(5) == 0 && primary.wait(8) == 0,
          "Terminal exit failed");
  std::cout << "PASS: terminal forwarding preserves parent, cwd and literal "
               "arguments; standalone process exits\n";
}
static void session_edit(const std::vector<std::string> &args) {
  require(args.size() == 1, "Usage: session-edit EDITOR");
  Scratch dir("luxe-session-edit-");
  Child wm({"openbox"}, {}, dir.path / "openbox.log");
  pause_for(.4);
  int number = 0;
  for (auto key : {"window_manager", "windows_manager/command"}) {
    auto root = dir.path / std::to_string(number++), config = root / "config",
         conf = config / "luxesession/LXDE/desktop.conf";
    fs::create_directories(conf.parent_path());
    write_file(conf, "[Session]\n" + std::string(key) + "=" +
                         (number == 1 ? "openbox" : "openbox-lxde") + "\n");
    Env env = {{"XDG_CONFIG_HOME", config.string()},
               {"XDG_CONFIG_DIRS", (root / "system").string()},
               {"XDG_CURRENT_DESKTOP", "X-Luxe"},
               {"DESKTOP_SESSION", "LXDE"},
               {"_LUXESESSION_PID", "1"}};
    Child first({fs::absolute(args[0]).string()}, env, root / "editor.log");
    auto windows = [&]() {
      Child search({"xdotool", "search", "--onlyvisible", "--name",
                    "^Session Settings$"},
                   env, root / "windows");
      search.wait(5);
      return lines(read_file(root / "windows"));
    };
    wait_for(first, [&]() { return !windows().empty(); }, 10);
    command({fs::absolute(args[0]).string()}, env, 10);
    pause_for(.5);
    auto ids = windows();
    require(ids.size() == 1, "Multiple session editor windows");
    command({"xdotool", "windowactivate", "--sync", ids[0]}, env);
    pause_for(.3);
    command({"xdotool", "key", "ctrl+s"}, env);
    require(first.wait(10) == 0, "Save did not close editor cleanly");
    auto text = read_file(conf);
    require(text.find(std::string(key) + "=openbox\n") != std::string::npos,
            "Effective window manager not saved");
    auto log = read_file(root / "editor.log");
    for (auto bad :
         {"CRITICAL", "Gtk-WARNING", "Adwaita-WARNING", "GtkDialog mapped"})
      require(log.find(bad) == std::string::npos, log);
  }
  std::cout << "PASS: Adwaita startup, single instance, effective "
               "window-manager key saved\n";
}
