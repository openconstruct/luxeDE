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

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/file.h>
#include <signal.h>
#include <unistd.h>
#include <pwd.h>			// for getpwnam etc.
#include <fcntl.h>
#include <stdint.h>
#include <time.h>
#include <cerrno>
#include <climits>
#include <cstring>
#include <cstdio>
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <algorithm>
#include <X11/Xmu/WinUtil.h>	// for XmuClientWindow

#include "const.h"
#include "log.h"
#include "numlock.h"
#include "switchuser.h"
#include "util.h"
#include "panel.h"
#include "cfg.h"
#include "app.h"
#include "session-process.h"

#ifdef HAVE_SHADOW
#include <shadow.h>
#endif


// We generate a magic cookie of 32 hex digits for Xauth
#define  MCOOKIESIZE	(32)		/* Must be divisible by 4 */


using namespace std;

namespace {

void stopSessionGroup(pid_t pgid)
{
	const auto result = stopSessionProcessGroup(pgid, std::chrono::seconds(2));
	if (result == SessionGroupStop::Forced)
		logStream << APPNAME << ": force-stopped surviving session processes" << endl;
	else if (result == SessionGroupStop::Failed)
		logStream << APPNAME << ": could not fully stop session process group " << pgid << endl;
}

void markDisplayCloseOnExec(Display *display)
{
	const int fd = ConnectionNumber(display);
	const int flags = fcntl(fd, F_GETFD);
	if (flags < 0 || fcntl(fd, F_SETFD, flags | FD_CLOEXEC) < 0)
		logStream << APPNAME << ": could not protect the display-manager X connection: "
				<< strerror(errno) << endl;
}

bool runDisplaySetup(const string& configuredCommand, const char *display,
					 const string& authfile)
{
	vector<string> commandStorage = Util::splitCommand(configuredCommand);
	if (commandStorage.empty())
		return configuredCommand.find_first_not_of(" \t\r\n") == string::npos;

	vector<char *> command;
	command.reserve(commandStorage.size() + 1);
	for (string& stored : commandStorage)
		command.push_back(stored.data());
	command.push_back(NULL);

	const pid_t child = fork();
	if (child == 0) {
		setenv("DISPLAY", display, 1);
		setenv("XAUTHORITY", authfile.c_str(), 1);
		execvp(command[0], command.data());
		_exit(127);
	}
	if (child < 0) {
		logStream << APPNAME << ": could not start display setup: "
				  << strerror(errno) << endl;
		return false;
	}

	int status = 0;
	pid_t waited;
	do {
		waited = waitpid(child, &status, 0);
	} while (waited < 0 && errno == EINTR);
	if (waited != child || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		logStream << APPNAME << ": display setup failed; continuing with active outputs"
				  << endl;
		return false;
	}
	return true;
}

/* Run a root-side helper without a shell. The configured command is split
 * into argv, environment references ($DISPLAY) are expanded without
 * re-splitting, %user is substituted literally, then fork+execvp runs it. */
bool runRootCommand(const string& configuredCommand, const char *userName)
{
	vector<string> args = Util::splitCommand(configuredCommand);
	if (args.empty())
		return configuredCommand.find_first_not_of(" \t\r\n") == string::npos;
	Util::expandEnvVars(args);
	if (userName != NULL)
		Util::replacePlaceholder(args, USER_VAR, userName);
	return Util::spawnAndWait(args);
}

bool secureRootParent(const string& path)
{
	if (path.empty())
		return false;
	const size_t slash = path.find_last_of('/');
	const string parent = slash == string::npos ? "." :
		(slash == 0 ? "/" : path.substr(0, slash));
	struct stat statbuf;
	return stat(parent.c_str(), &statbuf) == 0 && S_ISDIR(statbuf.st_mode) &&
		statbuf.st_uid == geteuid() && (statbuf.st_mode & 022) == 0;
}

} // namespace

#ifdef USE_PAM
#include <string>

static void clearGreeterPamResponses(struct pam_response *responses, int count)
{
	if (!responses) return;
	for (int i = 0; i < count; ++i) {
		if (!responses[i].resp) continue;
		volatile unsigned char *secret = reinterpret_cast<volatile unsigned char *>(responses[i].resp);
		for (size_t n = 0, length = std::strlen(responses[i].resp); n < length; ++n)
			secret[n] = 0;
		std::free(responses[i].resp);
		responses[i].resp = nullptr;
	}
	std::free(responses);
}

int conv(int num_msg, const struct pam_message **msg,
		 struct pam_response **resp, void *appdata_ptr)
{
	if (resp) *resp = nullptr;
	if (num_msg <= 0 || msg == NULL || resp == NULL || appdata_ptr == NULL)
		return PAM_CONV_ERR;
	for (int i = 0; i < num_msg; ++i) {
		if (msg[i] == NULL) {
			*resp = nullptr;
			return PAM_CONV_ERR;
		}
	}
	*resp = static_cast<struct pam_response *>(
		calloc(static_cast<size_t>(num_msg), sizeof(struct pam_response)));
	if (*resp == NULL)
		return PAM_BUF_ERR;
	GreeterPamContext *context = static_cast<GreeterPamContext *>(appdata_ptr);
	Panel* panel = context->panel ? *context->panel : nullptr;
	if (panel == NULL) {
		clearGreeterPamResponses(*resp, num_msg);
		*resp = nullptr;
		return PAM_CONV_ERR;
	}
	bool identity_is_bound = context->identity_is_bound_hint;
	if (context->authenticator) {
		try {
			const char *user = static_cast<const char *>(
				context->authenticator->get_item(PAM::Authenticator::User));
			identity_is_bound = user && *user;
		} catch (...) {
			clearGreeterPamResponses(*resp, num_msg);
			*resp = nullptr;
			return PAM_CONV_ERR;
		}
	}
	int result = PAM_SUCCESS;
	int i;

