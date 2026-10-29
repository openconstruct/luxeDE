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

#include <sstream>
#include <algorithm>
#include <cctype>
#include <poll.h>
#include <memory>
#ifdef __GLIBC__
#include <malloc.h>
#endif
#include <X11/extensions/Xrandr.h>
#include <X11/Xatom.h>		// for XA_PIXMAP
#include <unistd.h>			// for sleep
#include <iostream>			// for cerr
#include "const.h"
#include "image.h"
#include "log.h"
#include "cfg.h"
#include "switchuser.h"
#include "panel.h"

using namespace std;

volatile sig_atomic_t luxedm_signal_pending = 0;

namespace {
Panel::FailureHandler failure_handler = nullptr;

[[noreturn]] void fail_panel(const char *message)
{
	logStream << APPNAME << ": " << (message == nullptr ? "panel failure" : message) << endl;
	if (failure_handler != nullptr)
		failure_handler(message == nullptr ? "panel failure" : message);
	std::exit(ERR_EXIT);
}
}

void Panel::SetFailureHandler(FailureHandler handler)
{
	failure_handler = handler;
}

Panel::Panel(Display* dpy, int scr, Window root, Cfg* config,
			 const string& themedir, PanelType panel_mode)
	: cfg(config), mode(panel_mode), Dpy(dpy), Scr(scr), Win(0),
	  Root(root), RealRoot(root), themedir(themedir), session_name(""), session_exec(""),
	  session_id("luxe"), session_desktop("X-Luxe")
{
	if (dpy == nullptr || scr < 0 || scr >= XScreenCount(dpy) || root == None || config == nullptr)
		fail_panel("invalid panel construction parameters");
	if ( mode == Mode_Test )
	{
		XWindowAttributes attributes{};
		if (XGetWindowAttributes(Dpy, Root, &attributes) == 0 ||
				attributes.width <= 0 || attributes.height <= 0) {
			logStream << APPNAME << ": could not determine test-window dimensions" << endl;
			fail_panel("panel operation failed");
		}
		viewport.x      = 0;	// Not actually used. The window's position
		viewport.y      = 0;	// is irrelevant to our drawing functions
		viewport.width  = static_cast<unsigned int>(attributes.width);
		viewport.height = static_cast<unsigned int>(attributes.height);
	}
	else
	{
		const vector<Rectangle> viewports = GetMonitorViewports();
		if (viewports.empty()) {
			logStream << APPNAME << ": no usable monitor viewport" << endl;
			fail_panel("panel operation failed");
		}
		viewport = viewports.front();
		/* setBackground() creates one child surface per physical viewport.
		 * Keeping the real X root separate lets extended monitors use their
		 * own centered theme instead of one stretched virtual desktop. */
		Root = 0;
		for (size_t index = 1; index < viewports.size(); ++index) {
			MirrorSurface surface;
			surface.viewport = viewports[index];
			mirrors.push_back(surface);
		}
	}

	/* Init GC */
	XGCValues gcv;
	unsigned long gcm;
	gcm = GCForeground|GCBackground|GCGraphicsExposures;
	gcv.foreground = GetColor("black");
	gcv.background = GetColor("white");
	gcv.graphics_exposures = False;

	TextGC = XCreateGC(Dpy, RealRoot, gcm, &gcv);
	if (TextGC == nullptr) {
		logStream << APPNAME << ": could not create the panel graphics context" << endl;
		fail_panel("panel operation failed");
	}

	// Intern _XROOTPMAP_ID property  -  does this belong here?
	BackgroundPixmapId = XInternAtom(Dpy, "_XROOTPMAP_ID", False);

	font = XftFontOpenName(Dpy, Scr, cfg->getOption("input_font").c_str());
	welcomefont = XftFontOpenName(Dpy, Scr, cfg->getOption("welcome_font").c_str());
	enterfont = XftFontOpenName(Dpy, Scr, cfg->getOption("username_font").c_str());
	msgfont = XftFontOpenName(Dpy, Scr, cfg->getOption("msg_font").c_str());
	if (!font || !welcomefont || !enterfont || !msgfont) {
		logStream << APPNAME << ": could not load configured fonts" << endl;
		fail_panel("panel operation failed");
	}

	Visual* visual = DefaultVisual(Dpy, Scr);
	Colormap colormap = DefaultColormap(Dpy, Scr);
	/* NOTE: using XftColorAllocValue() would be a better solution. Lazy me. */
	auto alloc_color = [&](const char *name, XftColor *color) {
		*color = XftColor{};
		if (!XftColorAllocName(Dpy, visual, colormap, name, color))
			logStream << APPNAME << ": could not allocate color " << name << endl;
	};
	alloc_color(cfg->getOption("input_color").c_str(), &inputcolor);
	alloc_color(cfg->getOption("input_shadow_color").c_str(), &inputshadowcolor);
	alloc_color(cfg->getOption("welcome_color").c_str(), &welcomecolor);
	alloc_color(cfg->getOption("welcome_shadow_color").c_str(), &welcomeshadowcolor);
	alloc_color(cfg->getOption("username_color").c_str(), &entercolor);
	alloc_color(cfg->getOption("username_shadow_color").c_str(), &entershadowcolor);
	alloc_color(cfg->getOption("msg_color").c_str(), &msgcolor);
	alloc_color(cfg->getOption("msg_shadow_color").c_str(), &msgshadowcolor);
	alloc_color(cfg->getOption("session_color").c_str(), &sessioncolor);
	alloc_color(cfg->getOption("session_shadow_color").c_str(), &sessionshadowcolor);

	/* Load properties from config / theme */
	input_name_x = cfg->getIntOption("input_name_x");
	input_name_y = cfg->getIntOption("input_name_y");
	input_pass_x = cfg->getIntOption("input_pass_x");
	input_pass_y = cfg->getIntOption("input_pass_y");
	inputShadowXOffset = cfg->getIntOption("input_shadow_xoffset");
	inputShadowYOffset = cfg->getIntOption("input_shadow_yoffset");

	if (input_pass_x < 0 || input_pass_y < 0){ /* single inputbox mode */
		input_pass_x = input_name_x;
		input_pass_y = input_name_y;
	}
	PrepareGraphics();
	welcome_message = cfg->getWelcomeMessage();
	if (mode == Mode_Lock) {
		const char *environment_user = getenv("USER");
		SetName(environment_user == nullptr ? "" : environment_user);
		field = Get_Passwd;
	}
	MsgExtents.width = 0;
}

