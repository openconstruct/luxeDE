// Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later
#include <memory>
struct PanelFixture {
  Scratch dir;
  Env env;
  fs::path panel;
  PanelFixture(const fs::path &binary, const std::string &plugins,
               int height = 30)
      : dir("luxe-panel-fixture-"), panel(fs::absolute(binary)) {
    auto config = dir.path / "config/luxepanel/test/panels";
    fs::create_directories(config);
    write_file(
        config / "panel",
        "Global {\n edge=bottom\n widthtype=percent\n width=100\n height=" +
            std::to_string(height) + "\n}\n" + plugins);
    env = {{"GDK_BACKEND", "x11"},
           {"GSK_RENDERER", "cairo"},
           {"GTK_A11Y", "none"},
           {"XDG_CONFIG_HOME", (dir.path / "config").string()},
           {"XDG_CACHE_HOME", (dir.path / "cache").string()},
           {"XDG_CURRENT_DESKTOP", "X-Luxe"},
           {"LUXEPANEL_PROFILE", "test"},
           {"LD_LIBRARY_PATH",
            panel.parent_path().string() + ":/usr/lib/luxepanel"}};
  }
  std::string run(std::vector<std::string> args) {
    return trim(command(args, env, 6));
  }
  std::string log() { return read_file(dir.path / "panel.log"); }
};
static void panel_run(const std::vector<std::string> &args) {
  require(args.size() == 2, "Usage: panel-run CTL PANEL");
  PanelFixture f(args[1], "Plugin {\n type=separator\n}\n");
  auto path = f.dir.path / "many-programs";
  fs::create_directory(path);
  for (int i = 0; i < 2500; i++) {
    auto p = path / ("luxe-run-program-" + std::to_string(i));
    write_file(p, "#!/bin/sh\nexit 0\n");
    fs::permissions(p, static_cast<fs::perms>(0755));
  }
  f.env["PATH"] = path.string() + ":" + (getenv("PATH") ? getenv("PATH") : "");
  Child wm({"openbox"}, f.env, f.dir.path / "wm.log");
  Child panel({"dbus-run-session", "--", f.panel.string(), "--profile", "test"},
              f.env, f.dir.path / "panel.log");
  pause_for(1.5);
  for (int i = 0; i < 8; i++)
    for (int j = 0; j < 2; j++) {
      f.run({fs::absolute(args[0]).string(), "run"});
      pause_for(.03);
      f.run({"xdotool", "key", (i % 2) == j ? "alt+F4" : "Escape"});
      if (j == 0)
        pause_for(.08);
    }
  pause_for(2);
  require(panel.alive(), f.log());
  for (auto bad : {"CRITICAL", "Segmentation fault"})
    require(f.log().find(bad) == std::string::npos, f.log());
  std::cout
      << "PASS: rapid Run dialog lifecycle keeps autocomplete jobs isolated\n";
}
static void panel_taskbar(const fs::path &self,
                          const std::vector<std::string> &args) {
  require(args.size() == 1, "Usage: panel-taskbar PANEL");
  PanelFixture f(args[0],
                 "Plugin {\n type=taskbar\n Config {\n GroupedTasks=0\n }\n}\n",
                 28);
  Child wm({"openbox"}, f.env, f.dir.path / "wm.log");
  pause_for(.5);
  Child panel({"dbus-run-session", "--", f.panel.string(), "--profile", "test"},
              f.env, f.dir.path / "panel.log");
  pause_for(1);
  Child windows({(self.parent_path() / "gtk3-fixtures").string(), "windows"},
                f.env, f.dir.path / "windows.log");
  pause_for(1);
  auto a = f.run({"xdotool", "search", "--name", "^Luxe Alpha$"}),
       b = f.run({"xdotool", "search", "--name", "^Luxe Beta$"});
  auto click = [&](const std::string &win) {
    f.run({"xdotool", "mousemove", win == a ? "210" : "390", "184"});
    pause_for(.1);
    f.run({"xdotool", "click", "1"});
    pause_for(.3);
  };
  auto active = [&](const std::string &win) {
    wait_for(
        panel,
        [&]() {
          try {
            return f.run({"xdotool", "getactivewindow"}) == win;
          } catch (...) {
            return false;
          }
        },
        2);
  };
  for (auto win : {a, b, a}) {
    click(win);
    active(win);
  }
  click(a);
  require(f.run({"xprop", "-id", a, "WM_STATE"}).find("Iconic") !=
              std::string::npos,
          "Active task not minimized");
  click(a);
  active(a);
  require(f.log().find("Trying to measure GtkLabel") == std::string::npos,
          f.log());
  std::cout << "PASS: taskbar activation, active-window minimize/restore and "
               "28px labels\n";
}
static void panel_respawn(const std::vector<std::string> &args) {
  require(args.size() == 1 || args.size() == 2, "Usage: panel-respawn CTL [PANEL]");
  auto ctl = fs::absolute(args[0]);
  PanelFixture f(
      ctl.parent_path() / "luxepanel",
      "Plugin {\n type=menu\n Config {\n system {\n }\n }\n}\nPlugin {\n "
      "type=taskbar\n expand=1\n}\nPlugin {\n type=dclock\n}\n");
  f.env["PATH"] =
      ctl.parent_path().string() + ":" + (getenv("PATH") ? getenv("PATH") : "");
  Child wm({"openbox"}, f.env, f.dir.path / "wm.log");
  pause_for(.4);
  std::vector<std::unique_ptr<Child>> calls;
  auto invoke = [&](const std::string &op, int expected = 0) {
    auto log = f.dir.path / ("ctl-" + std::to_string(calls.size()));
    calls.push_back(std::make_unique<Child>(
        std::vector<std::string>{ctl.string(), op}, f.env, log));
    int code = calls.back()->wait(15);
    auto text = read_file(log);
    require(code == expected, text);
    return text;
  };
  try {
    require(invoke("config", 1).find("no running luxepanel") !=
                std::string::npos,
            "Missing dead-panel diagnostic");
    require(invoke("restart").find("started new instance (profile test)") !=
                std::string::npos,
            "Respawn profile ignored");
    pause_for(4);
    require(invoke("restart").find("restart requested") != std::string::npos,
            "Respawned panel not reached");
    pause_for(4);
    require(invoke("restart").find("restart requested") != std::string::npos,
            "Graceful restart failed");
    pause_for(5);
    invoke("exit");
    bool started = false;
    for (int i = 0; i < 20; i++) {
      if (invoke("restart").find("started new instance") != std::string::npos) {
        started = true;
        break;
      }
      pause_for(.5);
    }
    require(started, "Exit did not stop panel");
    pause_for(4);
    invoke("exit");
    pause_for(.3);
  } catch (...) {
    try {
      invoke("exit");
    } catch (...) {
    }
    throw;
  }
  std::cout << "PASS: dead-panel errors, profile-aware respawn, repeated "
               "restart and exit\n";
}
static void panel_volume(const std::vector<std::string> &args) {
  require(args.size() == 2, "Usage: panel-volume PANEL PLUGIN");
  PanelFixture f(args[0], "Plugin {\n type=volume\n Config {\n }\n}\n", 32);
  auto root = f.dir.path;
  auto plugins = root / "plugins", bin = root / "bin",
       runtime = root / "runtime";
  for (auto p : {plugins, bin, runtime, root / "home/Templates"})
    fs::create_directories(p);
  fs::permissions(runtime, static_cast<fs::perms>(0700));
  fs::copy_file(args[1], plugins / "volume.so");
  write_file(bin / "pavucontrol", "#!/bin/sh\nprintf mixer >> '" +
                                      (root / "mixer-called").string() + "'\n");
  fs::permissions(bin / "pavucontrol", static_cast<fs::perms>(0755));
  auto socket = runtime / "pulse.sock", alsa = root / "alsa.conf";
  write_file(alsa, "ctl.default { type pulse server \"unix:" + socket.string() +
                       "\" }\n");
  f.env["HOME"] = (root / "home").string();
  f.env["XDG_RUNTIME_DIR"] = runtime.string();
  f.env["PULSE_SERVER"] = "unix:" + socket.string();
  f.env["ALSA_CONFIG_PATH"] = alsa.string();
  f.env["PATH"] = bin.string() + ":" + (getenv("PATH") ? getenv("PATH") : "");
  Child pulse({"pulseaudio", "--daemonize=no", "--exit-idle-time=-1",
               "--use-pid-file=no", "-n",
               "--load=module-null-sink sink_name=luxe_test",
               "--load=module-native-protocol-unix socket=" + socket.string() +
                   " auth-anonymous=1"},
              f.env, root / "pulse.log");
  try { wait_for(pulse, [&]() { return fs::exists(socket); }, 5); } catch (...) { std::cerr << read_file(root / "pulse.log"); throw; }
  Child wm({"openbox"}, f.env, root / "wm.log");
  pause_for(.5);
  Child panel({"bwrap", "--die-with-parent", "--ro-bind", "/", "/", "--dev",
               "/dev", "--bind", root.string(), root.string(), "--ro-bind",
               plugins.string(), "/usr/lib/luxepanel/plugins", "--",
               f.panel.string(), "--profile", "test"},
              f.env, root / "panel.log");
  pause_for(2);
  f.run({"scrot", (root / "screen.png").string()});
  f.run({"xdotool", "mousemove", "6", "224", "click", "3"});
  pause_for(1);
  require(read_file(root / "mixer-called") == "mixer",
          "Mixer not invoked exactly once");
  f.run({"scrot", (root / "right.png").string()});
  f.run({"xdotool", "click", "1"});
  pause_for(.5);
  require(panel.alive(), f.log());
  f.run({"scrot", (root / "popup.png").string()});
  require(f.run({"xwininfo", "-root", "-tree"}).find("80x140") !=
              std::string::npos,
          "Volume popup missing");
  for (auto bad : {"CRITICAL", "WARNING", "cannot initialize"})
    require(f.log().find(bad) == std::string::npos, f.log());
  std::cout << "PASS: private ALSA applet, one mixer launch and volume popup\n";
}