	for (i = 0; i < num_msg; i++)
	{
		(*resp)[i].resp = 0;
		(*resp)[i].resp_retcode = 0;
		switch (msg[i]->msg_style)
		{
			case PAM_PROMPT_ECHO_ON:
				/* Echo-on replies may be a username or a second factor. Keep
				 * a bound PAM account identity separate from that reply. */
				if (msg[i]->msg != nullptr)
					panel->Message(msg[i]->msg);
				{
					const std::string identity = panel->GetName();
				panel->EventHandler(Panel::Get_Name);
					const std::string answer = panel->GetName();
					(*resp)[i].resp = strdup(answer.c_str());
					if (identity_is_bound) panel->SetName(identity);
				}
				break;

			case PAM_PROMPT_ECHO_OFF:
				/* We assume PAM is asking for the password. Every action,
				 * including Console and Exit, must authenticate here like
				 * any login: there are no unauthenticated magic usernames.
				 * (P1 #6: the old code aborted the conversation for
				 * Console/Exit and the auth-failure path waved them
				 * through without credentials.) */
				if (msg[i]->msg != nullptr)
					panel->Message(msg[i]->msg);
				panel->EventHandler(Panel::Get_Passwd);
				(*resp)[i].resp=strdup(panel->GetPasswd().c_str());
				panel->ResetPasswd();
				break;

			case PAM_ERROR_MSG:
			case PAM_TEXT_INFO:
				if (msg[i]->msg != nullptr)
					panel->Message(msg[i]->msg);
				break;
			default:
				result = PAM_CONV_ERR;
				break;
		}
		if ((msg[i]->msg_style == PAM_PROMPT_ECHO_ON ||
			 msg[i]->msg_style == PAM_PROMPT_ECHO_OFF) && (*resp)[i].resp == NULL)
			result = PAM_BUF_ERR;
		if (result != PAM_SUCCESS)
			break;
	}

	if (result != PAM_SUCCESS)
	{
		clearGreeterPamResponses(*resp, num_msg);
		*resp = nullptr;
	}

	return result;
}
#endif

extern App* LoginApp;

int xioerror(Display *disp)
{
	(void)disp;
	LoginApp->RestartServer();
	return 0;
}

void CatchSignal(int sig)
{
	/* Signal context: only store the fact. Main performs X/server cleanup. */
	(void)sig;
	luxedm_signal_pending = 1;
}

static volatile bool got_sigusr1 = false;
void User1Signal(int sig)
{
	(void)sig;
	got_sigusr1 = true;
}

static void installSignalHandler(int signal_number, void (*handler)(int))
{
	struct sigaction action;
	sigemptyset(&action.sa_mask);
	action.sa_handler = handler;
	action.sa_flags = 0;
	(void)sigaction(signal_number, &action, nullptr);
}

App::App(int argc, char** argv)
	: Root(0), Dpy(NULL), Scr(0), LoginPanel(NULL), ServerPID(-1), lockFd(-1),
	  DisplayName(DISPLAY), serverStarted(false),
#ifdef USE_PAM
	  pam_conversation_context{nullptr, nullptr, false},
	  pam(conv, static_cast<void*>(&pam_conversation_context)),
#endif
	  cfg(0), firstlogin(true), daemonmode(false), force_nodaemon(false),
	  testtheme(nullptr), testing(false), tww(1280), twh(1024),
	  mcookie(string(MCOOKIESIZE, 'a'))
{
#ifdef USE_PAM
	pam_conversation_context.panel = &LoginPanel;
	pam_conversation_context.authenticator = &pam;
#endif
	for (int i = 0; i < argc; ++i)
		commandLine.emplace_back(argv[i]);

	int tmp;
	char * win_size = 0;
	optind = 1;
	optarg = nullptr;
	
	/* Parse command line
	   Note: we allow an arg for the -n option to handle "-nodaemon" as
			 originally quoted in the docs. However, the parser has never
			 checked the arg, so "-noddy" works exactly the same */
	while ((tmp = getopt(argc, argv, "c:vhp:w:n::d")) != EOF)
	{
		switch (tmp)
		{
		case 'c': /* Config */
			if (optarg == NULL)
			{
				cerr << "The -c option requires an argument" << endl;
				exit(ERR_EXIT);
			}
			if ( cfg != 0 )
			{
				cerr << "The -c option can only be given once" << endl;
				exit(ERR_EXIT);
			}
			cfg = new Cfg;
			if (!cfg->readConf(optarg)) {
				logStream << APPNAME << ": " << cfg->getError() << endl;
				exit(ERR_EXIT);
			}
			break;

		case 'p':	/* Test theme */
			testtheme = optarg;
			testing = true;
			if (testtheme == NULL)
			{
				cerr << "The -p option requires an argument" << endl;
				exit(ERR_EXIT);
			}
			break;

		case 'w':	/* Window size for theme test mode */
			if ( !testing )
			{
				cerr << "The -w option is only valid after -p" << endl;
				exit(ERR_EXIT);
			}
			win_size = optarg;
			// Test for valid syntax later
			break;

		case 'd':	/* Daemon mode */
			daemonmode = true;
			break;

		case 'n':	/* Daemon mode */
			daemonmode = false;
			force_nodaemon = true;
			break;

		case 'v':	/* Version */
			std::cout << APPNAME << " version " << VERSION
				<< ", © 1997-1998 Per Liden, 2004-2006 Simone Rota and "
				   "Johannes Winkelmann, 2006-2013 Nobuhiro Iwamatsu, "
				   "2022-2023 Rob Pearce, 2026 Jerry Howell" << endl;
		exit(OK_EXIT);
		break;

		case '?':	/* Illegal option - getopt will have printed an error */
			std::cout << endl;
			[[fallthrough]];
		case 'h':   /* Help */
			std::cout << "usage:  " << APPNAME << " [option ...]" << endl
			<< "options:" << endl
			<< "  -c /path/to/config   select configuration file" << endl
			<< "  -d                   daemon mode" << endl
			<< "  -n                   no-daemon mode" << endl
			<< "  -p /path/to/themedir preview theme" << endl
			<< "  -w <www>x<hhh>       size of window for preview" << endl
			<< "  -h                   show this help" << endl
			<< "  -v                   show version" << endl;
			exit(OK_EXIT);
			break;
		}
	}
#ifndef XNEST_DEBUG
	if (getuid() != 0 && !testing)
	{
		logStream << APPNAME << ": only root can run this program" << endl;
		exit(ERR_EXIT);
	}
#endif /* XNEST_DEBUG */
	if ( win_size )
	{
		char* end = nullptr;
		errno = 0;
		long width = strtol(win_size, &end, 10);
		if (errno == ERANGE || end == win_size || *end++ != 'x' ||
				width <= 0 || width > SHRT_MAX) {
			cerr << "Malformed argument to -w option" << endl;
			exit(ERR_EXIT);
		}
		errno = 0;
		char* height_end = nullptr;
		long height = strtol(end, &height_end, 10);
		if (errno == ERANGE || height_end == end || *height_end != '\0' ||
				height <= 0 || height > SHRT_MAX) {
			cerr << "Malformed argument to -w option" << endl;
			exit(ERR_EXIT);
		}
		tww = static_cast<short>(width);
		twh = static_cast<short>(height);
	}
}