void Panel::PrepareGraphics()
{
	if (PanelPixmap && BackgroundPixmap)
		return;

	/* Load panel and background images. Each extended monitor gets its own
	 * background rendering and panel pixmap so mixed resolutions remain sharp. */
	string panelpng = themedir + "/panel.png";
	auto image = std::make_unique<Image>();
	bool loaded = image->Read(panelpng.c_str());
	if (!loaded) { /* try jpeg if png failed */
		panelpng = themedir + "/panel.jpg";
		loaded = image->Read(panelpng.c_str());
		if (!loaded) {
			logStream << APPNAME
				 << ": could not load panel image for theme '"
				 << themedir << "'" << endl;
			fail_panel("panel operation failed");
		}
	}
	const string panel_path = panelpng;

	auto bgImg = std::make_unique<Image>();
	string bgstyle = cfg->getOption("background_style");
	string background_path;
	if (bgstyle != "color") {
		background_path = themedir + "/background.png";
		loaded = bgImg->Read(background_path.c_str());
		if (!loaded) { /* try jpeg if png failed */
			background_path = themedir + "/background.jpg";
			loaded = bgImg->Read(background_path.c_str());
			if (!loaded){
				logStream << APPNAME
					 << ": could not load background image for theme '"
					 << themedir << "'" << endl;
				fail_panel("panel operation failed");
			}
		}
	}

	string hexvalue = cfg->getOption("background_color");
	if (hexvalue.size() != 7 || hexvalue[0] != '#' ||
			!std::all_of(hexvalue.begin() + 1, hexvalue.end(),
				[](unsigned char c) { return std::isxdigit(c) != 0; }))
		hexvalue = "#CCCCCC";
	else
		hexvalue = hexvalue.substr(1, 6);
	string cfgX = cfg->getOption("input_panel_x");
	string cfgY = cfg->getOption("input_panel_y");
	auto size_background = [&](Image *background, const Rectangle& bounds) {
		// Center before Resize and Tile to blend transparent pixels to bg_color.
		if (bgstyle == "stretch") {
			background->Center(background->Width(), background->Height(), hexvalue.c_str());
			background->Resize(bounds.width, bounds.height);
		}
		else if (bgstyle == "tile") {
			background->Center(background->Width(), background->Height(), hexvalue.c_str());
			background->Tile(bounds.width, bounds.height);
		}
		else if (bgstyle == "center") {
			background->Center(bounds.width, bounds.height, hexvalue.c_str());
		}
		else {
			background->Plain(bounds.width, bounds.height, hexvalue.c_str());
		}
	};
	size_background(bgImg.get(), viewport);

	X = Cfg::absolutepos(cfgX, viewport.width, image->Width());
	Y = Cfg::absolutepos(cfgY, viewport.height, image->Height());

	/* Merge image with cropped background, so that PanelPixmap is the
	 * panel with the relevant part of the X root image included instead
	 * of the alpha channel */
	image->Merge(bgImg.get(), X, Y);
	panel_width = image->Width();
	panel_height = image->Height();

	PanelPixmap = image->createPixmap(Dpy, Scr, RealRoot);
	BackgroundPixmap = bgImg->createPixmap(Dpy, Scr, RealRoot);
	if (PanelPixmap == None || BackgroundPixmap == None) {
		logStream << APPNAME << ": could not create panel/background pixmaps" << endl;
		fail_panel("panel operation failed");
	}

	for (MirrorSurface& surface : mirrors) {
		Image mirror_panel;
		Image mirror_background;
		if (!mirror_panel.Read(panel_path.c_str())) {
			logStream << APPNAME << ": could not reload panel image for monitor" << endl;
			fail_panel("panel operation failed");
		}
		if (bgstyle != "color" && !mirror_background.Read(background_path.c_str())) {
			logStream << APPNAME << ": could not reload background image for monitor" << endl;
			fail_panel("panel operation failed");
		}
		size_background(&mirror_background, surface.viewport);
		surface.panel_x = Cfg::absolutepos(cfgX, surface.viewport.width,
									 mirror_panel.Width());
		surface.panel_y = Cfg::absolutepos(cfgY, surface.viewport.height,
									 mirror_panel.Height());
		mirror_panel.Merge(&mirror_background, surface.panel_x, surface.panel_y);
		surface.panel_pixmap = mirror_panel.createPixmap(Dpy, Scr, RealRoot);
		surface.background = mirror_background.createPixmap(Dpy, Scr, RealRoot);
		if (surface.panel_pixmap == None || surface.background == None) {
			logStream << APPNAME << ": could not create monitor pixmaps" << endl;
			fail_panel("panel operation failed");
		}
	}

	/* Pixmaps own the rendered pixels now; only geometry is needed for input
	 * and text drawing. Decoded buffers must not live through a user session. */
}

