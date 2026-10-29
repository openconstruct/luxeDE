/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#include "lock-guard.h"
#include <X11/Xutil.h>
#include <X11/extensions/Xrandr.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#include <poll.h>
#include <sys/socket.h>
#include <cstdlib>
#include <algorithm>
#include <iostream>
#include <stdexcept>

static void require(bool condition, const char *message)
{
    if (!condition) throw std::runtime_error(message);
}
struct Child {
    pid_t pid;
    ~Child() { if (pid > 0) { kill(pid, SIGKILL); waitpid(pid, nullptr, 0); } }
};
struct Windows { Window cover; };

int main() try
{
    int ready[2], control[2];
    require(pipe(ready) == 0 && pipe(control) == 0, "pipe failed");
    pid_t pid = fork();
    require(pid >= 0, "fork failed");
    if (pid == 0) {
        close(ready[0]); close(control[1]);
        require(InstallLockSignalPolicy(), "signal policy failed");
        require(XInitThreads() != 0, "XInitThreads failed");
        Display *display = XOpenDisplay(nullptr);
        require(display != nullptr, "display unavailable");
        Window root = DefaultRootWindow(display);
        const int screen = DefaultScreen(display);
        Windows windows{XCreateSimpleWindow(display, root, 0, 0,
            DisplayWidth(display, screen), DisplayHeight(display, screen), 0, 0, 0)};
        XSetWindowAttributes attrs{}; attrs.override_redirect = True;
        XChangeWindowAttributes(display, windows.cover, CWOverrideRedirect, &attrs);
        XMapRaised(display, windows.cover); XSync(display, False);
        require(XGrabKeyboard(display, windows.cover, False, GrabModeAsync, GrabModeAsync, CurrentTime) == GrabSuccess,
                "keyboard grab failed");
        require(XGrabPointer(display, windows.cover, False, 0, GrabModeAsync, GrabModeAsync, None, None, CurrentTime) == GrabSuccess,
                "pointer grab failed");
        LockCoverGuard guard;
        require(guard.start(nullptr, root, windows.cover), "cover guard failed");
        require(PublishLockReady(display, windows.cover), "readiness publication failed");
        require(write(ready[1], &windows, sizeof windows) == sizeof windows, "ready write failed");
        // Model the main/UI thread blocked in authentication. Only the guard
        // thread can update the cover while this read remains blocked.
        char command;
        require(read(control[0], &command, 1) == 1, "control read failed");
        guard.stop();
        XCloseDisplay(display);
        _exit(0);
    }
    Child child{pid};
    close(ready[1]); close(control[0]);
    pollfd descriptor{ready[0], POLLIN, 0};
    require(poll(&descriptor, 1, 5000) > 0, "locker startup timed out");
    Windows windows{};
    require(read(ready[0], &windows, sizeof windows) == sizeof windows, "locker startup failed");
    Display *probe = XOpenDisplay(nullptr);
    require(probe != nullptr, "probe display unavailable");
    int randr_event = 0, randr_error = 0;
    require(XRRQueryExtension(probe, &randr_event, &randr_error), "RandR unavailable in nested X server");
    const int screen = DefaultScreen(probe);
    const int old_width = DisplayWidth(probe, screen);
    const int old_height = DisplayHeight(probe, screen);
    int min_width = 0, min_height = 0, max_width = 0, max_height = 0;
    require(XRRGetScreenSizeRange(probe, DefaultRootWindow(probe), &min_width, &min_height,
                                  &max_width, &max_height), "cannot query nested root size range");
    require(LockAlreadyReady(probe), "running locker did not advertise readiness");
    int abandoned[2];
    require(socketpair(AF_UNIX, SOCK_STREAM, 0, abandoned) == 0, "socketpair failed");
    close(abandoned[0]);
    SendLockReady(abandoned[1]); // A timed-out requester must not cause SIGPIPE.
    const int signals[] = {SIGTERM, SIGINT, SIGHUP, SIGQUIT};
    for (int round = 0; round < 8; ++round) {
        for (int number : signals) require(kill(pid, number) == 0, "signal failed");
        usleep(10000);
        require(XGrabKeyboard(probe, DefaultRootWindow(probe), False, GrabModeAsync, GrabModeAsync, CurrentTime) == AlreadyGrabbed,
                "signal released keyboard");
        require(XGrabPointer(probe, DefaultRootWindow(probe), False, 0, GrabModeAsync, GrabModeAsync, None, None, CurrentTime) == AlreadyGrabbed,
                "signal released pointer");
    }
    const int target_widths[] = {
        std::min(max_width, old_width + 120),
        std::max(min_width, old_width - 120), old_width
    };
    for (int width : target_widths) {
        const int height = width == old_width ? old_height :
            std::clamp(old_height + (width > old_width ? 80 : -80), min_height, max_height);
        XRRSetScreenSize(probe, DefaultRootWindow(probe), width, height,
                         width * 25 / 96, height * 25 / 96);
        XSync(probe, False);
        bool covered = false;
        for (int attempt = 0; attempt < 100; ++attempt) {
            XWindowAttributes attrs{};
            require(XGetWindowAttributes(probe, windows.cover, &attrs) != 0, "cover disappeared");
            if (attrs.x == 0 && attrs.y == 0 && attrs.width == width && attrs.height == height) {
                covered = true; break;
            }
            usleep(10000);
        }
        require(covered, "cover did not follow nested X root geometry while authentication blocked");
    }
    require(write(control[1], "q", 1) == 1, "stop failed");
    close(control[1]); close(ready[0]);
    int status;
    pid_t waited = waitpid(pid, &status, 0);
    child.pid = -1;
    require(waited == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0, "locker test failed");
    require(!LockAlreadyReady(probe), "readiness outlived locker connection");
    XRRSetScreenSize(probe, DefaultRootWindow(probe), old_width, old_height,
                     old_width * 25 / 96, old_height * 25 / 96);
    XSync(probe, False);
    XCloseDisplay(probe);
    std::cout << "Signals retain both grabs; cover follows geometry while UI is blocked.\n";
}
catch (const std::exception &error) { std::cerr << error.what() << '\n'; return EXIT_FAILURE; }
