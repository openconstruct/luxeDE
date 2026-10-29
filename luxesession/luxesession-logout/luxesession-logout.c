#include "luxe-renderer.h"
#include "luxe-lock-request.h"
#include "../luxe-about.h"
/**
 * Copyright (c) 2010 LxDE Developers, see the file AUTHORS for details.
 * Copyright (C) 2026 Jerry Howell
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <locale.h>
#include <stdlib.h>
#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <cairo-xlib.h>
#include <gdk/x11/gdkx.h>
#include <glib/gi18n.h>
#include <sys/file.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <X11/Xatom.h>
#include <X11/Xlib.h>

#include "luxesession-logout-dbus-interface.h"
#include "../luxesession/profile.h"

/* Command parameters. */
static char * prompt = NULL;
static char * banner_side = NULL;
static char * banner_path = NULL;

static GOptionEntry opt_entries[] =
{
    { "prompt", 'p', 0, G_OPTION_ARG_STRING, &prompt, N_("Custom message to show on the dialog"), N_("message") },
    { "banner", 'b', 0, G_OPTION_ARG_STRING, &banner_path, N_("Banner to show on the dialog"), N_("image file") },
    { "side", 's', 0, G_OPTION_ARG_STRING, &banner_side, N_("Position of the banner"), "top|left|right|bottom" },
    { NULL }
};

typedef struct {
    GPid luxesession_pid;			/* Process ID of luxesession */
    guint64 luxesession_pid_start_time;
    GtkWidget * error_label;		/* Text of an error, if we get one */

    int shutdown_available : 1;		/* Shutdown is available */
    int reboot_available : 1;		/* Reboot is available */
    int suspend_available : 1;		/* Suspend is available */
    int hibernate_available : 1;	/* Hibernate is available */

    int shutdown_systemd : 1;		/* Shutdown is available via systemd */
    int reboot_systemd : 1;		/* Reboot is available via systemd */
    int suspend_systemd : 1;		/* Suspend is available via systemd */
    int hibernate_systemd : 1;		/* Hibernate is available via systemd */
    int shutdown_ConsoleKit : 1;	/* Shutdown is available via ConsoleKit */
    int reboot_ConsoleKit : 1;		/* Reboot is available via ConsoleKit */
    int suspend_ConsoleKit : 1;		/* Suspend is available via ConsoleKit */
    int hibernate_ConsoleKit : 1;	/* Hibernate is available via ConsoleKit */
    int suspend_UPower : 1;		/* Suspend is available via UPower */
    int hibernate_UPower : 1;		/* Hibernate is available via UPower */
    int ltsp : 1;			/* Shutdown and reboot is accomplished via LTSP */

    int lock_screen : 1;                /* Lock screen available */

} HandlerContext;

static GMainLoop *main_loop = NULL;
static GCancellable *lock_cancel;
static gboolean lock_pending;

static gboolean parse_session_pid(const char *text, GPid *pid)
{
    char *end = NULL;
    long value;

    if (text == NULL || pid == NULL || text[0] == '\0')
        return FALSE;
    errno = 0;
    value = strtol(text, &end, 10);
    if (errno == ERANGE || end == text || *end != '\0' || value <= 1 || value > INT_MAX)
        return FALSE;
    *pid = (GPid)value;
    return TRUE;
}

static gboolean
session_pid_is_current (const HandlerContext *context)
{
    guint64 current_start = 0;
    gchar path[64];
    gchar buffer[4096];
    ssize_t length;
    int fd;

    if (context == NULL || context->luxesession_pid <= 1)
        return FALSE;
    if (context->luxesession_pid_start_time != 0)
        return luxe_process_start_time((gint)context->luxesession_pid,
                                         &current_start) &&
               current_start == context->luxesession_pid_start_time;
    g_snprintf(path, sizeof path, "/proc/%d/cmdline", (int)context->luxesession_pid);
    fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0)
        return FALSE;
    length = read(fd, buffer, sizeof(buffer) - 1);
    close(fd);
    if (length <= 0)
        return FALSE;
    buffer[length] = '\0';
    return strstr(buffer, "luxesession") != NULL;
}

static void do_quit(void)
{
    if (lock_cancel) g_cancellable_cancel(lock_cancel);
    if (main_loop != NULL)
        g_main_loop_quit(main_loop);
}