Panel::~Panel()
{
	if (Dpy != nullptr)
	{
		if (input_context != nullptr) {
			XDestroyIC(input_context);
			input_context = nullptr;
		}
		if (input_method != nullptr) {
			XCloseIM(input_method);
			input_method = nullptr;
		}
		Visual* visual = DefaultVisual(Dpy, Scr);
		Colormap colormap = DefaultColormap(Dpy, Scr);

		XftColorFree(Dpy, visual, colormap, &inputcolor);
	XftColorFree(Dpy, visual, colormap, &inputshadowcolor);
	XftColorFree(Dpy, visual, colormap, &welcomecolor);
	XftColorFree(Dpy, visual, colormap, &welcomeshadowcolor);
	XftColorFree(Dpy, visual, colormap, &entercolor);
	XftColorFree(Dpy, visual, colormap, &entershadowcolor);
	XftColorFree(Dpy, visual, colormap, &msgcolor);
	XftColorFree(Dpy, visual, colormap, &msgshadowcolor);
	XftColorFree(Dpy, visual, colormap, &sessioncolor);
	XftColorFree(Dpy, visual, colormap, &sessionshadowcolor);

	if (TextGC) XFreeGC(Dpy, TextGC);
	if (font) XftFontClose(Dpy, font);
	if (msgfont) XftFontClose(Dpy, msgfont);
	if (welcomefont) XftFontClose(Dpy, welcomefont);
	if (enterfont) XftFontClose(Dpy, enterfont);
	if (sessionfont) XftFontClose(Dpy, sessionfont);
	FreePixmaps();
	}

	ResetPasswd();
}

void Panel::FreePixmaps()
{
	if (PanelPixmap) XFreePixmap(Dpy, PanelPixmap);
	if (BackgroundPixmap) XFreePixmap(Dpy, BackgroundPixmap);
	PanelPixmap = BackgroundPixmap = 0;
	for (MirrorSurface& surface : mirrors) {
		if (surface.panel_pixmap) XFreePixmap(Dpy, surface.panel_pixmap);
		if (surface.background) XFreePixmap(Dpy, surface.background);
		surface.panel_pixmap = surface.background = 0;
	}
}

void Panel::ReleaseSessionGraphics()
{
	/* Never release a mapped greeter or lock screen. Keep the PAM-bound
	 * object, user/session selection, fonts and conversation state alive. */
	if (mode != Mode_DM || Win || Root)
		return;
	for (const MirrorSurface& surface : mirrors)
		if (surface.panel || surface.root) return;
	FreePixmaps();
	XSync(Dpy, False);
#ifdef __GLIBC__
	/* Image buffers are already freed, but glibc retains their heap pages.
	 * Return those pages once at session handoff, never from an idle timer. */
	malloc_trim(0);
#endif
}


/**
 * Set the (previously loaded and adjusted) background image as the window
 * background for the root window.
 */
void Panel::setBackground(void)
{
	PrepareGraphics();
	if ( Root == 0 )
	{
		Root = XCreateSimpleWindow ( Dpy, RealRoot,
						viewport.x, viewport.y, viewport.width, viewport.height, 0, 0, 0);
		if (Root == 0) {
			logStream << APPNAME << ": could not create the background window" << endl;
			fail_panel("panel operation failed");
		}
		XMapWindow(Dpy, Root);
	}
	XSetWindowBackgroundPixmap(Dpy, Root, BackgroundPixmap);
	XChangeProperty(Dpy, Root, BackgroundPixmapId, XA_PIXMAP, 32,
				PropModeReplace, (unsigned char *)&BackgroundPixmap, 1);

	XClearWindow(Dpy, Root);
	for (MirrorSurface& surface : mirrors) {
		if (surface.root == 0) {
			surface.root = XCreateSimpleWindow(Dpy, RealRoot,
				surface.viewport.x, surface.viewport.y,
				surface.viewport.width, surface.viewport.height, 0, 0, 0);
			if (surface.root == 0) {
				logStream << APPNAME << ": could not create a monitor window" << endl;
				fail_panel("panel operation failed");
			}
			XMapWindow(Dpy, surface.root);
		}
		XSetWindowBackgroundPixmap(Dpy, surface.root, surface.background);
		XClearWindow(Dpy, surface.root);
	}

	XFlush(Dpy);
}


/* Hide the cursor */
void Panel::HideCursor()
{
	if (cfg->getOption("hidecursor") == "true")
	{
		XColor	black;
		char	cursordata[1];
		Pixmap	cursorpixmap;
		Cursor	cursor;
		cursordata[0]=0;
		cursorpixmap = XCreateBitmapFromData(Dpy, RealRoot, cursordata, 1, 1);
		black.red=0;
		black.green=0;
		black.blue=0;
		cursor = cursorpixmap == None ? None :
			XCreatePixmapCursor(Dpy, cursorpixmap, cursorpixmap, &black, &black, 0, 0);
		if (cursor == None) {
			logStream << APPNAME << ": could not create the hidden cursor" << endl;
			if (cursorpixmap != None)
				XFreePixmap(Dpy, cursorpixmap);
			return;
		}
		XFreePixmap(Dpy, cursorpixmap);
		XDefineCursor(Dpy, RealRoot, cursor);
		XFreeCursor(Dpy, cursor);
	}
}


