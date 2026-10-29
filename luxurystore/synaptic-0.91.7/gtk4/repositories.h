// Copyright (C) 2026 Jerry Howell
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <glib.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

struct Repository {
    const char *id;
    const char *title;
    const char *file;
    const char *package;
    const char *description;
};

// These package names are discovery hints, not an assertion that a vendor
// currently publishes them. APT remains the authority after a refresh.
inline const std::vector<Repository> &repository_catalog() {
    static const std::vector<Repository> entries{
        {"chatgpt", "ChatGPT", "chatgpt.sources", "chatgpt", "OpenAI's desktop application"},
        {"claude", "Claude Desktop", "claude-desktop.list", "claude-desktop", "Anthropic's desktop application"},
        {"cursor", "Cursor", "cursor.sources", "cursor", "Cursor code editor"},
        {"edge", "Microsoft Edge", "microsoft-edge.sources", "microsoft-edge-stable", "Microsoft's web browser"},
        {"mozilla", "Mozilla Firefox", "mozilla.list", "firefox", "Mozilla's web browser"},
        {"github-cli", "GitHub CLI", "github-cli.list", "gh", "GitHub command-line tool"},
        {"nodesource", "NodeSource", "nodesource.sources", "nodejs", "Node.js packages"},
        {"chrome", "Google Chrome", "google-chrome.list", "google-chrome-stable", "Google's web browser"},
        {"rocm-nightly", "ROCm nightly", "rocm-nightly.list", "rocm", "Community GPU compute builds"},
    };
    return entries;
}

inline const Repository *repository_by_id(const std::string &id) {
    for (const auto &entry : repository_catalog()) if (id == entry.id) return &entry;
    return nullptr;
}

inline std::string repository_path(const Repository &entry) {
    return std::string("/etc/apt/sources.list.d/") + entry.file;
}

inline bool repository_file_exists(const Repository &entry) {
    struct stat st{};
    return lstat(repository_path(entry).c_str(), &st) == 0 && S_ISREG(st.st_mode) &&
           st.st_uid == 0 && (st.st_mode & 022) == 0;
}

inline std::string read_bounded_regular_file(const std::string &path, size_t maximum) {
    int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0) throw std::runtime_error("Cannot open repository definition: " + path);
    struct stat st{};
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) ||
#ifdef LUXURYSTORE_TESTING
        st.st_uid != getuid() ||
#else
        st.st_uid != 0 ||
#endif
        (st.st_mode & 022) != 0 || st.st_nlink != 1 || st.st_size < 0 ||
        static_cast<size_t>(st.st_size) > maximum) {
        close(fd);
        throw std::runtime_error("Repository definition is missing or unsafe: " + path);
    }
    std::string result;
    result.reserve(static_cast<size_t>(st.st_size));
    char buffer[4096];
    for (;;) {
        ssize_t n = read(fd, buffer, sizeof(buffer));
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) { close(fd); throw std::runtime_error("Cannot read repository definition: " + path); }
        if (n == 0) break;
        if (result.size() + static_cast<size_t>(n) > maximum) {
            close(fd);
            throw std::runtime_error("Repository definition is too large: " + path);
        }
        result.append(buffer, static_cast<size_t>(n));
    }
    close(fd);
    if (result.find('\0') != std::string::npos)
        throw std::runtime_error("Invalid repository definition.");
    return result;
}

