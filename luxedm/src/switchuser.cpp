/*
 * SLiM - Simple Login Manager
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

#include <unistd.h>
#include <cstdio>
#include <iostream>
#include <grp.h>
#include <sys/stat.h>
#include "const.h"
#include "cfg.h"
#include "log.h"
#include "util.h"
#include "switchuser.h"

using namespace std;

SwitchUser::SwitchUser(struct passwd *pw, Cfg *c, const string& display,
					   char** _env)
	: cfg(c), Pw(pw), Display(display), env(_env)
{
}

SwitchUser::~SwitchUser()
{
	/* Never called */
}

void SwitchUser::Login(const char* cmd, const char* mcookie)
{
	SetUserId();
	if (!SetClientAuth(mcookie) || !Execute(cmd))
		_exit(ERR_EXIT);
}

void SwitchUser::SetUserId()
{
	if( (Pw == nullptr) || Pw->pw_name == nullptr || *Pw->pw_name == '\0' ||
			(initgroups(Pw->pw_name, Pw->pw_gid) != 0) ||
			(setgid(Pw->pw_gid) != 0) ||
			(setuid(Pw->pw_uid) != 0) ) {
		logStream << APPNAME << ": could not switch user id" << endl;
		exit(ERR_EXIT);
	}
}

bool SwitchUser::Execute(const char* cmd)
{
	const char *shell = (Pw->pw_shell != nullptr && *Pw->pw_shell != '\0')
		? Pw->pw_shell : "/bin/sh";
	if (Pw == nullptr || Pw->pw_dir == nullptr || *Pw->pw_dir == '\0') {
		logStream << APPNAME << ": user has no valid home directory" << endl;
		return false;
	}
	const char *home = Pw->pw_dir;
	if (cmd == nullptr || *cmd == '\0' || env == nullptr || cfg == nullptr) {
		logStream << APPNAME << ": invalid login execution parameters" << endl;
		return false;
	}
	if ( chdir(home) < 0 ) {
		logStream << APPNAME << ": unable to chdir() to user's home: "
			<< strerror(errno) << endl;
		return false;
	}
	logStream.closeLog();
	execle(shell, shell, "-c", cmd, NULL, env);
	/// @todo this copy-paste of App:CloseLog() should be cleaned up
	if ( !logStream.openLog( cfg->getOption("logfile").c_str() ) ) {
		cerr <<  APPNAME << ": Could not access log file: " << cfg->getOption("logfile") << endl;
		return false;
	}
	logStream << APPNAME << ": could not execute login command: " << strerror(errno) << endl;
	return false;
}

bool SwitchUser::SetClientAuth(const char* mcookie)
{
	if (Pw == nullptr || Pw->pw_dir == nullptr || *Pw->pw_dir == '\0' ||
			mcookie == nullptr || *mcookie == '\0')
		return false;
	string home = string(Pw->pw_dir);
	string authfile = home + "/.Xauthority";
	if (!Util::add_mcookie(mcookie, Display.c_str(), cfg->getOption("xauth_path"), authfile)) {
		logStream << APPNAME << ": could not update the user's Xauthority file" << endl;
		return false;
	}
	struct stat auth_stat;
	if (lstat(authfile.c_str(), &auth_stat) != 0 || !S_ISREG(auth_stat.st_mode) ||
			auth_stat.st_uid != geteuid() || auth_stat.st_nlink != 1 ||
			chmod(authfile.c_str(), 0600) != 0) {
		logStream << APPNAME << ": could not secure the user's Xauthority file" << endl;
		return false;
	}
	return true;
}
