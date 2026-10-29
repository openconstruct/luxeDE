// Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later
#include "diagnostic-common.hh"
int main(int argc, char **argv) {
  signal(SIGPIPE, SIG_IGN);
  try {
    require(argc == 3, "Expected diagnostic binary and source root");
    auto binary = fs::canonical(argv[1]);
    Scratch dir("luxe-diagnostic-test-");
    auto values = memory(getpid());
    require(values.at("pss_kib").get<long long>() > 0, "Missing live PSS");
    require(ticks(getpid()) >= 0, "Invalid CPU ticks");
    Env env = {{"LUXE_TEST_LITERAL", "spaces 'quotes' $dollars; untouched"}};
    auto result =
        command({"sh", "-c", "printf '%s' \"$LUXE_TEST_LITERAL\""}, env);
    require(result == *env["LUXE_TEST_LITERAL"],
            "Environment was not passed literally");
    pid_t stopped;
    {
      Child child({"sleep", "30"});
      stopped = child.pid;
      require(child.alive(), "Sleep did not start");
      bool expired = false;
      try {
        child.wait(.05);
      } catch (const std::runtime_error &) {
        expired = true;
      }
      require(expired, "Timeout did not fire");
    }
    require(kill(stopped, 0) == -1 && errno == ESRCH, "Timed-out child leaked");
    {
      Child child({"sh", "-c", "read -r value; printf '%s\\n' \"$value\""}, {},
                  dir.path / "log", true);
      child.send("message with spaces\n");
      require(child.line(2) == "message with spaces",
              "Interactive response mismatch");
      require(child.wait(2) == 0, "Interactive child failed");
    }
    auto one = dir.path / "one", two = dir.path / "two";
    fs::create_directory(one);
    fs::create_directory(two);
    for (auto root : {one, two}) {
      write_file(root / "a.svg", "same artwork");
      fs::create_symlink("a.svg", root / "alias.svg");
      write_file(root / "icon-theme.cache", "cache");
    }
    auto rows = lines(command({binary.string(), argv[2], "icon-payload",
                               one.string(), two.string()}));
    require(rows.size() == 2, "Expected two icon reports");
    auto first = Json::parse(rows[0]);
    require(first["svg_paths"] == 2 && first["symlinks"] == 1 &&
                first["host_cache_bytes"] == 5,
            "Wrong icon metrics");
    write_file(two / "a.svg", "different artwork");
    bool rejected = false;
    try {
      command({binary.string(), argv[2], "icon-payload", one.string(),
               two.string()});
    } catch (const std::runtime_error &) {
      rejected = true;
    }
    require(rejected, "Changed artwork accepted");
    fs::create_symlink("/etc/passwd", one / "outside");
    rejected = false;
    try {
      command({binary.string(), argv[2], "icon-payload", one.string()});
    } catch (const std::runtime_error &) {
      rejected = true;
    }
    require(rejected, "External icon link accepted");
    std::cout
        << "PASS: diagnostic memory, literal environment, timeout cleanup, "
           "interactive IPC, icon comparison and link boundaries\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