static void on_window_destroy(GtkWidget * widget, gpointer user_data)
{
    (void) widget;
    (void) user_data;
    do_quit();
}

static const gchar* determine_lock_screen(void);
static void logout_clicked(GtkButton * button, HandlerContext * handler_context);
static void change_root_property(GtkWidget* w, const char* prop_name, const char* value);
static void shutdown_clicked(GtkButton * button, HandlerContext * handler_context);
static void reboot_clicked(GtkButton * button, HandlerContext * handler_context);
static void suspend_clicked(GtkButton * button, HandlerContext * handler_context);
static void hibernate_clicked(GtkButton * button, HandlerContext * handler_context);
static void cancel_clicked(GtkButton * button, gpointer user_data);
static GtkPositionType get_banner_position(void);
static GdkPixbuf * get_background_pixbuf(void);
static void draw_func(GtkDrawingArea * area, cairo_t * cr, int width, int height, gpointer data);

static void
logout_close_child (GPid pid, gint status, gpointer data)
{
    (void)status;
    (void)data;
    g_spawn_close_pid(pid);
}

static const gchar* determine_lock_screen(void)
{
    g_autofree gchar *path = g_find_program_in_path("luxelock");
    return path ? "luxelock" : NULL;
}


/* Handler for "clicked" signal on Logout button. */
static void logout_clicked(GtkButton * button, HandlerContext * handler_context)
{
    if (lock_pending) return;
    if (session_pid_is_current (handler_context))
    {
        kill(handler_context->luxesession_pid, SIGTERM);
    }
    else
    {
        /* Assume we are under openbox */
        gchar *argv[] = { (gchar *)"openbox", (gchar *)"--exit", NULL };
        GError *error = NULL;
        GPid pid = 0;
        if (!g_spawn_async(NULL, argv, NULL,
                           G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD,
                           NULL, NULL, &pid, &error))
            g_warning("luxesession-logout: unable to exit Openbox: %s",
                      error != NULL ? error->message : "unknown error");
        else
            g_child_watch_add_full(G_PRIORITY_DEFAULT_IDLE, pid,
                                   logout_close_child, NULL, NULL);
        g_clear_error(&error);
    }
    do_quit();
}

/* Replace a property on the root window. */
static void change_root_property(GtkWidget* w, const char* prop_name, const char* value)
{
    (void) w;
    GdkDisplay *display = gdk_display_get_default();
    Display *d;
    Window root;
    if (display == NULL || prop_name == NULL || value == NULL)
        return;
    d = GDK_DISPLAY_XDISPLAY(display);
    if (d == NULL)
        return;
    root = DefaultRootWindow(d);
    XChangeProperty(d, root,
                      XInternAtom(d, prop_name, False), XA_STRING, 8,
                      PropModeReplace, (unsigned char*) value, strlen(value) + 1);
}

/* Handler for "clicked" signal on Shutdown button. */
static void shutdown_clicked(GtkButton * button, HandlerContext * handler_context)
{
    if (lock_pending) return;
    GError *err = NULL;
    gtk_label_set_text(GTK_LABEL(handler_context->error_label), NULL);

    if (handler_context->ltsp)
    {
        change_root_property(GTK_WIDGET(button), "LTSP_LOGOUT_ACTION", "HALT");
        if (session_pid_is_current (handler_context))
        {
            kill(handler_context->luxesession_pid, SIGTERM);
        }
    }
    else if (handler_context->shutdown_ConsoleKit)
        dbus_ConsoleKit_PowerOff(&err);
    else if (handler_context->shutdown_systemd)
        dbus_systemd_PowerOff(&err);

	if (err)
	{
		gtk_label_set_text(GTK_LABEL(handler_context->error_label), err->message);
		g_error_free (err);
	}
	else
    {
        do_quit();
    }
}

/* Handler for "clicked" signal on Reboot button. */
static void reboot_clicked(GtkButton * button, HandlerContext * handler_context)
{
    if (lock_pending) return;
    GError *err = NULL;
    gtk_label_set_text(GTK_LABEL(handler_context->error_label), NULL);

    if (handler_context->ltsp)
    {
        change_root_property(GTK_WIDGET(button), "LTSP_LOGOUT_ACTION", "REBOOT");
        if (session_pid_is_current (handler_context))
        {
            kill(handler_context->luxesession_pid, SIGTERM);
        }
    }
    else if (handler_context->reboot_ConsoleKit)
        dbus_ConsoleKit_Reboot(&err);
    else if (handler_context->reboot_systemd)
        dbus_systemd_Reboot(&err);

	if (err)
	{
		gtk_label_set_text(GTK_LABEL(handler_context->error_label), err->message);
		g_error_free (err);
	}
	else
    {
        do_quit();
    }
}

