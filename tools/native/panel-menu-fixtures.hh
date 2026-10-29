// Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later
#include <X11/Xlib.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
static std::string escape_xml(const std::string &s) {
  auto escaped = g_markup_escape_text(s.c_str(), s.size());
  std::string result = escaped;
  g_free(escaped);
  return result;
}
static void panel_focus(const fs::path &self,
                        const std::vector<std::string> &args) {
  require(args.size() == 2, "Usage: panel-focus PANEL CTL");
  PanelFixture f(args[0], "");
  auto root = f.dir.path, ctl = fs::absolute(args[1]);
  write_file(root / "config/luxepanel/test/panels/panel",
             "Global {\n edge=bottom\n widthtype=percent\n width=100\n "
             "height=30\n background=1\n "
             "backgroundfile=/usr/share/luxepanel/images/background.png\n "
             "usefontcolor=1\n fontcolor=#ffffff\n}\nPlugin {\n type=menu\n "
             "Config {\n image=/usr/share/luxepanel/images/luxe-button.png\n "
             "item {\n name=Alpha\n action=/usr/bin/touch " +
                 (root / "wrong").string() +
                 "\n }\n item {\n name=Needle\n action=/usr/bin/touch " +
                 (root / "matched").string() +
                 "\n }\n system {\n }\n }\n}\nPlugin {\n type=dclock\n Config "
                 "{\n ClockFmt=%R\n }\n}\n");
  if (auto theme = getenv("LUXE_MENU_THEME")) {
    fs::create_directory(root / "config/gtk-4.0");
    write_file(root / "config/gtk-4.0/settings.ini",
               "[Settings]\ngtk-theme-name=" + std::string(theme) + "\n");
  }
  write_file(
      root / "openbox.xml",
      "<openbox_config xmlns=\"http://openbox.org/3.4/rc\"><keyboard><keybind "
      "key=\"Super_L\"><action name=\"Execute\"><command>" +
          escape_xml(ctl.string()) +
          " menu</command></action></keybind></keyboard></openbox_config>");
  Child wm({"openbox", "--config-file", (root / "openbox.xml").string()}, f.env,
           root / "wm.log");
  Child panel({f.panel.string(), "--profile", "test"}, f.env,
              root / "panel.log");
  Child entry({(self.parent_path() / "gtk3-fixtures").string(), "focus",
               (root / "entry").string()},
              f.env, root / "fixture.log");
  pause_for(1.5);
  auto target = f.run(
      {"xdotool", "search", "--onlyvisible", "--name", "^Menu focus fixture$"});
  auto outside = [&]() {
    wait_for(
        panel,
        [&]() {
          return fs::exists(root / "entry") &&
                 read_file(root / "entry") == "outside";
        },
        3);
    f.run({"xdotool", "key", "ctrl+a", "BackSpace"});
  };
  for (auto mode :
       {"pointer", "shortcut", "held", "grab", "expiry", "shortcut"}) {
    f.run({"xdotool", "windowactivate", "--sync", target});
    std::string held = mode;
    if (held == "pointer") {
      std::istringstream in(f.run({"xdotool", "getdisplaygeometry"}));
      int w, h;
      in >> w >> h;
      f.run(
          {"xdotool", "mousemove", "20", std::to_string(h - 15), "click", "1"});
    } else if (held == "grab" || held == "expiry") {
      auto display = XOpenDisplay(nullptr);
      require(display, "No X display");
      try {
        require(XGrabKeyboard(display, std::stoul(target), false, GrabModeAsync,
                              GrabModeAsync, CurrentTime) == GrabSuccess,
                "Cannot acquire fixture grab");
        f.run({ctl.string(), "menu"});
        pause_for(held == "expiry" ? 2.4 : .4);
        auto probe = XOpenDisplay(nullptr);
        require(probe, "No probe X display");
        int result = XGrabKeyboard(probe, std::stoul(target), false,
                                   GrabModeAsync, GrabModeAsync, CurrentTime);
        XCloseDisplay(probe);
        require(result == AlreadyGrabbed, "Panel stole competing grab");
        XUngrabKeyboard(display, CurrentTime);
        XSync(display, false);
      } catch (...) {
        XCloseDisplay(display);
        throw;
      }
      XCloseDisplay(display);
    } else if (held == "held") {
      f.run({"xdotool", "keydown", "Super_L"});
      pause_for(.4);
      f.run({"xdotool", "keyup", "Super_L"});
    } else
      f.run({"xdotool", "key", "Super_L"});
    pause_for(.4);
    if (held == "pointer")
      if (auto screenshot = getenv("LUXE_MENU_SCREENSHOT"))
        f.run({"import", "-window", "root", screenshot});
    if (held == "expiry") {
      f.run({"xdotool", "type", "outside"});
      outside();
      continue;
    }
    f.run({"xdotool", "type", "--clearmodifiers", "needle"});
    pause_for(.1);
    f.run({"xdotool", "key", "Return"});
    wait_for(panel, [&]() { return fs::exists(root / "matched"); }, 3);
    require(!fs::exists(root / "wrong"), "Typing failed to filter menu");
    fs::remove(root / "matched");
    pause_for(.15);
    f.run({ctl.string(), "menu"});
    pause_for(.35);
    f.run({"xdotool", "key", "Escape"});
    pause_for(.15);
    f.run({"xdotool", "type", "outside"});
    outside();
  }
  require(f.log().find("CRITICAL") == std::string::npos, f.log());
  std::cout << "PASS: pointer, search/Enter, held keys, competing/expired "
               "grabs and Escape release\n";
}
struct Pixels {
  GdkPixbuf *image = nullptr;
  explicit Pixels(const fs::path &path) {
    image = gdk_pixbuf_new_from_file(path.c_str(), nullptr);
    require(image, "Cannot decode screenshot");
  }
  ~Pixels() { g_object_unref(image); }
  std::array<int, 3> at(int x, int y) {
    require(x >= 0 && y >= 0 && x < gdk_pixbuf_get_width(image) &&
                y < gdk_pixbuf_get_height(image),
            "Pixel outside image");
    auto p = gdk_pixbuf_get_pixels(image) +
             y * gdk_pixbuf_get_rowstride(image) +
             x * gdk_pixbuf_get_n_channels(image);
    return {p[0], p[1], p[2]};
  }
};
static std::array<int, 4> geometry(PanelFixture &f, const std::string &window) {
  std::map<std::string, int> values;
  for (auto &line :
       lines(f.run({"xdotool", "getwindowgeometry", "--shell", window}))) {
    auto pos = line.find('=');
    if (pos != std::string::npos)
      values[line.substr(0, pos)] = std::stoi(line.substr(pos + 1));
  }
  return {values.at("X"), values.at("Y"), values.at("WIDTH"),
          values.at("HEIGHT")};
}
static void panel_effects(const fs::path &self,
                          const std::vector<std::string> &args) {
  require(args.size() == 4, "Usage: panel-effects PANEL CTL TRAY THEMES");
  PanelFixture f(args[0], "");
  auto root = f.dir.path, ctl = fs::absolute(args[1]),
       tray = fs::absolute(args[2]), themes = fs::absolute(args[3]);
  auto config = root / "config/luxepanel/test/panels/panel",
       settings = root / "config/gtk-4.0/settings.ini";
  fs::create_directories(settings.parent_path());
  f.env["XDG_DATA_DIRS"] = themes.string() + ":/usr/share";
  write_file(config,
             "Global {\n edge=bottom\n widthtype=percent\n width=100\n "
             "height=30\n iconsize=30\n}\nPlugin {\n type=menu\n Config {\n "
             "image=/usr/share/luxepanel/images/luxe-button.png\n item {\n "
             "name=Alpha\n action=/usr/bin/touch " +
                 (root / "alpha").string() +
                 "\n }\n item {\n name=Needle\n action=/usr/bin/touch " +
                 (root / "needle").string() +
                 "\n }\n menu {\n name=Category\n item {\n name=Child\n "
                 "action=/usr/bin/touch " +
                 (root / "child").string() +
                 "\n }\n }\n system {\n }\n }\n}\nPlugin {\n type=tray\n}\n");
  write_file(root / "openbox.xml",
             "<openbox_config xmlns=\"http://openbox.org/3.4/rc\"/>");
  Child wm({"openbox", "--config-file", (root / "openbox.xml").string()}, f.env,
           root / "wm.log");
  pause_for(.4);
  auto snapshot = [&](const std::string &name) {
    auto file = root / (name + ".png");
    f.run({"import", "-window", "root", file.string()});
    return std::make_unique<Pixels>(file);
  };
  auto panel_geometry = [&]() {
    auto ids = lines(f.run(
        {"xdotool", "search", "--onlyvisible", "--class", "^luxepanel$"}));
    require(!ids.empty(), "Panel not mapped");
    return geometry(f, ids.back());
  };
  auto check_log = [&](Child &panel) {
    require(panel.alive(), f.log());
    for (auto bad : {"CRITICAL", "Theme parser error"})
      require(f.log().find(bad) == std::string::npos, f.log());
  };
  auto close_color = [](std::array<int, 3> a, std::array<int, 3> b,
                        int tolerance) {
    for (int i = 0; i < 3; i++)
      if (std::abs(a[i] - b[i]) > tolerance)
        return false;
    return true;
  };
  std::vector<fs::path> palettes;
  for (auto &e : fs::directory_iterator(themes / "bridges"))
    palettes.push_back(e.path());
  std::sort(palettes.begin(), palettes.end());
  for (auto &palette : palettes) {
    auto index = g_key_file_new();
    require(g_key_file_load_from_file(index, (palette / "metadata.ini").c_str(),
                                      G_KEY_FILE_NONE, nullptr),
            "Invalid theme metadata");
    auto color = [&](const char *key) {
      auto text = g_key_file_get_string(index, "Theme", key, nullptr);
      require(text, "Missing palette color");
      std::string value = text;
      g_free(text);
      return value;
    };
    auto bg = color("bg"), fg = color("fg");
    g_key_file_unref(index);
    auto rgb = [](std::string s) {
      return std::array<int, 3>{std::stoi(s.substr(1, 2), nullptr, 16),
                                std::stoi(s.substr(3, 2), nullptr, 16),
                                std::stoi(s.substr(5, 2), nullptr, 16)};
    };
    auto expected = rgb(bg), foreground = rgb(fg), outline = expected;
    for (int i = 0; i < 3; i++)
      outline[i] = static_cast<int>(
          std::nearbyint(expected[i] * .78 + foreground[i] * .22));
    write_file(settings, "[Settings]\ngtk-theme-name=Luxe-" +
                             palette.filename().string() +
                             "\ngtk-application-prefer-dark-theme=false\ngtk-"
                             "enable-animations=false\n");
    Child panel({f.panel.string(), "--profile", "test"}, f.env,
                root / "panel.log");
    pause_for(.5);
    auto bounds = panel_geometry();
    require(bounds == std::array<int, 4>{0, 738, 1024, 30},
            "Outline changed panel geometry");
    auto frame = snapshot("frame");
    auto center = frame->at(512, 753), edge = frame->at(512, 738);
    require(close_color(center, expected, 2),
            "Panel fill differs from palette");
    require(close_color(edge, outline, 3) && edge != center,
            "Panel outline missing");
    std::ostringstream hex;
    hex << '#' << std::hex << std::setfill('0');
    for (auto channel : outline)
      hex << std::setw(2) << channel;
    f.run({tray.string(), bg, hex.str()});
    require(f.run({(self.parent_path() / "gtk3-fixtures").string(), "tray",
                   (root / "gtk-tray.png").string()})
                    .find("real GTK3 tray embedding") != std::string::npos,
            "GTK3 tray failed");
    f.run({ctl.string(), "menu"});
    pause_for(.4);
    std::array<int, 4> popup{};
    bool found = false;
    for (int i = 0; i < 40 && !found; i++) {
      for (auto &id : lines(
               f.run({"xdotool", "search", "--onlyvisible", "--name", ".*"}))) {
        auto b = geometry(f, id);
        if (b[2] > 300 && b[2] < 600 && b[3] > 100) {
          popup = b;
          found = true;
          break;
        }
      }
      if (!found)
        pause_for(.05);
    }
    require(found, "Menu did not map: " + f.log());
    auto [x, y, width, height] = popup;
    (void)height;
    f.run({"xdotool", "mousemove", std::to_string(x + width + 30),
           std::to_string(y + 20)});
    pause_for(.15);
    auto base = snapshot("base");
    int px = x + width - 70, py = y + 61, cy = y + 123;
    f.run({"xdotool", "mousemove", std::to_string(px), std::to_string(py)});
    pause_for(.15);
    auto hovered = snapshot("hovered");
    require(base->at(px, py) != hovered->at(px, py),
            "Application hover missing");
    f.run({"xdotool", "mousemove", std::to_string(px), std::to_string(cy)});
    pause_for(.15);
    auto header = snapshot("header");
    require(base->at(px, cy) != header->at(px, cy), "Category hover missing");
    require(base->at(px, py) == header->at(px, py), "Hover stuck after leave");
    if (auto preview = getenv("LUXE_MENU_EFFECTS_PREVIEW")) {
      std::string path = preview;
      size_t pos;
      while ((pos = path.find("{id}")) != std::string::npos)
        path.replace(pos, 4, palette.filename().string());
      require(
          gdk_pixbuf_save(header->image, path.c_str(), "png", nullptr, nullptr),
          "Preview save failed");
    }
    f.run({"xdotool", "type", "needle"});
    f.run({"xdotool", "key", "Return"});
    pause_for(.15);
    require(fs::exists(root / "needle") && !fs::exists(root / "alpha"),
            "Menu filtering/activation failed");
    fs::remove(root / "needle");
    check_log(panel);
    std::cout << "PASS: " << palette.filename().string()
              << " outline/geometry, hover/leave, input and tray palette\n";
  }
  auto tile = gdk_pixbuf_new(GDK_COLORSPACE_RGB, false, 8, 8, 8);
  gdk_pixbuf_fill(tile, 0x525252ff);
  require(gdk_pixbuf_save(tile, (root / "custom.png").c_str(), "png", nullptr,
                          nullptr),
          "Tile save failed");
  g_object_unref(tile);
  for (auto name :
       {"top", "left", "right", "image", "transparent", "high-contrast"}) {
    std::string label = name,
                edge = (label == "top" || label == "left" || label == "right")
                           ? label
                           : "bottom",
                extra,
                theme = label == "high-contrast" ? "HighContrast"
                                                 : "Luxe-tokyo-night";
    if (label == "image")
      extra = "background=1\n backgroundfile=\"" +
              (root / "custom.png").string() + "\"";
    if (label == "transparent")
      extra = "transparent=1\n alpha=0";
    write_file(config, "Global {\n edge=" + edge +
                           "\n widthtype=percent\n width=100\n height=30\n " +
                           extra + "\n}\n");
    write_file(settings, "[Settings]\ngtk-theme-name=" + theme + "\n");
    Child panel({f.panel.string(), "--profile", "test"}, f.env,
                root / "panel.log");
    pause_for(.5);
    auto [x, y, w, h] = panel_geometry();
    bool vertical = edge == "left" || edge == "right";
    require(w == (vertical ? 30 : 1024) && h == (vertical ? 768 : 30),
            "Changed panel allocation");
    auto frame = snapshot("frame");
    auto center = frame->at(x + w / 2, y + h / 2),
         rim = vertical ? frame->at(x, y + h / 2) : frame->at(x + w / 2, y);
    if (label == "image" || label == "transparent") {
      require(rim == center, "Custom background gained outline");
      if (label == "image")
        require(center == std::array<int, 3>{82, 82, 82},
                "Wrong custom background");
    } else {
      require(rim != center, "Outline missing");
      if (label == "high-contrast")
        require(std::abs(rim[0] - center[0]) + std::abs(rim[1] - center[1]) +
                        std::abs(rim[2] - center[2]) >
                    300,
                "Low contrast outline");
    }
    check_log(panel);
    std::cout << "PASS: " << label << " outline/background and allocation\n";
  }
}
