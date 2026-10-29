/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Raw-Xlib XEmbed tray client.  The LuxePanel system tray is the legacy
 * _NET_SYSTEM_TRAY protocol, and GTK4 has no GtkPlug, so LuxUp owns a small
 * X window on a private display connection and reparents itself into the
 * panel's tray frame.  The icon is rendered with cairo. */
#include "tray.h"

#ifndef LUXUP_HEADLESS_TRAY
#include <gtk/gtk.h>
#include "luxe-gtk-ui.h"
#include <gdk-pixbuf/gdk-pixbuf.h>
#else
#include <gio/gio.h>
#include <math.h>
typedef struct {double red,green,blue,alpha;} GdkRGBA;
#endif
#include <cairo.h>
#include <cairo-xlib.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <string.h>
#include <stdio.h>

#define LUXUP_TRAY_DEFAULT_SIZE 24
#define LUXUP_XEMBED_MAPPED 1

struct _LuxUpTray {
    Display *dpy;
    int screen;
    Window root;
    Window window;
    Window manager;
    Visual *visual;
    Colormap colormap;
    int depth;
    gboolean argb;
    int width;
    int height;
    cairo_surface_t *surface;
    cairo_t *cr;
    LuxUpTrayState state;
    char *tooltip;
    guint dock_timeout;
    guint dock_attempts;
    LuxUpTrayActivateFunc activate;
    gpointer user_data;
    GSource *source;
    gulong theme_changed;
    gulong mode_changed;
    gulong gtk_theme_changed;
#ifdef LUXUP_HEADLESS_TRAY
    GSettings *settings;
#endif
};

typedef struct {
    GSource source;
    GPollFD fd;
    LuxUpTray *tray;
} TraySource;

static Display *trapped_display;
static XErrorHandler saved_handler;
static guint trap_depth;

static int tray_ignore_error(Display *dpy, XErrorEvent *event)
{
    if (dpy != trapped_display && saved_handler) return saved_handler(dpy, event);
    return 0;
}

/* Scoped X error trap (cf. shotty's screenshot path).  X errors are
 * delivered asynchronously, so each window of foreign-window requests
 * installs the ignore handler, syncs the private connection to drain any
 * error those requests produced, then restores the previous handler (GDK's
 * own). Xlib's handler is process-global: preserve unrelated displays'
 * handling and use this nested scope only on the GTK thread. */
static void tray_trap_push(LuxUpTray *tray)
{
    if (trap_depth++ == 0) {
        trapped_display = tray->dpy;
        saved_handler = XSetErrorHandler(tray_ignore_error);
    }
}

static void tray_trap_pop(LuxUpTray *tray)
{
    XSync(tray->dpy, False);
    if (--trap_depth == 0) {
        XSetErrorHandler(saved_handler);
        trapped_display = NULL;
    }
}

static Atom atom(Display *dpy, const char *name)
{
    return XInternAtom(dpy, name, False);
}

static void tray_set_xembed_info(LuxUpTray *tray)
{
    tray_trap_push(tray);
    long info[2] = {0, LUXUP_XEMBED_MAPPED};
    XChangeProperty(tray->dpy, tray->window, atom(tray->dpy, "_XEMBED_INFO"),
                    atom(tray->dpy, "_XEMBED_INFO"), 32, PropModeReplace,
                    (unsigned char *) info, 2);
    /* LuxePanel leaves edge-aware clients full-size: this client already
     * paints a matching panel edge, so insetting it would double the line. */
    unsigned long edge_aware = 1;
    XChangeProperty(tray->dpy, tray->window,
                    atom(tray->dpy, "_LUXE_TRAY_EDGE_AWARE"), XA_CARDINAL, 32,
                    PropModeReplace, (unsigned char *)&edge_aware, 1);
    tray_trap_pop(tray);
}

static gboolean tray_embedded(LuxUpTray *tray)
{
    tray_trap_push(tray);
    Window root_return, parent_return;
    Window *children = NULL;
    unsigned int n_children = 0;
    gboolean ok = XQueryTree(tray->dpy, tray->window, &root_return, &parent_return,
                             &children, &n_children);
    if (children)
        XFree(children);
    gboolean embedded = ok && parent_return != tray->root;
    tray_trap_pop(tray);
    return embedded;
}

