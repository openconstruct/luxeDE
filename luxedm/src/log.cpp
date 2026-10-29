/* SLiM - Simple Login Manager
 *  Copyright (C) 1997, 1998 Per Liden
 *  Copyright (C) 2004-06 Simone Rota <sip@varlock.com>
 *  Copyright (C) 2004-06 Johannes Winkelmann <jw@tks6.net>
 *  Copyright (C) 2022 Rob Pearce <slim@flitspace.org.uk>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */

#include "const.h"
#include "log.h"
#include <iostream>
#include <cstring>
#include <cerrno>
#include <sys/stat.h>
#include <unistd.h>

LogUnit::LogUnit()
{
	logOut = &cerr;
}

bool LogUnit::openLog(const char * filename)
{
	if (filename == nullptr || *filename == '\0')
		return false;
	if (logFile.is_open()) {
		cerr << APPNAME
			<< ": opening a new Log file, while another is already open"
			<< endl;
		logFile.close();
		logOut = &cerr;
	}

	// cerr is the default
	if ( ( strcmp(filename, "/dev/stderr") == 0 )
	  || ( strcmp(filename, "stderr") == 0 ) )
	{
		logOut = &cerr;
		return true;
	}

	struct stat file_stat;
	if (lstat(filename, &file_stat) == 0) {
		if (!S_ISREG(file_stat.st_mode) || file_stat.st_uid != geteuid() ||
				file_stat.st_nlink != 1 || (file_stat.st_mode & 022) != 0)
			return false;
	} else if (errno != ENOENT) {
		return false;
	}

	logFile.open(filename, ios_base::out|ios_base::app);
	if ( logFile )
	{
		logOut = &logFile;
		return true;
	}
	return false;
}

void LogUnit::closeLog()
{
	if (logFile.is_open())
		logFile.close();
	logOut = &cerr;
}


/* Now instantiate a singleton for all the rest of the code to use */
LogUnit logStream;
