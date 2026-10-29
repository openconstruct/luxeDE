/* SLiM - Simple Login Manager
   Copyright (C) 2004-06 Simone Rota <sip@varlock.com>
   Copyright (C) 2004-06 Johannes Winkelmann <jw@tks6.net>
   Copyright (C) 2026 Jerry Howell

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.
*/

#ifndef _CFG_H_
#define _CFG_H_

#include <string>
#include <map>
#include <vector>

#define INPUT_MAXLENGTH_NAME    256
#define INPUT_MAXLENGTH_PASSWD  256

#define CFGFILE SYSCONFDIR"/luxedm.conf"
#define THEMESDIR PKGDATADIR"/themes"
#define THEMESFILE "/luxedm.theme"

struct SessionChoice {
	std::string name;
	std::string exec;
	std::string id;
	std::string desktop;
};

struct SessionEnvironment {
	std::string session_desktop;
	std::string current_desktop;
	std::string desktop_session;
};

SessionChoice legacySessionChoice(const std::string &command);
SessionEnvironment sessionEnvironment(const SessionChoice &session);

class Cfg
{

public:
	Cfg();
	~Cfg();

	bool readConf(std::string configfile);
	bool parseOption ( std::string line );
	const std::string& getError() const;
	std::string& getOption(std::string option);
	int getIntOption(std::string option);
	std::string getWelcomeMessage();

	static int absolutepos(const std::string &position, int max, int width);
	static int string2int(const char *string, bool *ok = 0);
	static void split(std::vector<std::string> &v, const std::string &str, 
					  char c, bool useEmpty=true);
	static std::string Trim(const std::string &s);

    SessionChoice nextSession();
	static bool validThemeName(const std::string &name);
	static std::string findValidRandomTheme(const std::string &set);

private:
	void fillSessionList();

private:
	std::map<std::string,std::string> options;
    std::vector<SessionChoice> sessions;
	int currentSession;
	std::string error;
};

#endif /* _CFG_H_ */