inline bool write_repository_atomic(const std::string &path, const std::string &contents,
                                    const std::string *expected_current = nullptr) {
    if (contents.size() > 65536 || contents.find('\0') != std::string::npos)
        throw std::runtime_error("Repository definition is too large or invalid.");
    gchar *directory = g_path_get_dirname(path.c_str());
    if (!directory) throw std::runtime_error("Cannot determine repository directory.");
    int dirfd = open(directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    g_free(directory);
    if (dirfd < 0) throw std::runtime_error("Repository directory is unsafe: " + path);
    std::string temporary = path + ".luxurystore.XXXXXX";
    std::vector<char> name(temporary.begin(), temporary.end());
    name.push_back('\0');
    int fd = mkstemp(name.data());
    if (fd < 0) { close(dirfd); throw std::runtime_error("Cannot create temporary repository definition."); }
    bool ok = fchmod(fd, 0644) == 0;
    size_t written = 0;
    while (ok && written < contents.size()) {
        ssize_t n = write(fd, contents.data() + written, contents.size() - written);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { ok = false; break; }
        written += static_cast<size_t>(n);
    }
    if (ok && fsync(fd) != 0) ok = false;
    if (close(fd) != 0) ok = false;
    if (ok && expected_current) {
        try {
            if (read_bounded_regular_file(path, 65536) != *expected_current)
                throw std::runtime_error("Repository definition changed during this operation; the external edit was preserved.");
        } catch (...) {
            unlink(name.data());
            close(dirfd);
            throw;
        }
    }
    if (ok && rename(name.data(), path.c_str()) != 0) ok = false;
    if (!ok) {
        unlink(name.data());
        close(dirfd);
        throw std::runtime_error("Cannot atomically update repository definition: " + path);
    }
    bool durable = fsync(dirfd) == 0;
    close(dirfd);
    return durable;
}

inline std::string repository_contents(const Repository &entry) {
    return read_bounded_regular_file(repository_path(entry), 65536);
}

inline std::string repository_trim(const std::string &value) {
    size_t begin = 0;
    while (begin < value.size() && g_ascii_isspace(value[begin])) ++begin;
    size_t end = value.size();
    while (end > begin && g_ascii_isspace(value[end - 1])) --end;
    return value.substr(begin, end - begin);
}

struct RepositoryLine {
    std::string body, ending;
    bool source = false, disabled = false;
};
struct RepositoryState {
    bool valid = false, enabled = false, can_enable = false;
    std::string reason;
    std::vector<RepositoryLine> lines;
    std::map<std::string, size_t> fields;
};

inline std::string repository_lower(std::string value) {
    for (char &c : value) c = g_ascii_tolower(c);
    return value;
}

inline bool repository_unsafe_trust(const std::string &key, const std::string &value) {
    if (key != "trusted" && key != "allow-insecure" && key != "allow-weak" &&
        key != "allow-downgrade-to-insecure") return false;
    return repository_lower(repository_trim(value)) != "no";
}

// Parse conservatively: a definition we cannot interpret is displayed as
// unsupported and is never rewritten by the privileged helper.
inline RepositoryState repository_inspect(const std::string &text, bool deb822) {
    RepositoryState state;
    auto fail = [&](const char *message) { state.reason = message; return state; };
    for (size_t start = 0; start < text.size();) {
        size_t end = text.find('\n', start);
        bool newline = end != std::string::npos;
        if (!newline) end = text.size();
        size_t body_end = end;
        std::string ending = newline ? "\n" : "";
        if (newline && body_end > start && text[body_end - 1] == '\r') {
            --body_end; ending = "\r\n";
        }
        state.lines.push_back({text.substr(start, body_end - start), ending});
        start = newline ? end + 1 : end;
    }
    if (deb822) {
        bool started = false, ended = false;
        std::map<std::string, std::string> values;
        for (size_t i = 0; i < state.lines.size(); ++i) {
            const auto trimmed = repository_trim(state.lines[i].body);
            if (trimmed.empty()) { if (started) ended = true; continue; }
            if (trimmed[0] == '#') continue;
            if (ended) return fail("Multiple source paragraphs are unsupported.");
            started = true;
            auto colon = trimmed.find(':');
            if (colon == std::string::npos || colon == 0)
                return fail("Unsupported or malformed deb822 field.");
            auto key = repository_lower(trimmed.substr(0, colon));
            for (char c : key) if (!g_ascii_isalnum(c) && c != '-')
                return fail("Unsupported or malformed deb822 field.");
            if (!state.fields.emplace(key, i).second)
                return fail("Duplicate deb822 field.");
            values[key] = repository_trim(trimmed.substr(colon + 1));
        }
        if (values["types"] != "deb") return fail("Only one deb source paragraph is supported.");
        if (values["uris"].empty() || values["suites"].empty())
            return fail("Source URIs and Suites are required.");
        auto enabled = repository_lower(values["enabled"]);
        if (!enabled.empty() && enabled != "yes" && enabled != "no")
            return fail("Invalid Enabled value.");
        if (state.fields.count("enabled") && enabled.empty()) return fail("Empty Enabled value.");
        state.enabled = enabled != "no";
        auto key = values["signed-by"];
        state.can_enable = !key.empty() && key[0] == '/';
        for (const auto &field : values)
            if (repository_unsafe_trust(field.first, field.second)) state.can_enable = false;
    } else {
        bool found = false, first_enabled = false, all_scoped = true;
        const std::string disabled = "# luxurystore-disabled ";
        for (auto &line : state.lines) {
            auto item = repository_trim(line.body);
            bool off = item.rfind(disabled, 0) == 0;
            if (off) item = repository_trim(item.substr(disabled.size()));
            else if (item.empty() || item[0] == '#') continue;
            auto space = item.find_first_of(" \t");
            if (space == std::string::npos || (item.substr(0, space) != "deb" && item.substr(0, space) != "deb-src"))
                return fail("Unsupported list source syntax.");
            auto rest = repository_trim(item.substr(space));
            bool scoped = false;
            if (!rest.empty() && rest[0] == '[') {
                auto close = rest.find(']');
                if (close == std::string::npos) return fail("Malformed source options.");
                std::istringstream options(rest.substr(1, close - 1));
                std::map<std::string, std::string> seen;
                std::string option;
                while (options >> option) {
                    auto eq = option.find('=');
                    if (eq == std::string::npos || eq == 0 || eq + 1 == option.size())
                        return fail("Unsupported source option.");
                    auto key = repository_lower(option.substr(0, eq));
                    auto value = option.substr(eq + 1);
                    for (char c : key) if (!g_ascii_isalnum(c) && c != '-')
                        return fail("Unsupported source option name.");
                    if (!seen.emplace(key, value).second) return fail("Duplicate source option.");
                    if (key == "signed-by") scoped = value[0] == '/';
                    if (repository_unsafe_trust(key, value)) all_scoped = false;
                }
                rest = repository_trim(rest.substr(close + 1));
            }
            std::istringstream fields(rest);
            std::string uri, suite;
            if (!(fields >> uri >> suite)) return fail("Incomplete list source.");
            if (found && first_enabled == off) return fail("Mixed source states are unsupported.");
            found = true; first_enabled = !off;
            all_scoped = all_scoped && scoped;
            line.source = true; line.disabled = off;
        }
        if (!found) return fail("No supported APT source found.");
        state.enabled = first_enabled;
        state.can_enable = all_scoped;
    }
    state.valid = true;
    if (!state.can_enable) state.reason = "Every source needs a scoped signing key and must not bypass authentication.";
    return state;
}

inline bool repository_enabled_text(const std::string &text, bool deb822) {
    auto state = repository_inspect(text, deb822);
    return state.valid && state.enabled;
}
inline std::string repository_rewrite(const std::string &text, bool deb822, bool enable) {
    auto state = repository_inspect(text, deb822);
    if (!state.valid) throw std::runtime_error(state.reason);
    const std::string marker = "# luxurystore-disabled ";
    if (deb822) {
        auto found = state.fields.find("enabled");
        if (found != state.fields.end()) {
            auto &body = state.lines[found->second].body;
            auto colon = body.find(':');
            body.replace(colon + 1, std::string::npos, enable ? " yes" : " no");
        } else {
            auto index = state.fields.at("types");
            auto newline = state.lines[index].ending;
            auto inserted_ending = newline;
            if (newline.empty()) state.lines[index].ending = "\n";
            state.lines.insert(state.lines.begin() + index + 1,
                RepositoryLine{enable ? "Enabled: yes" : "Enabled: no", inserted_ending});
        }
    } else {
        for (auto &line : state.lines) {
            if (!line.source || line.disabled == !enable) continue;
            size_t offset = line.body.find_first_not_of(" \t");
            if (enable) line.body.erase(offset, marker.size());
            else line.body.insert(offset, marker);
        }
    }
    std::string out;
    for (const auto &line : state.lines) out += line.body + line.ending;
    return out;
}
