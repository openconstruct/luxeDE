/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <X11/Xlib.h>
#include <atomic>
#include <pthread.h>

// Termination requests cannot authenticate a user or replace the live lock.
bool InstallLockSignalPolicy();
bool LockAlreadyReady(Display *display);
bool PublishLockReady(Display *display, Window cover);
void SendLockReady(int fd);

// A separate X connection follows root geometry even while PAM waits for input.
// XInitThreads must precede all Xlib use. The cover must outlive this guard.
class LockCoverGuard {
public:
    bool start(const char *display_name, Window root, Window cover);
    void stop();
    ~LockCoverGuard() { stop(); }
    LockCoverGuard() = default;
    LockCoverGuard(const LockCoverGuard &) = delete;
    LockCoverGuard &operator=(const LockCoverGuard &) = delete;
private:
    static void *run(void *data);
    void cover_root();
    Display *display = nullptr;
    Window root = None, cover = None;
    pthread_t thread{};
    std::atomic_bool running{false};
};
