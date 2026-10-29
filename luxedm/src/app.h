/* SLiM - Simple Login Manager
 *  Copyright (C) 1997, 1998 Per Liden
 *  Copyright (C) 2004-06 Simone Rota <sip@varlock.com>
 *  Copyright (C) 2004-06 Johannes Winkelmann <jw@tks6.net>
 *  Copyright (C) 2022-23 Rob Pearce <slim@flitspace.org.uk>
 *  Copyright (C) 2026 Jerry Howell
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */

#ifndef _APP_H_
#define _APP_H_

#include <X11/Xlib.h>
#include <string>
#include <vector>

#ifdef USE_PAM
#include "PAM.h"
#endif

// Forward declarations
class Panel;
class Cfg;
#ifdef USE_PAM
struct GreeterPamContext {
	Panel **panel;
	PAM::Authenticator *authenticator;
	bool identity_is_bound_hint;
};
#endif


class App
{
public:
	App(int argc, char **argv);
	~App();
	void Run();
	int GetServerPID();
	void RestartServer();
	void StopServer();

	/* Lock functions */
	void GetLock();
	void RemoveLock();

	bool isServerStarted();

private:
#ifdef USE_PAM
	friend int conv(int, const struct pam_message **, struct pam_response **, void *);
#endif
	void Login();
	void Reboot();
	void Halt();
	void Suspend();
	void Console();
	void Exit();
	void KillAllClients(Bool top);
	void ReadConfig();
	void OpenLog();
	void CloseLog();
	void CreateServerAuth();
	char *StrConcat(const char *str1, const char *str2);
	void UpdatePid();

	bool AuthenticateUser(bool focuspass);

	static void replaceVariables(std::string &input,
								 const std::string &var,
								 const std::string &value);

	/* Server functions */
	int StartServer();
	int ServerTimeout(int timeout, char *string);
	int WaitForServer();

	/* Private data */
	Window Root;	///< The root window of the default screen, on which to draw
	Display *Dpy;	///< Connection to the X-server
	int Scr;		///< Which "screen" to use (which will be the default one)
	Panel *LoginPanel;	///< The panel we display and interact through
	int ServerPID;	///< Process ID of the X-server we launched
	int lockFd;	///< Advisory lock held for this process's lifetime
	const char *DisplayName;	///< The display to request, usually ":0.0"
	bool serverStarted;	///< Whether we (think we) have started an X server

#ifdef USE_PAM
	GreeterPamContext pam_conversation_context;
	PAM::Authenticator pam;	///< Interface to the PAM authentication library
#endif

	/* Options */
	Cfg *cfg;		///< Collection of options from the configuration file

	void blankScreen();

	bool firstlogin;	///< Whether to exhibit first login behaviour, or repeat
	bool daemonmode;	///< Are we running as a daemon?
	bool force_nodaemon;	///< Are we forced NOT to be a daemon?
	/* For testing themes */
	char *testtheme;	///< Name of the theme to test, from command line
	bool testing;		///< Whether we're running in theme testing mode
	short tww, twh;		///< The user's requested test window size

	std::string themeName;	///< Name of the theme in use
	std::string mcookie;	///< Randomly generated X auth cookie
	std::vector<std::string> commandLine;	///< Arguments used for a clean restart
};

#endif /* _APP_H_ */
