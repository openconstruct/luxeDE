/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */

// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <gio/gio.h>
#include <gio/gfiledescriptorbased.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/poll.h>
#include <fcntl.h>
#include <cerrno>
#include <chrono>
#include <unistd.h>
#include <regex>
#include "backend.h"
struct StorePackage {
    int source = 0;
    std::string id, title, summary, installed, available, scope;
    bool upgradable = false;
};
inline void isolate_store_command(gpointer) { setpgid(0, 0); }
inline void cancel_store_command(GCancellable *, gpointer data) {
    auto pid = static_cast<pid_t>(GPOINTER_TO_INT(data));
    if (pid > 0) kill(-pid, SIGTERM);
}
#ifdef LUXURYSTORE_TEST_STDOUT_LIMIT
constexpr gsize kStoreStdoutLimit = LUXURYSTORE_TEST_STDOUT_LIMIT;
#else
constexpr gsize kStoreStdoutLimit = 32 * 1024 * 1024;
#endif
constexpr gsize kStoreStderrLimit = 1024 * 1024;
#ifdef LUXURYSTORE_TEST_COMMAND_TIMEOUT_SECONDS
constexpr guint kStoreCommandTimeoutSeconds = LUXURYSTORE_TEST_COMMAND_TIMEOUT_SECONDS;
#else
constexpr guint kStoreCommandTimeoutSeconds = 45;
#endif
constexpr guint kStoreCatalogTimeoutSeconds = 90;
inline std::string command(const std::vector<std::string> &args, bool allow_empty_snap_list = false,
                           GCancellable *cancellable = nullptr, gint64 deadline_us = 0) {
    if (args.empty()) throw std::runtime_error("Store command is empty.");
    const gint64 now = g_get_monotonic_time();
    const gint64 command_deadline = now + kStoreCommandTimeoutSeconds * G_USEC_PER_SEC;
    if (deadline_us == 0 || command_deadline < deadline_us) deadline_us = command_deadline;
    std::vector<const char *> argv;
    for (auto &s : args) argv.push_back(s.c_str());
    argv.push_back(nullptr);
    GError *error = nullptr;
    auto launcher = g_subprocess_launcher_new(static_cast<GSubprocessFlags>(G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE));
    g_subprocess_launcher_setenv(launcher, "LC_ALL", "C.UTF-8", true);
    g_subprocess_launcher_set_child_setup(launcher, isolate_store_command, nullptr, nullptr);
    auto p = g_subprocess_launcher_spawnv(launcher, argv.data(), &error);
    g_object_unref(launcher);
    if (!p) {
        std::string msg = error ? error->message : "Could not start store command";
        g_clear_error(&error);
        throw std::runtime_error(msg);
    }
    const char *identifier = g_subprocess_get_identifier(p);
    auto pid = identifier ? static_cast<pid_t>(g_ascii_strtoll(identifier, nullptr, 10)) : 0;
    gulong cancel_id = cancellable
        ? g_cancellable_connect(cancellable, G_CALLBACK(cancel_store_command), GINT_TO_POINTER(pid), nullptr)
        : 0;
    GInputStream *streams[] = {g_subprocess_get_stdout_pipe(p), g_subprocess_get_stderr_pipe(p)};
    int fds[2] = {-1, -1};
    bool open[2] = {true, true};
    std::string output, errors, failure;
    bool terminating = false, force_sent = false;
    bool child_done = false;
    gint64 kill_deadline = 0;
    for (int i = 0; i < 2; ++i) {
        if (!G_IS_FILE_DESCRIPTOR_BASED(streams[i])) {
            failure = "Store output pipe cannot be monitored.";
            terminating = true;
        } else {
            fds[i] = g_file_descriptor_based_get_fd(G_FILE_DESCRIPTOR_BASED(streams[i]));
            int flags = fcntl(fds[i], F_GETFL);
            if (flags < 0 || fcntl(fds[i], F_SETFL, flags | O_NONBLOCK) != 0) {
                failure = "Store output pipe could not be made nonblocking.";
                terminating = true;
            }
        }
    }
    if (terminating && pid > 0) { kill(-pid, SIGTERM); kill_deadline = g_get_monotonic_time() + G_USEC_PER_SEC; }
    while (open[0] || open[1] || !child_done) {
        gint64 current = g_get_monotonic_time();
        siginfo_t child_info{};
        if (waitid(P_PID, static_cast<id_t>(pid), &child_info, WEXITED | WNOHANG | WNOWAIT) == 0)
            child_done = child_info.si_pid == static_cast<pid_t>(pid);
        else if (errno == ECHILD) child_done = true;
        if (!terminating && cancellable && g_cancellable_is_cancelled(cancellable)) {
            failure = "Store request was cancelled.";
            terminating = true; kill_deadline = current + G_USEC_PER_SEC;
            if (pid > 0) kill(-pid, SIGTERM);
        }
        if (!terminating && current >= deadline_us) {
            failure = "Store request exceeded its overall time limit.";
            terminating = true; kill_deadline = current + G_USEC_PER_SEC;
            if (pid > 0) kill(-pid, SIGTERM);
        }
        if (terminating && !force_sent && current >= kill_deadline) {
            if (pid > 0) kill(-pid, SIGKILL);
            g_subprocess_force_exit(p);
            force_sent = true;
        }
        struct pollfd poll_fds[2];
        nfds_t count = 0;
        for (int i = 0; i < 2; ++i)
            if (open[i]) poll_fds[count++] = {fds[i], POLLIN | POLLHUP | POLLERR, 0};
        int wait_ms = 100;
        if (!terminating && current < deadline_us)
            wait_ms = MIN(wait_ms, static_cast<int>((deadline_us - current + 999) / 1000));
        if (count && poll(poll_fds, count, wait_ms) < 0 && errno != EINTR && failure.empty()) {
            failure = "Could not monitor store command output.";
            terminating = true; kill_deadline = current + G_USEC_PER_SEC;
            if (pid > 0) kill(-pid, SIGTERM);
        }
        for (nfds_t j = 0; j < count; ++j) {
            int index = poll_fds[j].fd == fds[0] ? 0 : 1;
            if (!(poll_fds[j].revents & (POLLIN | POLLHUP | POLLERR))) continue;
            for (;;) {
                char buffer[8192];
                ssize_t got = read(fds[index], buffer, sizeof buffer);
                if (got < 0 && errno == EINTR) continue;
                if (got < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
                if (got <= 0) {
                    open[index] = false;
                    g_input_stream_close(streams[index], nullptr, nullptr);
                    break;
                }
                auto &target = index == 0 ? output : errors;
                gsize limit = index == 0 ? kStoreStdoutLimit : kStoreStderrLimit;
                if (static_cast<gsize>(got) > limit - target.size()) {
                    if (failure.empty()) failure = index == 0 ? "Store response exceeded its output limit." : "Store diagnostics exceeded their output limit.";
                    terminating = true; kill_deadline = g_get_monotonic_time() + G_USEC_PER_SEC;
                    if (pid > 0) kill(-pid, SIGTERM);
                } else if (!terminating) target.append(buffer, static_cast<size_t>(got));
            }
        }
        if (!count && !child_done) g_usleep(wait_ms * 1000);
    }
    if (cancellable && cancel_id) g_cancellable_disconnect(cancellable, cancel_id);
    GError *wait_error = nullptr;
    bool waited = g_subprocess_wait(p, nullptr, &wait_error);
    if (!waited && failure.empty()) failure = wait_error ? wait_error->message : "Could not reap store command.";
    g_clear_error(&wait_error);
    bool success = waited && g_subprocess_get_successful(p);
    if (failure.empty() &&
        (!g_utf8_validate(output.data(), output.size(), nullptr) || !g_utf8_validate(errors.data(), errors.size(), nullptr)))
        failure = "Store command returned invalid UTF-8 output.";
    g_object_unref(p);
    if (!failure.empty()) throw std::runtime_error(failure + (errors.empty() ? "" : "\n" + errors));
    if (!success && !(allow_empty_snap_list && errors.find("No snaps are installed") != std::string::npos)) {
        throw std::runtime_error(errors.empty() ? "Store command failed." : errors);
    }
    return output;
}
inline std::vector<std::string> tabs(const std::string &line) {
    std::vector<std::string> fields;
    size_t begin = 0, end;
    while ((end = line.find('\t', begin)) != std::string::npos) { fields.push_back(line.substr(begin, end - begin)); begin = end + 1; }
    fields.push_back(line.substr(begin));
    fields.resize(std::max<size_t>(fields.size(), 5)); return fields;
}
inline bool official_flathub(const std::string &url) {
    return url == "https://dl.flathub.org/repo/" || url == "https://dl.flathub.org/repo";
}
inline std::string flathub_scope(GCancellable *cancellable = nullptr, gint64 deadline_us = 0) {
    for (auto scope : {"--user", "--system"}) {
        std::istringstream lines(command({"/usr/bin/flatpak", "remotes", scope, "--columns=name,url"}, false, cancellable, deadline_us));
        std::string line;
        while (std::getline(lines, line)) {
            auto f = tabs(line);
            if (f[0] == "flathub") {
                if (!official_flathub(f[1])) throw std::runtime_error("The configured flathub remote is not the official Flathub URL.");
                return scope;
            }
        }
    }
    // No automatic remote-add here: callers must ask the user first and then
    // call flathub_add() explicitly.
    throw std::runtime_error("Flathub remote is not configured. Enable Flathub to add it.");
}
inline std::string flathub_add() {
    command({"/usr/bin/flatpak", "remote-add", "--user", "--if-not-exists", "flathub", "https://dl.flathub.org/repo/flathub.flatpakrepo"});
    return "--user";
}
inline std::vector<StorePackage> flatpak_packages(bool force = false, GCancellable *cancellable = nullptr) {
    /* The catalogue is refreshed on an explicit source/reload operation.
     * Keeping a process-global cache here allowed GTK callbacks and worker
     * threads to race on its vector and left installed state stale after an
     * operation; correctness is more important than the extra remote query. */
    (void)force;
    const gint64 deadline = g_get_monotonic_time() + kStoreCatalogTimeoutSeconds * G_USEC_PER_SEC;
    auto scope = flathub_scope(cancellable, deadline);
    std::map<std::string, StorePackage> packages;
    std::istringstream refs(command({"/usr/bin/flatpak", "remote-ls", scope, "--app", "--columns=ref,name,version,description", "flathub"}, false, cancellable, deadline));
    std::string line;
    while (std::getline(refs, line)) {
        auto f = tabs(line); if (f[0].empty()) continue;
        packages[f[0]] = {1, f[0], f[1], f[3], "", f[2], scope};
        if (packages.size() > 100000) throw std::runtime_error("Flathub catalog exceeded 100,000 rows.");
    }
    for (auto installed_scope : {"--user", "--system"}) {
        bool verified = false;
        std::istringstream remotes(command({"/usr/bin/flatpak", "remotes", installed_scope, "--columns=name,url"}, false, cancellable, deadline));
        while (std::getline(remotes, line)) { auto f = tabs(line); if (f[0] == "flathub" && official_flathub(f[1])) verified = true; }
        if (!verified) continue;
        std::istringstream updates(command({"/usr/bin/flatpak", "remote-ls", installed_scope, "--app", "--updates", "--columns=ref", "flathub"}, false, cancellable, deadline));
        std::set<std::string> pending;
        while (std::getline(updates, line)) pending.insert(line);
        std::istringstream installed(command({"/usr/bin/flatpak", "list", installed_scope, "--app", "--columns=ref,name,version,origin"}, false, cancellable, deadline));
        while (std::getline(installed, line)) {
            auto f = tabs(line); if (f[0].empty() || f[3] != "flathub") continue;
            // Keep installations in both scopes distinct so remove targets the displayed installation.
            auto key = f[0];
            if (packages.count(key) && !packages[key].installed.empty()) key += installed_scope;
            auto &p = packages[key];
            p.source = 1; p.id = f[0]; p.title = f[1]; p.installed = f[2].empty() ? "Installed" : f[2]; p.scope = installed_scope; p.upgradable = pending.count(f[0]);
        }
    }
    std::vector<StorePackage> result;
    for (auto &[id, p] : packages) {
        if (p.summary.empty()) p.summary = p.title;
        result.push_back(p);
    }
    return result;
}
inline std::vector<StorePackage> snap_packages(const std::string &query, GCancellable *cancellable = nullptr) {
    const gint64 deadline = g_get_monotonic_time() + kStoreCatalogTimeoutSeconds * G_USEC_PER_SEC;
    std::map<std::string, StorePackage> packages;
    std::string line;
    if (!query.empty()) {
        std::istringstream found(command({"/usr/bin/snap", "find", query}, false, cancellable, deadline));
        std::getline(found, line);
        while (std::getline(found, line)) {
            std::istringstream fields(line); std::string name, version, publisher, notes, summary;
            if (!(fields >> name >> version >> publisher >> notes)) continue;
            std::getline(fields >> std::ws, summary);
            if (!valid_name(name)) continue;
            packages[name] = {2, name, name, summary, "", version, ""};
            if (packages.size() > 100000) throw std::runtime_error("Snap search exceeded 100,000 rows.");
        }
    }
    std::istringstream installed(command({"/usr/bin/snap", "list"}, true, cancellable, deadline));
    std::getline(installed, line);
    while (std::getline(installed, line)) {
        std::istringstream fields(line); std::string name, version, rev, tracking, publisher, notes;
        if (!(fields >> name >> version >> rev >> tracking >> publisher >> notes)) continue;
        if (!valid_name(name) || publisher == "-") continue;
        auto &p = packages[name]; p.source = 2; p.id = p.title = name; p.installed = version;
    }
    bool has_installed = std::any_of(packages.begin(), packages.end(), [](const auto &entry) { return !entry.second.installed.empty(); });
    if (has_installed) {
        std::istringstream updates(command({"/usr/bin/snap", "refresh", "--list"}, false, cancellable, deadline));
        std::getline(updates, line);
        while (std::getline(updates, line)) {
            std::istringstream fields(line); std::string name, version;
            if (fields >> name >> version && packages.count(name)) { packages[name].upgradable = true; packages[name].available = version; }
        }
    }
    std::vector<StorePackage> result;
    for (auto &[id, p] : packages) result.push_back(p);
    return result;
}
