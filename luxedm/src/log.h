/* SLiM - Simple Login Manager
 *  Copyright (C) 1997, 1998 Per Liden
 *  Copyright (C) 2004-06 Simone Rota <sip@varlock.com>
 *  Copyright (C) 2004-06 Johannes Winkelmann <jw@tks6.net>
 *  Copyright (C) 2022-23 Rob Pearce <slim@flitspace.org.uk>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */

#ifndef _LOG_H_
#define _LOG_H_

#ifdef USE_PAM
#include "PAM.h"
#endif
#include <fstream>

using namespace std;

class LogUnit
{
	ofstream logFile;
	ostream * logOut;
public:
	bool openLog(const char * filename);
	void closeLog();

	LogUnit();

	~LogUnit() { closeLog(); }

	template<typename Type> LogUnit & operator<<(const Type & text)
	{
		*logOut << text; logOut->flush();
		return *this;
	}

	LogUnit & operator<<(ostream & (*fp)(ostream&))
	{
		*logOut << fp; logOut->flush();
		return *this;
	}

	LogUnit & operator<<(ios_base & (*fp)(ios_base&))
	{
		*logOut << fp; logOut->flush();
		return *this;
	}
};

extern LogUnit logStream;

#endif /* _LOG_H_ */