/* Open the login panel. Not used by LuxeDM Lock. */
void Panel::OpenPanel()
{
	if (Win) return;
	if (!Root || !PanelPixmap) setBackground();
	/* Create window */
	Win = XCreateSimpleWindow(Dpy, Root, X, Y,
							  panel_width,
							  panel_height,
							  0, GetColor("white"), GetColor("white"));
	if (Win == 0) {
		logStream << APPNAME << ": could not create the login window" << endl;
		fail_panel("panel operation failed");
	}

	/* Events */
	XSelectInput(Dpy, Win, ExposureMask | KeyPressMask);
	if (input_method == nullptr)
		input_method = XOpenIM(Dpy, nullptr, nullptr, nullptr);
	if (input_method != nullptr)
		input_context = XCreateIC(input_method,
			XNInputStyle, XIMPreeditNothing | XIMStatusNothing,
			XNClientWindow, Win, XNFocusWindow, Win, nullptr);
	if (input_context != nullptr)
		XSetICFocus(input_context);

	/* Set background */
	XSetWindowBackgroundPixmap(Dpy, Win, PanelPixmap);

	/* Show window */
	XMapWindow(Dpy, Win);
	XMoveWindow(Dpy, Win, X, Y); /* override wm positioning (for tests) */
	for (MirrorSurface& surface : mirrors) {
		surface.panel = XCreateSimpleWindow(Dpy, surface.root,
			surface.panel_x, surface.panel_y, panel_width, panel_height,
			0, GetColor("white"), GetColor("white"));
		if (surface.panel == 0) {
			logStream << APPNAME << ": could not create a monitor login window" << endl;
			fail_panel("panel operation failed");
		}
		XSelectInput(Dpy, surface.panel, ExposureMask);
		XSetWindowBackgroundPixmap(Dpy, surface.panel, surface.panel_pixmap);
		XMapWindow(Dpy, surface.panel);
	}

	/* Refuse to display a false sense of security if input is not captured. */
	if (XGrabKeyboard(Dpy, Win, False, GrabModeAsync, GrabModeAsync, CurrentTime)
			!= GrabSuccess) {
		logStream << APPNAME << ": could not grab the keyboard" << endl;
		fail_panel("panel operation failed");
	}

	XFlush(Dpy);
}

void Panel::ClosePanel()
{
	XUngrabKeyboard(Dpy, CurrentTime);
	if (input_context != nullptr) {
		XUnsetICFocus(input_context);
		XDestroyIC(input_context);
		input_context = nullptr;
	}
	if (Win != 0) {
		XUnmapWindow(Dpy, Win);
		XDestroyWindow(Dpy, Win);
		Win = 0;
	}
	for (MirrorSurface& surface : mirrors) {
		if (surface.panel) {
			XUnmapWindow(Dpy, surface.panel);
			XDestroyWindow(Dpy, surface.panel);
			surface.panel = 0;
		}
		if (surface.root) {
			XUnmapWindow(Dpy, surface.root);
			XDestroyWindow(Dpy, surface.root);
			surface.root = 0;
		}
	}
	if ( Root && Root != RealRoot )
	{
		XUnmapWindow(Dpy, Root);
		XDestroyWindow(Dpy, Root);
		Root = 0;
	}
	XFlush(Dpy);
}

void Panel::WrongPassword(int timeout)
{
	string message;

	if ( mode != Mode_Lock )
	{
		XClearWindow(Dpy, Root);
		for (const MirrorSurface& surface : mirrors)
			XClearWindow(Dpy, surface.root);
	}

#if 0
	if (CapsLockOn)
		message = cfg->getOption("passwd_feedback_capslock");
	else
#endif
	message = cfg->getOption("passwd_feedback_msg");

	XftTextExtents8(Dpy, msgfont, reinterpret_cast<const XftChar8*>(message.c_str()),
		message.length(), &MsgExtents);

	string cfgX = cfg->getOption("passwd_feedback_x");
	string cfgY = cfg->getOption("passwd_feedback_y");
	int msg_x = Cfg::absolutepos(cfgX, viewport.width, MsgExtents.width);
	int msg_y = Cfg::absolutepos(cfgY, viewport.height, MsgExtents.height);

	MsgExtents.x = msg_x;
	MsgExtents.y = msg_y - MsgExtents.height;

	if ( timeout > 0 )
	{
		OnExpose();
		if ( msg_x >= 0 && msg_y >= 0 )
			DrawRootText(Root, viewport, message, cfgX, cfgY);
		for (const MirrorSurface& surface : mirrors)
			DrawRootText(surface.root, surface.viewport, message, cfgX, cfgY);

		if (cfg->getOption("bell") == "1")
			XBell(Dpy, 100);
		XFlush(Dpy);
		sleep(timeout);
	}
	ResetPasswd();
	if ( mode != Mode_Lock )
	{
		if ( cfg->getIntOption("keep_user_on_fail") == 0 )
		{
			ResetName();
		}
		field = Get_Name;
	}
	OnExpose();
	// The message should stay on the screen even after the password field is
	// cleared, methinks. I don't like this solution, but it works.
	if ( msg_x >= 0 && msg_y >= 0 )
		DrawRootText(Root, viewport, message, cfgX, cfgY);
	for (const MirrorSurface& surface : mirrors)
		DrawRootText(surface.root, surface.viewport, message, cfgX, cfgY);
	XSync(Dpy, True);
}

void Panel::Message(const string& text)
{
	/* Login/Halt/Reboot close the input window before dispatching the
	 * action. Recreate the background surface so status text remains
	 * visible during the transition. */
	if (Root == 0 && mode != Mode_Lock)
		setBackground();
	const string cfgX = cfg->getOption("msg_x");
	const string cfgY = cfg->getOption("msg_y");
	DrawRootText(Root, viewport, text, cfgX, cfgY);
	for (const MirrorSurface& surface : mirrors)
		DrawRootText(surface.root, surface.viewport, text, cfgX, cfgY);
	XFlush(Dpy);
}

