/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-3.0-or-later */

#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <gdk/x11/gdkx.h>
#include <X11/extensions/Xfixes.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <cairo-xlib.h>
#include <slop.hpp>
#include <math.h>

#include "screenshot-logic.h"

/* GTK4 removed gdk_screen_get_active_window, gdk_pixbuf_get_from_window and
 * the GdkWindow capture API.  All window lookup and capture therefore goes
 * through Xlib directly, mirroring the root-capture pattern in
 * luxesession-logout. */

/* Xlib errors (dead or stale windows) must not terminate the application. */
int
ignore_xlib_errors (Display * dpy, XErrorEvent * event)
{
	(void) dpy;
	(void) event;
	return 0;
}

static unsigned long
window_pid (Display * dpy, Window win)
{
	Atom actual;
	int format;
	unsigned long count, remaining;
	unsigned char *data = NULL;
	unsigned long pid = 0;

	if (XGetWindowProperty (dpy, win,
	                        XInternAtom (dpy, "_NET_WM_PID", False),
	                        0, 1, False, XA_CARDINAL,
	                        &actual, &format, &count, &remaining, &data) == Success
	    && actual == XA_CARDINAL && format == 32 && count == 1)
		pid = ((unsigned long *) data)[0];

	if (data != NULL)
		XFree (data);
	return pid;
}

static gboolean
is_desktop_window (Display * dpy, Window win)
{
	Atom type, actual;
	int format;
	unsigned long count, remaining;
	unsigned char *data = NULL;
	gboolean desktop = FALSE;
	Atom desktop_type;

	type = XInternAtom (dpy, "_NET_WM_WINDOW_TYPE", False);
	desktop_type = XInternAtom (dpy, "_NET_WM_WINDOW_TYPE_DESKTOP", False);
	if (XGetWindowProperty (dpy, win, type, 0, 32, False, XA_ATOM,
	                        &actual, &format, &count, &remaining, &data) == Success
	    && actual == XA_ATOM && format == 32 && count > 0)
	{
		unsigned long i;
		for (i = 0; i < count; i++)
		{
			if (((unsigned long *) data)[i] == (unsigned long) desktop_type)
				desktop = TRUE;
		}
	}

	if (data != NULL)
		XFree (data);
	return desktop;
}

static Window
find_client_window (Display *dpy, Window window, guint depth)
{
	Atom actual = None;
	int format;
	unsigned long count, remaining;
	unsigned char *data = NULL;
	Atom state = XInternAtom (dpy, "WM_STATE", False);
	XGetWindowProperty (dpy, window, state, 0, 0, False, AnyPropertyType,
	                    &actual, &format, &count, &remaining, &data);
	if (data) XFree (data);
	if (actual == state) return window;
	if (depth == 0) return None;
	Window root, parent, *children = NULL, found = None;
	unsigned int nchildren;
	if (XQueryTree (dpy, window, &root, &parent, &children, &nchildren)) {
		for (guint i = 0; i < MIN (nchildren, 16u) && found == None; i++)
			found = find_client_window (dpy, children[i], depth - 1);
		if (children) XFree (children);
	}
	return found;
}

static Window
find_active_window (Display * dpy, Window root)
{
	Atom actual;
	int format;
	unsigned long count, remaining;
	unsigned char *data = NULL;
	Window win = None;

	// Try to find an active window through EWMH _NET_ACTIVE_WINDOW
	if (XGetWindowProperty (dpy, root,
	                        XInternAtom (dpy, "_NET_ACTIVE_WINDOW", False),
	                        0, 1, False, XA_WINDOW,
	                        &actual, &format, &count, &remaining, &data) == Success
	    && actual == XA_WINDOW && format == 32 && count == 1)
	{
		win = (Window) ((unsigned long *) data)[0];
		// Never capture our own (already hidden) screenshot window
		if (win != None && window_pid (dpy, win) == (unsigned long) getpid ())
			win = None;
	}
	if (data != NULL)
		XFree (data);

	// If no active window was found, try to get the window under the cursor
	if (win == None)
	{
		Window child = None;
		int rx, ry, wx, wy;
		unsigned int mask;

		if (XQueryPointer (dpy, root, &root, &child, &rx, &ry, &wx, &wy, &mask)) {
			Window client = child == None ? None : find_client_window (dpy, child, 2);
			win = client != None ? client : child;
		}
	}

	if (win == None || win == root || is_desktop_window (dpy, win))
	{
		// If no window was found or the found window is desktop, capture the whole screen
		return root;
	}

	/* EWMH identifies the client, not its reparenting frame. Decorations
	 * are added separately from this client's _NET_FRAME_EXTENTS. */
	return win;
}

