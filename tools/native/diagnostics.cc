// Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later
#include "diagnostic-common.hh"
#include <X11/Xlib.h>
#include <X11/extensions/XRes.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <memory>
static Json inspect_x() {
  Display *d = XOpenDisplay(nullptr);
  require(d, "Cannot connect to DISPLAY");
  std::unique_ptr<Display, decltype(&XCloseDisplay)> guard(d, XCloseDisplay);
  int major = 0, minor = 0, count = 0;
  XResClient *clients = nullptr;
  require(XResQueryVersion(d, &major, &minor),
          "X-Resource extension unavailable");
  require(XResQueryClients(d, &count, &clients), "Cannot query X clients");
  std::unique_ptr<XResClient, decltype(&XFree)> cg(clients, XFree);
  Json rows = Json::array();
  for (int i = 0; i < count; ++i) {
    auto base = clients[i].resource_base;
    int pid = 0;
    if (base && (major > 1 || (major == 1 && minor >= 2))) {
      XResClientIdSpec spec{base, XRES_CLIENT_ID_PID_MASK};
      long n = 0;
      XResClientIdValue *ids = nullptr;
      if (XResQueryClientIds(d, 1, &spec, &n, &ids) == Success) {
        for (long j = 0; j < n; ++j) {
          int p = XResGetClientPid(&ids[j]);
          if (p > 0)
            pid = p;
        }
        XResClientIdsDestroy(n, ids);
      }
    }
    unsigned long size = 0;
    if (!XResQueryClientPixmapBytes(d, base, &size))
      continue;
    int ntypes = 0;
    XResType *types = nullptr;
    Json resources = Json::object();
    if (XResQueryClientResources(d, base, &ntypes, &types)) {
      std::unique_ptr<XResType, decltype(&XFree)> tg(types, XFree);
      for (int j = 0; j < ntypes; ++j) {
        char *name = XGetAtomName(d, types[j].resource_type);
        if (name) {
          resources[name] = types[j].count;
          XFree(name);
        }
      }
    }
    Json name = nullptr;
    if (pid)
      try {
        name = trim(read_file("/proc/" + std::to_string(pid) + "/comm"));
      } catch (const std::exception &) {
      }
    std::ostringstream hex;
    hex << "0x" << std::hex << base;
    rows.push_back({{"client", hex.str()},
                    {"pid", pid ? Json(pid) : Json(nullptr)},
                    {"command", name},
                    {"pixmap_bytes", size},
                    {"resources", resources}});
  }
  return {{"version", {major, minor}}, {"clients", rows}};
}
static Json owners(pid_t pid) {
  Json rows = Json::array();
  auto snapshot = inspect_x();
  for (auto &c : snapshot["clients"])
    if (c["pid"] == pid)
      rows.push_back(c);
  return rows;
}
static void icon_payload(const std::vector<std::string> &args) {
  std::map<std::string, std::string> previous;
  bool have = false;
  for (auto &argument : args) {
    auto root = fs::canonical(argument);
    std::map<std::string, std::string> logical;
    long long content = 0, blocks = 0, files = 0, links = 0, caches = 0;
    for (auto &entry : fs::recursive_directory_iterator(root)) {
      auto p = entry.path();
      struct stat st{};
      require(lstat(p.c_str(), &st) == 0, "Cannot stat " + p.string());
      if (entry.is_symlink()) {
        auto target = fs::canonical(p).lexically_relative(root);
        require(!target.empty() && *target.begin() != "..",
                "External icon alias: " + p.string());
        ++links;
        blocks += st.st_blocks * 512;
      } else if (entry.is_regular_file()) {
        if (p.filename() == "icon-theme.cache") {
          caches += st.st_size;
          continue;
        }
        ++files;
        content += st.st_size;
        blocks += st.st_blocks * 512;
      }
      // Comparing original bytes is equivalent to comparing SHA256 digests,
      // without starting a checksum process for every icon.
      if (p.extension() == ".svg")
        logical[p.lexically_relative(root).string()] = read_file(p);
    }
    require(!have || logical == previous, "SVG paths or artwork differ");
    previous = std::move(logical);
    have = true;
    std::cout << Json({{"root", root.string()},
                       {"svg_paths", previous.size()},
                       {"regular_files", files},
                       {"symlinks", links},
                       {"regular_content_bytes", content},
                       {"allocated_bytes", blocks},
                       {"host_cache_bytes", caches},
                       {"artwork_identical", true}})
                     .dump()
              << std::endl;
  }
}
static void dm_resources(const std::vector<std::string> &args) {
  require(args.size() == 3 || (args.size() == 4 && args[3] == "--release"),
          "Usage: ProfileDMResources BINARY THEME OUTPUT [--release]");
  Scratch dir("luxe-dm-resource-");
  auto log = dir.path / "log";
  Child app({fs::absolute(args[0]).string(), fs::absolute(args[1]).string()},
            {}, log, true);
  Json stages = Json::array();
  auto stage = [&](const std::string &name) {
    if (name != "connected")
      app.send(name + "\n");
    require(app.line(15) == name,
            "Unexpected controller response: " + read_file(log));
    pause_for(.1);
    auto memory =
        select_fields(proc_fields(app.pid, "smaps_rollup"),
                      {"Pss", "Pss_Anon", "Pss_Shmem", "Private_Dirty"});
    auto owner = owners(app.pid);
    require(owner.size() == 1, "Expected one XRes owner");
    auto &resources = owner[0]["resources"];
    if (name == "release")
      require(resources.value("PIXMAP", 0) == 0 &&
                  resources.value("WINDOW", 0) == 0,
              "Released resources retained");
    if (name == "release-visible")
      require(resources.value("PIXMAP", 0) > 0 &&
                  resources.value("WINDOW", 0) > 0,
              "Visible resources released");
    stages.push_back({{"stage", name}, {"memory", memory}, {"xres", owner[0]}});
    std::cout << name << " " << memory.dump() << " " << resources.dump()
              << std::endl;
  };
  stage("connected");
  stage("create");
  stage("open");
  bool release = args.size() == 4;
  if (release)
    stage("release-visible");
  stage("close");
  if (release) {
    stage("release");
    stage("release");
    stage("message");
    stage("close");
    stage("release");
    for (int i = 0; i < 3; ++i) {
      stage("open");
      stage("close");
      stage("release");
    }
  }
  stage("destroy");
  stage("trim");
  app.send("quit\n");
  require(app.wait(5) == 0, "Controller failed: " + read_file(log));
  require(read_file(log).empty(), "Controller diagnostics: " + read_file(log));
  report(args[2], stages);
}
static void image(const fs::path &path, int width, int height,
                  bool alpha = false, guint32 color = 0x405080ff) {
  GdkPixbuf *p = gdk_pixbuf_new(GDK_COLORSPACE_RGB, alpha, 8, width, height);
  require(p, "Cannot allocate image");
  gdk_pixbuf_fill(p, color);
  GError *error = nullptr;
  bool ok = gdk_pixbuf_save(p, path.c_str(), "png", &error, nullptr);
  g_object_unref(p);
  std::string message = error ? error->message : "Cannot save image";
  if (error)
    g_error_free(error);
  require(ok, message);
}
static Json viewer_memory(pid_t pid) {
  auto r = select_fields(proc_fields(pid, "smaps_rollup"),
                         {"Pss", "Pss_Anon", "Pss_Shmem"});
  r["VmHWM"] = proc_fields(pid, "status").at("VmHWM");
  r["cpu_ticks"] = ticks(pid);
  return r;
}
static void viewer(const std::vector<std::string> &args) {
  require(args.size() == 3, "Usage: ProfileViewerMemory BEFORE AFTER OUTPUT");
  Json results = Json::array();
  Scratch root("luxe-viewer-profile-");
  for (bool alpha : {false, true}) {
    auto folder = root.path / (alpha ? "True" : "False");
    fs::create_directory(folder);
    auto large = folder / "large.png", small = folder / "small.png";
    image(large, 3840, 2160, alpha, alpha ? 0x40508080 : 0x405080ff);
    image(small, 64, 32, false, 0xff0000ff);
    auto original = digest(large);
    for (int cohort : alpha ? std::vector<int>{1, 0} : std::vector<int>{0, 1}) {
      std::string kind = cohort ? "after" : "before";
      auto binary = fs::absolute(args[cohort]);
      auto dir = root.path / ((alpha ? "True-" : "False-") + kind);
      fs::create_directory(dir);
      Env env = {{"HOME", dir.string()},
                 {"XDG_CONFIG_HOME", (dir / "config").string()},
                 {"XDG_CACHE_HOME", (dir / "cache").string()},
                 {"GSETTINGS_BACKEND", "memory"}};
      auto log = dir / "log";
      Child app({binary.string(), large.string()}, env, log);
      Json stages = Json::array();
      pause_for(3);
      healthy(app, log);
      auto stage = [&](const char *name) {
        stages.push_back({{"stage", name}, {"memory", viewer_memory(app.pid)}});
      };
      stage("large");
      auto windows = lines(command({"xdotool", "search", "--onlyvisible",
                                    "--pid", std::to_string(app.pid)}));
      require(!windows.empty(), "No viewer window");
      command({"xdotool", "windowfocus", "--sync", windows[0]});
      command({"xdotool", "key", "r"});
      pause_for(1);
      stage("rotated");
      for (int i = 0; i < 3; ++i)
        command({"xdotool", "key", "r"});
      pause_for(1);
      stage("four-rotations");
      auto start = ticks(app.pid);
      pause_for(3);
      auto idle = ticks(app.pid) - start;
      command({binary.string(), small.string()}, env, 5);
      pause_for(1);
      stage("small-after-large");
      require(digest(large) == original, "Source image changed");
      auto errors = read_file(log);
      require(errors.find("CRITICAL") == std::string::npos &&
                  errors.find("ERROR") == std::string::npos,
              errors);
      results.push_back({{"cohort", kind},
                         {"alpha", alpha},
                         {"stages", stages},
                         {"idle_seconds", 3},
                         {"idle_cpu_ticks", idle},
                         {"log", errors},
                         {"executable", binary.string()},
                         {"sha256", digest(binary)}});
      std::cout << kind << " alpha=" << alpha << " idle ticks=" << idle
                << std::endl;
    }
  }
  report(args[2], results);
}
static void wallpaper_resources(const std::vector<std::string> &args) {
  require(args.size() == 2, "Usage: ProfileWallpaperResources BINARY OUTPUT");
  auto binary = fs::absolute(args[0]);
  int cycles = getenv("LUXE_WALLPAPER_PROFILE_CYCLES")
                   ? std::stoi(getenv("LUXE_WALLPAPER_PROFILE_CYCLES"))
                   : 4;
  require(cycles >= 1 && cycles <= 30, "Cycles must be 1..30");
  Json results = Json::array();
  for (auto size : {std::pair{2, 2}, {1366, 768}, {3840, 2160}}) {
    Scratch dir("luxe-wallpaper-resource-");
    image(dir.path / "image.png", size.first, size.second);
    auto original = digest(dir.path / "image.png");
    Env env = {{"XDG_CONFIG_HOME", (dir.path / "config").string()},
               {"XDG_CACHE_HOME", (dir.path / "cache").string()}};
    auto log = dir.path / "log";
    pid_t pid;
    {
      Child app({binary.string(), "--set-wallpaper",
                 (dir.path / "image.png").string()},
                env, log);
      pid = app.pid;
      pause_for(2);
      healthy(app, log);
      Json stages = Json::array();
      for (int cycle = 0; cycle < cycles; ++cycle) {
        Json duration = nullptr;
        if (cycle) {
          auto start = now();
          command({binary.string(), "--set-wallpaper",
                   (dir.path / "image.png").string()},
                  env, 5);
          duration = now() - start;
          pause_for(.3);
        }
        auto fields =
            select_fields(proc_fields(pid, "smaps_rollup"),
                          {"Pss", "Pss_Anon", "Pss_Shmem", "Private_Dirty"});
        fields["VmHWM"] = proc_fields(pid, "status").at("VmHWM");
        Json segments = Json::array();
        for (auto &line :
             lines(read_file("/proc/" + std::to_string(pid) + "/maps")))
          if (line.find("/SYSV") != std::string::npos ||
              line.find("memfd:") != std::string::npos)
            segments.push_back(line);
        stages.push_back({{"cycle", cycle},
                          {"apply_seconds", duration},
                          {"memory", fields},
                          {"segments", segments},
                          {"xres", inspect_x()}});
      }
      require(digest(dir.path / "image.png") == original,
              "Source image changed");
      auto start = ticks(pid);
      pause_for(1);
      auto cache = dir.path / "cache/frame/wallpaper.png";
      results.push_back(
          {{"source", {size.first, size.second}},
           {"stages", stages},
           {"idle_seconds", 1},
           {"idle_cpu_ticks", ticks(pid) - start},
           {"cache_bytes",
            fs::exists(cache) ? Json(fs::file_size(cache)) : Json(nullptr)},
           {"log", read_file(log)}});
    }
    require(owners(pid).empty(), "Stopped wallpaper client retained");
    std::cout << size.first << "x" << size.second << " captured" << std::endl;
  }
  report(args[1], results);
}
static void wallpaper_collections(const std::vector<std::string> &args) {
  require(args.size() == 3,
          "Usage: ProfileWallpaperCollections BINARY COLLECTION OUTPUT");
  auto binary = fs::absolute(args[0]), collection = fs::absolute(args[1]);
  std::vector<fs::path> images;
  std::map<fs::path, std::string> originals;
  for (auto &folder : fs::directory_iterator(collection))
    if (folder.is_directory())
      for (auto &e : fs::directory_iterator(folder.path()))
        if (e.path().extension() == ".png")
          images.push_back(e.path());
  std::sort(images.begin(), images.end());
  require(images.size() == 18, "Expected 18 collection images");
  for (auto &p : images)
    originals[p] = digest(p);
  Scratch dir("luxe-collection-cycle-");
  Env env = {{"XDG_CONFIG_HOME", (dir.path / "config").string()},
             {"XDG_CACHE_HOME", (dir.path / "cache").string()}};
  auto log = dir.path / "log";
  Child app({binary.string(), "--set-wallpaper", images[0].string()}, env, log);
  auto cache = dir.path / "cache/frame/wallpaper.png";
  Json stages = Json::array();
  for (int i = 0; i < 200 && !fs::exists(cache); ++i) {
    healthy(app, log);
    pause_for(.025);
  }
  require(fs::exists(cache), "Wallpaper cache missing");
  for (auto &p : images) {
    double start = now();
    command({binary.string(), "--set-wallpaper", p.string()}, env, 10);
    double elapsed = now() - start;
    auto fields = select_fields(proc_fields(app.pid, "smaps_rollup"),
                                {"Pss", "Pss_Anon", "Pss_Shmem"});
    require(fields["Pss_Anon"] < 16384 && fields["Pss_Shmem"] == 0,
            "Wallpaper memory bounds exceeded");
    require(
        read_file("/proc/" + std::to_string(app.pid) + "/maps").find("/SYSV") ==
            std::string::npos,
        "SYSV memory retained");
    auto owner = owners(app.pid);
    require(owner.size() == 1 && owner[0]["resources"].at("PIXMAP") <= 2,
            "Unexpected pixmap ownership");
    require(read_file(dir.path / "config/luxe/wallpaper.ini")
                    .find("current=" + p.string()) != std::string::npos,
            "Wrong wallpaper selection");
    require(std::distance(fs::directory_iterator(cache.parent_path()),
                          fs::directory_iterator{}) == 1,
            "Unexpected cache files");
    stages.push_back({{"image", p.lexically_relative(collection).string()},
                      {"memory", fields},
                      {"apply_seconds", elapsed},
                      {"xres", owner[0]}});
  }
  for (auto &[path, original] : originals)
    require(digest(path) == original, "Original wallpaper changed");
  auto start = ticks(app.pid);
  pause_for(3);
  auto idle = ticks(app.pid) - start;
  require(idle == 0, "Unexpected idle CPU ticks");
  command({binary.string(), "--wallpaper-stop"}, env, 5);
  require(app.wait(5) == 0 && read_file(log).empty(),
          "Wallpaper stop failed: " + read_file(log));
  require(owners(app.pid).empty(), "Stopped wallpaper client retained");
  report(args[2],
         {{"stages", stages}, {"idle_seconds", 3}, {"idle_cpu_ticks", idle}});
  std::cout << "PASS: 18 original wallpapers, bounded memory/pixmaps, idle CPU "
               "and stop\n";
}
static void capture_viewer(const fs::path &source,
                           const std::vector<std::string> &args) {
  require(args.size() == 2, "Usage: CaptureViewerPalettes BINARY OUTPUT");
  auto binary = fs::absolute(args[0]), output = fs::absolute(args[1]);
  fs::create_directories(output);
  for (auto pair : {std::pair{"light", "luxe-light"},
                    {"dark", "luxe-dark"},
                    {"nord", "nord"},
                    {"mocha", "catppuccin"},
                    {"dracula", "dracula"},
                    {"tokyo-night", "tokyo"}}) {
    Scratch dir("luxe-viewer-palette-");
    auto image = source / "luxesession/data/wallpapers/horizon" /
                 (std::string("luxe-horizon-") + pair.second + ".png");
    Env env = {{"HOME", dir.path.string()},
               {"XDG_CONFIG_HOME", (dir.path / "config").string()},
               {"XDG_CACHE_HOME", (dir.path / "cache").string()},
               {"GTK_THEME", std::string("Luxe-") + pair.first}};
    auto log = dir.path / "log";
    Child app({binary.string(), image.string()}, env, log);
    pause_for(2);
    healthy(app, log);
    auto ids = lines(command({"xdotool", "search", "--onlyvisible", "--pid",
                              std::to_string(app.pid)}));
    require(!ids.empty(), "No viewer window");
    std::map<std::string, int> geometry;
    for (auto &line :
         lines(command({"xdotool", "getwindowgeometry", "--shell", ids[0]}))) {
      auto p = line.find('=');
      if (p != std::string::npos)
        geometry[line.substr(0, p)] = std::stoi(line.substr(p + 1));
    }
    require(geometry.at("WIDTH") <= 1280 && geometry.at("HEIGHT") <= 720,
            "Viewer exceeds 720p window");
    command({"import", "-window", "root",
             (output / (std::string(pair.first) + ".png")).string()});
    kill(-app.pid, SIGTERM);
    app.wait(5);
    require(read_file(log).empty(), read_file(log));
    std::cout << pair.first << " PASS: real 4K artwork fits 720p window"
              << std::endl;
  }
}
#include "memory-diagnostics.hh"
int main(int argc, char **argv) {
  signal(SIGPIPE, SIG_IGN);
  signal(SIGTERM, diagnostic_signal);
  signal(SIGINT, diagnostic_signal);
  try {
    require(argc >= 3, "Usage: diagnostics REPOSITORY MODE [ARGUMENTS]");
    fs::path source = fs::canonical(argv[1]);
    std::string mode = argv[2];
    std::vector<std::string> args(argv + 3, argv + argc);
    if (mode == "inspect-x") {
      require(args.empty(), "inspect-x takes no arguments");
      std::cout << inspect_x().dump(2) << '\n';
    } else if (mode == "icon-payload")
      icon_payload(args);
    else if (mode == "dm-resources")
      dm_resources(args);
    else if (mode == "viewer")
      viewer(args);
    else if (mode == "wallpaper-resources")
      wallpaper_resources(args);
    else if (mode == "wallpaper-collections")
      wallpaper_collections(args);
    else if (mode == "capture-viewer")
      capture_viewer(source, args);
    else if (mode == "theme-memory")
      theme_memory(args);
    else if (mode == "resident-memory")
      resident_memory(source, args);
    else if (mode == "active-memory")
      active_memory(source, args);
    else if (mode == "compare-settings")
      compare_settings(args);
    else if (mode == "trace-idle")
      trace_idle(source, args);
    else
      throw std::runtime_error("Unknown diagnostic: " + mode);
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
