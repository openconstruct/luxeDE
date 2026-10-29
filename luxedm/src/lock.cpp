/* LuxeDM Lock, derived from slimlock
 * Copyright (c) 2010-2012 Joel Burget <joelburget@gmail.com>
 * Copyright (c) 2022-2023 Rob Pearce <slim@flitspace.org.uk>
 * Copyright (C) 2026 Jerry Howell
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <cstdio>
#include <cstring>
#include <iostream>
#include <sys/types.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/dpms.h>
#include <security/pam_appl.h>
#include <err.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <errno.h>
#include <sys/socket.h>
#include <pwd.h>

#include "cfg.h"
#include "util.h"
#include "panel.h"
#include "const.h"
#include "lock-guard.h"

#undef APPNAME
#define APPNAME "luxedm-lock"
#define LUXEDMLOCKCFG SYSCONFDIR"/luxedm-lock.conf"

using namespace std;

bool AuthenticateUser();
static void ClearPamResponses(struct pam_response *responses, int count)
{
	if (responses == nullptr)
		return;
	for (int i = 0; i < count; ++i) {
		if (responses[i].resp == nullptr)
			continue;
		volatile char *secret = responses[i].resp;
		for (size_t j = 0, length = strlen(responses[i].resp); j < length; ++j)
			secret[j] = 0;
		free(responses[i].resp);
		responses[i].resp = nullptr;
	}
	free(responses);
}

static int ConvCallback(int num_msgs, const struct pam_message **msg,
						struct pam_response **resp, void *appdata_ptr);

// In the absence of a class instance to contain these, just make them file
// public, as they're needed by multiple functions
static Display* Dpy;
static int Scr;
static Window Root;
static Window win;
static Cfg* cfg;
static Panel* LoginPanel;

static pam_handle_t *pam_handle;

static const struct pam_conv conv = {ConvCallback, NULL};

static CARD16 dpms_standby, dpms_suspend, dpms_off, dpms_level;
static BOOL dpms_state, using_dpms;


static int OpenInstanceLock()
{
	const uid_t uid = getuid();
	const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
	std::string path;
	struct stat directory_stat;
	if (runtime_dir != NULL && runtime_dir[0] == '/' &&
		stat(runtime_dir, &directory_stat) == 0 &&
		S_ISDIR(directory_stat.st_mode) && directory_stat.st_uid == uid &&
		(directory_stat.st_mode & 0077) == 0) {
		path = std::string(runtime_dir) + "/luxedm-lock.lock";
	} else {
		path = "/tmp/luxedm-lock-" + std::to_string(uid) + ".lock";
	}

	const int fd = open(path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
	if (fd < 0)
		return -1;

	struct stat file_stat;
	if (fstat(fd, &file_stat) != 0 || !S_ISREG(file_stat.st_mode) ||
		file_stat.st_uid != uid || file_stat.st_nlink != 1 ||
			(file_stat.st_mode & 022) != 0) {
		close(fd);
		errno = EPERM;
		return -1;
	}
	return fd;
}


[[noreturn]] static void die(const std::string &message)
{
	fputs(message.c_str(), stderr);
	exit(EXIT_FAILURE);
}

[[noreturn]] static void hold_lock_on_panel_failure(const char *message)
{
	fputs("luxedm-lock: panel setup failed; keeping the screen locked: ",
		stderr);
	fputs(message == nullptr ? "unknown failure" : message, stderr);
	fputc('\n', stderr);
	for (;;) {
		struct timespec delay = {1, 0};
		(void)nanosleep(&delay, nullptr);
	}
}

int main(int argc, char **argv)
{
	int ready_fd = -1;
	if((argc == 2) && !strcmp("-v", argv[1])) {
		printf(APPNAME " " VERSION ", © 2010-2012 Joel Burget, 2022-2023 Rob Pearce, 2026 Jerry Howell\n");
		return 0;
	}
	else if (argc == 3 && !strcmp(argv[1], "--ready-fd") && !strcmp(argv[2], "3")) {
		int type = 0;
		socklen_t size = sizeof type;
		if (getsockopt(3, SOL_SOCKET, SO_TYPE, &type, &size) != 0 || type != SOCK_STREAM ||
			fcntl(3, F_SETFD, FD_CLOEXEC) < 0)
			die(APPNAME ": readiness requires an inherited stream socket on fd 3\n");
		ready_fd = 3;
	}
	else if(argc != 1)
		die ( "usage: " APPNAME " [-v | --ready-fd 3]\n" );

	if (!InstallLockSignalPolicy())
		die(APPNAME ": cannot establish lock signal policy\n");

	// This is a per-user screen locker. Its lock file belongs in the calling
	// user's runtime directory; using /run/lock required the whole UI to be
	// setuid root.
	const int lock_file = OpenInstanceLock();
	if (lock_file < 0)
		die(std::string(APPNAME ": cannot safely create lock file: ")
				+ strerror(errno) + "\n");

	int rc = flock(lock_file, LOCK_EX | LOCK_NB);

	if(rc) {
		if(EWOULDBLOCK == errno || EAGAIN == errno) {
			Display *existing = XOpenDisplay(nullptr);
			bool ready = existing && LockAlreadyReady(existing);
			if (existing) XCloseDisplay(existing);
			if (ready && ready_fd >= 0) { SendLockReady(ready_fd); return 0; }
			die(APPNAME" already running\n");
		}
		die(std::string(APPNAME ": cannot lock instance file: ")
				+ strerror(errno) + "\n");
	}

	int cfg_passwd_timeout;

	/* Read configuration and theme */
	cfg = new Cfg;
	if (!cfg->readConf(CFGFILE) || !cfg->readConf(LUXEDMLOCKCFG))
		die(APPNAME ": cannot read lock configuration\n");
	string themeName = "";
	string themebase = "";
	string themefile = "";
	string themedir = "";

	themebase = string(THEMESDIR) + "/";
	themeName = cfg->getOption("current_theme");
	if (themeName.find(',') == std::string::npos &&
			!Cfg::validThemeName(themeName)) {
		cerr << APPNAME << ": invalid current theme name; using default" << endl;
		themeName = "default";
	}
	string::size_type pos;
	if ((pos = themeName.find(",")) != string::npos)
	{
		/* input is a set */
		themeName = cfg->findValidRandomTheme(themeName);
	}

	bool loaded = false;
	while (!loaded)
	{
		themedir =  themebase + themeName;
		themefile = themedir + THEMESFILE;
		if (!cfg->readConf(themefile))
		{
			if (themeName == "default")
			{
				cerr << APPNAME << ": Failed to open default theme file "
					 << themefile << endl;
				exit(ERR_EXIT);
			}
			else
			{
				cerr << APPNAME << ": Invalid theme in config: "
					 << themeName << endl;
				themeName = "default";
			}
		}
		else
		{
			loaded = true;
		}
	}

	const char *DisplayName = getenv("DISPLAY");
	if (!DisplayName)
		DisplayName = DISPLAY;
	if (strpbrk(DisplayName, "\r\n") != nullptr || *DisplayName == '\0')
		die(APPNAME ": invalid DISPLAY value\n");

	if (!XInitThreads())
		die(APPNAME ": Xlib thread initialization failed\n");
	Dpy = XOpenDisplay(DisplayName);
	if ( Dpy == 0 )
		die(APPNAME": cannot open display\n");

	/* Get screen and root window */
	Scr = DefaultScreen(Dpy);
	Root = RootWindow(Dpy, Scr);

	XSetWindowAttributes wa;
	wa.override_redirect = 1;
	wa.background_pixel = BlackPixel(Dpy, Scr);

	// Create a full screen window
	win = XCreateWindow(Dpy, Root,
			0, 0, DisplayWidth(Dpy, Scr), DisplayHeight(Dpy, Scr),
			0, DefaultDepth(Dpy, Scr), CopyFromParent,
			DefaultVisual(Dpy, Scr), CWOverrideRedirect | CWBackPixel,
			&wa);
	if (win == None)
		die(APPNAME ": cannot create lock window\n");
	XMapWindow(Dpy, win);

	XFlush(Dpy);
	bool keyboard_grabbed = false;
	for (int len = 1000; len; len--) {
		if(XGrabKeyboard(Dpy, win, False, GrabModeAsync, GrabModeAsync, CurrentTime)
			== GrabSuccess) {
			keyboard_grabbed = true;
			break;
		}
		usleep(1000);
	}
	if (!keyboard_grabbed)
		die(APPNAME ": cannot secure the keyboard grab\n");
	bool pointer_grabbed = false;
	for (int attempts = 1000; attempts; --attempts) {
		if (XGrabPointer(Dpy, win, False,
				ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
				GrabModeAsync, GrabModeAsync, None, None, CurrentTime) == GrabSuccess) {
			pointer_grabbed = true;
			break;
		}
		usleep(1000);
	}
	if (!pointer_grabbed)
		die(APPNAME ": cannot secure the pointer grab\n");
	XSelectInput(Dpy, win, ExposureMask | KeyPressMask);
	LockCoverGuard cover_guard;
	if (!cover_guard.start(DisplayName, Root, win))
		hold_lock_on_panel_failure("cannot monitor display geometry");

	/* Any panel-resource failure must not tear down the fullscreen lock. */
	Panel::SetFailureHandler(hold_lock_on_panel_failure);
	/* Create panel */
	LoginPanel = new Panel(Dpy, Scr, win, cfg, themedir, Panel::Mode_Lock);
	LoginPanel->HideCursor();

	struct passwd *caller = getpwuid(getuid());
	if (!caller)
		die(APPNAME ": cannot identify the calling user\n");
	LoginPanel->SetName(caller->pw_name);
	int ret = pam_start(APPNAME, caller->pw_name, &conv, &pam_handle);
	// Without PAM, the lock screen cannot authenticate the user.
	if (ret != PAM_SUCCESS) {
		const char *message = pam_handle == nullptr ? nullptr : pam_strerror(pam_handle, ret);
		die(std::string("PAM: ") +
			(message == nullptr ? "unable to start authentication" : message) + "\n");
	}

	// Set up DPMS without ever changing enablement (see below).
	int cfg_dpms_standby = Cfg::string2int(cfg->getOption("dpms_standby_timeout").c_str());
	int cfg_dpms_off = Cfg::string2int(cfg->getOption("dpms_off_timeout").c_str());
	using_dpms = DPMSCapable(Dpy) && cfg_dpms_standby > 0 && cfg_dpms_off >= 0;
	if (using_dpms) {
		// Respect the session: never force-enable DPMS. Enabling it
		// here added a power transition the session owner did not ask
		// for, observed tripping an AMDGPU crtc_dpms race that takes
		// down the X server while locked.
		if (DPMSInfo(Dpy, &dpms_level, &dpms_state) == 0 || !dpms_state)
			using_dpms = false;
	}
	if (using_dpms) {
		if (DPMSGetTimeouts(Dpy, &dpms_standby, &dpms_suspend, &dpms_off) == 0)
			using_dpms = false;
		else
			DPMSSetTimeouts(Dpy, static_cast<unsigned int>(cfg_dpms_standby),
						static_cast<unsigned int>(cfg_dpms_standby),
						static_cast<unsigned int>(cfg_dpms_off));

		// DPMS enablement is never changed (see above); timeouts are
		// restored on exit.
	}

	// Get password timeout
	cfg_passwd_timeout = Cfg::string2int(cfg->getOption("wrong_passwd_timeout").c_str());
	// Keep malformed/negative values from turning into a huge unsigned delay.
	if (cfg_passwd_timeout < 0)
		cfg_passwd_timeout = 0;
	if (cfg_passwd_timeout > 60)
		cfg_passwd_timeout = 60;

	/* Init Root */
	LoginPanel->setBackground();

	/* Show panel */
	LoginPanel->OpenPanel();
	if (!PublishLockReady(Dpy, win))
		hold_lock_on_panel_failure("cannot publish lock readiness");
	SendLockReady(ready_fd);

	// The live X connection, cover and grabs remain until authentication.
	// Ignored termination signals never restart or tear down the locker.
	bool authenticated = false;
	while (!authenticated)
	{
		LoginPanel->ResetPasswd();
		authenticated = AuthenticateUser();
		if (!authenticated)
			LoginPanel->WrongPassword(cfg_passwd_timeout);
	}

	// Stop the raise loop before destroying the window it uses.
	cover_guard.stop();

	LoginPanel->ClosePanel();
	delete LoginPanel;

	// Get DPMS stuff back to normal
	if (using_dpms) {
		DPMSSetTimeouts(Dpy, dpms_standby, dpms_suspend, dpms_off);
		// turn off DPMS if it was off when we entered
		if (!dpms_state)
			DPMSDisable(Dpy);
	}

	XCloseDisplay(Dpy);

	close(lock_file);	// will inherently release the flock

	pam_end(pam_handle, PAM_SUCCESS);

	/* Only successful authentication reaches this teardown, so exiting
	 * 0 here always means the user proved who they are (P1 #7: there is
	 * no longer a signal-induced unlocked exit path). */
	return 0;
}