App::~App()
{
#ifdef USE_PAM
	try {
		pam.end();
	} catch (PAM::Exception &e) {
		/* Destructors must not throw during teardown. */
	}
#endif
	if (LoginPanel != nullptr) {
		delete LoginPanel;
		LoginPanel = nullptr;
	}
	if (Dpy != nullptr) {
		XCloseDisplay(Dpy);
		Dpy = nullptr;
	}
	if (cfg != nullptr) {
		RemoveLock();
		delete cfg;
		cfg = nullptr;
	}
}

void App::Run()
{
	DisplayName = DISPLAY;
	if (testing) {
		const char *requested_display = getenv("DISPLAY");
		if (requested_display && requested_display[0])
			DisplayName = requested_display;
	}

#ifdef XNEST_DEBUG
	char* p = getenv("DISPLAY");
	if (p && p[0])
	{
		DisplayName = p;
		cout << "Using display name " << DisplayName << endl;
	}
#endif

	/* Read configuration and theme */
	if ( cfg == 0 )
	{
		cfg = new Cfg;
		if (!cfg->readConf(CFGFILE)) {
			logStream << APPNAME << ": " << cfg->getError() << endl;
			exit(ERR_EXIT);
		}
	}
	string themebase = "";
	string themefile = "";
	string themedir = "";

	if (testing)
	{
		themeName = testtheme;
	}
	else
	{
		themebase = string(THEMESDIR) + "/";
		themeName = cfg->getOption("current_theme");
		if (themeName.find(',') == string::npos &&
				!Cfg::validThemeName(themeName)) {
			logStream << APPNAME << ": invalid current theme name; using default" << endl;
			themeName = "default";
		}
		string::size_type pos;
		if ((pos = themeName.find(",")) != string::npos)
		{
			/* input is a set */
			themeName = cfg->findValidRandomTheme(themeName);
			if (themeName == "")
			{
				themeName = "default";
			}
		}
	}

#ifdef USE_PAM
	try {
		pam.start("luxedm");
		pam.set_item(PAM::Authenticator::TTY, DisplayName);
		pam.set_item(PAM::Authenticator::Requestor, "root");
		pam.setenv("XDG_SESSION_CLASS", "greeter");	// so eLogind works right
	}
	catch(PAM::Exception& e){
		logStream << APPNAME << ": " << e << endl;
		exit(ERR_EXIT);
	}
#endif

	bool loaded = false;
	while (!loaded)
	{
		themedir = themebase + themeName;
		themefile = themedir + THEMESFILE;
		if (!cfg->readConf(themefile))
		{
			if (themeName == "default")
			{
				logStream << APPNAME << ": Failed to open default theme file "
					 << themefile << endl;
				exit(ERR_EXIT);
			}
			else
			{
				logStream << APPNAME << ": Invalid theme in config: "
					 << themeName << endl;
				themeName = "default";
			}
		}
		else
		{
			loaded = true;
		}
	}

	if (!testing)
	{
		/* Create lock file */
		LoginApp->GetLock();

		/* Start x-server */
		setenv("DISPLAY", DisplayName, 1);
		installSignalHandler(SIGQUIT, CatchSignal);
		installSignalHandler(SIGTERM, CatchSignal);
		installSignalHandler(SIGINT, CatchSignal);
		installSignalHandler(SIGHUP, CatchSignal);
		installSignalHandler(SIGPIPE, CatchSignal);
		installSignalHandler(SIGUSR1, User1Signal);

#ifndef XNEST_DEBUG
		if ( !force_nodaemon && cfg->getOption("daemon") == "yes" )
		{
			daemonmode = true;
		}

		/* Daemonize */
		if (daemonmode)
		{
			if (daemon(0, 0) == -1)
			{
				logStream << APPNAME << ": " << strerror(errno) << endl;
				exit(ERR_EXIT);
			}
		}

		OpenLog();

		if (daemonmode)
			UpdatePid();

		CreateServerAuth();
		if (StartServer() < 0) {
			logStream << APPNAME << ": could not start the configured X server" << endl;
			StopServer();
			RemoveLock();
			exit(ERR_EXIT);
		}
		if (luxedm_signal_pending != 0) {
			StopServer();
			RemoveLock();
			exit(ERR_EXIT);
		}
#endif
	}

	/* Open display if we haven't already (e.g. testing) */
	if ( Dpy == 0 )
		Dpy = XOpenDisplay(DisplayName);
	/* Now check that it succeeded */
	if ( Dpy == 0 )
	{
		logStream << APPNAME << ": could not open display '"
			 << DisplayName << "'" << endl;
#ifndef XNEST_DEBUG
		if (!testing)
			StopServer();
#endif
		exit(ERR_EXIT);
	}
	markDisplayCloseOnExec(Dpy);
	if (!testing) {
		runDisplaySetup(cfg->getOption("display_setup_cmd"), DisplayName,
			cfg->getOption("authfile"));
		XSync(Dpy, False);
	}

	/* Get screen and root window */
	Scr = DefaultScreen(Dpy);
	Root = RootWindow(Dpy, Scr);

	/* for tests we use a standard window */
	if (testing)
	{
		Window RealRoot = Root;		// already done RootWindow(Dpy, Scr);
		Root = XCreateSimpleWindow(Dpy, RealRoot, 0, 0, tww, twh, 0, 0, 0);
		if (Root == 0) {
			logStream << APPNAME << ": could not create the test window" << endl;
			exit(ERR_EXIT);
		}
		XMapWindow(Dpy, Root);
		XFlush(Dpy);
	}
	else
	{
		blankScreen();
	}

	/* Create panel */
	LoginPanel = new Panel ( Dpy, Scr, Root, cfg, themedir,
							( testing ? Panel::Mode_Test : Panel::Mode_DM ) );
	LoginPanel->HideCursor();

	bool firstloop = true; /* 1st time panel is shown (for automatic username) */
	bool session_initialized = false;
	bool focuspass = cfg->getOption("focus_password")=="yes";
	bool autologin = cfg->getOption("auto_login")=="yes";

	if ( firstlogin && ( cfg->getOption("default_user") != "" ) )
	{
		LoginPanel->SetName ( cfg->getOption("default_user") );
		firstlogin = false;
#ifdef USE_PAM
		pam.set_item(PAM::Authenticator::User, LoginPanel->GetName().c_str());
#endif
		if (autologin)
		{
			/* Resolve the configured session before Login() consumes it.  The
			 * old ordering left the command empty and Xsession fell back to the
			 * first available desktop (commonly plain Openbox). */
			LoginPanel->SwitchSession();
			session_initialized = true;
#ifdef USE_PAM
			try {
				pam.check_acct();
#endif
			Login();
#ifdef USE_PAM
			}
			catch(PAM::Auth_Exception& e){
				// The default user is invalid
			}
#endif
		}
	}

	/* Set NumLock */
	string numlock = cfg->getOption("numlock");
	if (numlock == "on")
		NumLock::setOn(Dpy);
	else if (numlock == "off")
		NumLock::setOff(Dpy);


	/* Start looping */
	int panelclosed = 1;
	Panel::ActionType Action;

	while (1)
	{
		if (luxedm_signal_pending != 0) {
			logStream << APPNAME << ": termination signal received" << endl;
			if (isServerStarted())
				StopServer();
			RemoveLock();
			exit(ERR_EXIT);
		}
		if (panelclosed)
		{
			/* Init root */
			LoginPanel->setBackground();

			/* Show panel */
			LoginPanel->OpenPanel();
		}

		if ( firstloop )
		{
			LoginPanel->Reset();

			if ( cfg->getOption("default_user") != "" )
				LoginPanel->SetName(cfg->getOption("default_user"));

			if (!session_initialized)
			{
				LoginPanel->SwitchSession();
				session_initialized = true;
			}
		}

		if ( !AuthenticateUser(focuspass && firstloop) )
		{
			unsigned int cfg_passwd_timeout;
			cfg_passwd_timeout = Cfg::string2int(cfg->getOption("wrong_passwd_timeout").c_str());
			if ( cfg_passwd_timeout > 60 )
				cfg_passwd_timeout = 60;
			panelclosed = 0;
			firstloop = false;
			LoginPanel->WrongPassword(cfg_passwd_timeout);
			XBell(Dpy, 100);
			continue;
		}

		firstloop = false;

		Action = LoginPanel->getAction();
		/* for themes test we just quit */
		if ( testing )
		{
			Action = Panel::Exit;
		}

		panelclosed = 1;
		LoginPanel->ClosePanel();

		switch (Action)
		{
			case Panel::Login:
				Login();
				break;
			case Panel::Console:
				Console();
				break;
			case Panel::Reboot:
				Reboot();
				break;
			case Panel::Halt:
				Halt();
				break;
			case Panel::Suspend:
				Suspend();
				break;
			case Panel::Exit:
				Exit();
				break;
			default:
				break;
		}

		/* Close all clients ready for another round */
		if (!testing)
		{
			KillAllClients(False);
			KillAllClients(True);
		}
	}
}