static void tray_request_dock(LuxUpTray *tray)
{
    tray_trap_push(tray);
    char selection_name[64];
    g_snprintf(selection_name, sizeof selection_name, "_NET_SYSTEM_TRAY_S%d", tray->screen);
    Window owner = XGetSelectionOwner(tray->dpy, atom(tray->dpy, selection_name));
    if (owner == None) {
        tray_trap_pop(tray);
        return;
    }
    tray->manager = owner;
    XSelectInput(tray->dpy, owner, StructureNotifyMask);

    XClientMessageEvent event;
    memset(&event, 0, sizeof event);
    event.type = ClientMessage;
    event.window = owner;
    event.message_type = atom(tray->dpy, "_NET_SYSTEM_TRAY_OPCODE");
    event.format = 32;
    event.data.l[0] = CurrentTime;
    event.data.l[1] = 0; /* SYSTEM_TRAY_REQUEST_DOCK */
    event.data.l[2] = (long) tray->window;
    XSendEvent(tray->dpy, owner, False, NoEventMask, (XEvent *) &event);
    XFlush(tray->dpy);
    tray_trap_pop(tray);
}

/* Bounded re-dock backoff: a short burst of retries with increasing
 * intervals, then stop.  Re-armed on Reparent/Unmap/Destroy/MANAGER events
 * below so a panel restart still docks without polling forever (~28.8k
 * wakeups/day with the old unbounded 3 s timer). */
#define LUXUP_DOCK_MAX_ATTEMPTS 8

static guint tray_dock_interval(guint attempt)
{
    static const guint intervals[] = {2, 4, 8, 15, 30, 60, 120, 300};
    if (attempt >= LUXUP_DOCK_MAX_ATTEMPTS)
        return 0;
    return intervals[attempt];
}

static gboolean tray_ensure_docked(gpointer data);

static void tray_schedule_dock(LuxUpTray *tray, guint interval_secs)
{
    if (tray->dock_timeout)
        g_source_remove(tray->dock_timeout);
    tray->dock_timeout = 0;
    if (interval_secs == 0)
        return;
    tray->dock_timeout = g_timeout_add_seconds(interval_secs, tray_ensure_docked, tray);
}

/* Reset the backoff and retry docking (e.g. panel restarted). */
void luxup_tray_redock(LuxUpTray *tray)
{
    if (!tray)
        return;
    tray->dock_attempts = 0;
    tray_request_dock(tray);
    if (tray_embedded(tray))
        return;
    tray_schedule_dock(tray, tray_dock_interval(0));
}

/* Re-dock after a panel restart and if the original embed was released. */
static gboolean tray_ensure_docked(gpointer data)
{
    LuxUpTray *tray = data;
    tray->dock_timeout = 0;
    char selection_name[64];
    g_snprintf(selection_name, sizeof selection_name, "_NET_SYSTEM_TRAY_S%d", tray->screen);
    Window owner = XGetSelectionOwner(tray->dpy, atom(tray->dpy, selection_name));
    if (owner != None) {
        if (owner != tray->manager || !tray_embedded(tray))
            tray_request_dock(tray);
        /* Docked only once the embed actually landed; events re-arm us. */
        if (tray_embedded(tray))
            return G_SOURCE_REMOVE;
    }
    /* Not docked (or no owner yet): back off, then stop. */
    if (tray->dock_attempts + 1 >= LUXUP_DOCK_MAX_ATTEMPTS)
        return G_SOURCE_REMOVE;
    tray->dock_attempts++;
    tray_schedule_dock(tray, tray_dock_interval(tray->dock_attempts));
    return G_SOURCE_REMOVE;
}

/* Icon themes draw panel/status glyphs on a pixel grid (16, 22, 32, ...);
 * scaling a 22/24 px drawing up to "tile minus 2" makes it look larger and
 * softer than neighbouring tray icons. Use the largest grid size that fits
 * inside a one-pixel inset, or the inset itself on very small panels. */