void Panel::DrawRootText(Window root, const Rectangle& bounds,
		const string& text, const string& x_option, const string& y_option)
{
	if (!root)
		return;
	XGlyphInfo extents;
	XftTextExtents8(Dpy, msgfont,
		reinterpret_cast<const XftChar8*>(text.c_str()), text.length(), &extents);
	const int x = Cfg::absolutepos(x_option, bounds.width, extents.width);
	const int y = Cfg::absolutepos(y_option, bounds.height, extents.height);
	if (x < 0 || y < 0)
		return;
	XftDraw *draw = XftDrawCreate(Dpy, root,
		DefaultVisual(Dpy, Scr), DefaultColormap(Dpy, Scr));
	if (draw == nullptr)
		return;
	SlimDrawString8(draw, &msgcolor, msgfont, x, y, text, &msgshadowcolor,
		cfg->getIntOption("msg_shadow_xoffset"),
		cfg->getIntOption("msg_shadow_yoffset"));
	XftDrawDestroy(draw);
}

unsigned long Panel::GetColor(const char* colorname)
{
	XColor color{};
	color.pixel = 0;

	if (colorname == NULL)
		return color.pixel;
	const Colormap colormap = DefaultColormap(Dpy, Scr);
	if(!XParseColor(Dpy, colormap, colorname, &color))
		logStream << APPNAME << ": can't parse color " << colorname << endl;
	else if(!XAllocColor(Dpy, colormap, &color))
		logStream << APPNAME << ": can't allocate color " << colorname << endl;

	return color.pixel;
}

void Panel::TextCursor(Window window, int visible)
{
	const char* text = NULL;
	int xx = 0, yy = 0, y2 = 0, cheight = 0;
	const char* txth = "Wj"; /* used to get cursor height */

	// The constructor and other conditionals guarantee that
	// if (mode == Mode_Lock)	field = Get_Passwd;

	switch(field) {
		case Get_Passwd:
			text = HiddenPasswdBuffer.c_str();
			xx = input_pass_x;
			yy = input_pass_y;
			break;

		case Get_Name:
			text = NameBuffer.c_str();
			xx = input_name_x;
			yy = input_name_y;
			break;

		default:
			return;
	}

	XGlyphInfo extents;
	XftTextExtents8(Dpy, font, (XftChar8*)txth, strlen(txth), &extents);
	cheight = extents.height;
	y2 = yy - extents.y + extents.height;
	XftTextExtents8(Dpy, font, (XftChar8*)text, strlen(text), &extents);
	xx += extents.width;

	if ( visible == SHOW )
	{
		XSetForeground(Dpy, TextGC,
			GetColor(cfg->getOption("input_color").c_str()));

		XDrawLine(Dpy, window, TextGC,
				  xx+1, yy-cheight,
				  xx+1, y2);
	}
	else
	{
		XClearArea(Dpy, window, xx+1, yy-cheight, 1, y2-(yy-cheight)+1, false);
	}
}

void Panel::EventHandler(const Panel::FieldType& curfield)
{
	/* PAM may ask again on session failure/close. Recreate a closed greeter
	 * before waiting for input; the locker retains its separate grab path. */
	if (mode == Mode_DM && !Win) OpenPanel();
	XEvent event;
	field = curfield;
	bool loop = true;

	if ( (mode != Mode_Lock) && ( MsgExtents.width == 0 ) && Win )
		OnExpose();

	struct pollfd x11_pfd{};
	x11_pfd.fd = ConnectionNumber(Dpy);
	x11_pfd.events = POLLIN;

	while (loop)
	{
		const int poll_result = XPending(Dpy) ? 1 : poll(&x11_pfd, 1, -1);
		if (poll_result < 0)
			return;
		if (poll_result > 0)
		{
			while(XPending(Dpy))
			{
				XNextEvent(Dpy, &event);
				if (event.type == KeyPress && input_context != nullptr &&
						XFilterEvent(&event, Win))
					continue;
				switch(event.type)
				{
					case Expose:
						OnExpose();
						break;

					case KeyPress:
						loop=OnKeyPress(event);
						break;
				}
			}
			if ( MsgExtents.width > 0 )
			{
				XClearWindow(Dpy, Root);
				for (const MirrorSurface& surface : mirrors)
					XClearWindow(Dpy, surface.root);
				MsgExtents.width = 0;
			}
		}
	}

	return;
}

void Panel::OnExpose(void)
{
	if (Win)
		DrawPanel(Win);
	for (const MirrorSurface& surface : mirrors) {
		if (surface.panel)
			DrawPanel(surface.panel);
	}
}

void Panel::DrawPanel(Window window)
{
	XftDraw *draw = XftDrawCreate(Dpy, window,
		DefaultVisual(Dpy, Scr), DefaultColormap(Dpy, Scr));
	if (draw == nullptr)
		return;

	XClearWindow(Dpy, window);

	if (input_pass_x != input_name_x || input_pass_y != input_name_y){
		SlimDrawString8 (draw, &inputcolor, font, input_name_x, input_name_y,
						 NameBuffer,
						 &inputshadowcolor,
						 inputShadowXOffset, inputShadowYOffset);
		SlimDrawString8 (draw, &inputcolor, font, input_pass_x, input_pass_y,
						 HiddenPasswdBuffer,
						 &inputshadowcolor,
						 inputShadowXOffset, inputShadowYOffset);
	} else { /*single input mode */
		switch(field) {
			case Get_Passwd:
				SlimDrawString8 (draw, &inputcolor, font,
								 input_pass_x, input_pass_y,
								 HiddenPasswdBuffer,
								 &inputshadowcolor,
								 inputShadowXOffset, inputShadowYOffset);
				break;
			case Get_Name:
				SlimDrawString8 (draw, &inputcolor, font,
								 input_name_x, input_name_y,
								 NameBuffer,
								 &inputshadowcolor,
								 inputShadowXOffset, inputShadowYOffset);
				break;
		}
	}

	XftDrawDestroy (draw);
	TextCursor(window, SHOW);
	ShowText(window);
}