#ifdef USE_PAM
bool App::AuthenticateUser(bool focuspass)
{
	/* Reset the username */
	try{
		if (!focuspass)
			pam.set_item(PAM::Authenticator::User, 0);
		pam.authenticate();
	}
	catch(PAM::Auth_Exception& e){
		/* P1 #6: Console and Exit authenticate like any other login.
		 * A failed PAM authentication denies every action; the old
		 * code returned true here for Console/Exit ("fake" auth),
		 * handing out a root console and greeter shutdown to anyone. */
		logStream << APPNAME << ": " << e << endl;
		return false;
	}
	catch(PAM::Exception& e){
		logStream << APPNAME << ": " << e << endl;
		exit(ERR_EXIT);
	}
	return true;
}
#else
bool App::AuthenticateUser(bool focuspass)
{
	if (!focuspass)
	{
		/* P1 #6: no magic-username bypass here either. Console and
		 * Exit fall through to the password check below like any
		 * other login. */
		LoginPanel->EventHandler(Panel::Get_Name);
	}
	LoginPanel->EventHandler(Panel::Get_Passwd);

	char *encrypted;
	std::string correct;
	struct passwd *pw = NULL;

	switch (LoginPanel->getAction())
	{
		case Panel::Suspend:
		case Panel::Halt:
		case Panel::Reboot:
			pw = getpwnam("root");
			break;

		case Panel::Console:
		case Panel::Exit:
		case Panel::Login:
			pw = getpwnam(LoginPanel->GetName().c_str());
			break;
		default:
			break;
	}
	if (pw == 0) {
		endpwent();
		return false;
	}
	const std::string user_name = pw->pw_name == nullptr ? "" : pw->pw_name;

#ifdef HAVE_SHADOW
	bool have_shadow = false;
	struct spwd *sp = user_name.empty() ? nullptr : getspnam(user_name.c_str());
	if (sp != nullptr) {
		have_shadow = true;
		if (sp->sp_pwdp != nullptr)
			correct = sp->sp_pwdp;
	}
	endspent();
	if (!have_shadow && pw->pw_passwd != nullptr)
		correct = pw->pw_passwd;
#else
	if (pw->pw_passwd != nullptr)
		correct = pw->pw_passwd;
#endif		/* HAVE_SHADOW */
	endpwent();

	if (correct.empty())
		return false;

	encrypted = crypt(LoginPanel->GetPasswd().c_str(), correct.c_str());
	return ((encrypted && strcmp(encrypted, correct.c_str()) == 0) ? true : false);
}
#endif

int App::GetServerPID()
{
	return ServerPID;
}