static int tray_icon_size(int width, int height)
{
    static const int grid[] = {128, 96, 64, 48, 32, 22, 16};
    int available = MAX(1, MIN(width, height) - 2);
    for (guint i = 0; i < G_N_ELEMENTS(grid); i++)
        if (available >= grid[i])
            return grid[i];
    return available;
}

/* GTK 4.18 has no gtk_icon_theme_load_icon; render the paintable through a
 * snapshot onto the tray's cairo surface instead. */
static gboolean tray_draw_icon(cairo_t *cr, LuxUpTray *tray)
{
#ifdef LUXUP_HEADLESS_TRAY
    int size=tray_icon_size(tray->width,tray->height);
    cairo_save(cr);
    cairo_translate(cr,(tray->width-size)/2.0,(tray->height-size)/2.0);
    cairo_scale(cr,size/24.0,size/24.0);
    cairo_set_line_width(cr,2);
    cairo_set_line_cap(cr,CAIRO_LINE_CAP_ROUND);
    if(tray->state==LUXUP_TRAY_UPDATES) cairo_set_source_rgb(cr,.25,.65,1);
    else if(tray->state==LUXUP_TRAY_ERROR) cairo_set_source_rgb(cr,1,.35,.25);
    else cairo_set_source_rgb(cr,.65,.68,.72);
    cairo_arc(cr,12,12,8,.5,5.7); cairo_stroke(cr);
    cairo_move_to(cr,18,3); cairo_line_to(cr,20,8); cairo_line_to(cr,15,8); cairo_stroke(cr);
    if(tray->state==LUXUP_TRAY_UPDATES || tray->state==LUXUP_TRAY_UNKNOWN || tray->state==LUXUP_TRAY_ERROR) {
        cairo_move_to(cr,12,7); cairo_line_to(cr,12,13); cairo_stroke(cr);
        cairo_arc(cr,12,17,1,0,2*G_PI); cairo_fill(cr);
    } else if(tray->state==LUXUP_TRAY_UPTODATE) {
        cairo_move_to(cr,8,12); cairo_line_to(cr,11,15); cairo_line_to(cr,16,9); cairo_stroke(cr);
    }
    cairo_restore(cr); return TRUE;
#else
    static const char *updates[] = {"software-update-available", "system-software-update", "software-update-urgent", NULL};
    static const char *checking[] = {"emblem-synchronizing-symbolic", "view-refresh-symbolic", "view-refresh", NULL};
    static const char *error[] = {"dialog-warning", "dialog-error", NULL};
    static const char *unknown[] = {"dialog-question", "help-about", "dialog-warning", NULL};
    static const char *uptodate[] = {"emblem-ok-symbolic", "object-select-symbolic", "software-update-available", NULL};
    const char **names;
    switch (tray->state) {
        case LUXUP_TRAY_UPDATES: names = updates; break;
        case LUXUP_TRAY_CHECKING: names = checking; break;
        case LUXUP_TRAY_ERROR: names = error; break;
        case LUXUP_TRAY_UNKNOWN: names = unknown; break;
        case LUXUP_TRAY_UPTODATE:
        default: names = uptodate; break;
    }
    GtkIconTheme *theme = gtk_icon_theme_get_for_display(gdk_display_get_default());
    /* The XEmbed window/click target stays full-size; only the glyph shrinks. */
    int size = tray_icon_size(tray->width, tray->height);
    for (int i = 0; names[i]; ++i) {
        if (!gtk_icon_theme_has_icon(theme, names[i]))
            continue;
        GtkIconPaintable *icon = gtk_icon_theme_lookup_icon(theme, names[i], NULL, size, 1, GTK_TEXT_DIR_NONE, 0);
        if (!icon)
            continue;
        GtkSnapshot *snapshot = gtk_snapshot_new();
        gboolean dark = luxe_style_get_dark(luxe_style_settings());
        GdkRGBA colors[] = { dark ? (GdkRGBA){.red=.9,.green=.9,.blue=.9,.alpha=1} :
                                   (GdkRGBA){.red=.15,.green=.15,.blue=.15,.alpha=1},
                            {.red=.2,.green=.7,.blue=.3,.alpha=1},
                            {.red=1,.green=.7,.blue=.1,.alpha=1},
                            {.red=.9,.green=.2,.blue=.2,.alpha=1} };
        gtk_symbolic_paintable_snapshot_symbolic(GTK_SYMBOLIC_PAINTABLE(icon),
            GDK_SNAPSHOT(snapshot), size, size, colors, G_N_ELEMENTS(colors));
        GskRenderNode *node = gtk_snapshot_free_to_node(snapshot);
        g_object_unref(icon);
        if (!node)
            continue;
        cairo_save(cr);
        cairo_translate(cr, (tray->width - size) / 2.0, (tray->height - size) / 2.0);
        gsk_render_node_draw(node, cr);
        cairo_restore(cr);
        gsk_render_node_unref(node);
        return TRUE;
    }
    return FALSE;
#endif
}