static GdkRectangle
get_region_area (gboolean *cancelled)
{
	GdkRectangle area = { 0 };
	struct slop_options options;
	struct slop_selection selection;

	// Set slop options
	options = slop_options_default ();
	options.noopengl = 1;
	options.border = 4;
	options.r = 0.8;
	options.g = 0.2;
	options.b = 0.2;

	// Start slop selection
	selection = slop_select (&options);
	*cancelled = selection.cancelled;
	g_debug ("Selected region: %.0f,%.0f %.0fx%.0f; cancelled=%d",
	         selection.x, selection.y, selection.w, selection.h, selection.cancelled);

	if (selection.cancelled)
		return area;
	/* slop reports floats; reject invalid or out-of-range geometry before
	 * converting to int, including non-finite values. */
	if (!isfinite (selection.x) || !isfinite (selection.y) ||
	    !isfinite (selection.w) || !isfinite (selection.h) ||
	    (double)selection.x < G_MININT || (double)selection.x > G_MAXINT ||
	    (double)selection.y < G_MININT || (double)selection.y > G_MAXINT ||
	    (double)selection.w < G_MININT || (double)selection.w > G_MAXINT ||
	    (double)selection.h < G_MININT || (double)selection.h > G_MAXINT)
		return area;
	// Convert slop selection to rectangle
	area.x = (int) selection.x;
	area.y = (int) selection.y;
	area.width = (int) selection.w;
	area.height = (int) selection.h;

	return area;
}

/* Bound the captured pixel count before any Cairo/pixbuf allocation. A
 * 32-Mi-pixel image accommodates one 8K UHD display; encoding and backend
 * intermediates may require additional memory beyond the four-byte estimate. */
#define SHOTTY_MAX_CAPTURE_PIXELS (32ULL * 1024 * 1024)
static gboolean
capture_size_allowed (gint width, gint height, GError **error)
{
	if (width <= 0 || height <= 0) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED,
		                     _("The selected capture area is unavailable."));
		return FALSE;
	}
	if ((guint64) width * (guint64) height > SHOTTY_MAX_CAPTURE_PIXELS) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NO_SPACE,
		                     _("The capture is too large. Choose a smaller region."));
		return FALSE;
	}
	return TRUE;
}

static void
get_window_geometry (Display    * dpy,
                     Window       win,
                     gboolean     include_border,
                     GdkRectangle * area)
{
	Window root, translated;
	int x, y;
	unsigned int w, h, depth, border;
	Atom actual;
	int format;
	unsigned long count, remaining;
	unsigned char *data = NULL;

	// Absolute position and size of the client window
	if (! XGetGeometry (dpy, win, &root, &x, &y, &w, &h, &border, &depth))
	{
		area->x = area->y = 0;
		area->width = area->height = 0;
		return;
	}
	if (! XTranslateCoordinates (dpy, win, root, 0, 0, &x, &y, &translated))
	{
		area->x = area->y = 0;
		area->width = area->height = 0;
		return;
	}
	area->x = x;
	area->y = y;
	area->width = w;
	area->height = h;

	if (include_border)
	{
		// Grow the bounding box by the frame extents reported by the window manager
		if (XGetWindowProperty (dpy, win,
		                        XInternAtom (dpy, "_NET_FRAME_EXTENTS", False),
		                        0, 4, False, XA_CARDINAL,
		                        &actual, &format, &count, &remaining, &data) == Success
		    && actual == XA_CARDINAL && format == 32 && count == 4)
		{
			unsigned long * extents = (unsigned long *) data;
			/* X11 geometry is 16-bit; larger extents cannot describe a
			 * real frame and must not overflow the capture rectangle. */
			if (extents[0] <= 65535 && extents[1] <= 65535 &&
			    extents[2] <= 65535 && extents[3] <= 65535) {
				area->x -= (int) extents[0];
				area->y -= (int) extents[2];
				area->width += (int) (extents[0] + extents[1]);
				area->height += (int) (extents[2] + extents[3]);
			}
		}
		if (data != NULL)
			XFree (data);
	}
}

static GdkRectangle
get_root_size (Display * dpy, Window root)
{
	GdkRectangle size;
	Window root_ret;
	int x, y;
	unsigned int w = 0, h = 0, depth, border;

	XGetGeometry (dpy, root, &root_ret, &x, &y, &w, &h, &border, &depth);
	size.x = 0;
	size.y = 0;
	size.width = w;
	size.height = h;
	return size;
}

static void
clamp_to_screen (GdkRectangle * area, GdkRectangle screen)
{
	gint64 right = MIN ((gint64) area->x + area->width, screen.width);
	gint64 bottom = MIN ((gint64) area->y + area->height, screen.height);
	area->x = CLAMP (area->x, 0, screen.width);
	area->y = CLAMP (area->y, 0, screen.height);
	area->width = MAX (0, right - area->x);
	area->height = MAX (0, bottom - area->y);
}