void App::Login()
{
	struct passwd *pw;
	pid_t pid;

#ifdef USE_PAM
	try{
		/* pam_systemd classifies display-manager PAM services as greeters
		 * unless the manager identifies the session being opened.  Desktop
		 * services must see the authenticated session as an ordinary X11
		 * user session, including after RandR mode changes. */
		pam.setenv("XDG_SESSION_CLASS", "user");
		pam.setenv("XDG_SESSION_TYPE", "x11");
		const SessionEnvironment environment = sessionEnvironment({
			"", LoginPanel->getSession(), LoginPanel->getSessionId(),
			LoginPanel->getSessionDesktop()});
		pam.setenv("XDG_SESSION_DESKTOP", environment.session_desktop.c_str());
		pam.setenv("XDG_CURRENT_DESKTOP", environment.current_desktop.c_str());
		pam.setenv("DESKTOP_SESSION", environment.desktop_session.c_str());
		pam.open_session();
		const char *pam_user = static_cast<const char*>(
			pam.get_item(PAM::Authenticator::User));
		if (pam_user == nullptr || *pam_user == '\0') {
			logStream << APPNAME << ": PAM did not provide a user name" << endl;
			try {
				pam.close_session();
			} catch (PAM::Exception &e) {
				logStream << APPNAME << ": " << e << endl;
			}
			return;
		}
		pw = getpwnam(pam_user);
	}
	catch(PAM::Cred_Exception& e){
		/* Credentials couldn't be established */
		logStream << APPNAME << ": " << e << endl;
		return;
	}
	catch(PAM::Exception& e){
		logStream << APPNAME << ": " << e << endl;
		exit(ERR_EXIT);
	}
#else
	pw = getpwnam(LoginPanel->GetName().c_str());
#endif
	if (pw == 0) {
		endpwent();
#ifdef USE_PAM
		try {
			pam.close_session();
		} catch (PAM::Exception &e) {
			logStream << APPNAME << ": " << e << endl;
		}
#endif
		return;
	}
	/* getpwnam() returns storage owned by NSS. Copy every value used after
	 * endpwent(), especially in the parent while the child is running. */
	const std::string user_name = pw->pw_name == nullptr ? "" : pw->pw_name;
	const std::string user_home = pw->pw_dir == nullptr ? "" : pw->pw_dir;
	const std::string user_shell = (pw->pw_shell != nullptr && *pw->pw_shell != '\0')
		? pw->pw_shell : "/bin/sh";
	if (user_name.empty() || user_home.empty()) {
		endpwent();
#ifdef USE_PAM
		try {
			pam.close_session();
		} catch (PAM::Exception &e) {
			logStream << APPNAME << ": " << e << endl;
		}
#endif
		return;
	}
	const char *login_shell = user_shell.c_str();
	/* Setup the environment */
	char* term = getenv("TERM");
	string maildir = _PATH_MAILDIR;
	maildir.append("/");
	maildir.append(user_name);
	string xauthority = user_home;
	xauthority.append("/.Xauthority");

#ifdef USE_PAM
	/* Setup the PAM environment */
	try{
		if (term)
			pam.setenv("TERM", term);
		pam.setenv("HOME", user_home);
		pam.setenv("PWD", user_home);
		pam.setenv("SHELL", login_shell);
		pam.setenv("USER", user_name);
		pam.setenv("LOGNAME", user_name);
		pam.setenv("PATH", cfg->getOption("default_path").c_str());
		pam.setenv("DISPLAY", DisplayName);
		pam.setenv("MAIL", maildir.c_str());
		pam.setenv("XAUTHORITY", xauthority.c_str());
	}
	catch(PAM::Exception& e){
		logStream << APPNAME << ": " << e << endl;
		exit(ERR_EXIT);
	}
#endif

		/* Create new process */
		pid = fork();
		if (pid < 0) {
			logStream << APPNAME << ": could not create the session process: "
					<< strerror(errno) << endl;
#ifdef USE_PAM
			try {
				pam.close_session();
			} catch(PAM::Exception& e) {
				logStream << APPNAME << ": " << e << endl;
			}
#endif
			endpwent();
			return;
		}
		if (pid == 0)
		{
			if (setpgid(0, 0) != 0)
				_exit(ERR_EXIT);
#ifdef USE_PAM
		/* Get a copy of the environment and close the child's copy */
		/* of the PAM-handle. */
		char** child_env = pam.getenvlist();

#else

		const int Num_Of_Variables = 11; /* Number of env. variables + 1 */
		char** child_env = static_cast<char**>(malloc(sizeof(char*)*Num_Of_Variables));
		int n = 0;
		if (term)
			child_env[n++]=StrConcat("TERM=", term);
		child_env[n++]=StrConcat("HOME=", user_home.c_str());
		child_env[n++]=StrConcat("PWD=", user_home.c_str());
		child_env[n++]=StrConcat("SHELL=", login_shell);
		child_env[n++]=StrConcat("USER=", user_name.c_str());
		child_env[n++]=StrConcat("LOGNAME=", user_name.c_str());
		child_env[n++]=StrConcat("PATH=", cfg->getOption("default_path").c_str());
		child_env[n++]=StrConcat("DISPLAY=", DisplayName);
		child_env[n++]=StrConcat("MAIL=", maildir.c_str());
		child_env[n++]=StrConcat("XAUTHORITY=", xauthority.c_str());
		child_env[n++]=0;

#endif

		/* Login process starts here */
		SwitchUser Su(pw, cfg, DisplayName, child_env);
		string session = LoginPanel->getSession();
		string loginCommand = cfg->getOption("login_cmd");
		replaceVariables(loginCommand, SESSION_VAR, session);
		replaceVariables(loginCommand, THEME_VAR, themeName);
		string sessStart = cfg->getOption("sessionstart_cmd");
		if (sessStart != "")
		{
			if (!runRootCommand(sessStart, user_name.c_str()))
				logStream << APPNAME << ": Failed to run sessionstart_cmd" << endl;
		}
		Su.Login(loginCommand.c_str(), mcookie.c_str());
			_exit(OK_EXIT);
		}
		/* The child has its own copy of the NSS result; release the parent's
		 * lookup state before waiting on the session. */
		endpwent();
		/* Establish the process group from both sides to avoid a fork/exec race. */
		if (setpgid(pid, pid) != 0 && errno != EACCES)
			logStream << APPNAME << ": could not establish the session process group: "
					<< strerror(errno) << endl;
		LoginPanel->ReleaseSessionGraphics();

#ifndef XNEST_DEBUG
	CloseLog();
#endif

	/* Wait until user is logging out (login process terminates) */
		siginfo_t session_status{};
		bool session_observed = false;
		do {
			if (waitid(P_PID, static_cast<id_t>(pid), &session_status,
					WEXITED | WNOWAIT) == 0) {
				session_observed = true;
				break;
			}
			if (errno == EINTR && luxedm_signal_pending != 0)
				break;
		} while (errno == EINTR);
		if (!session_observed) {
			logStream << APPNAME << ": could not wait for the session process: "
					<< strerror(errno) << endl;
		}
#ifndef XNEST_DEBUG
	/* Re-activate log file */
	OpenLog();
#endif
	const bool session_failed = !session_observed ||
		(session_status.si_code == CLD_EXITED && session_status.si_status != 0);
	if (session_failed)
	{
		LoginPanel->Message("Failed to execute login command");
		sleep(3);
	}
	else
	{
		string sessStop = cfg->getOption("sessionstop_cmd");
		if ( sessStop != "" )
		{
			if (!runRootCommand(sessStop, user_name.c_str()))
				logStream << APPNAME << "Session stop command failed" << endl;
		}
	}

#ifdef USE_PAM
	try {
		pam.close_session();
	}
	catch(PAM::Exception& e){
		logStream << APPNAME << ": " << e << endl;
	}
#endif

	/* Close all clients */
	KillAllClients(False);
	KillAllClients(True);

	/* Give session descendants a bounded grace period, then stop survivors. */
	stopSessionGroup(pid);
	/* Keep the exited group leader unreaped until group cleanup finishes. Its
	 * PID therefore cannot be recycled into an unrelated process group while
	 * killpg() is still using it as the group identity. */
	int status = 0;
	pid_t wpid;
	do {
		wpid = waitpid(pid, &status, 0);
	} while (wpid < 0 && errno == EINTR);
	if (wpid == pid && WIFEXITED(status))
		logStream << APPNAME << ": session process exited with status "
				<< WEXITSTATUS(status) << endl;
	else if (wpid == pid && WIFSIGNALED(status))
		logStream << APPNAME << ": session process terminated by signal "
				<< WTERMSIG(status) << endl;
	else if (wpid != pid)
		logStream << APPNAME << ": could not reap the session process: "
				<< strerror(errno) << endl;

	LoginPanel->HideCursor();

#ifndef XNEST_DEBUG
	RestartServer();
#endif
}