typedef enum { LOCK_ONLY, LOCK_THEN_SUSPEND, LOCK_THEN_HIBERNATE } LockAction;
typedef struct { HandlerContext *context; LockAction action; } LockActionRequest;

static void lock_action_ready(GObject *source, GAsyncResult *result, gpointer data)
{
    (void)source;
    g_autofree LockActionRequest *request = data;
    HandlerContext *context = request->context;
    g_autoptr(GError) error = NULL;
    gboolean ready = luxe_lock_request_finish(result, &error);
    lock_pending = FALSE;
    g_clear_object(&lock_cancel);
    // Context has process lifetime; widgets must only be used while the
    // dialog loop is running. Cancel/close never initiates a sleep operation.
    if (!main_loop || !g_main_loop_is_running(main_loop)) return;
    if (ready && request->action == LOCK_THEN_SUSPEND) {
        if (context->suspend_UPower) dbus_UPower_Suspend(&error);
        else if (context->suspend_ConsoleKit) dbus_ConsoleKit_Suspend(&error);
        else if (context->suspend_systemd) dbus_systemd_Suspend(&error);
        else g_set_error_literal(&error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "Suspend is unavailable");
    } else if (ready && request->action == LOCK_THEN_HIBERNATE) {
        if (context->hibernate_UPower) dbus_UPower_Hibernate(&error);
        else if (context->hibernate_ConsoleKit) dbus_ConsoleKit_Hibernate(&error);
        else if (context->hibernate_systemd) dbus_systemd_Hibernate(&error);
        else g_set_error_literal(&error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "Hibernate is unavailable");
    }
    if (error) gtk_label_set_text(GTK_LABEL(context->error_label), error->message);
    else if (ready) do_quit();
}

static void request_lock_action(HandlerContext *context, LockAction action)
{
    if (lock_pending) return;
    if (!determine_lock_screen()) {
        gtk_label_set_text(GTK_LABEL(context->error_label), _("Screen locking is unavailable. Install or update LuxeSession and LuxeDM."));
        return;
    }
    lock_pending = TRUE;
    lock_cancel = g_cancellable_new();
    gtk_label_set_text(GTK_LABEL(context->error_label), _("Waiting for the screen to lock…"));
    LockActionRequest *request = g_new0(LockActionRequest, 1);
    request->context = context; request->action = action;
    luxe_lock_request_async("luxelock", 10000, lock_cancel, lock_action_ready, request);
}

static void suspend_clicked(GtkButton *button, HandlerContext *context)
{ (void)button; request_lock_action(context, LOCK_THEN_SUSPEND); }
static void hibernate_clicked(GtkButton *button, HandlerContext *context)
{ (void)button; request_lock_action(context, LOCK_THEN_HIBERNATE); }
static void lock_screen_clicked(GtkButton *button, HandlerContext *context)
{ (void)button; request_lock_action(context, LOCK_ONLY); }

/* Handler for "clicked" signal on Cancel button. */
static void cancel_clicked(GtkButton * button, gpointer user_data)
{
    do_quit();
}

/* Shortcut action callback: activate the Cancel button on Escape. */
static gboolean cancel_escape_activated(GtkWidget * widget, GVariant * args, gpointer user_data)
{
    (void) widget;
    (void) args;
    gtk_widget_activate(GTK_WIDGET(user_data));
    return TRUE;
}

/* Convert the --side parameter to a GtkPositionType. */
static GtkPositionType get_banner_position(void)
{
    if (banner_side != NULL)
    {
        if (strcmp(banner_side, "right") == 0)
            return GTK_POS_RIGHT;
        if (strcmp(banner_side, "top") == 0)
            return GTK_POS_TOP;
        if (strcmp(banner_side, "bottom") == 0)
            return GTK_POS_BOTTOM;
    }
    return GTK_POS_LEFT;
}

