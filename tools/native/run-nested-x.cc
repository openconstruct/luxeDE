// Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later
#include <X11/Xlib.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
#include <csignal>
#include <cstdlib>
#include <cerrno>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <thread>
#include <vector>
namespace fs = std::filesystem;
static volatile sig_atomic_t interrupted = 0;
static void signal_handler(int sig) { interrupted = sig; }
static bool enabled(const char* key) { auto s = getenv(key); return s && *s; }
static void stop(pid_t pid) {
  if (pid <= 0) return;
  kill(-pid, SIGTERM);
  for (int n=0; n<50; ++n) {
    if (waitpid(pid, nullptr, WNOHANG) == pid) return;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  kill(-pid, SIGKILL); while (waitpid(pid, nullptr, 0) < 0 && errno == EINTR) {}
}
static pid_t spawn(std::vector<std::string> args, int log = -1) {
  pid_t pid = fork();
  if (pid < 0) throw std::runtime_error("fork failed");
  if (!pid) {
    setpgid(0, 0);
    if (log >= 0) { dup2(log, STDOUT_FILENO); dup2(log, STDERR_FILENO); }
    std::vector<char*> raw;
    for (auto& arg : args) raw.push_back(arg.data());
    raw.push_back(nullptr); execvp(raw[0], raw.data()); perror("execvp"); _exit(127);
  }
  setpgid(pid, pid); return pid;
}
int main(int argc, char** argv) {
  Display* display = nullptr;
  pid_t server = -1, test = -1;
  int pipefd[2] = {-1,-1}, log = -1;
  fs::path directory;
  int result = 1;
  try {
    if (argc < 2) throw std::runtime_error("Usage: run-nested-x COMMAND [ARG ...]");
    signal(SIGTERM, signal_handler); signal(SIGINT, signal_handler);
    std::string screen = getenv("LUXE_TEST_SCREEN") ? getenv("LUXE_TEST_SCREEN") : "1024x768";
    std::smatch dimensions;
    if (!std::regex_match(screen, dimensions, std::regex("([0-9]+)x([0-9]+)"))) throw std::runtime_error("Invalid LUXE_TEST_SCREEN");
    int width = std::stoi(dimensions[1]), height = std::stoi(dimensions[2]);
    if (width < 1 || height < 1 || width > 32767 || height > 32767) throw std::runtime_error("Invalid screen dimensions");
    display = XOpenDisplay(nullptr);
    if (!display) throw std::runtime_error("Nested X tests require an available host X display");
    fcntl(ConnectionNumber(display), F_SETFD, FD_CLOEXEC);
    auto parent = XCreateSimpleWindow(display, DefaultRootWindow(display), 0, 0, width, height, 0, 0, 0);
    XSync(display, False);
    std::string pattern = (fs::temp_directory_path()/"luxe-ui-test-XXXXXX").string();
    if (!mkdtemp(pattern.data())) throw std::runtime_error("mkdtemp failed");
    directory = pattern;
    if (pipe(pipefd)) throw std::runtime_error("pipe failed");
    fcntl(pipefd[0], F_SETFD, FD_CLOEXEC);
    log = open((directory/"x.log").c_str(), O_CREAT|O_RDWR|O_CLOEXEC, 0600);
    if (log < 0) throw std::runtime_error("Cannot open X log");
    std::vector<std::string> args = {"Xephyr"};
    if (enabled("LUXE_TEST_RESIZEABLE")) args.push_back("-resizeable");
    args.insert(args.end(), {"-parent", std::to_string(parent), "-no-host-grab", "-displayfd", std::to_string(pipefd[1]), "-screen", screen, "-ac"});
    server = spawn(args, log); close(pipefd[1]); pipefd[1] = -1;
    pollfd ready{pipefd[0], POLLIN, 0};
    if (poll(&ready, 1, 10000) <= 0) throw std::runtime_error("Nested X server did not become ready");
    char number[64] = {}; auto count = read(pipefd[0], number, sizeof(number)-1);
    std::string value = count > 0 ? std::string(number, count) : "";
    if (!std::regex_match(value, std::regex("[0-9]+\\n"))) {
      std::ifstream error(directory/"x.log"); std::cerr << error.rdbuf();
      throw std::runtime_error("Nested X server failed");
    }
    value.pop_back();
    if (enabled("LUXE_TEST_RESIZEABLE")) {
      setenv("LUXE_TEST_HOST_DISPLAY", DisplayString(display), 1);
      setenv("LUXE_TEST_PARENT_WINDOW", std::to_string(parent).c_str(), 1);
    }
    for (auto pair : {std::pair{"XDG_CONFIG_HOME","config"}, {"XDG_CACHE_HOME","cache"}, {"XDG_DATA_HOME","data"}, {"XDG_STATE_HOME","state"}})
      setenv(pair.first, (directory/pair.second).c_str(), 1);
    if (!enabled("LUXE_TEST_HOST_RUNTIME")) {
      fs::create_directory(directory/"runtime"); chmod((directory/"runtime").c_str(), 0700);
      setenv("XDG_RUNTIME_DIR", (directory/"runtime").c_str(), 1);
    }
    for (auto pair : {std::pair{"GSETTINGS_BACKEND","memory"}, {"GIO_USE_VFS","local"}, {"GDK_BACKEND","x11"}, {"GSK_RENDERER","cairo"}, {"GTK_A11Y","none"}}) setenv(pair.first, pair.second, 1);
    setenv("DISPLAY", (":"+value).c_str(), 1);
    args = {"dbus-run-session", "--"};
    for (int i=1; i<argc; ++i) args.emplace_back(argv[i]);
    test = spawn(args);
    std::string timeout_text = getenv("LUXE_TEST_TIMEOUT_SECONDS") ? getenv("LUXE_TEST_TIMEOUT_SECONDS") : "90";
    if (!std::regex_match(timeout_text, std::regex("[0-9]{1,4}"))) throw std::runtime_error("Invalid nested X timeout");
    int timeout_seconds = std::stoi(timeout_text);
    if (timeout_seconds < 1 || timeout_seconds > 3600) throw std::runtime_error("Nested X timeout must be 1..3600 seconds");
    auto deadline = std::chrono::steady_clock::now()+std::chrono::seconds(timeout_seconds);
    int status;
    while (true) {
      auto waited = waitpid(test, &status, WNOHANG);
      if (waited == test) { kill(-test, SIGTERM); test = -1; result = WIFEXITED(status) ? WEXITSTATUS(status) : 128+WTERMSIG(status); break; }
      if (waited < 0 && errno != EINTR) throw std::runtime_error("waitpid failed");
      if (interrupted) { result = 128+interrupted; break; }
      if (std::chrono::steady_clock::now() >= deadline) throw std::runtime_error("Nested X test timed out");
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; }
  stop(test); stop(server);
  for (int fd : {pipefd[0], pipefd[1], log}) if (fd >= 0) close(fd);
  if (display) XCloseDisplay(display);
  if (!directory.empty()) { std::error_code error; fs::remove_all(directory, error); }
  return result;
}