static int ConvCallback(int num_msgs, const struct pam_message **msg,
						struct pam_response **resp, void *appdata_ptr)
{
	(void)appdata_ptr;
	if (num_msgs <= 0 || msg == NULL || resp == NULL || LoginPanel == NULL)
		return PAM_CONV_ERR;
	// PAM expects an array of responses, one for each message
	*resp = static_cast<pam_response *>(
		calloc(static_cast<size_t>(num_msgs), sizeof(struct pam_response)));
	if (*resp == NULL)
		return PAM_BUF_ERR;

	for (int i = 0; i < num_msgs; i++) {
		const char *answer = NULL;
		if (msg[i] == NULL) {
			ClearPamResponses(*resp, num_msgs);
			*resp = NULL;
			LoginPanel->ResetPasswd();
			return PAM_CONV_ERR;
		}
		if (msg[i]->msg_style == PAM_PROMPT_ECHO_OFF) {
			if (msg[i]->msg != NULL)
				LoginPanel->Message(msg[i]->msg);
			LoginPanel->EventHandler(Panel::Get_Passwd);
			answer = LoginPanel->GetPasswd().c_str();
		}
		else if (msg[i]->msg_style == PAM_PROMPT_ECHO_ON) {
			if (msg[i]->msg != NULL)
				LoginPanel->Message(msg[i]->msg);
			answer = LoginPanel->GetName().c_str();
		}
		else if (msg[i]->msg_style == PAM_ERROR_MSG ||
				msg[i]->msg_style == PAM_TEXT_INFO) {
			if (msg[i]->msg != NULL)
				LoginPanel->Message(msg[i]->msg);
			continue;
		}
		else {
			ClearPamResponses(*resp, num_msgs);
			*resp = NULL;
			LoginPanel->ResetPasswd();
			return PAM_CONV_ERR;
		}

		// return code is currently not used but should be set to zero
		(*resp)[i].resp_retcode = 0;
		if (((*resp)[i].resp = strdup(answer)) == NULL) {
			ClearPamResponses(*resp, num_msgs);
			*resp = NULL;
			LoginPanel->ResetPasswd();
			return PAM_BUF_ERR;
		}
		if (msg[i]->msg_style == PAM_PROMPT_ECHO_OFF)
			LoginPanel->ResetPasswd();
	}
	LoginPanel->ResetPasswd();

	return PAM_SUCCESS;
}

bool AuthenticateUser()
{
	return pam_handle != nullptr && pam_authenticate(pam_handle, 0) == PAM_SUCCESS;
}


