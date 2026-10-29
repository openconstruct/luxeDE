// Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
static std::string next_arg(const std::vector<std::string> &args, size_t &i) {
  require(i + 1 < args.size(), "Missing value for " + args[i]);
  return args[++i];
}
static void theme_memory(const std::vector<std::string> &args) {
  int repeat = 2;
  double delay = 3;
  std::string icon_root;
  bool icons_only = false;
  for (size_t i = 0; i < args.size(); ++i) {
    if (args[i] == "--repeat")
      repeat = std::stoi(next_arg(args, i));
    else if (args[i] == "--delay")
      delay = std::stod(next_arg(args, i));
    else if (args[i] == "--icon-root")
      icon_root = next_arg(args, i);
    else if (args[i] == "--icons-only")
      icons_only = true;
    else
      throw std::runtime_error("Unknown argument: " + args[i]);
  }
  require(repeat >= 1 && repeat <= 3 && delay >= 1 && delay <= 5,
          "repeat must be 1..3 and delay 1..5");
  struct Case {
    std::string theme, icons;
    bool bridge;
  };
  std::vector<Case> cases = {{"Adwaita", "Adwaita", false},
                             {"Adwaita", "Adwaita", true},
                             {"Luxe-tokyo-night", "Adwaita", false},
                             {"Luxe-tokyo-night", "Adwaita", true},
                             {"Luxe-tokyo-night", "Luxe-Dark", true}};
  if (icons_only)
    cases.erase(cases.begin(), cases.begin() + 3);
  require(repeat * 2 * cases.size() * delay <= 75,
          "Sampling must stay below 75 seconds");
  for (int r = 1; r <= repeat; ++r)
    for (std::string renderer : {"default", "cairo"})
      for (auto &c : cases) {
        Scratch dir("luxe-theme-memory-");
        auto env = private_env(dir.path);
        for (auto key : {"GTK_PATH", "GTK_DATA_PREFIX", "GTK_EXE_PREFIX",
                         "LIBGL_ALWAYS_SOFTWARE"})
          env[key] = std::nullopt;
        env["GTK_THEME"] = c.theme;
        if (renderer != "default")
          env["GSK_RENDERER"] = renderer;
        if (!icon_root.empty())
          env["XDG_DATA_DIRS"] = icon_root + ":/usr/local/share:/usr/share";
        gtk_settings(dir.path, c.theme, c.icons, c.bridge);
        auto log = dir.path / "application.log";
        Child app(
            {"/usr/bin/luxeterm", "--no-remote", "-e", "/bin/sleep", "30"}, env,
            log);
        pause_for(delay);
        healthy(app, log);
        auto result = memory(app.pid);
        result.update(
            {{"repeat", r},
             {"icon_root", icon_root.empty() ? Json(nullptr) : Json(icon_root)},
             {"theme", c.theme},
             {"icons", c.icons},
             {"bridge", c.bridge},
             {"renderer", renderer},
             {"diagnostics", read_file(log)}});
        std::cout << result.dump() << std::endl;
      }
}
static std::vector<std::string> sandbox(const fs::path &root,
                                        const fs::path &tested,
                                        const std::vector<std::string> &extra) {
  require(getenv("HOME"), "HOME is required for the sandbox");
  std::vector<std::string> command = {"bwrap",
                                      "--die-with-parent",
                                      "--ro-bind",
                                      "/",
                                      "/",
                                      "--bind",
                                      (root / "home").string(),
                                      getenv("HOME"),
                                      "--bind",
                                      root.string(),
                                      root.string(),
                                      "--dev-bind",
                                      "/dev",
                                      "/dev",
                                      "--",
                                      tested.string()};
  command.insert(command.end(), extra.begin(), extra.end());
  return command;
}
static void resident_memory(const fs::path &source,
                            const std::vector<std::string> &args) {
  require(
      args.size() == 3,
      "Choose --session|--clipboard|--notify|--luxup|--desktop|--panel|--video "
      "OLD NEW");
  std::string kind = args[0];
  require(kind.rfind("--", 0) == 0, "Expected comparison kind");
  kind = kind.substr(2);
  require(std::set<std::string>{"session", "clipboard", "notify", "luxup",
                                "desktop", "panel", "video"}
              .count(kind),
          "Invalid comparison kind");
  for (int repeat = 1; repeat <= 2; ++repeat)
    for (int cohort = 0; cohort < 2; ++cohort) {
      auto binary = fs::canonical(args[cohort + 1]);
      Scratch dir("luxe-resident-memory-");
      auto env = private_env(dir.path);
      gtk_settings(dir.path, "Adwaita", "Adwaita");
      std::vector<std::string> extra;
      if (kind == "session") {
        auto profile = dir.path / "config/luxesession/LightTest";
        fs::create_directories(profile);
        write_file(profile / "desktop.conf",
                   "[Session]\ndisable_autostart=all\n[State]\nguess_default="
                   "false\n[Dbus]\nlxde=true\n");
        extra = {"--session", "LightTest", "--de", "X-Luxe", "--noautostart"};
      } else if (kind == "luxup")
        extra = {"--background", "--no-refresh"};
      else if (kind == "desktop")
        extra = {"--desktop", "--profile", "LightTest"};
      else if (kind == "panel") {
        extra = {"--profile", "Luxe"};
        auto profile = dir.path / "config/luxepanel/Luxe/panels";
        fs::create_directories(profile);
        fs::copy_file(source / "luxepanel/data/default/panels/panel",
                      profile / "panel");
      } else if (kind == "video") {
        require(getenv("LUXE_TEST_VIDEO"), "Set LUXE_TEST_VIDEO");
        extra = {getenv("LUXE_TEST_VIDEO")};
        env["GSETTINGS_SCHEMA_DIR"] =
            (binary.parent_path() / "schemas").string();
        env["GSK_RENDERER"] = "cairo";
      }
      auto tested = dir.path / "program";
      fs::copy_file(binary, tested);
      auto log = dir.path / "log";
      Child app(sandbox(dir.path, tested, extra), env, log);
      pause_for(kind == "video" ? 1 : 3);
      healthy(app, log);
      auto pid = application_pid(app.pid, tested);
      auto metrics = memory(pid);
      auto status = proc_fields(pid, "status");
      if (status.contains("VmHWM"))
        metrics["own_vm_hwm_kib"] = status["VmHWM"];
      if (kind != "video") {
        auto before = ticks(pid);
        pause_for(2);
        metrics["idle_cpu_ticks_2s"] = ticks(pid) - before;
        metrics["clock_ticks_per_second"] = sysconf(_SC_CLK_TCK);
      }
      metrics.update({{"package", kind},
                      {"cohort", cohort ? "new" : "old"},
                      {"binary", args[cohort + 1]},
                      {"repeat", repeat},
                      {"sample_pid", pid},
                      {"diagnostics", read_file(log)}});
      std::cout << metrics.dump() << std::endl;
    }
}
static Json active_sample(Child &child) {
  require(child.alive(), "Workload exited early");
  auto pid = child.pid;
  auto memory =
      select_fields(proc_fields(pid, "smaps_rollup"),
                    {"Rss", "Pss", "Pss_Anon", "Pss_File", "Pss_Shmem"});
  memory["VmHWM"] = proc_fields(pid, "status").value("VmHWM", 0LL);
  memory["cpu_ticks"] = ticks(pid);
  std::set<std::string> drivers;
  for (auto &line : lines(read_file("/proc/" + std::to_string(pid) + "/maps")))
    if (line.find("libLLVM") != std::string::npos ||
        line.find("libgallium") != std::string::npos) {
      std::istringstream in(line);
      std::string part, last;
      while (in >> part)
        last = part;
      drivers.insert(last);
    }
  memory["drivers"] = drivers;
  return memory;
}
static void active_memory(const fs::path &source,
                          const std::vector<std::string> &args) {
  require(args.size() == 2 || (args.size() == 4 && args[2] == "--capture"),
          "Usage: ProfileActiveMemory THEME OUTPUT [--capture PNG]");
  Json records = Json::array();
  std::vector<std::string> names = {"luxeterm", "pcmanager", "luxesettings",
                                    "frame"};
  for (int repeat = 1; repeat <= 2; ++repeat)
    for (auto &kind : repeat == 1
                          ? std::vector<std::string>{"baseline", "candidate"}
                          : std::vector<std::string>{"candidate", "baseline"}) {
      Scratch dir("luxe-active-memory-");
      auto env = private_env(dir.path);
      env["GDK_DISABLE"] = std::nullopt;
      env["GTK_THEME"] = args[0];
      gtk_settings(dir.path, args[0], "Luxe-Dark");
      auto files = dir.path / "files";
      fs::create_directory(files);
      for (int i = 0; i < 2000; ++i) {
        std::ostringstream name;
        name << "file-" << std::setw(4) << std::setfill('0') << i << ".txt";
        write_file(files / name.str(), "Fixture\n");
      }
      image(dir.path / "image.png", 3840, 2160);
      // The terminal workload needs no interpreter other than the existing
      // shell.
      std::map<std::string, std::vector<std::string>> extras = {
          {"luxeterm",
           {"--no-remote", "-e", "/bin/sh", "-c",
            "i=0; while [ \"$i\" -lt 2000 ]; do printf 'line %d: terminal "
            "rendering and scrollback\\n' \"$i\"; i=$((i+1)); done; exec sleep "
            "60"}},
          {"pcmanager", {"--no-desktop", files.string()}},
          {"luxesettings", {}},
          {"frame", {(dir.path / "image.png").string()}}};
      std::map<std::string, fs::path> binaries;
      std::map<std::string, std::unique_ptr<Child>> apps;
      double started = now();
      for (auto &name : names) {
        auto binary = source /
                      (kind == "baseline" ? "build/lean-renderer-baseline"
                                          : "build/lean-renderer-" + name) /
                      name;
        binaries[name] = binary;
        std::vector<std::string> cmd = {binary.string()};
        cmd.insert(cmd.end(), extras[name].begin(), extras[name].end());
        apps[name] = std::make_unique<Child>(
            cmd, env, dir.path / (name + ".log"), false, dir.path);
      }
      long long peak = 0;
      double deadline = now() + 8;
      while (now() < deadline) {
        long long sum = 0;
        bool ready = true;
        for (auto &[name, app] : apps) {
          require(app->alive(), "Workload exited: " + name + ": " +
                                    read_file(dir.path / (name + ".log")));
          try {
            sum += active_sample(*app)["Rss"].get<long long>();
          } catch (const Json::exception &) {
            ready = false;
          } catch (const std::runtime_error &) {
            require(app->alive(), "Workload exited");
            ready = false;
          }
        }
        if (ready)
          peak = std::max(peak, sum);
        pause_for(.05);
      }
      Json before = Json::object(), after = Json::object();
      for (auto &[name, app] : apps)
        before[name] = active_sample(*app);
      double interval = now();
      pause_for(3);
      for (auto &[name, app] : apps)
        after[name] = active_sample(*app);
      double elapsed = now() - interval;
      long long pss = 0, anon = 0;
      Json executables = Json::object(), logs = Json::object();
      for (auto &name : names) {
        after[name]["interval_cpu_percent"] =
            (after[name]["cpu_ticks"].get<double>() -
             before[name]["cpu_ticks"].get<double>()) /
            sysconf(_SC_CLK_TCK) / elapsed * 100;
        pss += after[name]["Pss"].get<long long>();
        anon += after[name]["Pss_Anon"].get<long long>();
        executables[name] = {{"path", binaries[name].string()},
                             {"sha256", digest(binaries[name])}};
        logs[name] = read_file(dir.path / (name + ".log"));
      }
      if (args.size() == 4 && kind == "candidate")
        command({"import", "-window", "root", fs::absolute(args[3]).string()});
      records.push_back({{"kind", kind},
                         {"theme", args[0]},
                         {"applications", after},
                         {"concurrent_polled_peak_rss_kib", peak},
                         {"total_pss_kib", pss},
                         {"total_anonymous_kib", anon},
                         {"executables", executables},
                         {"startup_sample_seconds", interval - started},
                         {"cpu_interval_seconds", elapsed},
                         {"logs", logs},
                         {"repeat", repeat}});
      std::cout << kind << " " << args[0] << " " << pss << " " << anon
                << std::endl;
    }
  report(args[1], records);
}
static void compare_settings(const std::vector<std::string> &args) {
  std::map<std::string, std::string> options;
  for (size_t i = 0; i < args.size(); ++i) {
    auto key = args[i];
    require(std::set<std::string>{"--reference", "--prototype", "--output",
                                  "--renderer", "--theme-data", "--repeat"}
                .count(key),
            "Unknown argument: " + key);
    options[key] = next_arg(args, i);
  }
  for (auto key : {"--reference", "--prototype", "--output", "--renderer"})
    require(options.count(key), std::string("Missing ") + key);
  auto renderer = options["--renderer"];
  require(renderer == "default" || renderer == "cairo", "Invalid renderer");
  int repeats = options.count("--repeat") ? std::stoi(options["--repeat"]) : 2;
  require(repeats == 1 || repeats == 2, "repeat must be 1 or 2");
  auto output = fs::absolute(options["--output"]);
  fs::create_directories(output);
  Json results = Json::array();
  std::vector<std::pair<std::string, std::string>> cases = {
      {"Default", ""}, {"Default:dark", ""}};
  for (auto name : {"light", "dark", "nord", "mocha", "dracula", "tokyo-night"})
    cases.push_back({std::string("Luxe-") + name, name});
  for (int r = 1; r <= repeats; ++r)
    for (auto &[theme, bridge] : cases)
      for (auto pair :
           {std::pair{"adwaita", "--reference"}, {"gtk4", "--prototype"}}) {
        Scratch dir("luxe-settings-compare-");
        auto env = private_env(dir.path);
        for (auto key : {"GTK_PATH", "GTK_DATA_PREFIX", "GTK_EXE_PREFIX",
                         "LIBGL_ALWAYS_SOFTWARE"})
          env[key] = std::nullopt;
        env["GTK_THEME"] = theme;
        if (renderer != "default")
          env["GSK_RENDERER"] = renderer;
        fs::path theme_data;
        if (options.count("--theme-data")) {
          theme_data = fs::absolute(options["--theme-data"]);
          fs::create_directory_symlink(theme_data / "themes",
                                       dir.path / "data/themes");
        }
        auto gtk = dir.path / "config/gtk-4.0";
        fs::create_directories(gtk);
        write_file(
            gtk / "settings.ini",
            "[Settings]\ngtk-icon-theme-name=Adwaita\ngtk-font-name=Sans 10\n");
        if (!bridge.empty()) {
          auto folder = (theme_data.empty() ? fs::path("/usr/share/luxe/themes")
                                            : theme_data / "bridges") /
                        bridge;
          write_file(gtk / "gtk.css", read_file(folder / "colors.css") + "\n" +
                                          read_file(folder / "modern.css"));
        }
        auto log = dir.path / "log";
        Child app({fs::absolute(options[pair.second]).string()}, env, log);
        pause_for(2);
        healthy(app, log);
        auto m = proc_fields(app.pid, "smaps_rollup");
        if (r == 1 && renderer == "cairo") {
          auto ids = lines(command({"xdotool", "search", "--onlyvisible",
                                    "--pid", std::to_string(app.pid)},
                                   env));
          require(!ids.empty(), "No settings window");
          auto label = theme;
          std::replace(label.begin(), label.end(), ':', '-');
          command({"import", "-window", ids[0],
                   (output / (std::string(pair.first) + "-" + label + ".png"))
                       .string()},
                  env);
        }
        auto errors = read_file(log);
        for (auto token : {"WARNING", "ERROR", "CRITICAL"})
          require(errors.find(token) == std::string::npos, errors);
        results.push_back(
            {{"pss_kib", m.at("Pss")},
             {"anonymous_kib", m.at("Pss_Anon")},
             {"private_kib", m.at("Private_Clean").get<long long>() +
                                 m.at("Private_Dirty").get<long long>()},
             {"rss_kib", m.at("Rss")},
             {"diagnostics", errors},
             {"interface", pair.first},
             {"theme", theme},
             {"renderer", renderer},
             {"repeat", r}});
        std::cout << theme << " " << pair.first << " PSS=" << m["Pss"] << " KiB"
                  << std::endl;
      }
  report(output / ("memory-" + renderer + ".json"), results);
}
static Json mapping_memory(pid_t pid) {
  std::map<std::string, long long> groups;
  std::string group = "anonymous";
  for (auto &line :
       lines(read_file("/proc/" + std::to_string(pid) + "/smaps"))) {
    std::istringstream in(line);
    std::string first;
    in >> first;
    if (first.find('-') != std::string::npos) {
      std::string token, path;
      for (int i = 0; i < 4; ++i)
        in >> token;
      std::getline(in, path);
      auto start = path.find_first_not_of(' ');
      path = start == std::string::npos ? "" : path.substr(start);
      if (path.empty() || path.rfind("[anon", 0) == 0)
        group = "anonymous";
      else if (path == "[heap]")
        group = "heap";
      else if (path.find("libLLVM") != std::string::npos ||
               path.find("libgallium") != std::string::npos ||
               path.find("libvulkan") != std::string::npos ||
               path.find("_dri.so") != std::string::npos)
        group = "graphics-libraries";
      else if (path.rfind("/dev/dri/", 0) == 0)
        group = "drm";
      else
        group = "other-mappings";
    } else if (first == "Pss:") {
      long long n;
      in >> n;
      groups[group] += n;
    }
  }
  std::set<std::string> devices;
  for (auto &e :
       fs::directory_iterator("/proc/" + std::to_string(pid) + "/fd")) {
    std::error_code ec;
    auto p = fs::read_symlink(e.path(), ec).string();
    if (!ec && p.rfind("/dev/dri/", 0) == 0)
      devices.insert(p);
  }
  return {{"mapping_pss_kib", groups}, {"drm_fds", devices}};
}
static void trace_idle(const fs::path &source,
                       const std::vector<std::string> &args) {
  require(args.size() >= 3,
          "Usage: TraceIdleRegression PANEL DESKTOP ICON_SHARE [OPTIONS]");
  auto panel = fs::absolute(args[0]), desktop = fs::absolute(args[1]),
       icons = fs::absolute(args[2]);
  std::string component, wallpaper_mode = "fit";
  std::vector<std::string> requested, renderers;
  bool debug = false, no_portals = false;
  for (size_t i = 3; i < args.size(); ++i) {
    auto key = args[i];
    if (key == "--component")
      component = next_arg(args, i);
    else if (key == "--wallpaper-mode")
      wallpaper_mode = next_arg(args, i);
    else if (key == "--renderer-debug")
      debug = true;
    else if (key == "--no-portals")
      no_portals = true;
    else if (key == "--cases" || key == "--renderers") {
      auto &out = key == "--cases" ? requested : renderers;
      size_t n = out.size();
      while (i + 1 < args.size() && args[i + 1].rfind("--", 0) != 0)
        out.push_back(args[++i]);
      require(out.size() > n, "Expected values after " + key);
    } else
      throw std::runtime_error("Unknown argument: " + key);
  }
  require(component.empty() || component == "panel" || component == "desktop",
          "Invalid component");
  require(wallpaper_mode == "fit" || wallpaper_mode == "stretch",
          "Invalid wallpaper mode");
  for (auto &r : renderers)
    require(
        std::set<std::string>{"default", "gl", "ngl", "vulkan", "cairo"}.count(
            r),
        "Invalid renderer");
  auto svg = source / "build/xcomp-luxesession-make-release/theme-data/"
                      "backgrounds/tokyo-night.svg";
  if (!fs::is_regular_file(svg))
    svg = "/usr/share/backgrounds/luxe/tokyo-night.svg";
  auto png =
      source /
      "build/png-luxesession-release/theme-data/backgrounds/tokyo-night.png";
  struct Case {
    std::string label, theme, icons, renderer;
    bool bridge;
    std::string image;
  };
  for (std::string kind : {"panel", "desktop"}) {
    if (!component.empty() && component != kind)
      continue;
    std::vector<Case> cases = {
        {"stock-default", "Adwaita", "Adwaita", "default", false, ""},
        {"stock-cairo", "Adwaita", "Adwaita", "cairo", false, ""},
        {"palette-only", "Luxe-tokyo-night", "Adwaita", "default", true, ""},
        {"palette-icons", "Luxe-tokyo-night", "Luxe-Dark", "default", true,
         ""}};
    if (kind == "desktop")
      for (std::string image : {"svg", "png"})
        for (std::string renderer : {"default", "cairo"})
          cases.push_back(
              {"palette-" + image + (renderer == "cairo" ? "-cairo" : ""),
               "Luxe-tokyo-night", "Luxe-Dark", renderer, true, image});
    if (!requested.empty()) {
      for (auto &name : requested)
        require(std::any_of(cases.begin(), cases.end(),
                            [&](auto &c) { return c.label == name; }),
                "Unknown case: " + name);
      cases.erase(std::remove_if(cases.begin(), cases.end(),
                                 [&](auto &c) {
                                   return std::find(requested.begin(),
                                                    requested.end(),
                                                    c.label) == requested.end();
                                 }),
                  cases.end());
    }
    if (!renderers.empty()) {
      auto original = cases;
      cases.clear();
      for (auto c : original)
        for (auto &r : renderers) {
          auto copy = c;
          copy.label += ":" + r;
          copy.renderer = r;
          cases.push_back(copy);
        }
    }
    for (auto &c : cases) {
      Scratch dir("luxe-idle-trace-");
      auto env = private_env(dir.path);
      env["GTK_THEME"] = c.theme;
      env["XDG_DATA_DIRS"] = icons.string() + ":/usr/local/share:/usr/share";
      if (c.renderer != "default")
        env["GSK_RENDERER"] = c.renderer;
      if (debug) {
        env["GSK_DEBUG"] = "renderer";
        env["GDK_DEBUG"] = "opengl,vulkan";
      }
      if (no_portals) {
        env["GDK_DEBUG"] = debug ? "no-portals,opengl,vulkan" : "no-portals";
        env["ADW_DISABLE_PORTAL"] = "1";
      }
      gtk_settings(dir.path, c.theme, c.icons, c.bridge);
      std::vector<std::string> extra;
      if (kind == "panel") {
        auto profile = dir.path / "config/luxepanel/Luxe/panels";
        fs::create_directories(profile);
        fs::copy_file(source / "luxepanel/data/default/panels/panel",
                      profile / "panel");
        extra = {"--profile", "Luxe"};
      } else {
        auto profile = dir.path / "config/pcmanager/LightTrace";
        fs::create_directories(profile);
        std::string wallpaper;
        if (!c.image.empty()) {
          auto target = dir.path / ("wallpaper." + c.image);
          fs::copy_file(c.image == "png" ? png : svg, target);
          fs::permissions(target, fs::perms(0644));
          wallpaper = target.string();
        }
        write_file(profile / "pcmanager.conf",
                   "[desktop]\nwallpaper_mode=" + wallpaper_mode +
                       "\nwallpaper=" + wallpaper + "\ndesktop_bg=#1a1b26\n");
        extra = {"--desktop", "--profile", "LightTrace"};
      }
      auto tested = dir.path / "program";
      fs::copy_file(kind == "panel" ? panel : desktop, tested);
      auto log = dir.path / "log";
      Child app(sandbox(dir.path, tested, extra), env, log);
      pause_for(3);
      healthy(app, log);
      auto pid = application_pid(app.pid, tested);
      std::set<std::string> drivers;
      for (auto &line :
           lines(read_file("/proc/" + std::to_string(pid) + "/maps")))
        if (line.find("libLLVM") != std::string::npos ||
            line.find("libvulkan") != std::string::npos ||
            line.find("/dev/dri/") != std::string::npos ||
            line.find("librsvg") != std::string::npos) {
          std::istringstream in(line);
          std::string part, last;
          while (in >> part)
            last = part;
          drivers.insert(last);
        }
      auto errors = read_file(log);
      require(c.image.empty() ||
                  errors.find("cannot load wallpaper") == std::string::npos,
              errors);
      auto result = memory(pid);
      result.update(mapping_memory(pid));
      result.update({{"component", kind},
                     {"case", c.label},
                     {"peak_rss_kib", proc_fields(pid, "status").at("VmHWM")},
                     {"wallpaper_mode", wallpaper_mode},
                     {"mapped_drivers", drivers},
                     {"diagnostics", errors}});
      std::cout << result.dump() << std::endl;
    }
  }
}
