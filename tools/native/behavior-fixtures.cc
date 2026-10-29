// Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later
#include "diagnostic-common.hh"
#include <functional>
#include <gio/gio.h>
struct Bus {
  GDBusConnection *connection = nullptr;
  Bus() {
    GError *error = nullptr;
    connection = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
    if (!connection) {
      std::string message = error ? error->message : "No session bus";
      g_clear_error(&error);
      throw std::runtime_error(message);
    }
  }
  ~Bus() { g_object_unref(connection); }
  GVariant *call(const char *destination, const char *path,
                 const char *interface, const char *method,
                 GVariant *arguments = nullptr, int timeout = 6000) {
    GError *error = nullptr;
    auto result = g_dbus_connection_call_sync(
        connection, destination, path, interface, method, arguments, nullptr,
        G_DBUS_CALL_FLAGS_NO_AUTO_START, timeout, nullptr, &error);
    if (!result) {
      std::string message = error ? error->message : "D-Bus call failed";
      g_clear_error(&error);
      throw std::runtime_error(message);
    }
    return result;
  }
  bool has_owner() {
    auto result =
        call("org.freedesktop.DBus", "/org/freedesktop/DBus",
             "org.freedesktop.DBus", "NameHasOwner",
             g_variant_new("(s)", "org.freedesktop.Notifications"), 1000);
    gboolean present = false;
    g_variant_get(result, "(b)", &present);
    g_variant_unref(result);
    return present;
  }
  GVariant *notification(const char *method, GVariant *args = nullptr) {
    return call("org.freedesktop.Notifications",
                "/org/freedesktop/Notifications",
                std::string(method) == "GetActiveCount"
                    ? "org.luxe.Notifications"
                    : "org.freedesktop.Notifications",
                method, args);
  }
  guint32 count() {
    auto result = notification("GetActiveCount");
    guint32 value;
    g_variant_get(result, "(u)", &value);
    g_variant_unref(result);
    return value;
  }
  guint32 notify(guint32 replacement = 0, int expiry = 0) {
    const char *actions[] = {"default", "Open", nullptr};
    GVariantBuilder hints;
    g_variant_builder_init(&hints, G_VARIANT_TYPE_VARDICT);
    auto result = notification(
        "Notify", g_variant_new("(susss^as@a{sv}i)", "test", replacement, "",
                                "Broker test", "body", actions,
                                g_variant_builder_end(&hints), expiry));
    guint32 id;
    g_variant_get(result, "(u)", &id);
    g_variant_unref(result);
    return id;
  }
  void close(guint32 id) {
    auto r = notification("CloseNotification", g_variant_new("(u)", id));
    g_variant_unref(r);
  }
  std::string capabilities() {
    auto result = notification("GetCapabilities");
    auto text = g_variant_print(result, true);
    std::string s = text;
    g_free(text);
    g_variant_unref(result);
    return s;
  }
};
static void wait_for(Child &child, const std::function<bool()> &predicate,
                     double seconds = 6) {
  double deadline = now() + seconds;
  while (now() < deadline) {
    while (g_main_context_iteration(nullptr, false)) {
    }
    if (predicate())
      return;
    require(child.alive(), "Service exited while waiting");
    pause_for(.02);
  }
  throw std::runtime_error("Fixture timed out");
}
static std::vector<pid_t> children(pid_t pid) {
  std::istringstream in(read_file("/proc/" + std::to_string(pid) + "/task/" +
                                  std::to_string(pid) + "/children"));
  std::vector<pid_t> result;
  pid_t child;
  while (in >> child)
    result.push_back(child);
  return result;
}
static void headless(const std::vector<std::string> &args) {
  require(args.size() == 1 || args.size() == 2,
          "Usage: headless DAEMON [RENDERER]");
  Scratch dir("luxe-notify-headless-");
  Env env = {{"DISPLAY", ":65530"}, {"GDK_BACKEND", "x11"}};
  std::vector<std::string> command = {fs::absolute(args[0]).string()};
  if (args.size() == 2) {
    command.push_back("--renderer");
    command.push_back(fs::absolute(args[1]).string());
  }
  Child app(command, env, dir.path / "log");
  Bus bus;
  wait_for(app, [&] { return bus.has_owner(); }, 2);
  require(bus.capabilities().find("actions") != std::string::npos,
          "Missing notification actions");
  require(bus.count() == 0, "Unexpected active notifications");
  bool failed = false;
  try {
    bus.notify(0, 100);
  } catch (const std::exception &e) {
    failed = std::string(e.what()).find(
                 "Cannot open the notification display") != std::string::npos;
  }
  require(failed, "Unavailable display was not rejected cleanly");
  require(app.alive(), "Service exited after display failure");
  require(!bus.capabilities().empty(),
          "Service unavailable after display failure");
  std::cout << "PASS: headless registration/counts, clean display failure, "
               "service remains available\n";
}
static void broker(const std::vector<std::string> &args) {
  require(args.size() == 2, "Usage: broker DAEMON RENDERER");
  Scratch dir("luxe-notify-broker-");
  Bus bus;
  std::vector<std::pair<guint32, guint32>> events;
  auto subscription = g_dbus_connection_signal_subscribe(
      bus.connection, nullptr, nullptr, nullptr,
      "/org/freedesktop/Notifications", nullptr, G_DBUS_SIGNAL_FLAGS_NONE,
      [](GDBusConnection *, const gchar *, const gchar *, const gchar *,
         const gchar *name, GVariant *value, gpointer data) {
        if (std::string(name) == "NotificationClosed") {
          guint32 id, reason;
          g_variant_get(value, "(uu)", &id, &reason);
          static_cast<std::vector<std::pair<guint32, guint32>> *>(data)
              ->emplace_back(id, reason);
        }
      },
      &events, nullptr);
  try {
    {
      Child app({fs::absolute(args[0]).string(), "--renderer",
                 fs::absolute(args[1]).string()},
                {}, dir.path / "log");
      wait_for(app, [&] { return bus.has_owner(); });
      require(bus.count() == 0 && children(app.pid).empty(),
              "Broker started a renderer while idle");
      auto maps = read_file("/proc/" + std::to_string(app.pid) + "/maps");
      require(maps.find("libgtk") == std::string::npos &&
                  maps.find("libgdk") == std::string::npos,
              "Broker initialized GTK");
      auto closed = [&](guint32 id, guint32 reason) {
        require(std::find(events.begin(), events.end(),
                          std::pair{id, reason}) != events.end(),
                "Expected NotificationClosed signal missing");
      };
      auto first = bus.notify();
      require(!children(app.pid).empty() && bus.count() == 1,
              "No active renderer");
      require(bus.notify(first) == first, "Replacement changed ID");
      bus.close(first);
      wait_for(app, [&] { return children(app.pid).empty(); });
      closed(first, 3);
      auto second = bus.notify(0, 100);
      require(second > first, "Notification IDs did not advance");
      wait_for(app, [&] { return children(app.pid).empty(); });
      closed(second, 1);
      auto third = bus.notify();
      require(third > second, "Notification IDs did not advance");
      auto spawned = children(app.pid);
      require(!spawned.empty(), "Missing renderer to crash");
      require(kill(spawned[0], SIGKILL) == 0,
              "Cannot terminate fixture renderer");
      wait_for(app, [&] { return children(app.pid).empty(); });
      require(bus.count() == 0, "Crashed renderer left active notifications");
      closed(third, 3);
      auto fourth = bus.notify();
      require(fourth > third, "IDs reset after renderer crash");
      bus.close(fourth);
      wait_for(app, [&] { return children(app.pid).empty(); });
      require(bus.capabilities().size() > 4, "Empty capabilities");
    }
    require(read_file(dir.path / "log").find("CRITICAL") == std::string::npos,
            read_file(dir.path / "log"));
  } catch (...) {
    g_dbus_connection_signal_unsubscribe(bus.connection, subscription);
    throw;
  }
  g_dbus_connection_signal_unsubscribe(bus.connection, subscription);
  std::cout << "PASS: GTK-free broker, replacement, expiry/close signals, "
               "stable IDs, renderer crash/restart and idle teardown\n";
}
static const std::string request =
    "('org.luxe.fixture', 'Fixture authentication', '', 'fixture-cookie', "
    "['unix-user:1000'], {})";