void App::Reboot()
{
#ifdef USE_PAM
	try {
		pam.end();
	}
	catch(PAM::Exception& e){
		logStream << APPNAME << ": " << e << endl;
	}
#endif

	/* Write message */
	LoginPanel->Message((char*)cfg->getOption("reboot_msg").c_str());
	sleep(3);

	/* Stop server and reboot */
	StopServer();
	RemoveLock();
	if (!Util::runCommand(cfg->getOption("reboot_cmd")))
		logStream << APPNAME << ": Failed to execute reboot command" << endl;
	exit(OK_EXIT);
}


void App::Halt()
{
#ifdef USE_PAM
	try {
		pam.end();
	}
	catch(PAM::Exception& e){
		logStream << APPNAME << ": " << e << endl;
	}
#endif

	/* Write message */
	LoginPanel->Message((char*)cfg->getOption("shutdown_msg").c_str());
	sleep(3);

	/* Stop server and halt */
	StopServer();
	RemoveLock();
	if (!Util::runCommand(cfg->getOption("halt_cmd")))
		logStream << APPNAME << ": Failed to execute halt command" << endl;
	exit(OK_EXIT);
}


void App::Suspend()
{
	sleep(1);
	if (!Util::runCommand(cfg->getOption("suspend_cmd")))
		logStream << APPNAME << ": Failed to execute suspend command" << endl;
}


void App::Console()
{
	/* P1 #6: console_cmd defaults to empty, and an empty command is a
	 * silent no-op (Util::runCommand succeeds without spawning
	 * anything). The greeter never runs a root shell unless the admin
	 * explicitly configures console_cmd. */
	const string command = cfg->getOption("console_cmd");
	if (command.empty())
		return;
	if (!Util::runCommand(command))
		logStream << APPNAME << ": failed to run console command" << endl;
}

void App::Exit()
{
#ifdef USE_PAM
	try {
		pam.end();
	}
	catch(PAM::Exception& e){
		logStream << APPNAME << ": " << e << endl;
	}
#endif

	if (testing)
	{
		std::string testmsg = "User ";
		testmsg += LoginPanel->GetName();
		testmsg += " auth OK, session=";
		testmsg += LoginPanel->getSession();
		LoginPanel->Message(testmsg);
		sleep(3);
		delete LoginPanel;
		XCloseDisplay(Dpy);
	}
	else
	{
		delete LoginPanel;
		StopServer();
		RemoveLock();
	}
	delete cfg;
	exit(OK_EXIT);
}

int CatchErrors(Display *dpy, XErrorEvent *ev)
{
	(void)dpy;
	(void)ev;
	return 0;
}

void App::RestartServer()
{
#ifdef USE_PAM
	try {
		pam.end();
	}
	catch(PAM::Exception& e){
		logStream << APPNAME << ": " << e << endl;
	}
#endif

	/* Destroy X-backed panel resources while the display is still valid. */
	if (LoginPanel != NULL) {
		delete LoginPanel;
		LoginPanel = NULL;
	}
	StopServer();
	RemoveLock();
	while (waitpid(-1, NULL, WNOHANG) > 0); /* Collect all dead children. */

	vector<char *> arguments;
	arguments.reserve(commandLine.size() + 1);
	for (string &argument : commandLine)
		arguments.push_back(argument.data());
	arguments.push_back(NULL);
	if (luxedm_signal_pending != 0)
		exit(ERR_EXIT);
	execv("/proc/self/exe", arguments.data());
	logStream << APPNAME << ": could not restart cleanly: " << strerror(errno) << endl;
	exit(ERR_EXIT);
}


/*
 * Iterates over the list of all windows declared as children of Root and
 * kills the clients. Since Root is the root window of the screen, all 
 * running applications (of the logged-in session) should be caught by this
 * 
 * **** WARNING **** this includes child windows in the panel! Only call this
 * when the panel is closed!
 */
void App::KillAllClients(Bool top)
{
	Window dummywindow;
	Window *children = NULL;
	unsigned int nchildren;
	unsigned int i;
	XWindowAttributes attr;

	XSync(Dpy, 0);
	int (*previous_error_handler)(Display *, XErrorEvent *) =
		XSetErrorHandler(CatchErrors);

	nchildren = 0;
	if (!XQueryTree(Dpy, Root, &dummywindow, &dummywindow, &children, &nchildren)) {
		XSetErrorHandler(previous_error_handler);
		return;
	}
	if (!top)
	{
		for (i=0; i<nchildren; i++)
		{
			if (XGetWindowAttributes(Dpy, children[i], &attr) && (attr.map_state == IsViewable))
				children[i] = XmuClientWindow(Dpy, children[i]);
			else
				children[i] = 0;
		}
	}

	for (i=0; i<nchildren; i++)
	{
		if (children[i])
			XKillClient(Dpy, children[i]);
	}
	if (children != NULL)
		XFree(static_cast<void *>(children));

	XSync(Dpy, 0);
	XSetErrorHandler(previous_error_handler);
}


