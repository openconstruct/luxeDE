/* SLiM - Simple Login Manager
   Copyright (C) 1997, 1998 Per Liden
   Copyright (C) 2004-06 Simone Rota <sip@varlock.com>
   Copyright (C) 2004-06 Johannes Winkelmann <jw@tks6.net>
   Copyright (C) 2013 Nobuhiro Iwamatsu <iwamatsu@nigauri.org>
   Copyright (C) 2022-23 Rob Pearce <slim@flitspace.org.uk>
   Copyright (C) 2026 Jerry Howell

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.
*/

#ifndef _PANEL_H_
#define _PANEL_H_

#include <X11/Xlib.h>
#include <X11/Xft/Xft.h>
#include <stdlib.h>
#include <string>
#include <vector>

// Forward declarations
class Image;
class Cfg;


struct Rectangle {
	int x;
	int y;
	unsigned int width;
	unsigned int height;

	Rectangle() : x(0), y(0), width(0), height(0) {};
	Rectangle(int x, int y, unsigned int width, unsigned int height)
		: x(x), y(y), width(width), height(height)
	{}

	bool is_empty() const { return width == 0 || height == 0; }
};

class Panel
{
public:
	using FailureHandler = void (*)(const char *message);
	static void SetFailureHandler(FailureHandler handler);

	enum ActionType {
		Login,
		UnLock = Login,     // LuxeDM Lock treats this as the login action.
		Console,
		Reboot,
		Halt,
		Exit,
		Suspend
	};

	enum FieldType {
		Get_Name,
		Get_Passwd
	};

	enum PanelType {
		Mode_DM,
		Mode_Test,
		Mode_Lock
	};

	Panel(Display *dpy, int scr, Window root, Cfg *config,
		  const std::string& themed, PanelType panel_mode);
	~Panel();
	void OpenPanel();
	void ClosePanel();
	void ReleaseSessionGraphics();
	void WrongPassword(int timeout);
	void Message(const std::string &text);
	void EventHandler(const FieldType &curfield);
	std::string getSession();
	std::string getSessionId() const;
	std::string getSessionDesktop() const;
	ActionType getAction(void) const;

	void Reset(void);
	void ResetName(void);
	void ResetPasswd(void);
	void SetName(const std::string &name);
	const std::string& GetName(void) const;
	const std::string& GetPasswd(void) const;
	void SwitchSession();

	Atom BackgroundPixmapId;	// from XInternAtom -- does it need to be a member var?
	void setBackground(void);
	void HideCursor();

private:
	friend struct PanelLifetimeTest;
	Panel();
	unsigned long GetColor(const char *colorname);
	void OnExpose(void);
	void EraseLastChar(std::string &formerString);
	bool OnKeyPress(XEvent& event);
	void ShowText(Window window);
	void ShowSession();
	void DrawPanel(Window window);
	void DrawRootText(Window root, const Rectangle& bounds,
				  const std::string& text, const std::string& x_option,
				  const std::string& y_option);
	void TextCursor(Window window, int visible);

	void SlimDrawString8(XftDraw *d, XftColor *color, XftFont *font,
							int x, int y, const std::string &str,
							XftColor *shadowColor,
							int xOffset, int yOffset);

	std::vector<Rectangle> GetMonitorViewports();
	void PrepareGraphics();
	void FreePixmaps();

	struct MirrorSurface {
		Rectangle viewport;
		Window root = 0;
		Window panel = 0;
		Pixmap background = 0;
		Pixmap panel_pixmap = 0;
		int panel_x = 0;
		int panel_y = 0;
	};

	/* Private data */
	Cfg *cfg;
	PanelType mode; /* work mode */
	Display *Dpy;
	XIM input_method = nullptr;
	XIC input_context = nullptr;
	int Scr;
	Window Win = 0;
	Window Root = 0;
	Window RealRoot = 0;
	int X, Y;
	GC TextGC = 0;
	XftFont *font = nullptr;
	XftColor inputshadowcolor{};
	XftColor inputcolor{};
	XftColor msgcolor{};
	XftColor msgshadowcolor{};
	XftFont *msgfont = nullptr;
	XftFont *welcomefont = nullptr;
	XftColor welcomecolor{};
	XftFont *sessionfont = nullptr;
	XftColor sessioncolor{};
	XftColor sessionshadowcolor{};
	XftColor welcomeshadowcolor{};
	XftFont *enterfont = nullptr;
	XftColor entercolor{};
	XftColor entershadowcolor{};
	ActionType action = Login;
	FieldType field = Get_Name;
	XGlyphInfo MsgExtents{};
	
	/* Username/Password */
	std::string NameBuffer;
	std::string PasswdBuffer;
	std::string HiddenPasswdBuffer;

	/* screen stuff */
	Rectangle viewport;
	std::vector<MirrorSurface> mirrors;

	/* Configuration */
	int input_name_x;
	int input_name_y;
	int input_pass_x;
	int input_pass_y;
	int inputShadowXOffset;
	int inputShadowYOffset;
	int welcome_x;
	int welcome_y;
	int welcome_shadow_xoffset;
	int welcome_shadow_yoffset;
	int session_shadow_xoffset;
	int session_shadow_yoffset;
	int username_x;
	int username_y;
	int username_shadow_xoffset;
	int username_shadow_yoffset;
	int password_x;
	int password_y;
	std::string welcome_message;

	/* Pixmap data */
	Pixmap PanelPixmap = 0;
	Pixmap BackgroundPixmap = 0;

	int panel_width = 0;
	int panel_height = 0;

	std::string themedir;

	/* Session handling */
	std::string session_name;
    std::string session_exec;
    std::string session_id;
    std::string session_desktop;
};

#endif /* _PANEL_H_ */