/* Get the background pixbuf. */
static GdkPixbuf * get_background_pixbuf(void)
{
    /* Get the root window pixmap via Xlib and wrap it in a cairo surface. */
    Display * dpy = GDK_DISPLAY_XDISPLAY(gdk_display_get_default());
    Window root = DefaultRootWindow(dpy);
    int scr = DefaultScreen(dpy);
    int w = DisplayWidth(dpy, scr), h = DisplayHeight(dpy, scr);
    cairo_surface_t * s = cairo_xlib_surface_create(dpy, root, DefaultVisual(dpy, scr), w, h);
    GdkPixbuf * pixbuf = gdk_pixbuf_get_from_surface(s, 0, 0, w, h);
    cairo_surface_destroy(s);

    /* Make the background darker. */
    if (pixbuf != NULL)
    {
        unsigned char * pixels = gdk_pixbuf_get_pixels(pixbuf);
        int width = gdk_pixbuf_get_width(pixbuf);
        int height = gdk_pixbuf_get_height(pixbuf);
        int pixel_stride = ((gdk_pixbuf_get_has_alpha(pixbuf)) ? 4 : 3);
        int row_stride = gdk_pixbuf_get_rowstride(pixbuf);
        int y;
        for (y = 0; y < height; y += 1)
        {
            unsigned char * p = pixels;
            int x;
            for (x = 0; x < width; x += 1)
            {
                p[0] = p[0] / 2;
                p[1] = p[1] / 2;
                p[2] = p[2] / 2;
                p += pixel_stride;
            }
            pixels += row_stride;
        }
    }
    return pixbuf;
}

/* Handler for painting the background (GTK4: GtkDrawingArea draw function). */
static void draw_func(GtkDrawingArea * area, cairo_t * cr, int width, int height, gpointer data)
{
    GdkPixbuf * pixbuf = (GdkPixbuf *) data;
    (void) area;
    (void) width;
    (void) height;
    if (pixbuf != NULL)
    {
        gdk_cairo_set_source_pixbuf(cr, pixbuf, 0, 0);
        cairo_paint(cr);
    }
}

static char lockfile[PATH_MAX];
static int lockfile_valid = 0;

/* Unlink lockfile on exit (only a path this process verified). */
static void main_at_exit(void)
{
    if (lockfile_valid)
        unlink(lockfile);
}