/* Match LuxePanel's selected background and its 22%-foreground inset edge.
 * The tray icon is a child window inside the panel's XEmbed slot, and
 * compositors only blend top-level windows: a transparent child comes out
 * black with or without one, so always paint the panel colours. */
static void tray_panel_colors(GdkRGBA *background, GdkRGBA *outline)
{
#ifdef LUXUP_HEADLESS_TRAY
    g_autofree char *theme=NULL,*scheme=NULL;
    GSettingsSchemaSource *source=g_settings_schema_source_get_default();
    g_autoptr(GSettingsSchema) schema=source?g_settings_schema_source_lookup(source,"org.gnome.desktop.interface",TRUE):NULL;
    if(schema) {
        g_autoptr(GSettings) settings=g_settings_new_full(schema,NULL,NULL);
        theme=g_settings_get_string(settings,"gtk-theme"); scheme=g_settings_get_string(settings,"color-scheme");
    }
    gboolean dark=g_strcmp0(scheme,"prefer-dark")==0 || (theme && g_str_has_suffix(theme,"dark"));
    *background=dark?(GdkRGBA){.19,.19,.19,1}:(GdkRGBA){.965,.965,.965,1};
    GdkRGBA foreground=dark?(GdkRGBA){.95,.95,.95,1}:(GdkRGBA){.15,.15,.15,1};
    if(theme && g_str_has_prefix(theme,"Luxe-") && !strchr(theme+5,'/')) {
        g_autofree char *path=g_build_filename(LUXUP_THEME_DIR,theme+5,"metadata.ini",NULL);
        g_autoptr(GKeyFile) colors=g_key_file_new();
        if(g_key_file_load_from_file(colors,path,0,NULL)) {
            const char *keys[]={"bg","fg"}; GdkRGBA *values[]={background,&foreground};
            for(guint i=0;i<2;i++) {
                g_autofree char *hex=g_key_file_get_string(colors,"Theme",keys[i],NULL);
                unsigned int rgb;
                if(hex && strlen(hex)==7 && sscanf(hex,"#%x",&rgb)==1)
                    *values[i]=(GdkRGBA){((rgb>>16)&255)/255.,((rgb>>8)&255)/255.,(rgb&255)/255.,1};
            }
        }
    }
    double blend=theme&&g_str_has_prefix(theme,"HighContrast")?1:.22;
    *outline=(GdkRGBA){background->red*(1-blend)+foreground.red*blend,background->green*(1-blend)+foreground.green*blend,background->blue*(1-blend)+foreground.blue*blend,1};
#else
    gboolean dark = luxe_style_get_dark(luxe_style_settings());
    *background = dark ? (GdkRGBA){.red=.19,.green=.19,.blue=.19,.alpha=1} :
                         (GdkRGBA){.red=.8,.green=.8,.blue=.8,.alpha=1};
    GdkRGBA foreground = dark ? (GdkRGBA){.red=1,.green=1,.blue=1,.alpha=1} :
                                  (GdkRGBA){.red=0,.green=0,.blue=0,.alpha=1};
    GtkWidget *probe = g_object_ref_sink(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0));
    GtkStyleContext *style = gtk_widget_get_style_context(probe);
    gtk_style_context_lookup_color(style, "theme_bg_color", background);
    gtk_style_context_lookup_color(style, "theme_fg_color", &foreground);
    g_object_unref(probe);
    g_autofree char *theme = NULL;
    g_object_get(gtk_settings_get_default(), "gtk-theme-name", &theme, NULL);
    double blend = theme && g_str_has_prefix(theme, "HighContrast") ? 1.0 : .22;
    *outline = (GdkRGBA){.red=background->red*(1-blend)+foreground.red*blend,
                         .green=background->green*(1-blend)+foreground.green*blend,
                         .blue=background->blue*(1-blend)+foreground.blue*blend,
                         .alpha=1};
