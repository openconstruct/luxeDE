/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#include "lock-guard.h"
#include <signal.h>
#include <poll.h>
#include <cerrno>
#include <sys/socket.h>
#include <unistd.h>

static Atom ready_selection(Display *display)
{
    return XInternAtom(display, "_LUXEDM_LOCK_READY", False);
}

bool LockAlreadyReady(Display *display)
{
    // Selection ownership disappears with its X connection. An instance-file
    // flock alone is insufficient: that process may still be starting.
    return XGetSelectionOwner(display, ready_selection(display)) != None;
}

bool PublishLockReady(Display *display, Window cover)
{
    XSetSelectionOwner(display, ready_selection(display), cover, CurrentTime);
    XSync(display, False);
    return XGetSelectionOwner(display, ready_selection(display)) == cover;
}

void SendLockReady(int fd)
{
    if (fd < 0) return;
    ssize_t result;
    do { result = send(fd, "R", 1, MSG_NOSIGNAL); } while (result < 0 && errno == EINTR);
    // A closed/timed-out requester must never trigger SIGPIPE and unlock.
    close(fd);
}

bool InstallLockSignalPolicy()
{
    struct sigaction action{};
    action.sa_handler = SIG_IGN;
    sigemptyset(&action.sa_mask);
    const int signals[] = {SIGTERM, SIGINT, SIGHUP, SIGQUIT};
    for (int signal_number : signals)
        if (sigaction(signal_number, &action, nullptr) != 0) return false;
    return true;
}

void LockCoverGuard::cover_root()
{
    XWindowAttributes bounds{};
    if (!XGetWindowAttributes(display, root, &bounds)) return;
    XWindowAttributes current{};
    if (!XGetWindowAttributes(display, cover, &current)) return;
    if (current.x != 0 || current.y != 0 || current.width != bounds.width || current.height != bounds.height) {
        // Theme pixmaps describe the old dimensions. Fill newly exposed space
        // with opaque black rather than tiling an obsolete background image.
        XSetWindowBackground(display, cover, BlackPixelOfScreen(bounds.screen));
        XMoveResizeWindow(display, cover, 0, 0, bounds.width, bounds.height);
        XClearWindow(display, cover);
    }
    XRaiseWindow(display, cover);
    XFlush(display);
}

bool LockCoverGuard::start(const char *display_name, Window root_window, Window cover_window)
{
    if (display) return false;
    display = XOpenDisplay(display_name);
    if (!display) return false;
    root = root_window;
    cover = cover_window;
    XSelectInput(display, root, StructureNotifyMask);
    cover_root();
    XSync(display, False);
    running.store(true);
    if (pthread_create(&thread, nullptr, run, this) != 0) {
        running.store(false);
        XCloseDisplay(display);
        display = nullptr;
        return false;
    }
    return true;
}

void *LockCoverGuard::run(void *data)
{
    auto *guard = static_cast<LockCoverGuard *>(data);
    pollfd descriptor{ConnectionNumber(guard->display), POLLIN, 0};
    while (guard->running.load()) {
        // Keep the previous one-second raise policy, but handle geometry
        // events immediately, independently of the PAM/UI event loop.
        int result = XPending(guard->display) ? 1 : poll(&descriptor, 1, 1000);
        if (result < 0 && errno == EINTR) continue;
        if (result < 0 || (descriptor.revents & (POLLERR | POLLHUP | POLLNVAL))) break;
        bool geometry_changed = false;
        for (int count = 0; count < 128 && XPending(guard->display); ++count) {
            XEvent event;
            XNextEvent(guard->display, &event);
            geometry_changed |= event.type == ConfigureNotify && event.xconfigure.window == guard->root;
        }
        if (geometry_changed) guard->cover_root();
        else {
            XRaiseWindow(guard->display, guard->cover);
            XFlush(guard->display);
        }
    }
    return nullptr;
}

void LockCoverGuard::stop()
{
    if (!display) return;
    running.store(false);
    pthread_join(thread, nullptr);
    XCloseDisplay(display);
    display = nullptr;
}