static bool isPrintableUtf8(const char *text, size_t length)
{
	for (size_t offset = 0; offset < length;) {
		const unsigned char first = static_cast<unsigned char>(text[offset]);
		uint32_t codepoint;
		size_t width;
		if (first < 0x80) {
			codepoint = first;
			width = 1;
		} else if ((first & 0xe0) == 0xc0) {
			if (first < 0xc2)
				return false;
			codepoint = first & 0x1f;
			width = 2;
		} else if ((first & 0xf0) == 0xe0) {
			codepoint = first & 0x0f;
			width = 3;
		} else if ((first & 0xf8) == 0xf0) {
			if (first > 0xf4)
				return false;
			codepoint = first & 0x07;
			width = 4;
		} else {
			return false;
		}
		if (offset + width > length)
			return false;
		for (size_t i = 1; i < width; ++i) {
			const unsigned char continuation = static_cast<unsigned char>(text[offset + i]);
			if ((continuation & 0xc0) != 0x80)
				return false;
			codepoint = (codepoint << 6) | (continuation & 0x3f);
		}
		if ((width == 3 && first == 0xe0 &&
				static_cast<unsigned char>(text[offset + 1]) < 0xa0) ||
				(width == 3 && first == 0xed &&
				static_cast<unsigned char>(text[offset + 1]) >= 0xa0) ||
				(width == 4 && first == 0xf0 &&
				static_cast<unsigned char>(text[offset + 1]) < 0x90) ||
				(width == 4 && first == 0xf4 &&
				static_cast<unsigned char>(text[offset + 1]) > 0x8f))
			return false;
		if (codepoint < 0x20 || (codepoint >= 0x7f && codepoint <= 0x9f))
			return false;
		offset += width;
	}
	return length > 0;
}

static size_t utf8CodepointCount(const char *text, size_t length)
{
	size_t count = 0;
	for (size_t i = 0; i < length; ++i)
		if ((static_cast<unsigned char>(text[i]) & 0xc0) != 0x80)
			++count;
	return count;
}

void Panel::EraseLastChar(std::string &formerString)
{
	auto eraseLastCodepoint = [](std::string& value) {
		if (value.empty())
			return;
		size_t first = value.size() - 1;
		while (first > 0 &&
				(static_cast<unsigned char>(value[first]) & 0xc0) == 0x80)
			--first;
		value.erase(first);
	};
	switch(field) {
	case Get_Name:
		if (! NameBuffer.empty()) {
			formerString=NameBuffer;
			eraseLastCodepoint(NameBuffer);
		}
		break;

	case Get_Passwd:
		if (!PasswdBuffer.empty()) {
			formerString=HiddenPasswdBuffer;
			eraseLastCodepoint(PasswdBuffer);
			HiddenPasswdBuffer.erase(--HiddenPasswdBuffer.end());
		}
		break;
	}
}

bool Panel::OnKeyPress(XEvent& event)
{
	char text[1024] = {};
	int text_length = 0;
	KeySym keysym = NoSymbol;
	Status lookup_status = XLookupNone;
	string formerString = "";

	if (input_context != nullptr) {
		text_length = Xutf8LookupString(input_context, &event.xkey, text,
				static_cast<int>(sizeof(text) - 1), &keysym, &lookup_status);
		if (lookup_status == XBufferOverflow)
			text_length = 0;
	} else {
		XComposeStatus compstatus;
		text_length = XLookupString(&event.xkey, text,
				static_cast<int>(sizeof(text) - 1), &keysym, &compstatus);
	}
	if (text_length < 0 || text_length >= static_cast<int>(sizeof(text)))
		text_length = 0;
	switch(keysym){
		case XK_F1:
			if ( mode != Mode_Lock )	// Can't change session in a screen lock
				SwitchSession();
			return true;

		/* F11 screenshot removed: the greeter runs as root and must not
		 * write attacker-influenced files (previous screenshot_cmd wrote
		 * to /root as root via a shell). Unbound keys fall through. */

		case XK_Return:
		case XK_KP_Enter:
			if (field==Get_Name)
			{
				/* Don't allow an empty username */
				if (NameBuffer.empty())
					return true;

				if (NameBuffer==CONSOLE_STR)
					action = Console;
				else if (NameBuffer==HALT_STR)
					action = Halt;
				else if (NameBuffer==REBOOT_STR)
					action = Reboot;
				else if (NameBuffer==SUSPEND_STR)
					action = Suspend;
				else if (NameBuffer==EXIT_STR)
					action = Exit;
				else
					action = Login;
			}
			return false;

		default:
			break;
	}

	if (Win)
		TextCursor(Win, HIDE);
	for (const MirrorSurface& surface : mirrors) {
		if (surface.panel)
			TextCursor(surface.panel, HIDE);
	}
	switch(keysym){
		case XK_Delete:
		case XK_BackSpace:
			EraseLastChar(formerString);
			break;

		case XK_w:
		case XK_u:
			if (reinterpret_cast<XKeyEvent&>(event).state & ControlMask)
			{
				switch(field) {
					case Get_Passwd:
						formerString = HiddenPasswdBuffer;
						HiddenPasswdBuffer.clear();
						PasswdBuffer.clear();
						break;
					case Get_Name:
						formerString = NameBuffer;
						NameBuffer.clear();
						break;
				}
				break;
			}
			[[fallthrough]];
		case XK_h:
			if (reinterpret_cast<XKeyEvent&>(event).state & ControlMask)
			{
				EraseLastChar(formerString);
				break;
			}
			[[fallthrough]];
		default:
			if (text_length > 0 && isPrintableUtf8(text, text_length) &&
					(keysym < XK_Shift_L || keysym > XK_Hyper_R))
			{
				switch(field) {
					case Get_Name:
						formerString=NameBuffer;
						if (NameBuffer.length() + static_cast<size_t>(text_length) < INPUT_MAXLENGTH_NAME)
							NameBuffer.append(text, static_cast<size_t>(text_length));
						break;
					case Get_Passwd:
						formerString=HiddenPasswdBuffer;
						if (PasswdBuffer.length() + static_cast<size_t>(text_length) < INPUT_MAXLENGTH_PASSWD)
						{
							PasswdBuffer.append(text, static_cast<size_t>(text_length));
							HiddenPasswdBuffer.append(utf8CodepointCount(text, static_cast<size_t>(text_length)), '*');
						}
					break;
				}
			}
			else
			{	// *RP* I think this is to fix the fake bolding if the user presses TAB
				return true; //nodraw if notchange
			}
			break;
	}

	OnExpose();
	return true;
}


