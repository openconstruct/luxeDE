/* SLiM - Simple Login Manager
 *  Copyright (C) 2004-06 Simone Rota <sip@varlock.com>
 *  Copyright (C) 2004-06 Johannes Winkelmann <jw@tks6.net>
 *  Copyright (C) 2012-13 Nobuhiro Iwamatsu <iwamatsu@nigauri.org>
 *  Copyright (C) 2022-23 Rob Pearce <slim@flitspace.org.uk>
 *  Copyright (C) 2026 Jerry Howell
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */

#include <fstream>
#include <string>
#include <iostream>
#include <algorithm>
#include <cerrno>
#include <climits>
#include <limits.h>
#include <cstdint>
#include <cctype>
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>

#include "util.h"	// for Util::random
#include "log.h"	// for logStream

#include "cfg.h"

using namespace std;

typedef pair<string,string> option;

SessionChoice legacySessionChoice(const string &command)
{
	string executable = command.substr(0, command.find_first_of(" \t"));
	const size_t slash = executable.find_last_of('/');
	if (slash != string::npos)
		executable.erase(0, slash + 1);
	string desktop = command;
	if (executable == "luxe" || executable == "start-luxe") desktop = "X-Luxe";
	else if (executable == "openbox" || executable == "openbox-session") desktop = "Openbox";
	else if (executable == "gnome-session") desktop = "GNOME";
	else if (executable == "startxfce4" || executable == "xfce4-session") desktop = "XFCE";
	else if (executable == "startlxde" || executable == "lxsession") desktop = "LXDE";
	return {command, command, executable.empty() ? command : executable, desktop};
}

SessionEnvironment sessionEnvironment(const SessionChoice &session)
{
	string desktop = session.desktop;
	if (desktop.empty()) desktop = "X-Luxe";
	const size_t separator = desktop.find(':');
	string primary = desktop.substr(0, separator);
	if (primary.empty()) primary = "X-Luxe";
	string id = session.id;
	if (id.empty()) id = "luxe";
	return {primary, desktop, id};
}

/**
 * Constructor: creates the Cfg object and populates the available options
 * with default values.
 */