int App::ServerTimeout(int timeout, char* text)
{
	if (ServerPID < 0)
		return 1;
	int	i = 0;
	int pidfound = -1;
	static char	*lasttext;

	while (1)
	{
		pidfound = waitpid(ServerPID, NULL, WNOHANG);
		if (pidfound == ServerPID)
			break;
		if (pidfound < 0 && errno == ECHILD) {
			pidfound = ServerPID;
			break;
		}
		if (timeout)
		{
			if (i == 0 && text != lasttext)
				logStream << endl << APPNAME << ": waiting for " << text;
			else
				logStream << ".";
		}
		if (timeout)
			sleep(1);
		if (++i > timeout)
			break;
	}

	if (i > 0)
		logStream << endl;
	lasttext = text;

	return (ServerPID != pidfound);
}


int App::WaitForServer()
{
	int	ncycles	 = 120;
	int	cycles;

	/* The X server should send us a USR1 signal when it's ready. We trap 	*/
	/* that signal and set a flag. If that's not already happened, wait for */
	/* a good time. The incoming signal should terminate the sleep() call   */
	/* with a non-zero return value. Otherwise, time out and try anyway but */
	/* log the oddity.                                                      */
	if ( !got_sigusr1 && ( sleep(5)==0 ) )
		logStream << "WaitForServer: Not seen SigUSR1 from Xserver" << endl;

	for (cycles = 0; cycles < ncycles; cycles++)
	{
		Dpy = XOpenDisplay(DisplayName);
		if ( Dpy )
		{
			/* The greeter connection belongs to the manager.  Letting the
			 * session inherit it across exec can couple desktop X activity,
			 * including RandR reconfiguration, to the manager connection. */
			markDisplayCloseOnExec(Dpy);
			XSetIOErrorHandler(xioerror);
			return 1;
		}
		else
		{
			if (!ServerTimeout(1, (char *) "X server to begin accepting connections"))
				break;
		}
	}

	logStream << "Giving up." << endl;

	return 0;
}

int App::StartServer()
{
	got_sigusr1 = false;	// We're about to start the X server so clear the semaphore

	serverStarted = false;
	vector<string> argumentStorage;
	const string serverCommand = cfg->getOption("default_xserver");
	if (serverCommand.empty()) {
		logStream << APPNAME << ": no X server command is configured" << endl;
		return -1;
	}
	argumentStorage.push_back(serverCommand);
	const vector<string> configuredArguments = Util::splitCommand(
		cfg->getOption("xserver_arguments"));
	if (configuredArguments.empty() &&
			cfg->getOption("xserver_arguments").find_first_not_of(" \t\r\n") != string::npos) {
		logStream << APPNAME << ": malformed X server arguments" << endl;
		return -1;
	}
	argumentStorage.insert(argumentStorage.end(), configuredArguments.begin(),
		configuredArguments.end());
	argumentStorage.push_back("-auth");
	argumentStorage.push_back(cfg->getOption("authfile"));

	bool hasVtSet = false;
	for (const string &candidate : argumentStorage) {
		if (candidate.size() > 2 && candidate[0] == 'v' && candidate[1] == 't') {
			bool ok = false;
			Cfg::string2int(candidate.c_str() + 2, &ok);
			if (ok) {
				hasVtSet = true;
				break;
			}
		}
	}

	if (!hasVtSet && daemonmode)
		argumentStorage.push_back("vt07");

	vector<char *> server;
	server.reserve(argumentStorage.size() + 1);
	for (string &stored : argumentStorage)
		server.push_back(stored.data());
	server.push_back(NULL);

	ServerPID = fork();

	switch (ServerPID) {
	case 0:
		signal(SIGTTIN, SIG_IGN);
		signal(SIGTTOU, SIG_IGN);
		signal(SIGUSR1, SIG_IGN);
		setpgid(0,getpid());

		execvp(server[0], server.data());
		/* _exit avoids flushing duplicated parent buffers after fork. */
		_exit(ERR_EXIT);
		break;

	case -1:
		break;

	default:
		errno = 0;
		// Prime the server timeout function and check for an immediate crash
		if (!ServerTimeout(0, (char *)""))
		{
			ServerPID = -1;
			break;
		}

		/* Wait for server to start up */
		if (WaitForServer() == 0)
		{
			logStream << APPNAME << ": unable to connect to X server" << endl;
			StopServer();
			ServerPID = -1;
			exit(ERR_EXIT);
		}
		break;
	}

	serverStarted = ServerPID > 0;

	return ServerPID;
}


jmp_buf CloseEnv;
int IgnoreXIO(Display *d)
{
	(void)d;
	logStream << APPNAME << ": connection to X server lost." << endl;
	longjmp(CloseEnv, 1);
}

void App::StopServer()
{
	signal(SIGQUIT, SIG_IGN);
	signal(SIGINT, SIG_IGN);
	signal(SIGHUP, SIG_IGN);
	signal(SIGPIPE, SIG_IGN);
	signal(SIGTERM, SIG_DFL);
	signal(SIGKILL, SIG_DFL);

	/* Catch X error */
	XSetIOErrorHandler(IgnoreXIO);
	if (!setjmp(CloseEnv) && Dpy)
		XCloseDisplay(Dpy);
	Dpy = NULL;
	if (cfg != NULL) {
		const string authfile = cfg->getOption("authfile");
		struct stat auth_stat;
		if (!authfile.empty() && lstat(authfile.c_str(), &auth_stat) == 0 &&
				S_ISREG(auth_stat.st_mode) && auth_stat.st_uid == geteuid() &&
				auth_stat.st_nlink == 1)
			unlink(authfile.c_str());
	}

	/* Send HUP to process group */
	errno = 0;
	if ((killpg(getpid(), SIGHUP) != 0) && (errno != ESRCH))
		logStream << APPNAME << ": can't send HUP to process group " << getpid() << endl;

	/* Send TERM to server */
	if (ServerPID < 0) {
		serverStarted = false;
		return;
	}

	errno = 0;

	if (killpg(ServerPID, SIGTERM) < 0)
	{
		if (errno == EPERM)
		{
			logStream << APPNAME << ": can't kill X server" << endl;
			exit(ERR_EXIT);
		}
		if (errno == ESRCH)
			return;
	}

	/* Wait for server to shut down */
	if (!ServerTimeout(10, (char *)"X server to shut down"))
	{
		logStream << endl;
		return;
	}

	logStream << endl << APPNAME <<
		":  X server slow to shut down, sending KILL signal." << endl;

	/* Send KILL to server */
	errno = 0;
	if (killpg(ServerPID, SIGKILL) < 0)
	{
		if (errno == ESRCH)
			return;
	}

	/* Wait for server to die */
	if (ServerTimeout(3, (char*)"server to die"))
	{
		logStream << endl << APPNAME << ": can't kill server" << endl;
		exit(ERR_EXIT);
	}
	logStream << endl;
	serverStarted = false;
}