#endif
}

static void tray_draw(LuxUpTray *tray)
{
    if (!tray->cr || tray->window == None || tray->width <= 0 || tray->height <= 0)
        return;
    GdkRGBA background, outline;
    tray_panel_colors(&background, &outline);
    tray_trap_push(tray);
    cairo_t *cr = tray->cr;
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgb(cr, background.red, background.green, background.blue);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    cairo_set_source_rgb(cr, outline.red, outline.green, outline.blue);
    cairo_rectangle(cr, 0, 0, tray->width, 1);
    cairo_rectangle(cr, 0, tray->height-1, tray->width, 1);
    cairo_fill(cr);
    tray_draw_icon(cr, tray);
    cairo_surface_flush(tray->surface);
    XFlush(tray->dpy);
    tray_trap_pop(tray);
}

#ifndef LUXUP_HEADLESS_TRAY
static void tray_theme_changed(GObject *object, LuxUpTray *tray)
{
    (void)object;
    tray_draw(tray);
}
#else
static void tray_settings_changed(GSettings *settings,const char *key,gpointer data)
{(void)settings;(void)key;tray_draw(data);}
#endif

#ifndef LUXUP_HEADLESS_TRAY
static void tray_mode_changed(GObject *object, GParamSpec *param, LuxUpTray *tray)
{
    (void)object; (void)param;
    tray_draw(tray);
}
#endif

static void tray_create_window(LuxUpTray *tray);

static gboolean tray_source_prepare(GSource *source, gint *timeout)
{
    TraySource *s = (TraySource *) source;
    *timeout = -1;
    return s->tray->dpy != NULL && XPending(s->tray->dpy) > 0;
}

static gboolean tray_source_check(GSource *source)
{
    TraySource *s = (TraySource *) source;
    if (!(s->fd.revents & G_IO_IN) || s->tray->dpy == NULL)
        return FALSE;
    return XPending(s->tray->dpy) > 0;
}

static gboolean tray_source_dispatch(GSource *source, GSourceFunc callback, gpointer data)
{
    (void) callback;
    (void) data;
    TraySource *s = (TraySource *) source;
    LuxUpTray *tray = s->tray;
    XEvent event;
    while (XPending(tray->dpy) > 0) {
        XNextEvent(tray->dpy, &event);
        switch (event.type) {
            case ConfigureNotify:
                if (event.xconfigure.window != tray->window)
                    break;
                if (event.xconfigure.width != tray->width || event.xconfigure.height != tray->height) {
                    tray->width = event.xconfigure.width;
                    tray->height = event.xconfigure.height;
                    if (tray->surface)
                    {
                        tray_trap_push(tray);
                        cairo_xlib_surface_set_size(tray->surface, tray->width, tray->height);
                        tray_trap_pop(tray);
                    }
                    tray_draw(tray);
                }
                break;
            case Expose:
                if (event.xexpose.count == 0)
                    tray_draw(tray);
                break;
            case ButtonPress:
                if (event.xbutton.window == tray->window && tray->activate)
                    tray->activate(tray, event.xbutton.button, tray->user_data);
                break;
            case ReparentNotify:
                /* Panel restart reparents us back to the root: re-arm dock. */
                if (event.xreparent.window == tray->window &&
                    event.xreparent.parent == tray->root &&
                    tray->dock_timeout == 0)
                    luxup_tray_redock(tray);
                break;
            case UnmapNotify:
                if (event.xunmap.window == tray->window && tray->dock_timeout == 0)
                    luxup_tray_redock(tray);
                break;
            case DestroyNotify:
                if (event.xdestroywindow.window == tray->window) {
                    tray->window = None;
                    tray_create_window(tray);
                    luxup_tray_redock(tray);
                    break;
                }
                /* Tray owner went away: retry with fresh backoff. */
                if (event.xdestroywindow.window == tray->manager) {
                    tray->manager = None;
                    if (tray->dock_timeout == 0)
                        luxup_tray_redock(tray);
                }
                break;
            case ClientMessage: {
                /* New system-tray manager announces itself with a MANAGER
                 * client message on the root window. */
                Atom manager_atom = atom(tray->dpy, "MANAGER");
                char name[64];
                g_snprintf(name, sizeof name, "_NET_SYSTEM_TRAY_S%d", tray->screen);
                if (event.xclient.message_type == manager_atom && event.xclient.format == 32 &&
                    (Atom)event.xclient.data.l[1] == atom(tray->dpy, name))
                    luxup_tray_redock(tray);
                break;
            }
            default:
                break;
        }
    }
    return G_SOURCE_CONTINUE;
}