Cfg::Cfg()
	: currentSession(-1)
{
	/* Configuration options */
	options.insert(option("default_path","/bin:/usr/bin:/usr/local/bin"));
	options.insert(option("default_xserver","/usr/bin/X"));
	options.insert(option("xserver_arguments",""));
	options.insert(option("display_setup_cmd","/usr/libexec/luxedm/luxedm-display-setup"));
	options.insert(option("numlock",""));
	options.insert(option("daemon",""));
	options.insert(option("xauth_path","/usr/bin/xauth"));
	options.insert(option("login_cmd","exec /bin/bash -login ~/.xinitrc %session"));
	options.insert(option("halt_cmd","/sbin/shutdown -h now"));
	options.insert(option("reboot_cmd","/sbin/shutdown -r now"));
	options.insert(option("suspend_cmd",""));
	options.insert(option("sessionstart_cmd",""));
	options.insert(option("sessionstop_cmd",""));
	/* console_cmd defaults to empty (P1 #6): App::Console() is a silent
	 * no-op unless the admin explicitly configures a console command,
	 * so even an authenticated console spawns nothing by default. */
	options.insert(option("console_cmd",""));
	options.insert(option("default_user",""));
	options.insert(option("focus_password","no"));
	options.insert(option("auto_login","no"));
	options.insert(option("current_theme","luxe"));
	options.insert(option("lockfile","/run/luxedm.pid"));
	options.insert(option("logfile","/var/log/luxedm.log"));
	options.insert(option("authfile","/run/luxedm.auth"));
	options.insert(option("shutdown_msg","The system is halting..."));
	options.insert(option("reboot_msg","The system is rebooting..."));
	options.insert(option("sessions", ""));
	options.insert(option("sessiondir",""));
	options.insert(option("default_session","Luxe"));
	options.insert(option("hidecursor","false"));

	/* Theme stuff */
	options.insert(option("background_style","stretch"));
	options.insert(option("background_color","#CCCCCC"));

	options.insert(option("input_panel_x","50%"));	/* Panel position on screen */
	options.insert(option("input_panel_y","40%"));
	options.insert(option("input_font","Verdana:size=11"));
	options.insert(option("input_color", "#000000"));
	options.insert(option("input_shadow_xoffset", "0"));
	options.insert(option("input_shadow_yoffset", "0"));
	options.insert(option("input_shadow_color","#FFFFFF"));
	options.insert(option("input_name_x","200"));	/* relative to panel */
	options.insert(option("input_name_y","154"));
	options.insert(option("input_pass_x","-1")); /* default is single inputbox */
	options.insert(option("input_pass_y","-1"));

	options.insert(option("welcome_msg","Welcome to %host"));
	options.insert(option("welcome_font","Verdana:size=14"));
	options.insert(option("welcome_color","#FFFFFF"));
	options.insert(option("welcome_x","-1"));
	options.insert(option("welcome_y","-1"));
	options.insert(option("welcome_shadow_xoffset", "0"));
	options.insert(option("welcome_shadow_yoffset", "0"));
	options.insert(option("welcome_shadow_color","#FFFFFF"));

	options.insert(option("username_msg","Please enter your username"));
	options.insert(option("username_font","Verdana:size=12"));
	options.insert(option("username_color","#FFFFFF"));
	options.insert(option("username_x","-1"));
	options.insert(option("username_y","-1"));
	options.insert(option("username_shadow_xoffset", "0"));
	options.insert(option("username_shadow_yoffset", "0"));
	options.insert(option("username_shadow_color","#FFFFFF"));

	options.insert(option("password_msg","Please enter your password"));
	options.insert(option("password_x","-1"));
	options.insert(option("password_y","-1"));

	options.insert(option("msg_font","Verdana:size=16:bold"));
	options.insert(option("msg_color","#FFFFFF"));
	options.insert(option("msg_x","40"));
	options.insert(option("msg_y","40"));
	options.insert(option("msg_shadow_xoffset", "0"));
	options.insert(option("msg_shadow_yoffset", "0"));
	options.insert(option("msg_shadow_color","#FFFFFF"));

	options.insert(option("session_msg","Session:"));
	options.insert(option("session_font","Verdana:size=16:bold"));
	options.insert(option("session_color","#FFFFFF"));
	options.insert(option("session_x","50%"));
	options.insert(option("session_y","90%"));
	options.insert(option("session_shadow_xoffset", "0"));
	options.insert(option("session_shadow_yoffset", "0"));
	options.insert(option("session_shadow_color","#FFFFFF"));

	// What to do if the authorisation fails
	options.insert(option("keep_user_on_fail", "0"));
	options.insert(option("wrong_passwd_timeout", "2"));
	options.insert(option("passwd_feedback_msg", "Authentication failed"));
	options.insert(option("passwd_feedback_capslock", "Authentication failed (CapsLock is on)"));
	options.insert(option("passwd_feedback_x", "-1"));	/* no feedback by default */
	options.insert(option("passwd_feedback_y", "-1"));
	options.insert(option("bell", "0"));

	// LuxeDM Lock-specific options
	options.insert(option("dpms_standby_timeout", "60"));
	options.insert(option("dpms_off_timeout", "600"));
	options.insert(option("show_username", "1"));

	error = "";
}

Cfg::~Cfg()
{
	options.clear();
}

/**
 * Parses known options from the given configfile / themefile
 * 
 * @param	configfile	Path to configuration or theme
 * @return				true on sucess, false if file not found
 */
