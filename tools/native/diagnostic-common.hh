// Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cerrno>
#include <chrono>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <poll.h>
#include <regex>
#include <set>
#include <signal.h>
#include <sstream>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
using Json = nlohmann::json;
namespace fs = std::filesystem;
inline volatile sig_atomic_t diagnostic_interrupted = 0;
inline void diagnostic_signal(int sig) { diagnostic_interrupted = sig; }
inline void check_interrupted() {
  if (diagnostic_interrupted)
    throw std::runtime_error("Diagnostic interrupted");
}
using Env = std::map<std::string, std::optional<std::string>>;
inline void require(bool ok, const std::string &error) {
  if (!ok)
    throw std::runtime_error(error);
}
inline std::string read_file(const fs::path &path) {
  std::ifstream in(path, std::ios::binary);
  require(bool(in), "Cannot read " + path.string());
  std::string s((std::istreambuf_iterator<char>(in)), {});
  require(!in.bad(), "Read error: " + path.string());
  return s;
}
inline void write_file(const fs::path &path, const std::string &text) {
  std::ofstream out(path, std::ios::binary);
  out << text;
  out.close();
  require(bool(out), "Cannot write " + path.string());
}
inline void report(const fs::path &path, const Json &value) {
  write_file(path, value.dump(2) + "\n");
}
inline double now() {
  return std::chrono::duration<double>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}
