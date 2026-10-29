// Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later
#include <memory>
static std::string shell_quote(const std::string &s) {
  auto p = g_shell_quote(s.c_str());
  std::string out = p;
  g_free(p);
  return out;
}
static void mime_defaults(const fs::path &self,
                          const std::vector<std::string> &args) {
  require(args.size() == 1 || args.size() == 2,
          "Usage: mime-defaults DRIVER [SESSION]");
  auto driver = fs::absolute(args[0]);
  Scratch dir("luxe-mime-");
  auto root = dir.path, config = root / "config", data = root / "data",
       apps = data / "applications";
  fs::create_directories(config);
  fs::create_directories(apps);
  Env env = {{"XDG_CONFIG_HOME", config.string()},
             {"XDG_DATA_HOME", data.string()},
             {"XDG_CACHE_HOME", (root / "cache").string()},
             {"XDG_CURRENT_DESKTOP", "X-Luxe:LXDE"},
             {"LC_ALL", "C"},
             {"GIO_USE_VFS", "local"}};
  auto browser = root / "browser";
  write_file(browser, "#!/bin/sh\nexec " + shell_quote(self.string()) +
                          " capture-json " +
                          shell_quote((root / "launched").string()) +
                          " \"$@\"\n");
  fs::permissions(browser, static_cast<fs::perms>(0755));
  for (auto &[name, exec] : std::vector<std::pair<std::string, std::string>>{
           {"browser.desktop", browser.string()},
           {"aaa-other.desktop", "/usr/bin/true"}})
    write_file(
        apps / name,
        "[Desktop Entry]\nType=Application\nName=" + name + "\nExec=" + exec +
            " %U\nMimeType=x-scheme-handler/http;x-scheme-handler/https;\n");
  command({"update-desktop-database", apps.string()}, env);
  auto defaults = config / "mimeapps.list", legacy = apps / "mimeapps.list";
  std::string original =
      "# Keep my comments\n[Default "
      "Applications]\nx-scheme-handler/http=missing.desktop;\nx-scheme-handler/"
      "https=missing.desktop;\nx-scheme-handler/"
      "mailto=mail.desktop;\nx-scheme-handler/codex=chatgpt.desktop;\ntext/"
      "html=editor.desktop;\nimage/png=frame.desktop;\n[Added "
      "Associations]\nx-scheme-handler/"
      "https=aaa-other.desktop;browser.desktop;\n[Removed "
      "Associations]\nx-scheme-handler/"
      "https=browser.desktop;blocked.desktop;\n";
  write_file(defaults, original);
  write_file(
      legacy,
      "[Default Applications]\nx-scheme-handler/https=aaa-other.desktop;\n");
  std::vector<std::unique_ptr<Child>> commands;
  auto run = [&](std::vector<std::string> cmd, bool success = true) {
    auto log = root / ("command-" + std::to_string(commands.size()));
    auto output = log.string() + ".stdout";
    std::vector<std::string> wrapper = {
        "sh", "-c", "out=$1; shift; exec \"$@\" > \"$out\"", "fixture", output};
    wrapper.insert(wrapper.end(), cmd.begin(), cmd.end());
    commands.push_back(std::make_unique<Child>(wrapper, env, log));
    auto code = commands.back()->wait(10);
    auto text = trim(read_file(output));
    require((code == 0) == success,
            "Unexpected status: " + cmd[0] + ": " + text);
    return text;
  };
  auto handlers = [&](const std::string &expected) {
    for (auto scheme : {"http", "https"}) {
      std::string mime = "x-scheme-handler/" + std::string(scheme);
      require(run({"xdg-mime", "query", "default", mime}) == expected,
              "Wrong xdg-mime handler");
      auto rows = lines(run({"gio", "mime", mime}));
      require(!rows.empty() && rows[0].find(expected) != std::string::npos,
              "Wrong GIO handler");
    }
  };
  if (args.size() == 2) {
    auto home = root / "home", profile = config / "luxesession/MimeTest";
    fs::create_directories(home);
    fs::create_directories(profile);
    write_file(profile / "desktop.conf",
               "[Session]\ndisable_autostart=all\n[State]\nguess_default="
               "false\n[Dbus]\nlxde=true\n");
    auto tested = root / "session-under-test";
    fs::copy_file(args[1], tested);
    fs::permissions(tested, fs::status(args[1]).permissions());
    Env session_env = env;
    session_env["HOME"] = home.string();
    session_env["XDG_STATE_HOME"] = (root / "state").string();
    Child server({tested.string(),
                  "--session",
                  "MimeTest",
                  "--de",
                  "X-Luxe",
                  "--noxsettings",
                  "--noautostart"},
                 session_env, root / "session.log");
    try {
      wait_for(server, [&]() {
        return run({"gdbus", "call", "--session", "--dest",
                    "org.freedesktop.DBus", "--object-path",
                    "/org/freedesktop/DBus", "--method",
                    "org.freedesktop.DBus.NameHasOwner",
                    "org.lxde.SessionManager"})
                   .find("true") != std::string::npos;
      }, 5);
    } catch (...) {
      std::cerr << "Session startup log:\n" << read_file(root / "session.log");
      throw;
    }
    auto call = [&](const std::string &method, std::vector<std::string> params,
                    bool success = true) {
      std::vector<std::string> cmd = {"gdbus",
                                      "call",
                                      "--session",
                                      "--dest",
                                      "org.lxde.SessionManager",
                                      "--object-path",
                                      "/org/lxde/SessionManager",
                                      "--method",
                                      "org.lxde.SessionManager." + method};
      cmd.insert(cmd.end(), params.begin(), params.end());
      return run(cmd, success);
    };
    call("SessionSet", {"webbrowser", "command", browser.string()});
    handlers("browser.desktop");
    require(
        call("SessionGet", {"webbrowser", "command"}).find(browser.string()) !=
            std::string::npos,
        "Browser not saved");
    auto saved = read_file(defaults);
    call("SessionSet", {"webbrowser", "command", "/does/not/exist"}, false);
    require(
        call("SessionGet", {"webbrowser", "command"}).find(browser.string()) !=
                std::string::npos &&
            read_file(defaults) == saved,
        "Invalid command changed defaults");
    call("SessionSet", {"webbrowser", "command", ""});
    require(read_file(defaults) == saved, "Empty command changed defaults");
    call("SessionSet", {"webbrowser", "command", browser.string()});
  } else
    run({driver.string(), "\"" + browser.string() + "\""});
  handlers("browser.desktop");
  auto values = g_key_file_new();
  require(g_key_file_load_from_file(values, defaults.c_str(), G_KEY_FILE_NONE,
                                    nullptr),
          "Invalid defaults");
  auto check = [&](const char *group, const char *key,
                   const std::string &expected) {
    auto text = g_key_file_get_string(values, group, key, nullptr);
    std::string actual = text ? text : "";
    g_free(text);
    require(actual == expected, "Changed association: " + std::string(key));
  };
  for (auto [mime, app] :
       {std::pair{"x-scheme-handler/mailto", "mail.desktop;"},
        {"x-scheme-handler/codex", "chatgpt.desktop;"},
        {"text/html", "editor.desktop;"},
        {"image/png", "frame.desktop;"}})
    check("Default Applications", mime, app);
  check("Added Associations", "x-scheme-handler/https",
        "browser.desktop;aaa-other.desktop;");
  check("Removed Associations", "x-scheme-handler/https", "blocked.desktop;");
  g_key_file_unref(values);
  require(read_file(defaults).find("# Keep my comments") != std::string::npos &&
              read_file(legacy).find("aaa-other.desktop") != std::string::npos,
          "Comments/legacy associations changed");
  auto before = read_file(defaults);
  run({driver.string(), ""});
  require(read_file(defaults) == before, "Empty command changed defaults");
  for (auto cmd : std::vector<std::string>{
           "/does/not/exist", " ", "'unterminated", std::string(8193, 'a')}) {
    run({driver.string(), cmd}, false);
    require(read_file(defaults) == before, "Failed command changed defaults");
  }
  std::string cmd = browser.string() + " --profile \"my profile\" %u";
  auto hash =
      g_compute_checksum_for_string(G_CHECKSUM_SHA256, cmd.c_str(), cmd.size());
  std::string custom = "luxe-default-browser-" + std::string(hash) + ".desktop";
  g_free(hash);
  run({driver.string(), cmd});
  handlers(custom);
  std::string uri = "https://example.invalid/luxe?x=a%20b#anchor";
  run({"gio", "open", uri});
  double deadline = now() + 2;
  while (!fs::exists(root / "launched") && now() < deadline)
    pause_for(.02);
  require(Json::parse(read_file(root / "launched")) ==
              Json::array({"--profile", "my profile", uri}),
          "URI arguments changed");
  auto specific = config / "x-luxe-mimeapps.list";
  write_file(defaults, original);
  write_file(specific, original);
  run({driver.string(), "\"" + browser.string() + "\""});
  handlers("browser.desktop");
  require(read_file(defaults) == original, "General defaults overwritten");
  auto saved = read_file(specific);
  fs::remove(specific);
  auto outside = root / "outside";
  write_file(outside, saved);
  fs::create_symlink(outside, specific);
  run({driver.string(), "\"" + browser.string() + "\""}, false);
  require(read_file(outside) == saved, "Symlink target modified");
  fs::remove(specific);
  write_file(specific, "[broken config");
  auto launcher = read_file(apps / custom);
  run({driver.string(), browser.string() + " --profile changed"}, false);
  require(read_file(apps / custom) == launcher,
          "Failed write changed launcher");
  run({driver.string(), "\"" + browser.string() + "\""}, false);
  require(read_file(specific) == "[broken config",
          "Invalid config overwritten");
  fs::remove(specific);
  run({driver.string(), "--mime", "browser.desktop", "text/plain"});
  auto rows = lines(run({"gio", "mime", "text/plain"}));
  require(!rows.empty() && rows[0].find("browser.desktop") != std::string::npos,
          "Text handler not saved");
  std::cout << "PASS: effective URL defaults, preservation, URI delivery and "
               "failed writes\n";
}
static void compositor(const std::vector<std::string> &args) {
  require(args.size() == 4,
          "Usage: compositor READER MIGRATION ENTRY XCOMPMGR");
  Scratch dir("luxe-compositor-");
  auto root = dir.path, system = root / "system/autostart", bin = root / "bin";
  fs::create_directories(system);
  fs::create_directory(bin);
  auto marker = root / "picom-started", pidfile = root / "compositor-pid";
  write_file(bin / "xcompmgr", "#!/bin/sh\necho $$ > " +
                                   shell_quote(pidfile.string()) + "\nexec " +
                                   shell_quote(fs::absolute(args[3]).string()) +
                                   " \"$@\"\n");
  write_file(bin / "picom",
             "#!/bin/sh\ntouch " + shell_quote(marker.string()) + "\n");
  for (auto name : {"xcompmgr", "picom"})
    fs::permissions(bin / name, static_cast<fs::perms>(0700));
  fs::copy_file(args[2], system / fs::path(args[2]).filename());
  write_file(system / "picom.desktop",
             "[Desktop Entry]\nType=Application\nName=Picom\nExec=picom\n");
  Env env = {{"HOME", (root / "home").string()},
             {"XDG_CONFIG_HOME", (root / "config").string()},
             {"XDG_CONFIG_DIRS", (root / "system").string()},
             {"DESKTOP_SESSION", "LXDE"},
             {"PATH", bin.string() + ":/usr/bin:/bin"}};
  auto display = std::unique_ptr<Display, decltype(&XCloseDisplay)>(
      XOpenDisplay(nullptr), XCloseDisplay);
  require(bool(display), "No X display");
  auto atom = XInternAtom(display.get(), "_NET_WM_CM_S0", false);
  require(XGetSelectionOwner(display.get(), atom) == None,
          "Compositor already running");
  // The migration remains a packaged Python application; invoke its own
  // shebang.
  command({fs::absolute(args[1]).string()}, env, 10);
  Child reader(
      {fs::absolute(args[0]).string(), "--desktop_environnement", "LXDE"}, env,
      root / "reader.log");
  require(reader.wait(10) == 0, read_file(root / "reader.log"));
  pid_t pid = -1;
  try {
    double deadline = now() + 2;
    while (now() < deadline) {
      if (fs::exists(pidfile))
        pid = std::stoi(read_file(pidfile));
      if (XGetSelectionOwner(display.get(), atom))
        break;
      pause_for(.02);
    }
    require(pid > 0 && XGetSelectionOwner(display.get(), atom),
            "xcompmgr did not acquire compositor ownership");
    pause_for(.2);
    require(kill(pid, 0) == 0 && !fs::exists(marker), "Picom raced xcompmgr");
    Child other(
        {fs::absolute(args[0]).string(), "--desktop_environnement", "GNOME"},
        env, root / "other.log");
    require(other.wait(10) == 0, read_file(root / "other.log"));
    deadline = now() + 2;
    while (!fs::exists(marker) && now() < deadline)
      pause_for(.02);
    require(fs::exists(marker), "Migration suppressed Picom outside Luxe");
  } catch (...) {
    if (pid > 0)
      kill(pid, SIGTERM);
    throw;
  }
  if (pid > 0)
    kill(pid, SIGTERM);
  std::cout << "PASS: real xcompmgr ownership, no Picom race, other-desktop "
               "preservation\n";
}
