// Copyright (C) 2026 Jerry Howell
// SPDX-License-Identifier: GPL-2.0-or-later
#include "backend.h"
#include "repositories.h"
#include "rinstallprogress.h"
#include <apt-pkg/install-progress.h>
#include <apt-pkg/fileutl.h>
#include <apt-pkg/configuration.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <cerrno>
#include <iostream>
#include <regex>

class Dialog : public RUserDialog {
    bool message(const char *msg, DialogType, ButtonsType buttons, bool) override {
        std::cerr << msg << std::endl;
        return buttons == ButtonsOk || buttons == ButtonsDefault;
    }
};
class Downloads : public pkgAcquireStatus {
    bool MediaChange(std::string, std::string) override { return false; }
    void Fetch(pkgAcquire::ItemDesc &item) override { std::cout << "Downloading " << item.Description << std::endl; }
};
class Progress : public RInstallProgress {
    pkgPackageManager::OrderResult start(pkgPackageManager *pm, int, int) override {
        if (pm == nullptr) return pkgPackageManager::Failed;
        auto res = pm->DoInstallPreFork();
        if (res == pkgPackageManager::Failed) return res;
        pid_t pid = fork();
        if (pid < 0) return pkgPackageManager::Failed;
        if (pid == 0) {
            APT::Progress::PackageManagerProgressFd progress(-1);
            auto result = pm->DoInstallPostFork(&progress);
            _error->DumpErrors();
            _exit(result == pkgPackageManager::Failed ? 1 : 0);
        }
        int status = 0;
        pid_t waited;
        do {
            waited = waitpid(pid, &status, 0);
        } while (waited < 0 && errno == EINTR);
        if (waited != pid || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
            return pkgPackageManager::Failed;
        return static_cast<pkgPackageManager::OrderResult>(0);
    }
};

class RepositoryLock {
public:
    RepositoryLock() {
        fd = open("/run/lock/luxurystore-repository.lock", O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
        struct stat st{};
        if (fd < 0 || fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_uid != 0 ||
            (st.st_mode & 0777) != 0600 || st.st_nlink != 1) {
            if (fd >= 0) close(fd);
            fd = -1;
            throw std::runtime_error("Cannot safely lock repository changes.");
        }
        while (flock(fd, LOCK_EX) != 0) {
            if (errno == EINTR) continue;
            close(fd); fd = -1;
            throw std::runtime_error("Cannot lock repository changes.");
        }
    }
    ~RepositoryLock() { if (fd >= 0) close(fd); }
    RepositoryLock(const RepositoryLock &) = delete;
    RepositoryLock &operator=(const RepositoryLock &) = delete;
private:
    int fd = -1;
};

int main(int argc, char **argv) {
    try {
#ifdef LUXURYSTORE_FIXTURE_HELPER
        // This uninstalled test binary may use a private APT_CONFIG, but can
        // never perform privileged operations, even if accidentally elevated.
        if (getuid() == 0 || geteuid() == 0)
            throw std::runtime_error("The fixture helper cannot run as root.");
#endif
        if (argc == 2 && std::string(argv[1]) == "--authorize") {
            if (geteuid() != 0) throw std::runtime_error("Administrator authentication is required.");
            return 0;
        }
        if (argc == 4 && std::string(argv[1]) == "--repo-set") {
            if (geteuid() != 0) throw std::runtime_error("Administrator authentication is required.");
            RepositoryLock lock;
            auto entry = repository_by_id(argv[2]);
            if (!entry || (std::string(argv[3]) != "on" && std::string(argv[3]) != "off"))
                throw std::runtime_error("Unknown repository or state.");
            bool enable = std::string(argv[3]) == "on";
            auto original = repository_contents(*entry);
            bool deb822 = g_str_has_suffix(entry->file, ".sources");
            auto state = repository_inspect(original, deb822);
            if (!state.valid) throw std::runtime_error(state.reason);
            if (enable && !state.can_enable)
                throw std::runtime_error(state.reason.empty() ?
                    "This repository has no signed-by keyring; insecure sources cannot be enabled." : state.reason);
            auto rewritten = repository_rewrite(original, deb822, enable);
            if (rewritten == original) {
                std::cout << entry->title << (enable ? " is already enabled.\n" : " is already disabled.\n");
                return 0;
            }
            const auto path = repository_path(*entry);
            if (!write_repository_atomic(path, rewritten, &original)) {
                std::string failure = "Repository definition was changed, but directory synchronization failed; durability is uncertain.";
                try {
                    if (!write_repository_atomic(path, original, &rewritten))
                        failure += " Restoring the original also failed directory synchronization.";
                } catch (const std::exception &rollback_error) {
                    failure += std::string(" The original could not be restored: ") + rollback_error.what();
                }
                throw std::runtime_error(failure);
            }
            unsetenv("APT_CONFIG");
            auto l = open_apt();
            Downloads downloads;
            std::string error;
            if (!l->updateCache(&downloads, error)) {
                std::string failure = "Repository setting changed, but APT refresh failed: " + error + "\n" + apt_errors();
                try {
                    if (!write_repository_atomic(path, original, &rewritten))
                        failure += "\nRepository rollback was written, but directory synchronization failed; durability is uncertain.";
                } catch (const std::exception &rollback_error) {
                    failure += std::string("\nRepository rollback was not applied: ") + rollback_error.what();
                }
                throw std::runtime_error(failure);
            }
            std::cout << entry->title << (enable ? " enabled" : " disabled") << ". Package lists refreshed.\n";
            return 0;
        }
        if (argc == 3 && std::string(argv[1]) == "--refresh" && std::string(argv[2]) == "apt") {
            if (geteuid() != 0) throw std::runtime_error("Administrator authentication is required.");
            unsetenv("APT_CONFIG");
            auto l = open_apt();
            Downloads downloads;
            std::string error;
            if (!l->updateCache(&downloads, error)) throw std::runtime_error(error + "\n" + apt_errors());
            std::cout << "Package lists refreshed.\n";
            return 0;
        }
        if (argc == 3 && std::string(argv[1]).rfind("--snap-", 0) == 0) {
            if (geteuid() != 0) throw std::runtime_error("Administrator authentication is required.");
            std::string name(argv[2]), option(argv[1]);
            if (!std::regex_match(name, std::regex("[a-z0-9]+(-[a-z0-9]+)*")))
                throw std::runtime_error("Invalid Snap name.");
            const char *verb = option == "--snap-install" ? "install" : option == "--snap-remove" ? "remove" : option == "--snap-refresh" ? "refresh" : nullptr;
            if (!verb) throw std::runtime_error("Invalid Snap operation.");
            execl("/usr/bin/snap", "snap", verb, name.c_str(), static_cast<char *>(nullptr));
            throw std::runtime_error("Could not launch snapd client.");
        }
        if (argc != 3 || (std::string(argv[1]) != "--apply" && std::string(argv[1]) != "--simulate"))
            throw std::runtime_error("Usage: luxurystore-apt --apply|--simulate REQUEST");
        bool apply = std::string(argv[1]) == "--apply";
        if (apply && geteuid() != 0) throw std::runtime_error("Administrator authentication is required.");
        if (strlen(argv[2]) > 120000) throw std::runtime_error("Request is too large.");
        gsize size = 0;
        guchar *data = g_base64_decode(argv[2], &size);
        if (data == nullptr || size == 0 || size > 90000)
            throw std::runtime_error("Request is too large or invalid.");
        GKeyFile *key = g_key_file_new();
        GError *error = nullptr;
        bool loaded = g_key_file_load_from_data(key, reinterpret_cast<char *>(data), size, G_KEY_FILE_NONE, &error);
        g_clear_error(&error);
        g_free(data);
        if (!loaded) throw std::runtime_error("Invalid request format.");
        gchar **names = g_key_file_get_keys(key, "Packages", &size, nullptr);
        if (!names || size == 0 || size > 1000) throw std::runtime_error("Empty or oversized request.");
        Requests requests;
        for (gsize i = 0; i < size; ++i) {
            gchar *action = g_key_file_get_string(key, "Packages", names[i], nullptr);
            requests[names[i]] = safe(action); g_free(action);
        }
        g_strfreev(names);
        gchar *hash = g_key_file_get_string(key, "Review", "SHA256", nullptr);
        std::string expected = safe(hash); g_free(hash); g_key_file_unref(key);
        // pkexec sanitizes the environment; also ignore user-supplied APT configuration.
#ifndef LUXURYSTORE_FIXTURE_HELPER
        unsetenv("APT_CONFIG");
#endif
        auto l = open_apt();
        Dialog dialog;
        l->setUserDialog(&dialog);
        /* Hold the package-manager archive lock across resolve/plan/commit
         * so a concurrent APT or dpkg transaction cannot change the state
         * the reviewed plan was computed from. commitChanges() takes the
         * same lock again; re-acquiring within this process succeeds. */
        FileFd commit_lock;
        if (apply) {
            commit_lock.Fd(GetLock(_config->FindDir("Dir::Cache::Archives") + "lock"));
            if (_error->PendingError()) {
                _error->DumpErrors();
                throw std::runtime_error("Another package manager is running. Try again when it has finished.");
            }
        }
        resolve(*l, requests);
        auto review = plan(*l);
        if (digest(review) != expected)
            throw std::runtime_error("Packages changed since review. Close this window and review the refreshed plan again.");
        if (review.empty()) throw std::runtime_error("No changes to apply.");
        for (auto p : l->getPackages()) {
            auto action = action_for(p);
            if (action.empty()) continue;
            if (action == "Remove" && (p->getFlags() & RPackage::FImportant))
                throw std::runtime_error("Removal of essential packages is not supported: " + safe(p->name()));
            if (action != "Remove" && !p->isTrusted())
                throw std::runtime_error("Unauthenticated package refused: " + safe(p->name()));
        }
        std::cout << review << std::flush;
        if (!apply) return 0;
        Progress progress;
        Downloads downloads;
        if (!l->commitChanges(&downloads, &progress)) throw std::runtime_error(apt_errors());
        std::cout << "\nChanges applied successfully.\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "LuxuryStore: " << e.what() << std::endl;
        return 1;
    }
}