/* Main program. */
int main(int argc, char * argv[])
{
#ifdef ENABLE_NLS
    setlocale(LC_ALL, "");
    bindtextdomain(GETTEXT_PACKAGE, PACKAGE_LOCALE_DIR);
    bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");
    textdomain (GETTEXT_PACKAGE);
#endif

    static HandlerContext handler_context;
    memset(&handler_context, 0, sizeof(handler_context));

    /* Get the luxesession PID. */
    const char * p = g_getenv("_LUXESESSION_PID");
    if (p != NULL && !parse_session_pid(p, &handler_context.luxesession_pid))
        handler_context.luxesession_pid = 0;
    {
        const char *start = g_getenv("_LUXESESSION_PID_START");
        if (start != NULL && *start != '\0') {
            gchar *end = NULL;
            guint64 value = g_ascii_strtoull(start, &end, 10);
            if (end != start && *end == '\0')
                handler_context.luxesession_pid_start_time = value;
        }
    }

    /* Create lock file to prevent more than one logout dialog per luxesession process.
     * The lock lives in $XDG_RUNTIME_DIR (same directory checks LuxeDM uses);
     * only when the runtime dir is unavailable we fall back to the user cache
     * dir or to a uid-keyed path under /tmp. The file is created with
     * O_CREAT|O_EXCL|O_NOFOLLOW and verified via fstat (regular file, owned
     * by us, single link) before flock; only a verified path is unlinked. */
    {
        const uid_t uid = getuid();
        const char *runtime_dir = g_getenv("XDG_RUNTIME_DIR");
        struct stat dir_st;
        if (runtime_dir != NULL && runtime_dir[0] == '/' &&
            stat(runtime_dir, &dir_st) == 0 &&
            S_ISDIR(dir_st.st_mode) && dir_st.st_uid == uid &&
            (dir_st.st_mode & 0077) == 0)
        {
            snprintf(lockfile, sizeof(lockfile), "%s/.luxesession-logout-%d.lock",
                     runtime_dir, (int)handler_context.luxesession_pid);
        }
        else
        {
            const char *cache_dir = g_get_user_cache_dir();
            struct stat cache_st;
            if (cache_dir != NULL && cache_dir[0] == '/' &&
                stat(cache_dir, &cache_st) == 0 &&
                S_ISDIR(cache_st.st_mode) && cache_st.st_uid == uid)
            {
                snprintf(lockfile, sizeof(lockfile), "%s/.luxesession-logout-%d.lock",
                         cache_dir, (int)handler_context.luxesession_pid);
            }
            else
            {
                snprintf(lockfile, sizeof(lockfile), "/tmp/.luxesession-logout-%u-%d.lock",
                         (unsigned)uid, (int)handler_context.luxesession_pid);
            }
        }
        lockfile[sizeof(lockfile) - 1] = '\0';
        int fd = open(lockfile, O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
        if (fd < 0 && errno == EEXIST)
            fd = open(lockfile, O_RDWR | O_NOFOLLOW | O_CLOEXEC, 0);
        if (fd >= 0)
        {
            struct stat file_st;
            if (fstat(fd, &file_st) != 0 || !S_ISREG(file_st.st_mode) ||
                file_st.st_uid != uid || file_st.st_nlink != 1)
            {
                close(fd);
            }
            else
            {
                lockfile_valid = 1;
                if (flock(fd, LOCK_EX | LOCK_NB))
                {
                    exit(EXIT_FAILURE);
                }
                atexit(main_at_exit);
            }
        }
    }

    /* Query DBus before GTK+ initialization!!! Otherwise a race may occur. */

    /* Initialize capabilities of the systemd mechanism. */
    if (dbus_systemd_CanPowerOff())
    {
        handler_context.shutdown_available = TRUE;
        handler_context.shutdown_systemd = TRUE;
    }
    if (dbus_systemd_CanReboot())
    {
        handler_context.reboot_available = TRUE;
        handler_context.reboot_systemd = TRUE;
    }
    if (dbus_systemd_CanSuspend())
    {
        handler_context.suspend_available = TRUE;
        handler_context.suspend_systemd = TRUE;
    }
    if (dbus_systemd_CanHibernate())
    {
        handler_context.hibernate_available = TRUE;
        handler_context.hibernate_systemd = TRUE;
    }

    /* Initialize capabilities of the ConsoleKit mechanism. */
    if (!handler_context.shutdown_available && dbus_ConsoleKit_CanPowerOff())
    {
        handler_context.shutdown_available = TRUE;
        handler_context.shutdown_ConsoleKit = TRUE;
    }
    if (!handler_context.reboot_available && dbus_ConsoleKit_CanReboot())
    {
        handler_context.reboot_available = TRUE;
        handler_context.reboot_ConsoleKit = TRUE;
    }
    if (!handler_context.suspend_available && dbus_ConsoleKit_CanSuspend())
    {
        handler_context.suspend_available = TRUE;
        handler_context.suspend_ConsoleKit = TRUE;
    }
    if (!handler_context.hibernate_available && dbus_ConsoleKit_CanHibernate())
    {
        handler_context.hibernate_available = TRUE;
        handler_context.hibernate_ConsoleKit = TRUE;
    }

    /* Initialize capabilities of the UPower mechanism. */
    if (!handler_context.suspend_available && dbus_UPower_CanSuspend())
    {
        handler_context.suspend_available = TRUE;
        handler_context.suspend_UPower = TRUE;
    }
    if (!handler_context.hibernate_available && dbus_UPower_CanHibernate())
    {
        handler_context.hibernate_available = TRUE;
        handler_context.hibernate_UPower = TRUE;
    }

    /* LTSP support */
    if (g_getenv("LTSP_CLIENT"))
    {
        handler_context.ltsp = TRUE;
        handler_context.shutdown_available = TRUE;
        handler_context.reboot_available = TRUE;
    }

    /* Lock screen */
    const gchar* very_lock_screen = determine_lock_screen();
    if (very_lock_screen)
    {
        handler_context.lock_screen = TRUE;
    }

    /* Initialize GTK (via g_option_context_parse) and parse command line arguments. */
    GOptionContext * context = g_option_context_new("");
    g_option_context_add_main_entries(context, opt_entries, GETTEXT_PACKAGE);
    /* GTK4 removed gtk_get_option_group(); GTK is initialized separately below. */
    GError * err = NULL;
    if ( ! g_option_context_parse(context, &argc, &argv, &err))
    {
        g_print(_("Error: %s\n"), err->message);
        g_error_free(err);
        return 1;
    }
    g_option_context_free(context);
    luxe_gtk_init();

    /* Make the button images accessible. */
    /* gtk_icon_theme_get_default() was removed in GTK4. */
    gtk_icon_theme_add_search_path(gtk_icon_theme_get_for_display(gdk_display_get_default()), PACKAGE_DATA_DIR "/luxesession/images");

    /* Get the background pixbuf. */
    GdkPixbuf * pixbuf = get_background_pixbuf();

    /* Create the toplevel window. */
    GtkWidget * window = gtk_window_new();
    gtk_window_set_decorated(GTK_WINDOW(window), FALSE);
    gtk_window_fullscreen(GTK_WINDOW(window));
    /* The background is painted by a fullscreen GtkDrawingArea below. */
    g_signal_connect(G_OBJECT(window), "destroy", G_CALLBACK(on_window_destroy), NULL);

    /* Toplevel container */
    GtkWidget * overlay = gtk_overlay_new();
    gtk_window_set_child(GTK_WINDOW(window), overlay);

    /* Fullscreen background area showing a darkened copy of the root window. */
    GtkWidget * background = gtk_drawing_area_new();
    gtk_widget_set_hexpand(background, TRUE);
    gtk_widget_set_vexpand(background, TRUE);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(background), draw_func, pixbuf, NULL);
    gtk_overlay_set_child(GTK_OVERLAY(overlay), background);

    GtkWidget* center_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_margin_start(center_vbox, 12);
    gtk_widget_set_margin_end(center_vbox, 12);
    gtk_widget_set_margin_top(center_vbox, 12);
    gtk_widget_set_margin_bottom(center_vbox, 12);
    gtk_widget_set_halign(center_vbox, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(center_vbox, GTK_ALIGN_CENTER);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), center_vbox);

    GtkWidget* controls = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);

    /* A bottom banner is appended after the dialog content is built, below. */
    GtkWidget * bottom_separator = NULL;
    GtkWidget * bottom_banner = NULL;

    /* If specified, apply a user-specified banner image. */
    if (banner_path != NULL)
    {
        GtkWidget * banner_image = gtk_image_new_from_file(banner_path);
        GtkPositionType banner_position = get_banner_position();

        switch (banner_position)
        {
            case GTK_POS_LEFT:
            case GTK_POS_RIGHT:
                {
                /* Create a horizontal box to contain the image and the controls. */
                GtkWidget * box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
                gtk_box_append(GTK_BOX(center_vbox), box);

                /* Pack the image and a separator. */
                gtk_widget_set_halign(banner_image, GTK_ALIGN_CENTER);
                gtk_widget_set_valign(banner_image, GTK_ALIGN_START);
                if (banner_position == GTK_POS_LEFT)
                {
                    gtk_box_append(GTK_BOX(box), banner_image);
                    gtk_box_append(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_VERTICAL));
                    gtk_box_append(GTK_BOX(box), controls);
                }
                else
                {
                    gtk_box_append(GTK_BOX(box), controls);
                    gtk_box_append(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_VERTICAL));
                    gtk_box_append(GTK_BOX(box), banner_image);
                }
                }
                break;

            case GTK_POS_TOP:
                gtk_box_append(GTK_BOX(controls), banner_image);
                gtk_box_append(GTK_BOX(controls), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
                gtk_box_append(GTK_BOX(center_vbox), controls);
                break;

            case GTK_POS_BOTTOM:
                gtk_box_append(GTK_BOX(center_vbox), controls);
                bottom_separator = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
                bottom_banner = banner_image;
                break;
        }
    }
    else
        gtk_box_append(GTK_BOX(center_vbox), controls);

    /* Create the label. */
    GtkWidget * label = gtk_label_new("");
    if (prompt == NULL)
    {
        /* Product UI intentionally uses the lowercase Luxe wordmark.  The
         * internal LXDE compatibility profile and OS release are irrelevant
         * to a user's decision to end their desktop session. */
        prompt = g_strdup(_("<b><big>Logout luxe session</big></b>"));
    }
    gtk_label_set_markup(GTK_LABEL(label), prompt);
    gtk_box_append(GTK_BOX(controls), label);

    /* Create the Shutdown button. */
    if (handler_context.shutdown_available)
    {
        GtkWidget * shutdown_button = gtk_button_new();
        GtkWidget * shutdown_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        GtkWidget * shutdown_image = gtk_image_new_from_icon_name("system-shutdown");
        gtk_image_set_pixel_size(GTK_IMAGE(shutdown_image), 16);
        GtkWidget * shutdown_label = gtk_label_new_with_mnemonic(_("Sh_utdown"));
        gtk_label_set_mnemonic_widget(GTK_LABEL(shutdown_label), shutdown_button);
        gtk_label_set_xalign(GTK_LABEL(shutdown_label), 0.0);
        gtk_box_append(GTK_BOX(shutdown_box), shutdown_image);
        gtk_box_append(GTK_BOX(shutdown_box), shutdown_label);
        gtk_button_set_child(GTK_BUTTON(shutdown_button), shutdown_box);
        g_signal_connect(G_OBJECT(shutdown_button), "clicked", G_CALLBACK(shutdown_clicked), &handler_context);
        gtk_box_append(GTK_BOX(controls), shutdown_button);
    }

    /* Create the Reboot button. */
    if (handler_context.reboot_available)
    {
        GtkWidget * reboot_button = gtk_button_new();
        GtkWidget * reboot_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        GtkWidget * reboot_image = gtk_image_new_from_icon_name("gnome-session-reboot");
        gtk_image_set_pixel_size(GTK_IMAGE(reboot_image), 16);
        GtkWidget * reboot_label = gtk_label_new_with_mnemonic(_("_Reboot"));
        gtk_label_set_mnemonic_widget(GTK_LABEL(reboot_label), reboot_button);
        gtk_label_set_xalign(GTK_LABEL(reboot_label), 0.0);
        gtk_box_append(GTK_BOX(reboot_box), reboot_image);
        gtk_box_append(GTK_BOX(reboot_box), reboot_label);
        gtk_button_set_child(GTK_BUTTON(reboot_button), reboot_box);
        g_signal_connect(G_OBJECT(reboot_button), "clicked", G_CALLBACK(reboot_clicked), &handler_context);
        gtk_box_append(GTK_BOX(controls), reboot_button);
    }

    /* Create the Suspend button. */
    if (handler_context.suspend_available && !handler_context.ltsp)
    {
        GtkWidget * suspend_button = gtk_button_new();
        GtkWidget * suspend_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        GtkWidget * suspend_image = gtk_image_new_from_icon_name("gnome-session-suspend");
        gtk_image_set_pixel_size(GTK_IMAGE(suspend_image), 16);
        GtkWidget * suspend_label = gtk_label_new_with_mnemonic(_("_Suspend"));
        gtk_label_set_mnemonic_widget(GTK_LABEL(suspend_label), suspend_button);
        gtk_label_set_xalign(GTK_LABEL(suspend_label), 0.0);
        gtk_box_append(GTK_BOX(suspend_box), suspend_image);
        gtk_box_append(GTK_BOX(suspend_box), suspend_label);
        gtk_button_set_child(GTK_BUTTON(suspend_button), suspend_box);
        g_signal_connect(G_OBJECT(suspend_button), "clicked", G_CALLBACK(suspend_clicked), &handler_context);
        gtk_box_append(GTK_BOX(controls), suspend_button);
    }

    /* Create the Hibernate button. */
    if (handler_context.hibernate_available && !handler_context.ltsp)
    {
        GtkWidget * hibernate_button = gtk_button_new();
        GtkWidget * hibernate_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        GtkWidget * hibernate_image = gtk_image_new_from_icon_name("gnome-session-hibernate");
        gtk_image_set_pixel_size(GTK_IMAGE(hibernate_image), 16);
        GtkWidget * hibernate_label = gtk_label_new_with_mnemonic(_("_Hibernate"));
        gtk_label_set_mnemonic_widget(GTK_LABEL(hibernate_label), hibernate_button);
        gtk_label_set_xalign(GTK_LABEL(hibernate_label), 0.0);
        gtk_box_append(GTK_BOX(hibernate_box), hibernate_image);
        gtk_box_append(GTK_BOX(hibernate_box), hibernate_label);
        gtk_button_set_child(GTK_BUTTON(hibernate_button), hibernate_box);
        g_signal_connect(G_OBJECT(hibernate_button), "clicked", G_CALLBACK(hibernate_clicked), &handler_context);
        gtk_box_append(GTK_BOX(controls), hibernate_button);
    }

    /* Create the Lock Screen button. */
    if (handler_context.lock_screen && !handler_context.ltsp)
    {
        GtkWidget * lock_screen_button = gtk_button_new();
        GtkWidget * lock_screen_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        GtkWidget * lock_screen_image = gtk_image_new_from_icon_name("system-lock-screen");
        gtk_image_set_pixel_size(GTK_IMAGE(lock_screen_image), 16);
        GtkWidget * lock_screen_label = gtk_label_new_with_mnemonic(_("L_ock Screen"));
        gtk_label_set_mnemonic_widget(GTK_LABEL(lock_screen_label), lock_screen_button);
        gtk_label_set_xalign(GTK_LABEL(lock_screen_label), 0.0);
        gtk_box_append(GTK_BOX(lock_screen_box), lock_screen_image);
        gtk_box_append(GTK_BOX(lock_screen_box), lock_screen_label);
        gtk_button_set_child(GTK_BUTTON(lock_screen_button), lock_screen_box);
        g_signal_connect(G_OBJECT(lock_screen_button), "clicked", G_CALLBACK(lock_screen_clicked), &handler_context);
        gtk_box_append(GTK_BOX(controls), lock_screen_button);
    }

    /* Create the Logout button. */
    GtkWidget * logout_button = gtk_button_new();
    GtkWidget * logout_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget * logout_image = gtk_image_new_from_icon_name("system-log-out");
    gtk_image_set_pixel_size(GTK_IMAGE(logout_image), 16);
    GtkWidget * logout_label = gtk_label_new_with_mnemonic(_("_Logout"));
    gtk_label_set_mnemonic_widget(GTK_LABEL(logout_label), logout_button);
    gtk_label_set_xalign(GTK_LABEL(logout_label), 0.0);
    gtk_box_append(GTK_BOX(logout_box), logout_image);
    gtk_box_append(GTK_BOX(logout_box), logout_label);
    gtk_button_set_child(GTK_BUTTON(logout_button), logout_box);
    g_signal_connect(G_OBJECT(logout_button), "clicked", G_CALLBACK(logout_clicked), &handler_context);
    gtk_box_append(GTK_BOX(controls), logout_button);

    /* Create the Cancel button. */
    GtkWidget * cancel_button = gtk_button_new();
    GtkWidget * cancel_label = gtk_label_new_with_mnemonic(_("_Cancel"));
    gtk_label_set_mnemonic_widget(GTK_LABEL(cancel_label), cancel_button);
    gtk_label_set_xalign(GTK_LABEL(cancel_label), 0.0);
    gtk_button_set_child(GTK_BUTTON(cancel_button), cancel_label);
    g_signal_connect(G_OBJECT(cancel_button), "clicked", G_CALLBACK(cancel_clicked), NULL);
    {
        GtkShortcutTrigger * escape_trigger = gtk_keyval_trigger_new(GDK_KEY_Escape, 0);
        GtkShortcutAction * escape_action = gtk_callback_action_new(cancel_escape_activated, cancel_button, NULL);
        GtkShortcut * escape_shortcut = gtk_shortcut_new(escape_trigger, escape_action);
        GtkEventController * escape_controller = gtk_shortcut_controller_new();
        gtk_shortcut_controller_add_shortcut(GTK_SHORTCUT_CONTROLLER(escape_controller), escape_shortcut);
        gtk_widget_add_controller(window, escape_controller);
    }
    gtk_box_append(GTK_BOX(controls), cancel_button);

    /* Create the error text. */
    handler_context.error_label = gtk_label_new("");
    gtk_label_set_justify(GTK_LABEL(handler_context.error_label), GTK_JUSTIFY_CENTER);
    gtk_box_append(GTK_BOX(controls), handler_context.error_label);

    /* A bottom banner goes after the dialog content. */
    if (bottom_separator != NULL)
        gtk_box_append(GTK_BOX(controls), bottom_separator);
    if (bottom_banner != NULL)
        gtk_box_append(GTK_BOX(controls), bottom_banner);

    /* Show everything. */
    luxe_add_about_button(GTK_WINDOW(window), "LuxeSession", VERSION, "Session and power controls for Luxe.");
    gtk_window_present(GTK_WINDOW(window));

    /* Run the main event loop. */
    main_loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(main_loop);
    if (lock_cancel) g_cancellable_cancel(lock_cancel);
    g_clear_object(&lock_cancel);
    g_main_loop_unref(main_loop);
    main_loop = NULL;

    /* Return. */
    return 0;
}