static void polkit_owner(const std::vector<std::string> &args) {
  require(args.size() == 1, "owner requires fixture");
  int pipefd[2];
  require(pipe(pipefd) == 0, "pipe failed");
  pid_t owner = getpid();
  pid_t child = fork();
  require(child >= 0, "fork failed");
  if (child == 0) {
    close(pipefd[1]);
    dup2(pipefd[0], STDIN_FILENO);
    close(pipefd[0]);
    setenv("LUXE_POLKIT_FIXTURE_MODE", "wait", 1);
    auto pid = std::to_string(owner);
    execl(args[0].c_str(), args[0].c_str(), "--parent-pid", pid.c_str(),
          nullptr);
    _exit(127);
  }
  // Deliberately no harness-imposed parent-death signal: the fixture must
  // demonstrate its own production parent-ownership behavior.
  close(pipefd[0]);
  size_t position = 0;
  while (position < request.size()) {
    auto n =
        write(pipefd[1], request.data() + position, request.size() - position);
    require(n > 0, "Cannot send fixture request");
    position += n;
  }
  close(pipefd[1]);
  std::cout << child << std::endl;
  pause_for(30);
  throw std::runtime_error("Owner should have been killed by the test");
}
static void polkit_boundary(const fs::path &executable,
                            const std::vector<std::string> &args) {
  require(args.size() == 2, "Usage: polkit-boundary PRODUCTION FIXTURE");
  auto production = fs::absolute(args[0]), fixture = fs::absolute(args[1]);
  Scratch dir("luxe-polkit-boundary-");
  std::vector<std::string> requests = {"not a variant",
                                       "('a', 'b', '', 'cookie', [], {})",
                                       std::string(1024 * 1024 + 1, 'x')};
  for (auto &data : requests) {
    auto input = dir.path / "request";
    write_file(input, data);
    // Supply stdin via a fixed shell command, passing the program and data path
    // as literal argv entries. Capture all diagnostics to a regular log file.
    Child app({"sh", "-c", "exec \"$1\" --parent-pid \"$2\" < \"$3\"",
               "fixture", production.string(), std::to_string(getpid()),
               input.string()},
              {{"DISPLAY", ":65530"}}, dir.path / "invalid.log");
    require(app.wait(10) != 0, "Malformed request accepted");
    require(read_file(dir.path / "invalid.log").find("CRITICAL") ==
                std::string::npos,
            "Critical error on malformed request");
  }
  Child owner({executable.string(), "polkit-owner", fixture.string()}, {},
              dir.path / "owner.log", true);
  auto pid = std::stoi(owner.line(5));
  bool died = false;
  try {
    double deadline = now() + 5;
    while (now() < deadline) {
      if (read_file("/proc/" + std::to_string(pid) + "/maps")
              .find("libgtk-4") != std::string::npos)
        break;
      pause_for(.02);
    }
    pause_for(.3);
    require(kill(owner.pid, SIGKILL) == 0, "Cannot kill fixture owner");
    owner.wait(5);
    deadline = now() + 5;
    while (now() < deadline) {
      std::ifstream in("/proc/" + std::to_string(pid) + "/stat");
      if (!in) {
        died = true;
        break;
      }
      std::string text((std::istreambuf_iterator<char>(in)), {});
      auto close = text.rfind(')');
      if (close != std::string::npos && text.substr(close + 2, 1) == "Z") {
        died = true;
        break;
      }
      pause_for(.02);
    }
    require(died, "Password child survived owner exit");
  } catch (...) {
    if (!died)
      kill(pid, SIGKILL);
    throw;
  }
  std::cout << "PASS: malformed/empty/oversized requests denied; child exits "
               "when owner dies\n";
}
#include "application-fixtures.hh"
#include "migration-fixture.hh"
#include "panel-fixtures.hh"
#include "panel-menu-fixtures.hh"
#include "session-fixtures.hh"
#include "session-system-fixtures.hh"
#include "theme-fixtures.hh"
int main(int argc, char **argv) {
  signal(SIGPIPE, SIG_IGN);
  signal(SIGTERM, diagnostic_signal);
  signal(SIGINT, diagnostic_signal);
  try {
    require(argc >= 2, "Expected fixture name");
    std::vector<std::string> args(argv + 2, argv + argc);
    std::string mode = argv[1];
    if (mode == "migration-fixture")
      migration_fixture(args);
    else if (mode == "openbox-compact")
      openbox_compact(fs::canonical("/proc/self/exe"), args);
    else if (mode == "theme-widgets")
      theme_widgets(fs::canonical("/proc/self/exe"), args);
    else if (mode == "real-header")
      real_header(fs::canonical("/proc/self/exe"), args);
    else if (mode == "header-child") {
      require(args.size() == 1, "Missing marker");
      std::cout << "Private header regression" << std::endl;
      std::string line;
      std::getline(std::cin, line);
      write_file(args[0], line + "\n");
      pause_for(10);
    } else if (mode == "mime-defaults")
      mime_defaults(fs::canonical("/proc/self/exe"), args);
    else if (mode == "compositor")
      compositor(args);
    else if (mode == "capture-json") {
      require(!args.empty(), "Missing marker");
      report(args[0],
             Json(std::vector<std::string>(args.begin() + 1, args.end())));
    } else if (mode == "panel-effects")
      panel_effects(fs::canonical("/proc/self/exe"), args);
    else if (mode == "panel-focus")
      panel_focus(fs::canonical("/proc/self/exe"), args);
    else if (mode == "panel-run")
      panel_run(args);
    else if (mode == "panel-taskbar")
      panel_taskbar(fs::canonical("/proc/self/exe"), args);
    else if (mode == "panel-respawn")
      panel_respawn(args);
    else if (mode == "panel-volume")
      panel_volume(args);
    else if (mode == "luxup-service")
      luxup_service(args);
    else if (mode == "frame-wallpaper")
      frame_wallpaper(args);
    else if (mode == "wallpaper-install")
      wallpaper_install(args);
    else if (mode == "xdg-autostart")
      xdg_autostart(fs::canonical("/proc/self/exe"), args);
    else if (mode == "terminal-forward")
      terminal_forward(fs::canonical("/proc/self/exe"), args);
    else if (mode == "session-edit")
      session_edit(args);
    else if (mode == "capture-args") {
      require(args.size() >= 1, "Missing marker");
      std::string text;
      for (size_t i = 1; i < args.size(); ++i) {
        if (i > 1)
          text += "\\0";
        text += args[i];
      }
      write_file(args[0], text);
    } else if (mode == "capture-cwd") {
      require(args.size() >= 1, "Missing marker");
      std::string text = fs::current_path().string();
      for (size_t i = 1; i < args.size(); ++i) {
        text += "\\0";
        text += args[i];
      }
      write_file(args[0], text);
    } else if (mode == "terminal-child") {
      require(args.size() == 3, "Invalid terminal child arguments");
      auto env_value = [](const char *key) {
        const char *value = std::getenv(key);
        return value ? std::string(value) : std::string("<unset>");
      };
      report(args[0], Json::array({getppid(), fs::current_path().string(), args[1],
                                  env_value("LUXE_TEST_MARKER"), env_value("VIRTUAL_ENV"),
                                  env_value("GSK_RENDERER")}));
      pause_for(std::stod(args[2]));
    } else if (mode == "headless")
      headless(args);
    else if (mode == "broker")
      broker(args);
    else if (mode == "polkit-owner")
      polkit_owner(args);
    else if (mode == "polkit-boundary")
      polkit_boundary(fs::canonical("/proc/self/exe"), args);
    else
      throw std::runtime_error("Unknown fixture: " + mode);
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