static void
composite_cursor (GdkPixbuf *image, const XFixesCursorImage *cursor, GdkRectangle area)
{
	gint x = cursor->x - cursor->xhot - area.x;
	gint y = cursor->y - cursor->yhot - area.y;
	gint left = MAX (0, x), top = MAX (0, y);
	gint right = MIN (gdk_pixbuf_get_width (image), x + cursor->width);
	gint bottom = MIN (gdk_pixbuf_get_height (image), y + cursor->height);
	for (gint py = top; py < bottom; py++)
		for (gint px = left; px < right; px++) {
			gulong pixel = cursor->pixels[(py - y) * cursor->width + (px - x)];
			guint alpha = (pixel >> 24) & 0xff;
			guchar *p = gdk_pixbuf_get_pixels (image)
				+ py * gdk_pixbuf_get_rowstride (image)
				+ px * gdk_pixbuf_get_n_channels (image);
			/* XFixes cursor RGB is already premultiplied by alpha. */
			for (gint channel = 0; channel < 3; channel++) {
				guint color = (pixel >> (16 - channel * 8)) & 0xff;
				p[channel] = MIN (255u, color + p[channel] * (255 - alpha) / 255);
			}
		}
}

GdkPixbuf *
screenshot (gint     area_index,
            gboolean include_cursor,
            gboolean include_decorations,
            GError **error)
{
	GdkDisplay *display;
	Display *dpy;
	Window root;
	GdkPixbuf *screenshot_pixbuf;
	GdkRectangle area = { 0 };
	GdkRectangle screen;
	cairo_surface_t *surface;
	gboolean cancelled = FALSE;

	display = gdk_display_get_default ();
	if (!GDK_IS_X11_DISPLAY (display)) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
		                     _("Screen capture requires an X11 display."));
		return NULL;
	}
	if (area_index < AREA_SCREEN || area_index > AREA_REGION) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		                     _("The selected capture mode is invalid."));
		return NULL;
	}
	/* GDK 4.18 deprecated the X11 interop and pixbuf capture without
	 * replacements; the LuxeSession logout background uses the same calls.
	 * Xlib errors from stale windows must not terminate the application. */
	XErrorHandler old_handler = XSetErrorHandler (ignore_xlib_errors);
	G_GNUC_BEGIN_IGNORE_DEPRECATIONS
	dpy = GDK_DISPLAY_XDISPLAY (display);
	root = DefaultRootWindow (dpy);
	screen = get_root_size (dpy, root);

	if (area_index == AREA_SCREEN)
	{
		area = screen;
	}

	if (area_index == AREA_WINDOW)
	{
		get_window_geometry (dpy, find_active_window (dpy, root),
		                     include_decorations, &area);
		clamp_to_screen (&area, screen);
	}

	if (area_index == AREA_REGION)
	{
		area = get_region_area (&cancelled);
		clamp_to_screen (&area, screen);
	}

	if (area.x < 0 || area.y < 0 || area.width < 1 || area.height < 1
	    || (gint64)area.x + area.width > screen.width || (gint64)area.y + area.height > screen.height)
	{
		XSync (dpy, False);
		XSetErrorHandler (old_handler);
		if (!cancelled)
			g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED,
			                     _("The selected capture area is unavailable."));
		return NULL;
	}
	if (!capture_size_allowed (area.width, area.height, error)) {
		XSync (dpy, False);
		XSetErrorHandler (old_handler);
		return NULL;
	}

	// Capture the root window through a Cairo Xlib surface
	surface = cairo_xlib_surface_create (dpy, root,
	                                     DefaultVisual (dpy, DefaultScreen (dpy)),
	                                     screen.width, screen.height);
	screenshot_pixbuf = gdk_pixbuf_get_from_surface (surface,
	                                                 area.x, area.y,
	                                                 area.width, area.height);
	cairo_surface_destroy (surface);
	XSync (dpy, False);
	XSetErrorHandler (old_handler);
	G_GNUC_END_IGNORE_DEPRECATIONS
	if (!screenshot_pixbuf) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED,
		                     _("The display could not be captured. Try again."));
		return NULL;
	}

	if (include_cursor && area_index != AREA_REGION && screenshot_pixbuf)
	{
		/* Draw the current cursor image (shape + position) on the capture.
		 * Region captures exclude the cursor because slop already owns the
		 * pointer during selection. */
		Display *dpy2;
		XFixesCursorImage *cursor;
		G_GNUC_BEGIN_IGNORE_DEPRECATIONS
		dpy2 = gdk_x11_display_get_xdisplay (gdk_display_get_default ());
		cursor = XFixesGetCursorImage (dpy2);
		G_GNUC_END_IGNORE_DEPRECATIONS
		if (cursor)
		{
			composite_cursor (screenshot_pixbuf, cursor, area);
			XFree (cursor);
		}
	}

	return screenshot_pixbuf;
}
