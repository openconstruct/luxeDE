// Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later
#include "diagnostic-common.hh"
#include <iomanip>
#include <set>
#include <sys/socket.h>
#include <sys/un.h>
static std::string env_string(const char *key,
                              const std::string &fallback = "") {
  auto p = getenv(key);
  return p ? p : fallback;
}
static void init_profile(const fs::path &probe, const fs::path &output) {
  Json results = Json::array();
  Scratch dir("luxe-panel-init-");
  for (auto disabled : {"", "gl", "vulkan", "gl,vulkan"})
    for (auto theme : {"Default", "Default:dark", "HighContrast", "Luxe-light",
                       "Luxe-tokyo-night", "Luxe-mocha"})
      for (int trial = 0; trial < 3; trial++) {
        auto out = dir.path / "stdout", err = dir.path / "stderr";
        Child process({"sh", "-c", R"(out=$1; shift; exec "$@" > "$out")",
                       "fixture", out.string(), probe.string(), "gtk-stages"},
                      {{"GTK_THEME", theme}, {"GDK_DISABLE", disabled}}, err);
        require(process.wait(15) == 0, read_file(err));
        auto errors = read_file(err);
        for (auto bad : {"CRITICAL", "Failed to load", "Theme parser error"})
          require(errors.find(bad) == std::string::npos, errors);
        auto csv = lines(read_file(out));
        require(!csv.empty(), "No CSV output");
        auto split = [](const std::string &s) {
          std::vector<std::string> fields;
          std::istringstream in(s);
          std::string value;
          while (std::getline(in, value, ','))
            fields.push_back(value);
          return fields;
        };
        auto keys = split(csv[0]);
        Json stages = Json::array();
        for (size_t i = 1; i < csv.size(); i++) {
          auto values = split(csv[i]);
          require(values.size() == keys.size(), "Invalid CSV row");
          Json row = Json::object();
          for (size_t n = 0; n < keys.size(); n++)
            row[keys[n]] = values[n];
          stages.push_back(row);
        }
        results.push_back({{"theme", theme},
                           {"disabled", disabled},
                           {"trial", trial},
                           {"stages", stages},
                           {"stderr", errors}});
      }
  report(output, results);
  for (auto disabled : {"", "gl", "vulkan", "gl,vulkan"})
    for (auto theme : {"Default", "Default:dark", "HighContrast", "Luxe-light",
                       "Luxe-tokyo-night", "Luxe-mocha"}) {
      auto average = [&](const std::string &stage, const std::string &field) {
        double total = 0;
        int count = 0;
        for (auto &r : results)
          if (r["disabled"] == disabled && r["theme"] == theme) {
            for (auto &s : r["stages"])
              if (s["stage"] == stage) {
                total += std::stod(s[field].get<std::string>());
                count++;
              }
          }
        require(count == 3, "Missing stage");
        return total / count;
      };
      std::cout << std::setw(10) << (*disabled ? disabled : "baseline") << ' '
                << std::setw(20) << theme << std::fixed << std::setprecision(1)
                << " GTK init anon=" << average("gtk-init", "anon_kib") / 1024
                << " MiB; window anon="
                << average("gtk-window", "anon_kib") / 1024
                << "; settled anon=" << average("settled", "anon_kib") / 1024
                << "; heap=" << average("settled", "heap_live_bytes") / 1048576
                << '\n';
    }
}
static Json sample(pid_t pid, const std::string &stage) {
  auto all = proc_fields(pid, "smaps_rollup");
  Json row = Json::object();
  for (auto key :
       {"Pss", "Pss_Anon", "Pss_File", "Private_Clean", "Private_Dirty", "Rss"})
    row[key] = all.at(key);
  row["stage"] = stage;
  row["cpu_ticks"] = ticks(pid);
  std::set<std::string> drivers;
  for (auto &line : lines(read_file("/proc/" + std::to_string(pid) + "/maps")))
    for (auto key : {"libgallium", "_dri.so", "libvulkan_", "libLLVM"})
      if (line.find(key) != std::string::npos) {
        auto pos = line.find_last_of(' ');
        drivers.insert(line.substr(pos + 1));
      }
  row["drivers"] = drivers;
  return row;
}
static void launcher(const std::vector<std::string> &args) {
  require(!args.empty(), "Missing panel command");
  auto display = env_string("DISPLAY");
  display = display.substr(0, display.find('.'));
  auto path =
      fs::path(env_string("XDG_RUNTIME_DIR")) / ("menu-cached-" + display);
  Env env;
  if (!env_string("LUXE_PANEL_PROFILE_DAEMON").empty())
    env["G_MESSAGES_DEBUG"] = "all";
  Child daemon({"/usr/libexec/luxepanel/menu-cache/menu-cached", path.string()},
               env);
  if (env.empty())
    require(daemon.wait(15) == 0, "Menu daemon start failed");
  else {
    double deadline = now() + 5;
    while (!fs::exists(path) && now() < deadline)
      pause_for(.05);
    require(fs::exists(path), "Foreground daemon socket missing");
  }
  int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  require(fd >= 0, "Socket failed");
  sockaddr_un address{};
  address.sun_family = AF_UNIX;
  require(path.string().size() < sizeof(address.sun_path),
          "Socket path too long");
  strcpy(address.sun_path, path.c_str());
  int status =
      connect(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address));
  close(fd);
  require(status == 0, "Cannot connect to menu socket");
  std::vector<char *> raw;
  for (auto &arg : args)
    raw.push_back(const_cast<char *>(arg.c_str()));
  raw.push_back(nullptr);
  execv(raw[0], raw.data());
  throw std::runtime_error("Cannot execute panel");
}
static void panel_profile(const fs::path &self, const fs::path &panel,
                          const fs::path &ctl, const fs::path &output) {
  std::vector<std::pair<std::string, std::vector<std::string>>> profiles = {
      {"empty", {"space"}},
      {"menu", {"menu"}},
      {"clock", {"dclock"}},
      {"tray", {"tray"}},
      {"taskbar", {"taskbar"}},
      {"sound-battery", {"volume", "batt"}},
      {"full",
       {"menu", "taskbar", "dclock", "tray", "notifications", "volume",
        "batt"}}};
  if (!env_string("LUXE_PANEL_PROFILE_FULL_ONLY").empty())
    profiles = {profiles.back()};
  double settle =
             std::stod(env_string("LUXE_PANEL_PROFILE_SETTLE_SECONDS", "0")),
         idle = std::stod(env_string("LUXE_PANEL_PROFILE_IDLE_SECONDS", "1"));
  int cycles = std::stoi(env_string("LUXE_PANEL_PROFILE_MENU_CYCLES", "5"));
  require(cycles >= 1 && cycles <= 100 && settle >= 0 && idle >= 0,
          "Invalid sampling settings");
  auto icon = env_string("LUXE_PANEL_PROFILE_ICONS", "Adwaita");
  require(icon.find_first_of("\r\n") == std::string::npos,
          "Invalid icon theme");
  Json results = Json::array();
  Scratch dir("luxe-panel-profile-");
  auto root = dir.path, plugins = root / "plugins", helpers = root / "helpers",
       config = root / "config/luxepanel/test/panels";
  for (auto p : {plugins, helpers, root / "home/Templates", root / "runtime",
                 root / "data/applications", root / "state", config,
                 root / "config/gtk-4.0"})
    fs::create_directories(p);
  fs::permissions(root / "runtime", static_cast<fs::perms>(0700));
  for (auto &e : fs::directory_iterator(panel.parent_path()))
    if (e.path().extension() == ".so")
      fs::copy_file(e.path(), plugins / e.path().filename());
  for (auto name : {"menu-cached", "menu-cache-gen"}) {
    auto source = std::string(name) == "menu-cached" &&
                          !env_string("LUXE_PANEL_PROFILE_DAEMON").empty()
                      ? fs::path(env_string("LUXE_PANEL_PROFILE_DAEMON"))
                      : panel.parent_path() / name;
    fs::copy_file(source, helpers / name);
    fs::permissions(helpers / name, fs::status(source).permissions());
  }
  auto marker = root / "catalogue-activated";
  write_file(root / "data/applications/luxe-profile-sentinel.desktop",
             "[Desktop Entry]\nType=Application\nName=Luxe Catalogue "
             "Probe\nExec=/usr/bin/touch " +
                 marker.string() +
                 "\nCategories=Utility;\nIcon=utilities-terminal\n");
  Env env = {{"XDG_CONFIG_HOME", (root / "config").string()},
             {"XDG_CACHE_HOME", (root / "cache").string()},
             {"HOME", (root / "home").string()},
             {"XDG_RUNTIME_DIR", (root / "runtime").string()},
             {"XDG_DATA_HOME", (root / "data").string()},
             {"XDG_STATE_HOME", (root / "state").string()},
             {"XDG_CURRENT_DESKTOP", "X-Luxe"},
             {"XDG_MENU_PREFIX", "luxe-"},
             {"GTK_THEME", "Luxe-tokyo-night"},
             {"XDG_CONFIG_DIRS", "/etc/xdg"},
             {"XDG_DATA_DIRS", "/usr/local/share:/usr/share"}};
  if (!env_string("LUXE_TEST_HOST_RUNTIME").empty())
    env["PIPEWIRE_RUNTIME_DIR"] =
        env_string("XDG_RUNTIME_DIR", "/run/user/" + std::to_string(getuid()));
  write_file(root / "config/gtk-4.0/settings.ini",
             "[Settings]\ngtk-icon-theme-name=" + icon + "\n");
  Child wm({"openbox"}, env, root / "wm.log");
  pause_for(.3);
  for (auto disabled : {"", "gl,vulkan"})
    for (auto &[name, types] : profiles)
      for (int trial = 0; trial < 2; trial++) {
        std::string text = "Global {\n edge=bottom\n widthtype=percent\n "
                           "width=100\n height=28\n iconsize=28\n}\n";
        for (auto &type : types)
          text += "Plugin {\n type=" + type + "\n Config {\n" +
                  (type == "menu" ? " system {\n }\n" : "") + " }\n}\n";
        write_file(config / "panel", text);
        auto case_env = env;
        case_env["GDK_DISABLE"] = disabled;
        Child process({"bwrap",
                       "--ro-bind",
                       "/",
                       "/",
                       "--dev-bind",
                       "/dev",
                       "/dev",
                       "--unshare-pid",
                       "--proc",
                       "/proc",
                       "--die-with-parent",
                       "--bind",
                       root.string(),
                       root.string(),
                       "--ro-bind",
                       plugins.string(),
                       "/usr/lib/luxepanel/plugins",
                       "--ro-bind",
                       helpers.string(),
                       "/usr/libexec/luxepanel/menu-cache",
                       "--",
                       self.string(),
                       "launch",
                       panel.string(),
                       "--profile",
                       "test"},
                      case_env, root / "panel.log");
        try {
          pid_t pid = -1;
          double deadline = now() + 20;
          while (now() < deadline) {
            require(process.alive(), read_file(root / "panel.log"));
            try {
              pid = application_pid(process.pid, panel);
              break;
            } catch (const std::exception &) {
              pause_for(.05);
            }
          }
          require(pid > 0, "Panel missing from sandbox: " +
                               read_file(root / "panel.log"));
          pause_for(1.5);
          require(process.alive(), read_file(root / "panel.log"));
          Json stages = Json::array({sample(pid, "cold")});
          bool verified = false;
          auto run = [&](std::vector<std::string> cmd) {
            return command(cmd, case_env, 5);
          };
          if (name == "full") {
            fs::remove(marker);
            run({ctl.string(), "menu"});
            deadline = now() + 15;
            bool generated = false;
            while (now() < deadline) {
              auto menus = root / "cache/menus";
              if (fs::exists(menus))
                for (auto &e : fs::directory_iterator(menus))
                  if (e.is_regular_file() &&
                      read_file(e.path()).find("Luxe Catalogue Probe") !=
                          std::string::npos)
                    generated = true;
              if (generated)
                break;
              pause_for(.1);
            }
            require(generated, "Catalogue not generated");
            pause_for(.3);
            run({"xdotool", "type", "--clearmodifiers",
                 "Luxe Catalogue Probe"});
            pause_for(.3);
            run({"xdotool", "key", "Return"});
            deadline = now() + 5;
            while (!fs::exists(marker) && now() < deadline)
              pause_for(.05);
            require(fs::exists(marker), "Catalogue row did not launch");
            verified = true;
            pause_for(.3);
            for (int cycle = 0; cycle < cycles; cycle++) {
              run({ctl.string(), "menu"});
              pause_for(.3);
              if (cycle == 0)
                stages.push_back(sample(pid, "menu-open"));
              run({"xdotool", "key", "Escape"});
              pause_for(.3);
              if ((cycle + 1) % 5 == 0)
                stages.push_back(
                    sample(pid, "menu-closed-" + std::to_string(cycle + 1)));
            }
          }
          pause_for(settle);
          auto start = sample(pid, "idle-start");
          auto before = now();
          pause_for(idle);
          auto final = sample(pid, "idle-end");
          double cpu = 100 *
                       (final["cpu_ticks"].get<long long>() -
                        start["cpu_ticks"].get<long long>()) /
                       double(sysconf(_SC_CLK_TCK)) / (now() - before);
          stages.push_back(final);
          text = read_file(root / "panel.log");
          require(text.find("CRITICAL") == std::string::npos &&
                      text.find("can't load") == std::string::npos,
                  text);
          auto lower = text;
          std::transform(lower.begin(), lower.end(), lower.begin(),
                         [](unsigned char c) { return std::tolower(c); });
          require(lower.find("unable to connect") == std::string::npos, text);
          results.push_back({{"profile", name},
                             {"disabled", disabled},
                             {"trial", trial},
                             {"icon_theme", icon},
                             {"stages", stages},
                             {"idle_cpu_percent", cpu},
                             {"catalogue_verified", verified},
                             {"menu_cycles", cycles},
                             {"settle_seconds", settle},
                             {"idle_seconds", idle},
                             {"log", text}});
        } catch (...) {
          auto failure = output;
          failure.replace_extension(".failure");
          fs::create_directories(failure / "fixture");
          for (auto &e : fs::recursive_directory_iterator(root)) {
            auto dest = failure / "fixture" / e.path().lexically_relative(root);
            if (e.is_directory())
              fs::create_directories(dest);
            else if (e.is_regular_file())
              fs::copy_file(e.path(), dest,
                            fs::copy_options::overwrite_existing);
          }
          try {
            command({"import", "-window", "root",
                     (failure / "screen.png").string()},
                    case_env, 5);
          } catch (...) {
          }
          throw;
        }
      }
  report(output, results);
  for (auto disabled : {"", "gl,vulkan"})
    for (auto &[name, types] : profiles) {
      (void)types;
      double anon = 0, cpu = 0;
      int count = 0;
      for (auto &r : results)
        if (r["disabled"] == disabled && r["profile"] == name) {
          anon += r["stages"].back()["Pss_Anon"].get<double>();
          cpu += r["idle_cpu_percent"].get<double>();
          count++;
        }
      std::cout << std::setw(10) << (*disabled ? disabled : "baseline") << ' '
                << std::setw(14) << name << std::fixed << std::setprecision(2)
                << " settled anon=" << anon / count / 1024
                << " MiB idle CPU=" << cpu / count << "%\n";
    }
}
int main(int argc, char **argv) {
  signal(SIGPIPE, SIG_IGN);
  signal(SIGTERM, diagnostic_signal);
  signal(SIGINT, diagnostic_signal);
  try {
    require(argc >= 2, "Expected profile mode");
    std::string mode = argv[1];
    if (mode == "launch")
      launcher(std::vector<std::string>(argv + 2, argv + argc));
    else if (mode == "initialization") {
      require(argc == 4, "Usage: initialization PROBE OUTPUT");
      init_profile(fs::absolute(argv[2]), fs::absolute(argv[3]));
    } else if (mode == "panel") {
      require(argc == 5, "Usage: panel PANEL CTL OUTPUT");
      panel_profile(fs::canonical("/proc/self/exe"), fs::canonical(argv[2]),
                    fs::absolute(argv[3]), fs::absolute(argv[4]));
    } else
      throw std::runtime_error("Unknown profile mode");
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