bool Cfg::readConf(string configfile)
{
	size_t pos = 0;
	string line, next, fn(configfile);
	const int config_fd = open(fn.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
	if (config_fd < 0) {
		error = "Cannot read configuration file: " + configfile;
		return false;
	}
	struct stat config_stat;
	if (fstat(config_fd, &config_stat) != 0 || !S_ISREG(config_stat.st_mode)) {
		close(config_fd);
		error = "Configuration path is not a regular file: " + configfile;
		return false;
	}
	FILE *cfgfile = fdopen(config_fd, "r");
	if (cfgfile == nullptr) {
		close(config_fd);
		error = "Cannot read configuration file: " + configfile;
		return false;
	}
	constexpr size_t max_config_line = 1024 * 1024;
	while (true)
	{
		line.clear();
		bool got_line = false;
		bool too_long = false;
		int character;
		while ((character = fgetc(cfgfile)) != EOF) {
			got_line = true;
			if (character == '\0') {
				error = "NUL byte in configuration file: " + configfile;
				fclose(cfgfile);
				return false;
			}
			if (line.size() >= max_config_line + 1) {
				too_long = true;
				break;
			}
			line.push_back(static_cast<char>(character));
			if (character == '\n')
				break;
		}
		if (!got_line)
			break;
		if (too_long || ferror(cfgfile)) {
			error = "Configuration line is too long: " + configfile;
			fclose(cfgfile);
			return false;
		}
		if (!line.empty() && line.back() == '\n')
			line.pop_back();
		if (!line.empty() && line.back() == '\r')
			line.pop_back();
		// New parser to fix ticket #4
		pos = line.length();
		if ( ( pos > 0 ) && ( line[pos-1] == '\\' ) )
		{
			line.replace ( pos-1, 1, " " );
			if (next.size() + line.size() > max_config_line) {
				error = "Configuration continuation is too long: " + configfile;
				fclose(cfgfile);
				return false;
			}
			next = next + line;
			continue;
		}

		if ( !next.empty() )
		{
			line = next + line;
			next = "";
		}
		line = Trim(line);

		// Ignore blank lines and comment lines
		if ( line.empty() || line[0] == '#' )
			continue;

		// Now parse and assign
		if ( !parseOption ( line ) )
			cerr << error << '\n';	// not a fatal error
	}
	if (!next.empty()) {
		error = "Configuration ends with an unfinished continuation: " + configfile;
		fclose(cfgfile);
		return false;
	}
	fclose(cfgfile);

	fillSessionList();

	return true;
}

/**
 *  Sets an option value from a line. Returns true on success.
 */
bool Cfg::parseOption ( string line )
{
	if (line.find('\0') != string::npos) {
		error = "NUL byte in configuration option";
		return false;
	}
	size_t pos = 0;
	const string delims = " \t";
	string name, value;

	// First split the line into a name/value pair
	pos = line.find_first_of ( delims );
	if ( pos == string::npos )
	{
		name = Trim(line);
		value.clear();
	}
	else
	{
		name = line.substr ( 0, pos );
		value = Trim ( line.substr ( pos ) );
	}
	// In case of in-line comments:  value = 'strings # comment'.  
	// Find '#' past the 1st character (colour), then cut and trim again.
	pos = value.find_first_of ( '#', 1 );      
	if ( pos > 0 && pos < string::npos )
	{ 
		value = Trim ( value.substr( 0, pos ) ); 
	}
	// Look to see if it's a known option before validating optional empties.
	if ( options.find ( name ) == options.end() )
	{
		error = "Unknown option name: " + name;
		return false;
	}
	if ( value.empty() && name != "default_user" &&
			name != "xserver_arguments" && name != "display_setup_cmd" &&
			name != "suspend_cmd" && name != "console_cmd" &&
			name != "sessionstart_cmd" && name != "sessionstop_cmd" )
	{
		error = "Badly formed line: " + line;
		return false;
	}
	// finally assign it
	options[name] = value;
	return true;
}


const string& Cfg::getError() const
{
	return error;
}

string& Cfg::getOption(string option)
{
	return options[option];
}

/* return a trimmed string */
string Cfg::Trim( const string& s )
{
	if ( s.empty() ) {
		return s;
	}
	size_t pos = 0;
	string line = s;
	while ( pos < line.size() && isspace( static_cast<unsigned char>(line[pos]) ) ) {
		++pos;
	}
	line.erase( 0, pos );
	if (line.empty())
		return line;
	pos = line.size();
	while ( pos > 0 && isspace( static_cast<unsigned char>(line[pos - 1]) ) )
		--pos;
	line.erase(pos);
	return line;
}

/* Return the welcome message with replaced vars */
string Cfg::getWelcomeMessage()
{
	string s = getOption("welcome_msg");
	size_t n = s.find("%host");
	if (n != string::npos) {
		string tmp = s.substr(0, n);
		char host[256]{};
		if (gethostname(host, sizeof(host) - 1) != 0)
			host[0] = '\0';
		tmp += host;
		tmp += s.substr(n + 5);
		s = tmp;
	}
	n = s.find("%domain");
	if (n != string::npos) {
		string tmp = s.substr(0, n);
		char domain[256]{};
		if (getdomainname(domain, sizeof(domain) - 1) != 0)
			tmp += "<unknown domain>";
		else
			tmp += domain;
		tmp += s.substr(n + 7);
		s = tmp;
	}
	return s;
}

int Cfg::string2int(const char* string, bool* ok)
{
	char* end = nullptr;
	long parsed;

	if (ok != nullptr)
		*ok = false;
	if (string == nullptr || *string == '\0')
		return 0;

	errno = 0;
	parsed = strtol(string, &end, 10);
	if (errno == ERANGE || end == string || *end != '\0' ||
			parsed < INT_MIN || parsed > INT_MAX)
		return 0;
	if (ok != nullptr)
		*ok = true;
	return static_cast<int>(parsed);
}

int Cfg::getIntOption(std::string option)
{
	return string2int(options[option].c_str());
}


/**
 * Get absolute position
 * 
 * Converts a config position string into absolute coordinates. If the string 
 * is a plain number, this is just an atoi but if there is a percentage sign 
 * then the value is converted using the size of the canvas and the object.
 * 
 * @param	position	Configured position as a string
 * @param	max			Size of canvas in the relevant axis
 * @param	width		Size of the object being placed
 * @return				Absolute coordinate to achieve placement
 */
int Cfg::absolutepos(const string& position, int max, int width)
{
	const size_t percent = position.find('%');
	if (percent != string::npos && percent > 0) {
		bool ok = false;
		const int percentage = string2int(position.substr(0, percent).c_str(), &ok);
		if (!ok || percentage < 0 || percentage > 100 || max < 0 || width < 0)
			return 0;
		const int64_t scaled = static_cast<int64_t>(max) * percentage;
		const int64_t result = scaled / 100 - (width / 2);
		if (result < 0)
			return 0;
		return result > INT_MAX ? INT_MAX : static_cast<int>(result);
	}
	bool ok = false;
	const int result = string2int(position.c_str(), &ok);
	return ok ? result : 0;
}

/* split a comma separated string into a vector of strings */
void Cfg::split(vector<string>& v, const string& str, char c, bool useEmpty)
{
	v.clear();
	string::const_iterator s = str.begin();
	string tmp;
	while (true) {
		string::const_iterator begin = s;
		while (s != str.end() && *s != c)
		{
			++s;
		}
		tmp = string(begin, s);
		if (useEmpty || tmp.size() > 0)
			v.push_back(tmp);
		if (s == str.end()) {
			break;
		}
		if (++s == str.end()) {
			if (useEmpty)
				v.push_back("");
			break;
		}
	}
}

void Cfg::fillSessionList()
{
	string strSessionList = getOption("sessions");
	string strSessionDir  = getOption("sessiondir");

	sessions.clear();

	if ( !strSessionDir.empty() )
	{
		DIR *pDir = opendir(strSessionDir.c_str());

		if (pDir != NULL) {
			struct dirent *pDirent = NULL;

			while ((pDirent = readdir(pDir)) != NULL) {
				string strFile(strSessionDir);
				strFile += "/";
				strFile += pDirent->d_name;

				struct stat oFileStat;

				if (lstat(strFile.c_str(), &oFileStat) == 0) {
					if (S_ISREG(oFileStat.st_mode) && oFileStat.st_size <= 1024 * 1024 &&
							oFileStat.st_nlink == 1){
						ifstream desktop_file(strFile.c_str());
						if (desktop_file){
							string line, session_name = "", session_exec = "", desktop_names = "";
							bool in_desktop_entry = false;
							bool hidden = false;
							while (getline( desktop_file, line )) {
								if (!line.empty() && line.front() == '[') {
									in_desktop_entry = line == "[Desktop Entry]";
									continue;
								}
								if (!in_desktop_entry)
									continue;
								if (line.compare(0, 5, "Name=") == 0) {
									session_name = line.substr(5);

								}
								else if (line.compare(0, 5, "Exec=") == 0) {
									session_exec = line.substr(5);
								}
								else if (line.compare(0, 13, "DesktopNames=") == 0) {
									desktop_names = line.substr(13);
								}
								else if (line.compare(0, 7, "Hidden=") == 0) {
									hidden = line.substr(7) == "true";
								}
								else if (line.compare(0, 10, "NoDisplay=") == 0) {
									hidden = hidden || line.substr(10) == "true";
								}
							}
							desktop_file.close();
							string session_id = pDirent->d_name;
							if (session_id.size() > 8 && session_id.compare(session_id.size() - 8, 8, ".desktop") == 0)
								session_id.erase(session_id.size() - 8);
							if (desktop_names.empty())
								desktop_names = session_id;
							else {
								replace(desktop_names.begin(), desktop_names.end(), ';', ':');
								while (!desktop_names.empty() && desktop_names.back() == ':')
									desktop_names.pop_back();
								if (desktop_names.empty())
									desktop_names = session_id;
							}
							const auto invalid_identity = [](const string &value) {
								return any_of(value.begin(), value.end(),
									[](unsigned char ch) { return ch < 0x20 || ch == 0x7f; });
							};
							if (invalid_identity(session_id) || invalid_identity(desktop_names))
								continue;
							if (!hidden && !session_name.empty() && !session_exec.empty()) {
								sessions.push_back({session_name, session_exec, session_id, desktop_names});
							} else if (!hidden && access(strFile.c_str(), X_OK) == 0) {
								sessions.push_back({string(pDirent->d_name), strFile, session_id, desktop_names});
							}
						}
					}
				}
			}
			closedir(pDir);
		}
	}

	if (sessions.empty())
	{
		if (strSessionList.empty())
		{
			sessions.push_back({"", "", "luxe", "X-Luxe"});
		}
		else
		{
			// iterate through the split of the session list
			vector<string> sessit;
			split(sessit,strSessionList,',',false);
			for (vector<string>::iterator it = sessit.begin(); it != sessit.end(); ++it)
			{
					sessions.push_back(legacySessionChoice(*it));
			}
		}
	}

	/* Directory iteration order is filesystem-dependent. Keep the chooser
	 * stable, then move the configured session to the front. */
	stable_sort(sessions.begin(), sessions.end(),
		[](const SessionChoice& left, const SessionChoice& right) {
			return left.name < right.name;
		});
	const string preferred = getOption("default_session");
	if (!preferred.empty())
	{
		vector<SessionChoice>::iterator selected = find_if(
			sessions.begin(), sessions.end(),
			[&preferred](const SessionChoice& session) {
				return session.name == preferred || session.exec == preferred || session.id == preferred;
			});
		if (selected != sessions.end())
			rotate(sessions.begin(), selected, selected + 1);
	}
}


SessionChoice Cfg::nextSession()
{
	if (sessions.empty())
		return {"", "", "luxe", "X-Luxe"};
	currentSession = (currentSession + 1) % sessions.size();
	return sessions[currentSession];
}


bool Cfg::validThemeName(const string& name)
{
	if (name.empty() || name.size() > NAME_MAX || name == "." || name == ".." )
		return false;
	for (const unsigned char ch : name) {
		if (ch == '/' || ch == '\\' || std::iscntrl(ch))
			return false;
	}
	return true;
}

/*
 * Choose a theme at random from the list in the config file. IF the theme
 * file cannot be found then issue a warning and try again.
 */
string Cfg::findValidRandomTheme ( const string& set )
{
	/* extract random theme from theme set; return empty string on error */
	string name = set;
	if (name.empty())
		return "";
	struct stat buf;

	if (name[name.length()-1] == ',')
	{
		name.erase(name.length() - 1);
	}
	name = Cfg::Trim(name);
	if (name.empty())
		return "";

	Util::srandom(Util::makeseed());

	vector<string> themes;
	string themefile;
	Cfg::split(themes, name, ',');
	if (themes.empty())
		return "";
	do {
		int sel = Util::random() % themes.size();

		name = Cfg::Trim(themes[sel]);
		if (!Cfg::validThemeName(name)) {
			themes.erase(themes.begin() + sel);
			logStream << APPNAME << ": Invalid theme name in config" << endl;
			name = "";
			continue;
		}
		themefile = string(THEMESDIR) +"/" + name + THEMESFILE;
		if (stat(themefile.c_str(), &buf) != 0)
		{
			themes.erase(themes.begin() + sel);
			logStream << APPNAME << ": Invalid theme in config: "
				 << name << endl;
			name = "";
		}
	} while (name == "" && themes.size());
	return name;
}