/* Draw welcome and "enter username" messages */
void Panel::ShowText(Window window)
{
	string cfgX, cfgY;
	XGlyphInfo extents;

	bool singleInputMode = ( input_name_x == input_pass_x
			    && input_name_y == input_pass_y );

	/// @bug this draw context is assumed relative to the panel but in lock
	///		 mode it's actually relative to the background
	XftDraw *draw = XftDrawCreate(Dpy, window,
		  DefaultVisual(Dpy, Scr), DefaultColormap(Dpy, Scr));
	if (draw == nullptr)
		return;
	/* welcome message */
	XftTextExtents8(Dpy, welcomefont, (XftChar8*)welcome_message.c_str(),
					strlen(welcome_message.c_str()), &extents);
	cfgX = cfg->getOption("welcome_x");
	cfgY = cfg->getOption("welcome_y");
	int shadowXOffset = cfg->getIntOption("welcome_shadow_xoffset");
	int shadowYOffset = cfg->getIntOption("welcome_shadow_yoffset");

	welcome_x = Cfg::absolutepos(cfgX, panel_width, extents.width);
	welcome_y = Cfg::absolutepos(cfgY, panel_height, extents.height);
	if (welcome_x >= 0 && welcome_y >= 0) {
		SlimDrawString8 (draw, &welcomecolor, welcomefont,
						 welcome_x, welcome_y,
						 welcome_message,
						 &welcomeshadowcolor, shadowXOffset, shadowYOffset);
	}

	/* Enter username-password message */
	string msg;
	if (!singleInputMode|| field == Get_Passwd) {
		msg = cfg->getOption("password_msg");
		XftTextExtents8(Dpy, enterfont, (XftChar8*)msg.c_str(),
						strlen(msg.c_str()), &extents);
		cfgX = cfg->getOption("password_x");
		cfgY = cfg->getOption("password_y");
		int shadowXOffset = cfg->getIntOption("username_shadow_xoffset");
		int shadowYOffset = cfg->getIntOption("username_shadow_yoffset");
		password_x = Cfg::absolutepos(cfgX, panel_width, extents.width);
		password_y = Cfg::absolutepos(cfgY, panel_height, extents.height);
		if (password_x >= 0 && password_y >= 0){
			SlimDrawString8 (draw, &entercolor, enterfont, password_x, password_y,
							 msg, &entershadowcolor, shadowXOffset, shadowYOffset);
		}
	}

	if ((!singleInputMode|| field == Get_Name) && mode != Mode_Lock ) {
		msg = cfg->getOption("username_msg");
		XftTextExtents8(Dpy, enterfont, (XftChar8*)msg.c_str(),
						strlen(msg.c_str()), &extents);
		cfgX = cfg->getOption("username_x");
		cfgY = cfg->getOption("username_y");
		int shadowXOffset = cfg->getIntOption("username_shadow_xoffset");
		int shadowYOffset = cfg->getIntOption("username_shadow_yoffset");
		username_x = Cfg::absolutepos(cfgX, panel_width, extents.width);
		username_y = Cfg::absolutepos(cfgY, panel_height, extents.height);
		if (username_x >= 0 && username_y >= 0){
			SlimDrawString8 (draw, &entercolor, enterfont, username_x, username_y,
							 msg, &entershadowcolor, shadowXOffset, shadowYOffset);
		}
	}
	XftDrawDestroy(draw);

	if ( singleInputMode && (mode == Mode_Lock) )
	{
		// If only the password box is visible, draw the user name somewhere too
		int show_username = cfg->getIntOption("show_username");
		if (show_username)
		{
			string user_msg = "User: " + GetName();
			Message(user_msg);
		}
	}
}


string Panel::getSession()
{
	return session_exec;
}

string Panel::getSessionId() const
{
	return session_id;
}

string Panel::getSessionDesktop() const
{
	return session_desktop;
}


/* choose next available session type. Not used in lock mode */
void Panel::SwitchSession()
{
	SessionChoice ses = cfg->nextSession();
	session_name = ses.name;
	session_exec = ses.exec;
	session_id = ses.id;
	session_desktop = ses.desktop;
	if (session_name.size() > 0) {
		ShowSession();
	}
 }


