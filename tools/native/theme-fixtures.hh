// Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-3.0-or-later
#include <regex>
#include <memory>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
static void theme_widgets(const fs::path &self,
                          const std::vector<std::string> &args) {
  require(args.size() == 1, "Usage: theme-widgets THEME_ROOT");
  auto root = fs::absolute(args[0]),
       output = getenv("LUXE_THEME_SCREENSHOTS")
                    ? fs::path(getenv("LUXE_THEME_SCREENSHOTS"))
                    : root / "previews";
  fs::create_directories(output);
  std::vector<fs::path> wallpapers, themes;
  for (auto &e : fs::directory_iterator(root / "backgrounds")) {
    require(e.path().extension() != ".svg", "Unexpected SVG background");
    if (e.path().extension() == ".png")
      wallpapers.push_back(e.path());
  }
  require(wallpapers.size() == 6, "Expected six wallpapers");
  std::sort(wallpapers.begin(), wallpapers.end());
  for (auto &path : wallpapers) {
    auto bytes = read_file(path);
    require(bytes.substr(0, 8) == std::string("\x89PNG\r\n\x1a\n", 8),
            "Invalid PNG signature");
    auto source = gdk_pixbuf_new_from_file(path.c_str(), nullptr);
    require(source, "Decode failed");
    bool dimensions = gdk_pixbuf_get_width(source) == 3840 &&
                      gdk_pixbuf_get_height(source) == 2160;
    g_object_unref(source);
    require(dimensions, "Unexpected source dimensions");
    auto image = gdk_pixbuf_new_from_file_at_scale(path.c_str(), 960, 540, true,
                                                   nullptr);
    require(image, "Scale failed");
    bool valid = gdk_pixbuf_get_width(image) == 960 &&
                 gdk_pixbuf_get_height(image) == 540;
    bool saved = gdk_pixbuf_save(
        image, (output / (path.stem().string() + "-wallpaper.png")).c_str(),
        "png", nullptr, nullptr);
    g_object_unref(image);
    require(valid && saved, "Wallpaper preview failed");
  }
  for (auto &e : fs::directory_iterator(root / "bridges"))
    themes.push_back(e.path());
  std::sort(themes.begin(), themes.end());
  int minor = std::stoi(trim(command(
      {(self.parent_path() / "theme-widget-gtk4").string(), "--minor"})));
  for (auto toolkit : {"gtk3", "gtk4", "adw"})
    for (auto &theme : themes) {
      Scratch dir("luxe-theme-widget-");
      for (auto version : {"gtk-3.0", "gtk-4.0"}) {
        fs::create_directory(dir.path / version);
        write_file(dir.path / version / "settings.ini",
                   "[Settings]\ngtk-theme-name=Luxe-" +
                       theme.filename().string() + "\n");
      }
      if (std::string(toolkit) == "adw") {
        std::string css =
            "@import url(\"" + (theme / "colors.css").string() + "\");\n";
        if (minor >= 16)
          css += "@import url(\"" + (theme / "modern.css").string() + "\");\n";
        write_file(dir.path / "gtk-4.0/gtk.css", css);
      }
      Env env = {{"XDG_CONFIG_HOME", dir.path.string()},
                 {"XDG_DATA_DIRS", root.string() + ":/usr/share"},
                 {"G_DEBUG", "fatal-warnings"}};
      auto text = command(
          {(self.parent_path() / ("theme-widget-" + std::string(toolkit)))
               .string(),
           theme.string()},
          env, 12);
      std::cout << text;
    }
  std::cout << "PASS: all six global GTK3, GTK4 and libadwaita themes render "
               "without CSS warnings\n";
}
static void real_header(const fs::path &self,
                        const std::vector<std::string> &args) {
  require(args.size() == 2, "Usage: real-header TERMINAL THEMES");
  PanelFixture f(args[0], "");
  auto root = f.dir.path, binary = fs::absolute(args[0]),
       themes = fs::absolute(args[1]);
  f.env["XDG_DATA_DIRS"] = themes.string() + ":/usr/share";
  write_file(root / "openbox.xml",
             "<openbox_config xmlns=\"http://openbox.org/3.4/rc\"/>");
  Child wm({"openbox", "--config-file", (root / "openbox.xml").string()}, f.env,
           root / "wm.log");
  pause_for(.4);
  const std::regex compact_block(
      R"(/\* BEGIN luxe compact headerbars \*/[\s\S]*?/\* END luxe compact headerbars \*/)");
  auto baseline_data = root / "baseline-data";
  auto baseline_theme = baseline_data / "themes/Luxe-tokyo-night";
  fs::create_directories(baseline_theme.parent_path());
  fs::copy(themes / "themes/Luxe-tokyo-night", baseline_theme,
           fs::copy_options::recursive | fs::copy_options::copy_symlinks);
  int stripped = 0;
  for (auto &entry : fs::recursive_directory_iterator(baseline_theme)) {
    if (!entry.is_regular_file() || entry.is_symlink() || entry.path().extension() != ".css") continue;
    auto text = read_file(entry.path());
    if (std::regex_search(text, compact_block)) {
      write_file(entry.path(), std::regex_replace(text, compact_block, ""));
      stripped++;
    }
  }
  require(stripped > 0, "Baseline theme did not contain compact CSS");
  std::map<std::string, int> sizes;
  for (std::string mode : {"before", "compact"}) {
    f.env["XDG_DATA_DIRS"] = (mode == "before" ? baseline_data.string() + ":" : "") +
                              themes.string() + ":/usr/share";
    auto gtk = root / mode / "gtk-4.0";
    fs::create_directories(gtk);
    f.env["XDG_CONFIG_HOME"] = gtk.parent_path().string();
    write_file(gtk / "settings.ini",
               "[Settings]\ngtk-theme-name=Luxe-tokyo-night\ngtk-font-name="
               "Sans 10\ngtk-decoration-layout=:minimize,maximize,close\n");
    auto css = read_file(themes / "bridges/tokyo-night/colors.css");
    if (mode == "before")
      css = std::regex_replace(css, compact_block, "");
    write_file(gtk / "colors.css", css);
    fs::create_directory(gtk.parent_path() / "luxeterm");
    write_file(gtk.parent_path() / "luxeterm/luxeterm.conf",
               "[general]\nfollow_desktop_theme=false\n");
    write_file(gtk / "gtk.css",
               "@import url(\"colors.css\");\n@import url(\"" +
                   (themes / "bridges/tokyo-night/modern.css").string() +
                   "\");\n");
    auto marker = root / (mode + "-input");
    Child terminal({binary.string(), "--no-remote",
                    "--title=Compact real terminal", "-e", self.string(),
                    "header-child", marker.string()},
                   f.env, root / (mode + ".log"));
    auto visible = [&]() {
      Child search(
          {"xdotool", "search", "--onlyvisible", "--class", "^luxeterm$"},
          f.env, root / "windows");
      search.wait(5);
      return lines(trim(read_file(root / "windows")));
    };
    wait_for(terminal, [&]() { return !visible().empty(); }, 3);
    auto windows = visible();
    auto window = windows.back();
    pause_for(.3);
    auto ext = f.run({"xprop", "-id", window, "_NET_FRAME_EXTENTS"});
    require(ext.substr(ext.find('=') + 1).find("0, 0, 0, 0") !=
                std::string::npos,
            "Unexpected CSD frame extents");
    f.run({"xdotool", "windowactivate", "--sync", window});
    auto [x, y, w, h] = geometry(f, window);
    auto shot = root / (mode + ".png");
    f.run({"import", "-window", "root", "-crop",
           std::to_string(w) + "x" + std::to_string(h) + "+" +
               std::to_string(x) + "+" + std::to_string(y),
           shot.string()});
    Pixels pixels(shot);
    for (int row = 1; row < 90; row++) {
      auto pixel = pixels.at(w / 2, row);
      if (*std::max_element(pixel.begin(), pixel.end()) < 10) {
        sizes[mode] = row;
        break;
      }
    }
    require(sizes.count(mode), "Content row not found");
    if (mode == "compact") {
      require(sizes[mode] >= 24 && sizes[mode] <= 34 &&
                  sizes[mode] <= sizes["before"] * .7,
              "Header was not compacted: before="+std::to_string(sizes["before"])+" compact="+std::to_string(sizes[mode]));
      if (auto preview = getenv("LUXE_REAL_HEADER_PREVIEW"))
        require(gdk_pixbuf_save(pixels.image, preview, "png", nullptr, nullptr),
                "Preview save failed");
      auto click = [&](int offset) {
        auto [cx, cy, cw, ch] = geometry(f, window);
        (void)ch;
        f.run({"xdotool", "mousemove", std::to_string(cx + cw - offset),
               std::to_string(cy + 16), "click", "1"});
      };
      auto state = [&]() {
        return f.run({"xprop", "-id", window, "_NET_WM_STATE"});
      };
      click(105);
      pause_for(.15);
      wait_for(
          terminal, [&]() { return visible().size() > windows.size(); }, 3);
      f.run({"xdotool", "key", "Escape"});
      click(52);
      wait_for(
          terminal,
          [&]() {
            return state().find("_NET_WM_STATE_MAXIMIZED_VERT") !=
                   std::string::npos;
          },
          3);
      click(52);
      wait_for(
          terminal,
          [&]() {
            return state().find("_NET_WM_STATE_MAXIMIZED_VERT") ==
                   std::string::npos;
          },
          3);
      click(78);
      wait_for(
          terminal,
          [&]() {
            return state().find("_NET_WM_STATE_HIDDEN") != std::string::npos;
          },
          3);
      f.run({"xdotool", "windowactivate", "--sync", window});
      auto bounds = geometry(f, window);
      f.run({"xdotool", "mousemove", std::to_string(bounds[0] + 80),
             std::to_string(bounds[1] + 80), "click", "1"});
      pause_for(.15);
      f.run({"xdotool", "type", "--delay", "20", "header input"});
      f.run({"xdotool", "key", "Return"});
      wait_for(terminal, [&]() { return fs::exists(marker); }, 3);
      require(read_file(marker) == "header input\n", "Terminal lost input");
      click(27);
      terminal.wait(3);
      require(read_file(root / (mode + ".log")).find("CRITICAL") ==
                  std::string::npos,
              "Terminal critical");
    }
  }
  std::cout << "PASS: real terminal content row " << sizes["before"] << " -> "
            << sizes["compact"] << "; pointer controls and input\n";
}
static void openbox_compact(const fs::path &self,
                            const std::vector<std::string> &args) {
  require(args.size() == 2, "Usage: openbox-compact THEMES LEGACY");
  auto themes = fs::absolute(args[0]);
  PanelFixture f("/usr/bin/true", "");
  auto root = f.dir.path;
  f.env["XDG_DATA_HOME"] = (root / "data").string();
  f.env["XDG_DATA_DIRS"] = (root / "data").string() + ":/usr/share";
  f.env["GTK_CSD"] = "0";
  auto base = root / "data/themes/Before";
  fs::create_directories(base.parent_path());
  fs::copy(themes / "Luxe-dark", base, fs::copy_options::recursive);
  auto merc = base / "openbox-3/themerc";
  auto text = std::regex_replace(
      read_file(merc), std::regex("^padding.width: 5$", std::regex::multiline),
      "padding.width: 8");
  text = std::regex_replace(
      text, std::regex("^padding.height: 0$", std::regex::multiline),
      "padding.height: 5");
  write_file(merc, text);
  std::vector<fs::path> sources;
  for (auto &e : fs::directory_iterator(themes))
    if (e.path().filename().string().rfind("Luxe-", 0) == 0) {
      sources.push_back(e.path());
      fs::copy(e.path(), root / "data/themes" / e.path().filename(),
               fs::copy_options::recursive);
    }
  std::sort(sources.begin(), sources.end());
  require(!sources.empty(), "No generated themes");
  auto config = root / "openbox.xml";
  auto select = [&](const std::string &name, int size) {
    auto font = "<name>sans</name><size>" + std::to_string(size) + "</size>";
    write_file(
        config,
        "<openbox_config xmlns=\"http://openbox.org/3.4/rc\"><theme><name>" +
            escape_xml(name) +
            "</name><titleLayout>NLIMC</titleLayout><font "
            "place=\"ActiveWindow\">" +
            font + "</font><font place=\"InactiveWindow\">" + font +
            "</font></theme></openbox_config>");
  };
  select("Before", 10);
  Child wm({"openbox", "--config-file", config.string()}, f.env,
           root / "wm.log");
  Child client({(self.parent_path() / "gtk3-fixtures").string(), "compact",
                (root / "closed").string(), (root / "window-id").string()},
               f.env, root / "client.log");
  std::unique_ptr<Display, decltype(&XCloseDisplay)> display(XOpenDisplay(nullptr), XCloseDisplay);
  require(display != nullptr, "Cannot open X display for Openbox readiness check");
  Atom wm_check = XInternAtom(display.get(), "_NET_SUPPORTING_WM_CHECK", False);
  std::string window, last_error;
  bool map_requested = false;
  const char *unmap = std::getenv("LUXE_TEST_COMPACT_UNMAP");
  require(!unmap || std::string(unmap) == "frame", "Invalid compact fixture unmap mode");
  bool unmap_tested = false;
  double restore_at = 0;
  auto activate_window = [&](Window xid) {
    XEvent event{};
    event.xclient.type = ClientMessage;
    event.xclient.window = xid;
    event.xclient.message_type = XInternAtom(display.get(), "_NET_ACTIVE_WINDOW", False);
    event.xclient.format = 32;
    event.xclient.data.l[0] = 2; // Pager-style request, matching xdotool.
    XSendEvent(display.get(), DefaultRootWindow(display.get()), False,
               SubstructureRedirectMask | SubstructureNotifyMask, &event);
    XFlush(display.get());
  };
  try {
    wait_for(client, [&]() {
      require(wm.alive(), "Openbox exited during startup");
      try {
        if (!fs::exists(root / "window-id")) {
          last_error = "GTK client has not published its X11 window ID";
          return false;
        }
        window = trim(read_file(root / "window-id"));
        require(!window.empty() && window.find_first_not_of("0123456789") == std::string::npos,
                "Invalid X11 window ID from GTK client");
        Atom type = None;
        int format = 0;
        unsigned long count = 0, remaining = 0;
        unsigned char *value = nullptr;
        int status = XGetWindowProperty(display.get(), DefaultRootWindow(display.get()),
                                        wm_check, 0, 1, False, XA_WINDOW,
                                        &type, &format, &count, &remaining, &value);
        bool ready = status == Success && type == XA_WINDOW && format == 32 &&
                     count == 1 && value && *reinterpret_cast<unsigned long *>(value) != None;
        if (value) XFree(value);
        if (!ready) last_error = "Openbox has not published its window-manager identity";
        if (!ready || window.empty()) return false;
        auto xid = static_cast<Window>(std::stoul(window));
        XWindowAttributes attributes{};
        if (!XGetWindowAttributes(display.get(), xid, &attributes)) {
          last_error = "Cannot inspect GTK client window";
          return false;
        }
        if (attributes.map_state != IsViewable) {
          last_error = attributes.map_state == IsUnmapped ?
              "GTK client window is unmapped" : "GTK client window has an unmapped ancestor";
          if (!map_requested) {
            std::cout << "Recovering " << last_error << '\n';
            // The client may have requested mapping while Openbox was still
            // starting. Ask the ready WM to map/restore this test window.
            XMapWindow(display.get(), xid);
            if (attributes.map_state == IsUnviewable) {
              // An unmapped frame can disagree with Openbox's view of the
              // client. Minimize first, then ask it to restore the window.
              XIconifyWindow(display.get(), xid, DefaultScreen(display.get()));
              restore_at = now() + .3;
            } else {
              activate_window(xid);
            }
            XFlush(display.get());
            map_requested = true;
          }
          if (restore_at > 0 && now() >= restore_at) {
            activate_window(xid);
            restore_at = 0;
          }
          return false;
        }
        auto frame = f.run({"xprop", "-id", window, "_NET_FRAME_EXTENTS"});
        if (frame.find(" = ") == std::string::npos) {
          last_error = "Openbox has not finished managing the fixture window";
          return false;
        }
        if (unmap && !unmap_tested) {
          Window root_window = None, parent = None, *children = nullptr;
          unsigned int count = 0;
          require(XQueryTree(display.get(), xid, &root_window, &parent, &children, &count),
                  "Cannot inspect compact fixture frame");
          if (children) XFree(children);
          if (parent == None || parent == root_window) {
            last_error = "Openbox has not reparented the fixture window";
            return false;
          }
          // Inject the regression only after the client has published its ID
          // and startup recovery has produced a managed, viewable window.
          XUnmapWindow(display.get(), parent);
          XSync(display.get(), False);
          std::cout << "Testing recovery from an unmapped Openbox frame\n";
          unmap_tested = true;
          map_requested = false;
          restore_at = 0;
          return false;
        }
        return true;
      } catch (const std::exception &e) {
        last_error = e.what();
        return false;
      }
    }, 20);
  } catch (...) {
    std::cerr << "Last readiness check: " << last_error << '\n'
              << "Openbox log:\n" << read_file(root / "wm.log")
              << "Client log:\n" << read_file(root / "client.log");
    throw;
  }
  f.run({"xdotool", "windowactivate", "--sync", window});
  auto extents = [&]() {
    auto text = f.run({"xprop", "-id", window, "_NET_FRAME_EXTENTS"});
    text = text.substr(text.find('=') + 1);
    std::regex number("[0-9]+");
    std::vector<int> out;
    for (std::sregex_iterator i(text.begin(), text.end(), number), end;
         i != end; ++i)
      out.push_back(std::stoi(i->str()));
    require(out.size() == 4, "Missing frame extents");
    return out;
  };
  auto state = [&]() {
    return f.run({"xprop", "-id", window, "_NET_WM_STATE"});
  };
  auto buttons = [&]() {
    auto tree = f.run({"xwininfo", "-id", window, "-tree"});
    std::smatch parent;
    require(std::regex_search(tree, parent,
                              std::regex("Parent window id: (0x[0-9a-f]+)")),
            "Missing frame parent");
    tree = f.run({"xwininfo", "-id", parent[1], "-tree"});
    std::vector<std::array<int, 3>> buttons;
    std::regex pattern(
        R"((0x[0-9a-f]+).*? (\d+)x(\d+)[+-]\d+[+-]\d+  ([+-]\d+)([+-]\d+))");
    for (auto &line : lines(tree)) {
      std::smatch m;
      if (std::regex_search(line, m, pattern)) {
        int width = std::stoi(m[2]), height = std::stoi(m[3]);
        if (width >= 8 && width <= 24 && width == height &&
            f.run({"xwininfo", "-id", m[1]}).find("Map State: IsViewable") !=
                std::string::npos)
          buttons.push_back({std::stoi(m[4]), std::stoi(m[5]), width});
      }
    }
    std::sort(buttons.rbegin(), buttons.rend());
    require(buttons.size() >= 3, "Decoration buttons missing: " + tree);
    return buttons;
  };
  auto click = [&](std::array<int, 3> b) {
    f.run({"xdotool", "mousemove", std::to_string(b[0] + b[2] / 2),
           std::to_string(b[1] + b[2] / 2), "click", "1"});
  };
  std::map<int, int> before;
  for (int size : {8, 10, 14}) {
    select("Before", size);
    f.run({"openbox", "--reconfigure"});
    pause_for(.12);
    before[size] = extents()[2];
  }
  for (auto &source : sources) {
    auto name = source.filename().string();
    for (int size : {8, 10, 14}) {
      select(name, size);
      f.run({"openbox", "--reconfigure"});
      pause_for(.12);
      auto current = extents();
      require(current[2] == before[size] - 10 && current[0] == 1 &&
                  current[1] == 1,
              "Incorrect compact frame: " + name);
      std::cout << "PASS: " << name << " font " << size << ", top extent "
                << before[size] << " -> " << current[2] << '\n';
    }
    select(name, 10);
    f.run({"openbox", "--reconfigure"});
    pause_for(.12);
    click(buttons()[1]);
    wait_for(
        client,
        [&]() {
          return state().find("_NET_WM_STATE_MAXIMIZED_VERT") !=
                 std::string::npos;
        },
        3);
    click(buttons()[1]);
    wait_for(
        client,
        [&]() {
          return state().find("_NET_WM_STATE_MAXIMIZED_VERT") ==
                 std::string::npos;
        },
        3);
    pause_for(.15);
    click(buttons()[2]);
    wait_for(
        client,
        [&]() {
          return state().find("_NET_WM_STATE_HIDDEN") != std::string::npos;
        },
        3);
    f.run({"xdotool", "windowactivate", "--sync", window});
    pause_for(.15);
    require(client.alive(), "Client closed early");
    if (source == sources.back()) {
      click(buttons()[0]);
      double deadline = now() + 3;
      while (!fs::exists(root / "closed") && now() < deadline)
        pause_for(.05);
      require(fs::exists(root / "closed"), "Close button failed");
    }
    if (name == "Luxe-dark")
      if (auto preview = getenv("LUXE_COMPACT_PREVIEW"))
        f.run({"import", "-window", "root", preview});
    std::cout << "PASS: " << name << " pointer maximize/minimize"
              << (source == sources.back() ? "/close" : "") << '\n';
  }
  require(read_file(root / "client.log").find("CRITICAL") == std::string::npos,
          "Client critical");
  require(read_file(root / "wm.log").find("Unable to load the theme") ==
              std::string::npos,
          "Theme failed to load");
}
