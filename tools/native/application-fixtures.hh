// Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-3.0-or-later
#include <X11/Xlib.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
static void luxup_service(const std::vector<std::string> &args) {
  require(args.size() == 2, "Usage: luxup-service SERVICE UI");
  Scratch dir("luxe-update-test-");
  Bus bus;
  auto slow_state = dir.path / "slow-state";
  Child service({args[0], "--no-refresh"}, {{"LUXUP_TEST_DELAY_STATE", slow_state.string()}}, dir.path / "service.log");
  auto call = [&](const char *method, GVariant *params = nullptr) {
    return bus.call("org.luxe.LuxUp.Service", "/org/luxe/LuxUp",
                    "org.luxe.LuxUp.Service", method, params, 4000);
  };
  auto state = [&]() {
    auto value = call("GetState");
    auto dict = g_variant_get_child_value(value, 0);
    g_variant_unref(value);
    return dict;
  };
  auto ready = [&]() {
    try {
      auto dict = state();
      gboolean result = false, checking = true;
      g_variant_lookup(dict, "has-result", "b", &result);
      g_variant_lookup(dict, "checking", "b", &checking);
      g_variant_unref(dict);
      return result && !checking;
    } catch (const std::exception &) {
      return false;
    }
  };
  auto package = [&]() {
    auto dict = state();
    auto packages = g_variant_lookup_value(dict, "packages", nullptr);
    require(packages && g_variant_n_children(packages), "No package result");
    auto first = g_variant_get_child_value(packages, 0);
    auto name = g_variant_get_child_value(first, 1);
    std::string result = g_variant_get_string(name, nullptr);
    g_variant_unref(name);
    g_variant_unref(first);
    g_variant_unref(packages);
    g_variant_unref(dict);
    return result;
  };
  auto invoke = [&](const char *method, GVariant *params = nullptr) {
    g_variant_unref(call(method, params));
  };
  auto refreshed_at = [&]() {
    auto dict = state();
    gint64 value = 0;
    require(g_variant_lookup(dict, "apt-refreshed-at", "x", &value), "Missing APT refresh timestamp");
    g_variant_unref(dict);
    return value;
  };
  wait_for(service, ready, 4.5);
  auto maps = read_file("/proc/" + std::to_string(service.pid) + "/maps");
  require(maps.find("libgtk") == std::string::npos &&
              maps.find("libgdk") == std::string::npos,
          "Service loaded GTK");
  require(package() == "cached-fixture", "Cached result missing");
  {
    auto dict = state();
    auto rows = g_variant_lookup_value(dict, "packages-v2", G_VARIANT_TYPE("a(isssss)"));
    require(rows && g_variant_n_children(rows) == 1, "Missing scoped package result");
    auto row = g_variant_get_child_value(rows, 0);
    auto scope = g_variant_get_child_value(row, 4);
    auto ref = g_variant_get_child_value(row, 5);
    require(std::string(g_variant_get_string(scope, nullptr)) == "user" &&
            std::string(g_variant_get_string(ref, nullptr)) == "app/org.test.App/x86_64/stable",
            "Scoped package identity lost");
    g_variant_unref(ref); g_variant_unref(scope); g_variant_unref(row);
    g_variant_unref(rows); g_variant_unref(dict);
  }
  require(refreshed_at() == 0, "Cached check unexpectedly claimed a refresh");
  invoke("Check", g_variant_new("(b)", true));
  wait_for(
      service, [&]() { return ready() && package() == "refreshed-fixture"; },
      4.5);
  auto refreshed = refreshed_at();
  require(refreshed > 0, "Successful refresh timestamp missing");
  invoke("Check", g_variant_new("(b)", false));
  wait_for(service, [&]() { return ready() && package() == "cached-fixture"; }, 4.5);
  require(refreshed_at() == refreshed, "Cached check lost refresh timestamp");
  write_file(slow_state, "delay");
  double ui_started = now();
  Child ui({args[1], "--no-refresh"}, {}, dir.path / "ui.log");
  std::string window;
  wait_for(
      ui,
      [&]() {
        Child search(
            {"xdotool", "search", "--onlyvisible", "--name", "^LuxUp$"}, {},
            dir.path / "window");
        if (search.wait(5) != 0)
          return false;
        auto found = lines(read_file(dir.path / "window"));
        if (found.empty())
          return false;
        window = found[0];
        return true;
      },
      3);
  require(now() - ui_started < 2.0, "UI blocked while GetState reply was delayed");
  fs::remove(slow_state);
  pause_for(2.6);
  command({"xdotool", "windowclose", window});
  ui.wait(5);
  require(read_file(dir.path / "ui.log").find("CRITICAL") == std::string::npos,
          "UI critical");
  require(service.alive() && ready(), "Closing UI stopped service");
  invoke("Check", g_variant_new("(b)", true));
  invoke("Cancel");
  wait_for(service, ready, 4.5);
  invoke("Quit");
  require(service.wait(5) == 0, "Service exit failed");
  require(read_file(dir.path / "service.log").find("CRITICAL") ==
              std::string::npos,
          "Service critical");
  std::cout << "PASS: GTK-free update service, result transfer, refresh/cancel "
               "and independent UI lifetime\n";
}
static void frame_wallpaper(const std::vector<std::string> &args) {
  require(args.size() == 1, "Usage: frame-wallpaper FRAME");
  Scratch dir("frame-wallpaper-test-");
  auto folder = dir.path / "images";
  fs::create_directory(folder);
  for (auto [name, color] :
       {std::pair{"a.png", 0x00ff00ffU}, std::pair{"b.png", 0xff0000ffU}}) {
    auto image = gdk_pixbuf_new(GDK_COLORSPACE_RGB, false, 8, 2, 2);
    gdk_pixbuf_fill(image, color);
    bool saved = gdk_pixbuf_save(image, (folder / name).c_str(), "png", nullptr,
                                 nullptr);
    g_object_unref(image);
    require(saved, "Cannot save fixture image");
  }
  Env env = {{"XDG_CONFIG_HOME", (dir.path / "config").string()},
             {"XDG_CACHE_HOME", (dir.path / "cache").string()}};
  auto config = dir.path / "config/luxe/wallpaper.ini",
       cached = dir.path / "cache/frame/wallpaper.png";
  auto invoke = [&](std::vector<std::string> tail, bool success = true) {
    tail.insert(tail.begin(), fs::absolute(args[0]).string());
    Child app(tail, env, dir.path / "command.log");
    require((app.wait(5) == 0) == success, read_file(dir.path / "command.log"));
  };
  auto current = [&](Child &daemon, const fs::path &path) {
    wait_for(
        daemon,
        [&]() {
          return fs::exists(config) &&
                 read_file(config).find("current=" + path.string()) !=
                     std::string::npos;
        },
        3);
  };
  auto idle = [&](Child &daemon) {
    pause_for(.3);
    struct stat before{}, after{};
    require(stat(cached.c_str(), &before) == 0, "Missing cache");
    auto start = ticks(daemon.pid);
    pause_for(3);
    require(stat(cached.c_str(), &after) == 0, "Missing cache");
    auto used = ticks(daemon.pid) - start;
    require(before.st_ino == after.st_ino &&
                before.st_mtim.tv_sec == after.st_mtim.tv_sec &&
                before.st_mtim.tv_nsec == after.st_mtim.tv_nsec,
            "Idle cache repaint loop");
    require(used <= 1,
            "Wallpaper consumed " + std::to_string(used) + " idle CPU ticks");
    std::cout << "PASS: stable cache, idle CPU ticks=" << used << '\n';
  };
  {
    Child daemon({args[0], "--wallpaper-directory", folder.string()}, env,
                 dir.path / "daemon.log");
    current(daemon, folder / "a.png");
    require(fs::exists(cached) &&
                std::distance(fs::directory_iterator(cached.parent_path()),
                              fs::directory_iterator{}) == 1,
            "Unexpected cache contents");
    auto prop = command({"xprop", "-root", "_XROOTPMAP_ID"});
    require(prop.find("pixmap id #") != std::string::npos,
            "Root pixmap missing");
    auto maps = read_file("/proc/" + std::to_string(daemon.pid) + "/maps");
    for (auto bad : {"iris_dri", "swrast_dri", "/SYSV"})
      require(maps.find(bad) == std::string::npos,
              "Unexpected mapped resource");
    invoke({"--set-wallpaper", (dir.path / "absent").string()}, false);
    invoke({"--wallpaper-stop"});
    require(daemon.wait(5) == 0, "Daemon stop failed");
    require(read_file(dir.path / "daemon.log").find("CRITICAL") ==
                std::string::npos,
            "Daemon critical");
  }
  Child daemon({args[0], "--wallpaper-daemon"}, env, dir.path / "daemon.log");
  current(daemon, folder / "b.png");
  invoke({"--set-wallpaper", (folder / "a.png").string()});
  current(daemon, folder / "a.png");
  for (int i = 0; i < 5; i++)
    invoke({"--set-wallpaper", (folder / "a.png").string()});
  require(read_file("/proc/" + std::to_string(daemon.pid) + "/maps")
                  .find("/SYSV") == std::string::npos,
          "Repeated uploads retained SHM");
  idle(daemon);
  if (auto parent = getenv("LUXE_TEST_PARENT_WINDOW")) {
    auto host = XOpenDisplay(getenv("LUXE_TEST_HOST_DISPLAY"));
    require(host, "No host connection");
    try {
      Window root, owner, *children = nullptr;
      unsigned count = 0;
      auto window = std::stoul(parent);
      require(XQueryTree(host, window, &root, &owner, &children, &count),
              "Cannot query test parent");
      for (unsigned i = 0; i < count; i++)
        XResizeWindow(host, children[i], 800, 600);
      if (children)
        XFree(children);
      XResizeWindow(host, window, 800, 600);
      XSync(host, false);
      wait_for(
          daemon,
          [&]() {
            int w = 0, h = 0;
            return gdk_pixbuf_get_file_info(cached.c_str(), &w, &h) &&
                   w == 800 && h == 600;
          },
          3);
      current(daemon, folder / "a.png");
      idle(daemon);
    } catch (...) {
      XCloseDisplay(host);
      throw;
    }
    XCloseDisplay(host);
  }
  invoke({"--wallpaper-stop"});
  require(daemon.wait(5) == 0, "Daemon stop failed");
  require(read_file(dir.path / "daemon.log").find("CRITICAL") ==
              std::string::npos,
          "Daemon critical");
  std::cout << "PASS: root pixmap, PNG cache, persistence, login rotation, no "
               "driver/SHM retention, resize, reload/stop\n";
}