void App::blankScreen()
{
	GC gc = XCreateGC(Dpy, Root, 0, 0);
	if (gc == nullptr) {
		logStream << APPNAME << ": could not create the screen graphics context" << endl;
		exit(ERR_EXIT);
	}
	XSetForeground(Dpy, gc, BlackPixel(Dpy, Scr));
	XFillRectangle(Dpy, Root, gc, 0, 0,
				   XWidthOfScreen(ScreenOfDisplay(Dpy, Scr)),
				   XHeightOfScreen(ScreenOfDisplay(Dpy, Scr)));
	XFlush(Dpy);
	XFreeGC(Dpy, gc);
}


/* Hold an advisory lock instead of trusting a PID read through a pathname. */
void App::GetLock()
{
	const string path = cfg->getOption("lockfile");
	struct stat lock_stat;

	if (!secureRootParent(path)) {
		logStream << APPNAME << ": lock file parent is not secure: " << path << endl;
		exit(ERR_EXIT);
	}
	lockFd = open(path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
	if (lockFd < 0 || fstat(lockFd, &lock_stat) != 0 ||
			!S_ISREG(lock_stat.st_mode) || lock_stat.st_uid != geteuid() ||
			lock_stat.st_nlink != 1 || (lock_stat.st_mode & 022) != 0) {
		logStream << APPNAME << ": another instance is active or the lock file is unsafe: "
			<< path << endl;
		if (lockFd >= 0)
			close(lockFd);
		lockFd = -1;
		exit(ERR_EXIT);
	}
	if (flock(lockFd, LOCK_EX | LOCK_NB) != 0) {
		const int error = errno;
		close(lockFd);
		lockFd = -1;
		if (error == EWOULDBLOCK || error == EAGAIN)
			exit(OK_EXIT);
		logStream << APPNAME << ": cannot lock the instance file: "
			<< strerror(error) << endl;
		exit(ERR_EXIT);
	}
	UpdatePid();
}


/* Remove lockfile */
void App::RemoveLock()
{
	if (lockFd >= 0) {
		const string path = cfg->getOption("lockfile");
		struct stat held_stat;
		struct stat path_stat;
		if (fstat(lockFd, &held_stat) == 0 &&
				lstat(path.c_str(), &path_stat) == 0 &&
				S_ISREG(path_stat.st_mode) &&
				held_stat.st_dev == path_stat.st_dev &&
				held_stat.st_ino == path_stat.st_ino)
			unlink(path.c_str());
		close(lockFd);
		lockFd = -1;
	}
}


/* Get server start check flag. */
bool App::isServerStarted()
{
	return serverStarted;
}


/* Open a log file for error reporting */
void App::OpenLog()
{
	if ( !logStream.openLog( cfg->getOption("logfile").c_str() ) )
	{
		cerr <<  APPNAME << ": Could not access log file: " << cfg->getOption("logfile") << endl;
		RemoveLock();
		exit(ERR_EXIT);
	}
	/* I should set the buffers to imediate write, but I just flush on every << operation. */
}


/* Close the logging stream - just a wrapper round the real method */
void App::CloseLog()
{
	/* Simply closing the log */
	logStream.closeLog();
}


/*
 * Populate any matching fields in a markup string with the value of
 * a variable. The markup is %name but that is not enforced here - instead
 * the caller must pass the full markup tag - %name, not just name
 */
void App::replaceVariables(string& input,
			   const string& var,
			   const string& value)
{
	if (var.empty())
		return;
	string::size_type pos = 0;
	const size_t len = var.size();
	while ((pos = input.find(var, pos)) != string::npos) {
		input = input.substr(0, pos) + value + input.substr(pos+len);
		pos += value.size();
	}
}


/*
 * Set the required server authority parameters in the environment
 * and file system.
 */
void App::CreateServerAuth()
{
	string authfile;
	if (!Util::secure_random_hex(mcookie)) {
		logStream << APPNAME << ": secure random generation failed" << endl;
		exit(ERR_EXIT);
	}
	/* reinitialize auth file */
	authfile = cfg->getOption("authfile");
	if (!secureRootParent(authfile)) {
		logStream << APPNAME << ": X authority parent is not secure: "
			<< authfile << endl;
		exit(ERR_EXIT);
	}
	remove(authfile.c_str());
	setenv("XAUTHORITY", authfile.c_str(), 1);
	if (!Util::add_mcookie(mcookie, DisplayName, cfg->getOption("xauth_path"), authfile)) {
		logStream << APPNAME << ": could not create the X server authority file" << endl;
		exit(ERR_EXIT);
	}
	struct stat auth_stat;
	if (lstat(authfile.c_str(), &auth_stat) != 0 || !S_ISREG(auth_stat.st_mode) ||
			auth_stat.st_uid != geteuid() || auth_stat.st_nlink != 1 ||
			chmod(authfile.c_str(), 0600) != 0) {
		logStream << APPNAME << ": X server authority file is not private" << endl;
		exit(ERR_EXIT);
	}
}


/*
 * Concatenate two C-style strings into a newly alloc'd char array of the
 * right size.
 */
char* App::StrConcat(const char* str1, const char* str2)
{
	if (str1 == nullptr)
		str1 = "";
	if (str2 == nullptr)
		str2 = "";
	char* tmp = new char[strlen(str1) + strlen(str2) + 1];
	strcpy(tmp, str1);
	strcat(tmp, str2);
	return tmp;
}


/*
 * Write our process ID to the lock file specified in the config
 */
void App::UpdatePid()
{
	if (lockFd < 0 || ftruncate(lockFd, 0) != 0 || lseek(lockFd, 0, SEEK_SET) < 0 ||
			dprintf(lockFd, "%ld\n", static_cast<long>(getpid())) < 0) {
		logStream << APPNAME << ": could not update lock file: "
			<< cfg->getOption("lockfile") << endl;
		exit(ERR_EXIT);
	}
}