static GSourceFuncs tray_source_funcs = {
    .prepare = tray_source_prepare,
    .check = tray_source_check,
    .dispatch = tray_source_dispatch
};

static void tray_create_window(LuxUpTray *tray)
{
    tray_trap_push(tray);
    if (tray->cr) cairo_destroy(tray->cr);
    if (tray->surface) cairo_surface_destroy(tray->surface);
    Display *dpy = tray->dpy;
    XSetWindowAttributes attrs;
    memset(&attrs, 0, sizeof attrs);
    attrs.colormap = tray->colormap;
    attrs.background_pixel = tray->argb ? 0 : 0x303030;
    attrs.border_pixel = 0;
    attrs.event_mask = ExposureMask | StructureNotifyMask | ButtonPressMask | PropertyChangeMask;
    tray->window = XCreateWindow(dpy, tray->root, 0, 0, tray->width, tray->height, 0,
                                 tray->depth, InputOutput, tray->visual,
                                 CWColormap | CWBackPixel | CWBorderPixel | CWEventMask, &attrs);
    tray_set_xembed_info(tray);

    tray->surface = cairo_xlib_surface_create(dpy, tray->window, tray->visual, tray->width, tray->height);
    tray->cr = cairo_create(tray->surface);
    tray_trap_pop(tray);
    if (tray->tooltip) luxup_tray_set_tooltip(tray, tray->tooltip);
}