inline void pause_for(double seconds) {
  check_interrupted();
  std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
  check_interrupted();
}
struct Scratch {
  fs::path path;
  explicit Scratch(const std::string &prefix) {
    auto s = (fs::temp_directory_path() / (prefix + "XXXXXX")).string();
    require(mkdtemp(s.data()), "mkdtemp failed");
    path = s;
  }
  ~Scratch() {
    std::error_code ec;
    fs::remove_all(path, ec);
  }
};
struct Child {
  pid_t pid = -1;
  int input = -1, output = -1;
  std::optional<int> status;
  Child(std::vector<std::string> args, const Env &env = {},
        const fs::path &log = {}, bool interactive = false,
        const fs::path &cwd = {}) {
    int in[2] = {-1, -1}, out[2] = {-1, -1};
    if (interactive) {
      require(pipe2(in, O_CLOEXEC) == 0, "stdin pipe failed");
      if (pipe2(out, O_CLOEXEC) != 0) {
        close(in[0]);
        close(in[1]);
        throw std::runtime_error("stdout pipe failed");
      }
    }
    int logfd =
        log.empty()
            ? -1
            : open(log.c_str(), O_CREAT | O_WRONLY | O_TRUNC | O_CLOEXEC, 0600);
    if (!log.empty() && logfd < 0) {
      for (int f : {in[0], in[1], out[0], out[1]})
        if (f >= 0)
          close(f);
      throw std::runtime_error("Cannot open log");
    }
    pid_t parent = getpid();
    pid = fork();
    if (pid == 0) {
      setpgid(0, 0);
      prctl(PR_SET_PDEATHSIG, SIGTERM);
      if (getppid() != parent)
        _exit(125);
      if (!cwd.empty() && chdir(cwd.c_str()))
        _exit(126);
      if (logfd >= 0) {
        dup2(logfd, STDERR_FILENO);
        if (!interactive)
          dup2(logfd, STDOUT_FILENO);
        close(logfd);
      }
      if (interactive) {
        dup2(in[0], STDIN_FILENO);
        dup2(out[1], STDOUT_FILENO);
        for (int f : {in[0], in[1], out[0], out[1]})
          close(f);
      }
      for (auto &[key, value] : env) {
        if (value)
          setenv(key.c_str(), value->c_str(), 1);
        else
          unsetenv(key.c_str());
      }
      std::vector<char *> raw;
      for (auto &arg : args)
        raw.push_back(arg.data());
      raw.push_back(nullptr);
      execvp(raw[0], raw.data());
      perror("execvp");
      _exit(127);
    }
    if (logfd >= 0)
      close(logfd);
    if (pid < 0) {
      for (int f : {in[0], in[1], out[0], out[1]})
        if (f >= 0)
          close(f);
      throw std::runtime_error("fork failed");
    }
    setpgid(pid, pid);
    if (interactive) {
      close(in[0]);
      close(out[1]);
      input = in[1];
      output = out[0];
    }
  }
  Child(const Child &) = delete;
  bool alive() {
    if (status)
      return false;
    int s = 0;
    pid_t got = waitpid(pid, &s, WNOHANG);
    if (got == 0 || (got < 0 && errno == EINTR))
      return true;
    require(got == pid, "waitpid failed");
    status = WIFEXITED(s) ? WEXITSTATUS(s) : 128 + WTERMSIG(s);
    return false;
  }
  int wait(double timeout) {
    double deadline = now() + timeout;
    while (alive()) {
      check_interrupted();
      require(now() < deadline, "Child timed out");
      pause_for(.01);
    }
    return *status;
  }
  void send(const std::string &s) {
    size_t n = 0;
    while (n < s.size()) {
      ssize_t r = write(input, s.data() + n, s.size() - n);
      if (r < 0 && errno == EINTR)
        continue;
      require(r > 0, "Child stdin closed");
      n += r;
    }
  }
  std::string line(double timeout) {
    std::string result;
    double deadline = now() + timeout;
    while (true) {
      check_interrupted();
      pollfd p{output, POLLIN, 0};
      double remaining = deadline - now();
      require(remaining > 0, "Child response timed out");
      int ready = poll(&p, 1, int(remaining * 1000) + 1);
      if (ready < 0 && errno == EINTR)
        continue;
      require(ready > 0, "Child response timed out");
      char c;
      ssize_t n = read(output, &c, 1);
      if (n < 0 && errno == EINTR)
        continue;
      require(n == 1, "Child response ended");
      if (c == '\n')
        return result;
      result += c;
      require(result.size() < 1048576, "Child response too long");
    }
  }
  ~Child() {
    if (input >= 0)
      close(input);
    if (output >= 0)
      close(output);
    if (pid > 0) {
      kill(-pid, SIGTERM);
      try {
        wait(3);
      } catch (...) {
        kill(-pid, SIGKILL);
        int s;
        while (waitpid(pid, &s, 0) < 0 && errno == EINTR) {
        };
      }
    }
  }
};
inline std::string command(const std::vector<std::string> &args,
                           const Env &env = {}, double timeout = 10) {
  Scratch dir("luxe-command-");
  Child child(args, env, dir.path / "log");
  int status = child.wait(timeout);
  auto text = read_file(dir.path / "log");
  require(status == 0, "Command failed: " + args[0] +
          (args.size() > 1 ? " " + args[1] : "") +
          " (exit " + std::to_string(status) + "): " + text);
  return text;
}
inline std::string trim(std::string s) {
  while (!s.empty() && std::isspace((unsigned char)s.back()))
    s.pop_back();
  return s;
}
inline std::vector<std::string> lines(const std::string &text) {
  std::istringstream in(text);
  std::vector<std::string> result;
  std::string line;
  while (std::getline(in, line))
    result.push_back(line);
  return result;
}
inline std::string digest(const fs::path &path) {
  auto s = command({"sha256sum", "--zero", "--", path.string()});
  require(s.size() >= 64, "Invalid SHA256 output");
  return s.substr(0, 64);
}
inline Json proc_fields(pid_t pid, const char *name) {
  Json result = Json::object();
  for (auto &line :
       lines(read_file("/proc/" + std::to_string(pid) + "/" + name))) {
    std::istringstream in(line);
    std::string key, unit;
    long long value;
    if (in >> key >> value >> unit && unit == "kB" && key.back() == ':')
      result[key.substr(0, key.size() - 1)] = value;
  }
  return result;
}
inline long long ticks(pid_t pid) {
  auto text = read_file("/proc/" + std::to_string(pid) + "/stat");
  auto pos = text.rfind(')');
  require(pos != std::string::npos, "Invalid proc stat");
  std::istringstream in(text.substr(pos + 1));
  std::vector<std::string> values;
  std::string s;
  while (in >> s)
    values.push_back(s);
  require(values.size() > 12, "Short proc stat");
  return std::stoll(values[11]) + std::stoll(values[12]);
}
inline Json memory(pid_t pid) {
  auto v = proc_fields(pid, "smaps_rollup");
  return {{"pss_kib", v.at("Pss")},
          {"rss_kib", v.at("Rss")},
          {"private_kib",
           v.value("Private_Clean", 0LL) + v.value("Private_Dirty", 0LL)},
          {"pss_anon_kib", v.value("Pss_Anon", 0LL)},
          {"pss_file_kib", v.value("Pss_File", 0LL)},
          {"pss_shmem_kib", v.value("Pss_Shmem", 0LL)},
          {"anon_huge_kib", v.value("AnonHugePages", 0LL)},
          {"swap_pss_kib", v.value("SwapPss", 0LL)}};
}
inline Json select_fields(const Json &fields,
                          std::initializer_list<const char *> keys) {
  Json r = Json::object();
  for (auto key : keys)
    r[key] = fields.at(key);
  return r;
}
inline Env private_env(const fs::path &root) {
  Env env;
  for (auto key : {"GTK_THEME", "GSK_RENDERER", "GSK_DEBUG", "GDK_DEBUG",
                   "GSETTINGS_SCHEMA_DIR"})
    env[key] = std::nullopt;
  for (auto pair : {std::pair{"HOME", "home"},
                    {"XDG_CONFIG_HOME", "config"},
                    {"XDG_CACHE_HOME", "cache"},
                    {"XDG_DATA_HOME", "data"},
                    {"XDG_STATE_HOME", "state"},
                    {"XDG_RUNTIME_DIR", "runtime"}}) {
    fs::create_directories(root / pair.second);
    fs::permissions(root / pair.second, fs::perms(0700));
    env[pair.first] = (root / pair.second).string();
  }
  for (auto pair : {std::pair{"GTK_A11Y", "none"},
                    {"GSETTINGS_BACKEND", "memory"},
                    {"GIO_USE_VFS", "local"},
                    {"GDK_BACKEND", "x11"}})
    env[pair.first] = pair.second;
  return env;
}
inline void
gtk_settings(const fs::path &root, const std::string &theme,
             const std::string &icons, bool bridge = false,
             const fs::path &colors = "/usr/share/luxe/themes/tokyo-night") {
  auto gtk = root / "config/gtk-4.0";
  fs::create_directories(gtk);
  write_file(
      gtk / "settings.ini",
      "[Settings]\ngtk-theme-name=" + theme + "\ngtk-icon-theme-name=" + icons +
          "\ngtk-font-name=Sans 10\ngtk-application-prefer-dark-theme=false\n");
  if (bridge) {
    write_file(gtk / "luxe-colors.css", read_file(colors / "colors.css") +
                                            "\n" +
                                            read_file(colors / "modern.css"));
    write_file(gtk / "gtk.css", "@import url(\"luxe-colors.css\");\n");
  }
}
inline pid_t application_pid(pid_t initial, const fs::path &executable) {
  std::vector<pid_t> pending{initial};
  while (!pending.empty()) {
    auto pid = pending.back();
    pending.pop_back();
    std::error_code ec;
    auto path = fs::canonical("/proc/" + std::to_string(pid) + "/exe", ec);
    if (!ec && path == executable)
      return pid;
    std::ifstream in("/proc/" + std::to_string(pid) + "/task/" +
                     std::to_string(pid) + "/children");
    pid_t child;
    while (in >> child)
      pending.push_back(child);
  }
  throw std::runtime_error("Cannot find measured application ELF");
}
inline void healthy(Child &child, const fs::path &log) {
  require(child.alive(), "Application exited: " + read_file(log));
}
