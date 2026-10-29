// Regression fixtures for the preserved session migration application.
#include <tuple>
static void migration_fixture(const std::vector<std::string> &args) {
  require(args.size() == 1, "Usage: migration-fixture MIGRATION");
  auto script = fs::absolute(args[0]);
  auto run = [&](const fs::path &root, const std::string &session = "LXDE", int expected = 0) {
    Scratch log("luxe-migration-command-");
    Child child({script.string()},
            {{"HOME", (root / "home").string()},
             {"XDG_CONFIG_HOME", (root / "config").string()},
             {"XDG_CONFIG_DIRS", (root / "system").string()},
             {"DESKTOP_SESSION", session}},
            log.path / "log");
    int status = child.wait(10);
    require(status == expected, "Unexpected migration status: " + read_file(log.path / "log"));
  };
  auto contains = [](const fs::path &path, const std::string &value) {
    require(read_file(path).find(value) != std::string::npos,
            "Missing " + value + " in " + path.string());
  };
  {
    Scratch dir("luxe-autostart-");
    auto root = dir.path, profile = root / "config/luxesession/LXDE",
         user = root / "config/autostart", system = root / "system/autostart";
    for (auto p : {profile, user, system})
      fs::create_directories(p);
    write_file(
        profile / "desktop.conf",
        "[Session]\nwindow_manager=openbox\npanel/command=custom-panel\n");
    write_file(profile / "autostart",
               "@luxepanel --profile LXDE\n@pcmanager --desktop --profile "
               "LXDE\nxset s off\n#sleep 4\npicom\n");
    write_file(
        system / "picom.desktop",
        "[Desktop "
        "Entry]\nType=Application\nName=Picom\nExec=picom\nOnlyShowIn=LXDE;\n");
    write_file(user / "picom.desktop",
               "[Desktop "
               "Entry]\nType=Application\nName=Picom\nExec=picom\nNotShowIn=X-"
               "Luxe;\n");
    run(root);
    run(root);
    contains(profile / "desktop.conf", "panel/command=custom-panel");
    contains(profile / "desktop.conf",
             "desktop_manager/command=frame --wallpaper-daemon");
    write_file(profile / "desktop.conf",
               "[Session]\ndesktop_manager/command=pcmanager --desktop "
               "--profile LXDE\n");
    run(root);
    contains(profile / "desktop.conf",
             "desktop_manager/command=frame --wallpaper-daemon");
    write_file(profile / "desktop.conf",
               "[Session]\ndesktop_manager/command=my-custom-desktop\n");
    run(root);
    contains(profile / "desktop.conf",
             "desktop_manager/command=my-custom-desktop");
    require(!fs::exists(profile / "autostart") &&
                fs::exists(profile / "autostart.legacy-migrated") &&
                !fs::exists(profile / "autostart.legacy-migrated-2"),
            "Incorrect legacy backup");
    contains(user / "picom.desktop", "NotShowIn=X-Luxe;");
    int count = 0;
    bool xset = false, sleep = false;
    for (auto &e : fs::directory_iterator(user))
      if (e.path().filename().string().rfind("luxe-migrated-", 0) == 0 &&
          e.path().extension() == ".desktop") {
        count++;
        auto s = read_file(e.path());
        xset |= s.find("Exec=xset s off") != std::string::npos;
        sleep |= s.find("Exec=sleep 4") != std::string::npos &&
                 s.find("NotShowIn=X-Luxe;") != std::string::npos;
      }
    require(count == 2 && xset && sleep, "Incorrect migrated launchers");
  }
  {
    Scratch dir("luxe-migration-retry-");
    auto root = dir.path, profile = root / "config/luxesession/LXDE",
         user = root / "config/autostart";
    fs::create_directories(profile); fs::create_directories(user);
    const std::string first = "fixture-ok --mode safe", second = "fixture-app --mode unsafe";
    const std::string legacy = "@" + first + "\n@" + second + "\n";
    write_file(profile / "autostart", legacy);
    auto target = [&](const std::string &command, const std::string &slug) {
      char *hash = g_compute_checksum_for_string(G_CHECKSUM_SHA256, command.c_str(), command.size());
      auto name = "luxe-migrated-" + slug + "-" + std::string(hash, 10) + ".desktop";
      g_free(hash); return user / name;
    };
    auto okay = target(first, "fixture-ok"), blocked = target(second, "fixture-app");
    fs::create_directory(blocked); // One bad item must not retire the source.
    run(root, "LXDE", 1);
    require(fs::exists(profile / "autostart") && read_file(profile / "autostart") == legacy,
            "Failed entry retired the legacy autostart source");
    require(!fs::exists(profile / "autostart.legacy-migrated"), "Failed migration created a success backup");
    require(fs::exists(okay), "Successful partial conversion was not retained");
    auto modified = fs::last_write_time(okay);
    write_file(profile / "autostart.legacy-migrated", "previous backup\n");
    fs::remove(blocked);
    run(root);
    require(!fs::exists(profile / "autostart") &&
                read_file(profile / "autostart.legacy-migrated") == "previous backup\n" &&
                read_file(profile / "autostart.legacy-migrated-2") == legacy,
            "Successful retry did not finish retirement");
    require(fs::last_write_time(okay) == modified, "Retry rewrote an already-equivalent entry");
    contains(blocked, "Exec=" + second);
    run(root);
    require(!fs::exists(profile / "autostart.legacy-migrated-3"), "Retry created duplicate backups");
  }
  {
    Scratch dir("luxe-disabled-core-");
    auto profile = dir.path / "config/luxesession/LXDE";
    fs::create_directories(profile);
    write_file(profile / "desktop.conf", "[Session]\nwindow_manager=openbox\n");
    write_file(
        profile / "autostart",
        "#@luxepanel --profile LXDE\n#@pcmanager --desktop --profile LXDE\n");
    run(dir.path);
    auto text = read_file(profile / "desktop.conf");
    require(text.find("panel/command=") == std::string::npos &&
                text.find("desktop_manager/command=") == std::string::npos,
            "Disabled core enabled");
  }
  {
    Scratch dir("luxe-migration-boundary-");
    auto root = dir.path, profile = root / "config/luxesession/LXDE",
         outside = root / "outside", user = root / "config/autostart";
    fs::create_directories(profile);
    fs::create_directory(user);
    write_file(outside, "do-not-touch\n");
    fs::create_symlink(outside, profile / "desktop.conf");
    write_file(profile / "autostart", "echo migrated\n");
    fs::create_symlink(outside, user / "victim.desktop");
    for (auto invalid :
         std::vector<std::string>{"../escape", "/tmp/escape", "name.with.dot",
                                  "", std::string(65, 'a')}) {
      run(root, invalid);
      if (!invalid.empty())
        require(!fs::exists(root / "config/luxesession" / invalid),
                "Invalid profile created");
    }
    require(read_file(outside) == "do-not-touch\n" &&
                fs::is_symlink(profile / "desktop.conf"),
            "Symlink boundary violated");
  }
  {
    Scratch dir("luxe-migration-fifo-");
    auto path = dir.path / "config/luxesession/LXDE/autostart";
    fs::create_directories(path.parent_path());
    require(mkfifo(path.c_str(), 0600) == 0, "Cannot create FIFO");
    run(dir.path, "LXDE", 1);
    require(fs::is_fifo(path), "FIFO modified");
  }
  {
    Scratch dir("luxe-migration-invalid-command-");
    auto path = dir.path / "config/luxesession/LXDE/autostart";
    fs::create_directories(path.parent_path());
    const std::string legacy = "fixture-app 'unterminated\n";
    write_file(path, legacy);
    run(dir.path, "LXDE", 1);
    require(read_file(path) == legacy && !fs::exists(path.string() + ".legacy-migrated"),
            "Malformed command was retired");
  }
  {
    Scratch dir("luxe-migration-libfm-");
    auto root = dir.path;
    for (auto [name, before, after] :
         std::vector<std::tuple<std::string, std::string, std::string>>{
             {"plain", "terminal=lxterminal", "terminal=luxeterm"},
             {"custom", "terminal=xterm -e %s", "terminal=xterm -e %s"},
             {"args", "terminal=lxterminal --geometry=80x24",
              "terminal=lxterminal --geometry=80x24"}}) {
      auto file = root / name / "config/libfm/libfm.conf";
      fs::create_directories(file.parent_path());
      write_file(file, "[config]\nsingle_click=0\n" + before + "\n");
      run(root / name);
      contains(file, after + "\n");
      contains(file, "single_click=0");
    }
    auto outside = root / "outside",
         linked = root / "link/config/libfm/libfm.conf";
    write_file(outside, "terminal=lxterminal\n");
    fs::create_directories(linked.parent_path());
    fs::create_symlink(outside, linked);
    run(root / "link");
    require(read_file(outside) == "terminal=lxterminal\n",
            "Linked libfm changed");
  }
  {
    Scratch dir("luxe-compositor-migration-");
    auto root = dir.path, system = root / "system/autostart",
         user = root / "config/autostart";
    fs::create_directories(system);
    fs::create_directories(user);
    std::string
        picom = "[Desktop Entry]\nType=Application\nName=Custom "
                "Picom\nExec=/usr/bin/picom --config "
                "/tmp/custom.conf\nNotShowIn=GNOME;\n",
        custom =
            "[Desktop Entry]\nType=Application\nExec=picom -b\nHidden=true\n",
        other = "[Desktop Entry]\nType=Application\nExec=other-compositor\n",
        disabled = "[Desktop Entry]\nType=Application\nExec=xcompmgr "
                   "-n\nHidden=true\n";
    write_file(system / "picom.desktop", picom);
    write_file(user / "custom.desktop", custom);
    write_file(user / "other.desktop", other);
    write_file(user / "luxe-xcompmgr.desktop", disabled);
    write_file(root / "outside", custom);
    fs::create_symlink(root / "outside", user / "link.desktop");
    run(root);
    contains(user / "picom.desktop", "NotShowIn=GNOME;X-Luxe;");
    contains(user / "picom.desktop",
             "Exec=/usr/bin/picom --config /tmp/custom.conf");
    require(read_file(system / "picom.desktop") == picom,
            "System entry modified");
    contains(user / "custom.desktop", "Hidden=true");
    contains(user / "custom.desktop", "NotShowIn=X-Luxe;");
    require(read_file(user / "other.desktop") == other &&
                read_file(user / "luxe-xcompmgr.desktop") == disabled &&
                read_file(root / "outside") == custom &&
                fs::is_symlink(user / "link.desktop"),
            "Unrelated compositor entry changed");
    auto snapshot = [&]() {
      std::map<std::string, std::string> files;
      for (auto &e : fs::directory_iterator(user))
        if (!e.is_symlink())
          files[e.path().filename().string()] = read_file(e.path());
      return files;
    };
    auto before = snapshot();
    run(root);
    require(before == snapshot(), "Compositor migration not idempotent");
  }
  {
    Scratch dir("luxe-wallpaper-migration-");
    auto root = dir.path;
    std::vector<std::tuple<fs::path, std::string, std::string, std::string>>
        configs;
    int index = 0;
    for (auto [wallpaper, mode] :
         std::vector<std::pair<std::string, std::string>>{
             {"/usr/share/backgrounds/luxe/tokyo-night.svg", "fit"},
             {"/usr/share/backgrounds/luxe/light.svg", "crop"},
             {"/usr/share/backgrounds/luxe/nord.svg", "2"},
             {"/usr/share/backgrounds/luxe/mocha.svg", "tile"},
             {"/home/test/custom.svg", "fit"},
             {"/usr/share/backgrounds/luxe/dracula.png", "center"}}) {
      auto path = root / "config/pcmanager" /
                  ("profile" + std::to_string(index++)) / "pcmanager.conf";
      fs::create_directories(path.parent_path());
      std::string original = "# keep\n[desktop]\nwallpaper=" + wallpaper +
                             "\nwallpaper_mode=" + mode +
                             "\n[extra]\nwallpaper=untouched.svg\n";
      write_file(path, original);
      configs.emplace_back(path, wallpaper, mode, original);
    }
    auto state = root / "config/luxe/appearance.ini";
    fs::create_directories(state.parent_path());
    write_file(state,
               "# "
               "state\n[Appearance]\nwallpaper-follow=true\nlast-wallpaper=/"
               "usr/share/backgrounds/luxe/tokyo-night.svg\n");
    auto outside = root / "outside";
    write_file(outside, std::get<3>(configs[0]));
    fs::create_directory_symlink(outside.parent_path(),
                                 root / "config/pcmanager/linked");
    fs::create_directory(root / "config/pcmanager/link-file");
    fs::create_symlink(outside,
                       root / "config/pcmanager/link-file/pcmanager.conf");
    auto bad = root / "config/pcmanager/malformed/pcmanager.conf";
    fs::create_directories(bad.parent_path());
    write_file(bad, "[desktop]\nwallpaper=/usr/share/backgrounds/luxe/"
                    "dark.svg\nwallpaper=custom\n");
    auto bad_before = read_file(bad);
    run(root);
    std::map<fs::path, std::string> snapshot;
    for (auto &[path, wallpaper, mode, original] : configs) {
      auto expected = original;
      if (fs::path(wallpaper).extension() == ".svg" &&
          wallpaper.rfind("/usr/share/backgrounds/luxe/", 0) == 0) {
        auto pos = expected.find(wallpaper);
        expected.replace(pos, wallpaper.size(),
                         wallpaper.substr(0, wallpaper.size() - 4) + ".png");
        if (mode == "fit" || mode == "crop" || mode == "2" || mode == "5") {
          auto key = "wallpaper_mode=" + mode;
          expected.replace(expected.find(key), key.size(),
                           "wallpaper_mode=stretch");
        }
      }
      require(read_file(path) == expected,
              "Wallpaper migration changed unexpected content");
      snapshot[path] = read_file(path);
    }
    contains(state, "wallpaper-follow=true");
    contains(state,
             "last-wallpaper=/usr/share/backgrounds/luxe/tokyo-night.png");
    require(read_file(bad) == bad_before &&
                read_file(outside) == std::get<3>(configs[0]),
            "Wallpaper boundary modified");
    snapshot[state] = read_file(state);
    run(root);
    for (auto &[path, before] : snapshot)
      require(read_file(path) == before, "Wallpaper migration not idempotent");
  }
  std::cout << "PASS: XDG/core migration, idempotence, boundaries, libfm "
               "terminal, compositor and PNG/stretch migration\n";
}