LuxUpTray *luxup_tray_new(LuxUpTrayActivateFunc activate, gpointer user_data)
{
    Display *dpy = XOpenDisplay(NULL);
    if (!dpy)
        return NULL;

    LuxUpTray *tray = g_new0(LuxUpTray, 1);
    tray->dpy = dpy;
    tray->screen = DefaultScreen(dpy);
    tray->root = RootWindow(dpy, tray->screen);
    tray->window = None;
    tray->width = LUXUP_TRAY_DEFAULT_SIZE;
    tray->height = LUXUP_TRAY_DEFAULT_SIZE;
    tray->state = LUXUP_TRAY_CHECKING;
    tray->activate = activate;
    tray->user_data = user_data;

    /* Scoped trap around the setup X calls below (cf. shotty's screenshot
     * path): ignore errors from our private connection during construction,
     * then restore GDK's handler before returning. */
    tray_trap_push(tray);

    XVisualInfo vinfo;
    if (XMatchVisualInfo(dpy, tray->screen, 32, TrueColor, &vinfo)) {
        tray->argb = TRUE;
        tray->visual = vinfo.visual;
        tray->depth = vinfo.depth;
        tray->colormap = XCreateColormap(dpy, tray->root, tray->visual, AllocNone);
    } else {
        tray->argb = FALSE;
        tray->visual = DefaultVisual(dpy, tray->screen);
        tray->depth = DefaultDepth(dpy, tray->screen);
        tray->colormap = DefaultColormap(dpy, tray->screen);
    }

    tray_create_window(tray);

    TraySource *s = (TraySource *) g_source_new(&tray_source_funcs, sizeof(TraySource));
    s->tray = tray;
    s->fd.fd = ConnectionNumber(dpy);
    s->fd.events = G_IO_IN;
    g_source_add_poll((GSource *) s, &s->fd);
    g_source_attach((GSource *) s, NULL);
    tray->source = (GSource *) s;

    tray_request_dock(tray);
    /* Bounded backoff replaces the old unbounded 3 s poll. */
    tray->dock_attempts = 0;
    tray_schedule_dock(tray, tray_dock_interval(0));
    /* Observe the root for a new tray manager (MANAGER announcement) so a
     * late panel start re-arms the dock after the backoff stops. */
    XSelectInput(tray->dpy, tray->root, StructureNotifyMask | PropertyChangeMask);
    tray_trap_pop(tray);
#ifndef LUXUP_HEADLESS_TRAY
    tray->theme_changed = g_signal_connect(gtk_icon_theme_get_for_display(gdk_display_get_default()),
        "changed", G_CALLBACK(tray_theme_changed), tray);
    tray->mode_changed = g_signal_connect(luxe_style_settings(),
        "notify::gtk-application-prefer-dark-theme", G_CALLBACK(tray_mode_changed), tray);
    tray->gtk_theme_changed = g_signal_connect(gtk_settings_get_default(),
        "notify::gtk-theme-name", G_CALLBACK(tray_mode_changed), tray);
#else
    GSettingsSchemaSource *source=g_settings_schema_source_get_default();
    g_autoptr(GSettingsSchema) schema=source?g_settings_schema_source_lookup(source,"org.gnome.desktop.interface",TRUE):NULL;
    if(schema) {tray->settings=g_settings_new_full(schema,NULL,NULL); g_signal_connect(tray->settings,"changed",G_CALLBACK(tray_settings_changed),tray);}
#endif
    return tray;
}

void luxup_tray_set_state(LuxUpTray *tray, LuxUpTrayState state)
{
    if (!tray || tray->state == state)
        return;
    tray->state = state;
    tray_draw(tray);
}

void luxup_tray_set_tooltip(LuxUpTray *tray, const char *tooltip)
{
    if (!tray)
        return;
    char *copy = g_strdup(tooltip);
    g_free(tray->tooltip);
    tray->tooltip = copy;
    if (tray->window != None) {
        tray_trap_push(tray);
        XStoreName(tray->dpy, tray->window, tray->tooltip ? tray->tooltip : "LuxUp");
        XFlush(tray->dpy);
        tray_trap_pop(tray);
    }
}

void luxup_tray_free(LuxUpTray *tray)
{
    if (!tray)
        return;
#ifndef LUXUP_HEADLESS_TRAY
    if (tray->theme_changed) g_signal_handler_disconnect(
        gtk_icon_theme_get_for_display(gdk_display_get_default()), tray->theme_changed);
    if (tray->mode_changed) g_signal_handler_disconnect(luxe_style_settings(), tray->mode_changed);
    if (tray->gtk_theme_changed) g_signal_handler_disconnect(gtk_settings_get_default(), tray->gtk_theme_changed);
#else
    g_clear_object(&tray->settings);
#endif
    if (tray->dock_timeout)
        g_source_remove(tray->dock_timeout);
    if (tray->source) {
        g_source_destroy(tray->source);
        g_source_unref(tray->source);
    }
    tray_trap_push(tray);
    if (tray->cr)
        cairo_destroy(tray->cr);
    if (tray->surface)
        cairo_surface_destroy(tray->surface);
    tray_trap_pop(tray);
    if (tray->window != None || (tray->argb && tray->colormap != None)) {
        tray_trap_push(tray);
        if (tray->window != None)
            XDestroyWindow(tray->dpy, tray->window);
        if (tray->argb && tray->colormap != None)
            XFreeColormap(tray->dpy, tray->colormap);
        tray_trap_pop(tray);
    }
    XCloseDisplay(tray->dpy);
    g_free(tray->tooltip);
    g_free(tray);
}