/* Display session type on the screen. Not used in lock mode */
void Panel::ShowSession()
{
	/* Automatic login selects its session before creating greeter windows.
	 * Keep that selection without issuing drawing requests for window None. */
	if (!Root)
		return;
	XClearWindow(Dpy, Root);
	for (const MirrorSurface& surface : mirrors)
		if (surface.root) XClearWindow(Dpy, surface.root);
	string currsession = cfg->getOption("session_msg") + " " + session_name;
	XGlyphInfo extents;

	if (sessionfont)
		XftFontClose(Dpy, sessionfont);
	sessionfont = XftFontOpenName(Dpy, Scr, cfg->getOption("session_font").c_str());
	if (sessionfont == nullptr) {
		logStream << APPNAME << ": could not load the session font" << endl;
		return;
	}

	XftTextExtents8(Dpy, sessionfont, reinterpret_cast<const XftChar8*>(currsession.c_str()),
					currsession.length(), &extents);
	const string msg_x = cfg->getOption("session_x");
	const string msg_y = cfg->getOption("session_y");
	int shadowXOffset = cfg->getIntOption("session_shadow_xoffset");
	int shadowYOffset = cfg->getIntOption("session_shadow_yoffset");
	auto draw_session = [&](Window root, const Rectangle& bounds) {
		if (!root) return;
		const int x = Cfg::absolutepos(msg_x, bounds.width, extents.width);
		const int y = Cfg::absolutepos(msg_y, bounds.height, extents.height);
		XftDraw *draw = XftDrawCreate(Dpy, root,
			DefaultVisual(Dpy, Scr), DefaultColormap(Dpy, Scr));
		if (draw == nullptr)
			return;
		SlimDrawString8(draw, &sessioncolor, sessionfont, x, y, currsession,
			&sessionshadowcolor, shadowXOffset, shadowYOffset);
		XftDrawDestroy(draw);
	};
	draw_session(Root, viewport);
	for (const MirrorSurface& surface : mirrors)
		draw_session(surface.root, surface.viewport);
	XFlush(Dpy);
}


void Panel::SlimDrawString8(XftDraw *d, XftColor *color, XftFont *font,
							int x, int y, const string& str,
							XftColor* shadowColor,
							int xOffset, int yOffset)
{
	if (d == nullptr || color == nullptr || font == nullptr || shadowColor == nullptr)
		return;
	if (xOffset && yOffset) {
		XftDrawStringUtf8(d, shadowColor, font, x+xOffset, y+yOffset,
			reinterpret_cast<const FcChar8*>(str.c_str()), str.length());
	}

	XftDrawStringUtf8(d, color, font, x, y,
		reinterpret_cast<const FcChar8*>(str.c_str()), str.length());
}


Panel::ActionType Panel::getAction(void) const
{
	return action;
}

void Panel::Reset(void)
{
	ResetName();
	ResetPasswd();
}

void Panel::ResetName(void)
{
	NameBuffer.clear();
}

void Panel::ResetPasswd(void)
{
	fill(PasswdBuffer.begin(), PasswdBuffer.end(), '\0');
	PasswdBuffer.clear();
	HiddenPasswdBuffer.clear();
}


/* Pre-load the user name input box with the provided string.
 * Used to set the default user, if so configured, and by LuxeDM Lock to set the
 * currently logged-in user.
 */
void Panel::SetName(const string& name)
{
	NameBuffer = name.substr(0, INPUT_MAXLENGTH_NAME - 1);
	action = Login;
}

const string& Panel::GetName(void) const
{
	return NameBuffer;
}

const string& Panel::GetPasswd(void) const
{
	return PasswdBuffer;
}


/** Return active RandR viewports with the primary output first. Outputs that
 * share the same CRTC rectangle are clones and need only one surface. */
vector<Rectangle> Panel::GetMonitorViewports()
{
	Rectangle fallback;
	fallback.x = 0;
	fallback.y = 0;
	fallback.width = static_cast<unsigned int>(std::max(0, DisplayWidth(Dpy, Scr)));
	fallback.height = static_cast<unsigned int>(std::max(0, DisplayHeight(Dpy, Scr)));

	XRRScreenResources *resources = XRRGetScreenResourcesCurrent(Dpy, RealRoot);
	if (!resources)
	{
		cerr << "XRRGetScreenResources failed\n";
		return {fallback};
	}
	const RROutput primary = XRRGetOutputPrimary(Dpy, RealRoot);
	vector<Rectangle> primary_viewport;
	vector<Rectangle> remaining;
	for (int index = 0; index < resources->noutput; ++index) {
		const RROutput output = resources->outputs[index];
		XRROutputInfo *output_info = XRRGetOutputInfo(Dpy, resources, output);
		if (!output_info)
			continue;
		if (output_info->connection != RR_Connected || output_info->crtc == None) {
			XRRFreeOutputInfo(output_info);
			continue;
		}
		XRRCrtcInfo *crtc = XRRGetCrtcInfo(Dpy, resources, output_info->crtc);
		XRRFreeOutputInfo(output_info);
		if (!crtc || crtc->width <= 0 || crtc->height <= 0) {
			if (crtc)
				XRRFreeCrtcInfo(crtc);
			continue;
		}
		Rectangle candidate(crtc->x, crtc->y,
			static_cast<unsigned int>(crtc->width),
			static_cast<unsigned int>(crtc->height));
		XRRFreeCrtcInfo(crtc);
		if (candidate.is_empty())
			continue;
		auto same_rectangle = [&](const Rectangle& existing) {
			return existing.x == candidate.x && existing.y == candidate.y &&
				existing.width == candidate.width && existing.height == candidate.height;
		};
		if (any_of(primary_viewport.begin(), primary_viewport.end(), same_rectangle) ||
			any_of(remaining.begin(), remaining.end(), same_rectangle))
			continue;
		if (output == primary)
			primary_viewport.push_back(candidate);
		else
			remaining.push_back(candidate);
	}
	XRRFreeScreenResources(resources);
	if (primary_viewport.empty() && remaining.empty())
		return {fallback};
	if (primary_viewport.empty()) {
		primary_viewport.push_back(remaining.front());
		remaining.erase(remaining.begin());
	}
	primary_viewport.insert(primary_viewport.end(), remaining.begin(), remaining.end());
	return primary_viewport;
}
