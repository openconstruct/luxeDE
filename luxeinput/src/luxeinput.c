#include "../luxe-about.h"
/*
 *      lxinput.c
 *
 *      Copyright 2009-2011 PCMan <pcman.tw@gmail.com>
 *      Copyright 2009 martyj19 <martyj19@comcast.net>
 *      Copyright 2011-2013 Julien Lavergne <julien.lavergne@gmail.com>
 *      Copyright 2014 Andriy Grytsenko <andrej@rep.kiev.ua>
 *      Copyright (C) 2026 Jerry Howell
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with this program; if not, write to the Free Software
 *      Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 *      MA 02110-1301, USA.
 */

#include <gtk/gtk.h>
#include "luxe-gtk-ui.h"
#include <glib/gi18n.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <errno.h>

#include <gdk/gdk.h>
#ifdef GDK_WINDOWING_X11
#include <gdk/x11/gdkx.h>
#endif
#include <X11/Xlib.h>
#include <X11/XKBlib.h>

static GtkWidget *dlg;
static GtkRange *mouse_accel;
static GtkRange *mouse_threshold;
static GtkCheckButton* mouse_left_handed;
static GtkRange *kb_delay;
static GtkRange *kb_interval;
static GtkCheckButton* kb_beep;
static GtkButton* kb_layout;
static GtkLabel* status_label;

static int accel = 20, old_accel = 20;
static int threshold = 10, old_threshold = 10;
static gboolean left_handed = FALSE, old_left_handed = FALSE;

static int delay = 500, old_delay = 500;
static int interval = 30, old_interval = 30;
static gboolean beep = TRUE, old_beep = TRUE;
/* The bell's real X state when the dialog opened; Cancel restores it. */
static int old_bell_percent = -1, old_bell_pitch = -1, old_bell_duration = -1;
static int old_accel_num = 20, old_accel_den = 10;
static unsigned char old_pointer_map[256];
static int old_pointer_count;
#ifdef LUXEINPUT_TEST
static gboolean test_force_no_x11;
static gboolean test_force_no_xkb;
static int (*test_pointer_mapping_setter)(Display *, const unsigned char *, int) = XSetPointerMapping;
#define set_pointer_mapping test_pointer_mapping_setter
#else
#define set_pointer_mapping XSetPointerMapping
#endif

/* X11-entangled pointer/keyboard code stays X11-gated: the GUI itself is
 * backend-neutral and runs on Wayland too, but XChangePointerControl etc.
 * are no-ops without an X11 display. */
static gboolean have_x11(void)
{
#ifdef LUXEINPUT_TEST
    if (test_force_no_x11) return FALSE;
#endif
#ifdef GDK_WINDOWING_X11
    GdkDisplay* d = gdk_display_get_default();
    return d != NULL && GDK_IS_X11_DISPLAY(d);
#else
    return FALSE;
#endif
}

#ifdef GDK_WINDOWING_X11
static Display* xdisplay(void)
{
    /* GDK_DISPLAY_XDISPLAY is deprecated in GTK4 but still present and is
     * the direct replacement-free accessor for the X11 Display. */
G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    Display* d = GDK_DISPLAY_XDISPLAY(gdk_display_get_default());
G_GNUC_END_IGNORE_DEPRECATIONS
    return d;
}

static gboolean have_xkb(void)
{
#ifdef LUXEINPUT_TEST
    if (test_force_no_xkb) return FALSE;
#endif
    int opcode, event, error, major = XkbMajorVersion, minor = XkbMinorVersion;
    return have_x11() && XkbQueryExtension(xdisplay(), &opcode, &event, &error, &major, &minor);
}

static gboolean pointer_control_matches(void)
{
    int numerator, denominator, live_threshold;
    XSync(xdisplay(), False);
    XGetPointerControl(xdisplay(), &numerator, &denominator, &live_threshold);
    return denominator > 0 && numerator * 10 / denominator == accel && live_threshold == threshold;
}
#endif

static void set_status(const char *message)
{
    if (status_label) {
        gtk_label_set_text(status_label, message ? message : "");
        gtk_widget_set_visible(GTK_WIDGET(status_label), message != NULL);
    }
}

static void on_mouse_accel_changed(GtkRange* range, gpointer user_data)
{
    accel = (int)(gtk_range_get_value(range) * 10);
#ifdef GDK_WINDOWING_X11
    if(have_x11()) {
        XChangePointerControl(xdisplay(), True, False, accel, 10, 0);
        if (!pointer_control_matches()) set_status("The pointer acceleration could not be applied.");
        else set_status(NULL);
    }
#endif
}

static void on_mouse_threshold_changed(GtkRange* range, gpointer user_data)
{
    /* threshold = 110 - sensitivity. The lower the threshold, the higher the sensitivity */
    threshold = (int)gtk_range_get_value(range);
#ifdef GDK_WINDOWING_X11
    if(have_x11()) {
        XChangePointerControl(xdisplay(), False, True, 0, 10, threshold);
        if (!pointer_control_matches()) set_status("The pointer sensitivity could not be applied.");
        else set_status(NULL);
    }
#endif
}

static void on_kb_range_changed(GtkRange* range, int* val)
{
    *val = (int)gtk_range_get_value(range);
    /* apply keyboard values */
#ifdef GDK_WINDOWING_X11
    if(have_xkb()) {
        unsigned int live_delay, live_interval;
        if (!XkbSetAutoRepeatRate(xdisplay(), XkbUseCoreKbd, delay, interval) ||
            !XkbGetAutoRepeatRate(xdisplay(), XkbUseCoreKbd, &live_delay, &live_interval) ||
            live_delay != (unsigned int)delay || live_interval != (unsigned int)interval)
            set_status("The keyboard repeat settings could not be applied.");
        else set_status(NULL);
    }
#endif
}

/* This function is taken from Gnome's control-center 2.6.0.3 (gnome-settings-mouse.c) and was modified*/
static gboolean set_left_handed_mouse(void)
{
#ifdef GDK_WINDOWING_X11
    unsigned char buttons[256];
    gint n_buttons, i;
    gint idx_1 = -1, idx_3 = -1;

    if(!have_x11())
        return FALSE;

    n_buttons = XGetPointerMapping (xdisplay(), buttons, sizeof buttons);
    if (n_buttons < 2 || n_buttons > sizeof buttons)
        return FALSE;

    for (i = 0; i < n_buttons; i++)
    {
        if (buttons[i] == 1)
            idx_1 = i;
        else if (buttons[i] == ((n_buttons < 3) ? 2 : 3))
            idx_3 = i;
    }

    if (idx_1 < 0 || idx_3 < 0)
        return FALSE;
    if ((left_handed && idx_1 < idx_3) ||
        (!left_handed && idx_1 > idx_3))
    {
        buttons[idx_1] = ((n_buttons < 3) ? 2 : 3);
        buttons[idx_3] = 1;
        if (set_pointer_mapping(xdisplay(), buttons, n_buttons) != MappingSuccess)
            return FALSE;
        XSync(xdisplay(), False);
        unsigned char verify[256];
        int count = XGetPointerMapping(xdisplay(), verify, sizeof verify);
        if (count != n_buttons || memcmp(buttons, verify, count) != 0)
            return FALSE;
    }
    return TRUE;
#else
    return FALSE;
#endif
}

static void on_left_handed_toggle(GtkCheckButton* btn, gpointer user_data)
{
    gboolean previous = left_handed;
    left_handed = gtk_check_button_get_active(btn);
    if (!set_left_handed_mouse()) {
        left_handed = previous;
        g_signal_handlers_block_by_func(btn, G_CALLBACK(on_left_handed_toggle), user_data);
        gtk_check_button_set_active(btn, previous);
        g_signal_handlers_unblock_by_func(btn, G_CALLBACK(on_left_handed_toggle), user_data);
        set_status("The pointer button mapping is busy or unsupported.");
    } else set_status(NULL);
}

static void on_kb_beep_toggle(GtkCheckButton* btn, gpointer user_data)
{
#ifdef GDK_WINDOWING_X11
    XKeyboardControl values;
    beep = gtk_check_button_get_active(btn);
    if(have_x11())
    {
        values.bell_percent = beep ? -1 : 0;
        XChangeKeyboardControl(xdisplay(), KBBellPercent, &values);
        XSync(xdisplay(), False);
        XKeyboardState state;
        if (!XGetKeyboardControl(xdisplay(), &state) ||
            (beep != (state.bell_percent != 0)))
            set_status("The keyboard bell setting could not be applied.");
        else set_status(NULL);
    }
#else
    beep = gtk_check_button_get_active(btn);
#endif
}

static gchar* detect_keymap_program(void)
{
    return g_find_program_in_path("lxkeymap");
}

static void on_kb_layout_clicked(GtkButton *button,  gpointer   user_data)
{
    (void)button; (void)user_data;
    g_autofree gchar *program = detect_keymap_program();

    if (program)
    {
        GError *error = NULL;
        gchar *argv[] = { program, NULL };
        if (!g_spawn_async(NULL, argv, NULL, 0, NULL, NULL, NULL, &error))
            g_warning("Cannot run keymap tool '%s': %s", program, error ? error->message : "unknown");
        g_clear_error(&error);
    }

}


static char *settings_relative_path(void)
{
    const char* session_name = g_getenv("DESKTOP_SESSION");
    gboolean valid = session_name && *session_name && strlen(session_name) <= 64;
    for (const unsigned char *p = (const unsigned char *)session_name; valid && *p; p++)
        valid = g_ascii_isalnum(*p) || *p == '_' || *p == '-';
    if (!valid)
        session_name = "LXDE";
    else if (g_ascii_strcasecmp(session_name, "luxe") == 0)
        session_name = "LXDE";
    return g_build_filename("luxesession", session_name, "desktop.conf", NULL);
}

static GKeyFile *read_settings_file(char **user_path, GError **error)
{
    g_autofree char *relative = settings_relative_path();
    g_autofree char *path = g_build_filename(g_get_user_config_dir(), relative, NULL);
    GKeyFile *kf = g_key_file_new();
    const char * const *dirs = g_get_system_config_dirs();
    if (user_path) *user_path = g_strdup(path);
    for (;;) {
        GError *local = NULL;
        if (g_key_file_load_from_file(kf, path, G_KEY_FILE_KEEP_COMMENTS | G_KEY_FILE_KEEP_TRANSLATIONS, &local))
            return kf;
        if (!g_error_matches(local, G_FILE_ERROR, G_FILE_ERROR_NOENT)) {
            g_propagate_error(error, local);
            g_key_file_unref(kf);
            return NULL;
        }
        g_clear_error(&local);
        if (!*dirs) return kf;
        g_free(path);
        path = g_build_filename(*dirs++, relative, NULL);
    }
}

static void load_settings(void)
{
    GError *error = NULL;
    GKeyFile *kf = read_settings_file(NULL, &error);
    if (!kf) {
        g_warning("Cannot read LuxeInput settings: %s", error->message);
        g_clear_error(&error);
        kf = g_key_file_new();
    }

    int val;
    val = g_key_file_get_integer(kf, "Mouse", "AccFactor", NULL);
    if( val > 0)
        old_accel = accel = val;

    val = g_key_file_get_integer(kf, "Mouse", "AccThreshold", NULL);
    if (val >= 0)
        old_threshold = threshold = val;

    old_left_handed = left_handed = g_key_file_get_boolean(kf, "Mouse", "LeftHanded", NULL);

    val = g_key_file_get_integer(kf, "Keyboard", "Delay", NULL);
    if(val > 0)
        old_delay = delay = val;
    val = g_key_file_get_integer(kf, "Keyboard", "Interval", NULL);
    if(val > 0)
        old_interval = interval = val;

    if( g_key_file_has_key(kf, "Keyboard", "Beep", NULL ) )
        old_beep = beep = g_key_file_get_boolean(kf, "Keyboard", "Beep", NULL);

    g_key_file_free(kf);

#ifdef GDK_WINDOWING_X11
    /* Capture the live bell state so Cancel can put it back exactly. */
    if(have_x11())
    {
        XGetPointerControl(xdisplay(), &old_accel_num, &old_accel_den, &old_threshold);
        if (old_accel_den > 0) {
            accel = old_accel = (old_accel_num * 10 + old_accel_den / 2) / old_accel_den;
            threshold = old_threshold;
        }
        old_pointer_count = XGetPointerMapping(xdisplay(), old_pointer_map, sizeof old_pointer_map);
        for (int i = 0; i < old_pointer_count; i++) {
            if (old_pointer_map[i] == 1) old_left_handed = left_handed = (i > 0);
        }
        unsigned int live_delay, live_interval;
        if (XkbGetAutoRepeatRate(xdisplay(), XkbUseCoreKbd, &live_delay, &live_interval)) {
            delay = old_delay = (int)live_delay;
            interval = old_interval = (int)live_interval;
        }
        XKeyboardState state;
        if(XGetKeyboardControl(xdisplay(), &state))
        {
            old_bell_percent = state.bell_percent;
            old_bell_pitch = state.bell_pitch;
            old_bell_duration = state.bell_duration;
            beep = old_beep = state.bell_percent != 0;
        }
    }
#endif

}

/* GTK4 has no GtkDialog/gtk_dialog_run: the main window is an
 * GtkApplicationWindow with OK/Cancel in the header bar, wired as ordinary
 * callbacks. The application quits automatically when its last window
 * closes, so no nested main loop or explicit g_application_quit is needed. */
static gboolean handled = FALSE;

static gboolean save_settings_to_files(GError **error);
static void restore_original_settings(void);

static void finish_and_quit(gboolean save)
{
    if(handled)
        return;
    if(save) {
        GError *error = NULL;
        if (!save_settings_to_files(&error)) {
            GtkDialog *alert = GTK_DIALOG(luxe_choice_dialog_new("Could not save input settings", error->message));
            luxe_choice_add_response(alert, "close", "Close");
            luxe_choice_set_close_response(alert, "close");
            luxe_dialog_present(GTK_WINDOW(alert), dlg);
            g_clear_error(&error);
            return;
        }
    } else
        restore_original_settings();
    handled = TRUE;

    if(dlg)
    {
        gtk_window_destroy(GTK_WINDOW(dlg));
        dlg = NULL;
    }
    /* Returning to the main loop lets GtkApplication quit naturally with
     * its last window (quitting explicitly during teardown races GDK's
     * X11 surface cleanup and produces BadDrawable noise). */
}

static void on_ok_clicked(GtkButton* btn, gpointer user_data)
{
    finish_and_quit(TRUE);
}

static void on_cancel_clicked(GtkButton* btn, gpointer user_data)
{
    finish_and_quit(FALSE);
}

static gboolean on_window_close(GtkWindow* window, gpointer user_data)
{
    /* Treat window-manager close as Cancel. Restore settings here, then
     * return FALSE so the default handler destroys the window;
     * finish_and_quit's destroy is skipped via handled + dlg == NULL. */
    if(!handled)
    {
        handled = TRUE;
        restore_original_settings();
        dlg = NULL;
    }
    return FALSE;
}

static GtkWidget* lookup_widget(GtkBuilder* builder, const char* name)
{
    GtkWidget* w = GTK_WIDGET(gtk_builder_get_object(builder, name));
    if(!w)
        g_warning("luxeinput: widget '%s' not found in UI file", name);
    return w;
}

static void build_main_window(GtkApplication* application)
{
    GtkBuilder* builder;
    GtkWidget* ok_btn;
    GtkWidget* cancel_btn;

    /* build the UI */
    builder = gtk_builder_new();

    /* UI loads only from the installed datadir path. */
    GError* err = NULL;
#ifdef PACKAGE_DATA_DIR
    {
        char* path = g_build_filename(PACKAGE_DATA_DIR, "luxeinput.ui", NULL);
        if(!gtk_builder_add_from_file(builder, path, &err))
        {
            g_warning("luxeinput: cannot load UI file '%s': %s",
                      path, err ? err->message : "unknown error");
            g_clear_error(&err);
            g_free(path);
            g_object_unref(builder);
            return;
        }
        g_free(path);
    }
#else
    g_warning("luxeinput: cannot load luxeinput.ui: PACKAGE_DATA_DIR is not defined");
    g_object_unref(builder);
    return;
#endif

    dlg = lookup_widget(builder, "dlg");
    gtk_window_set_icon_name(GTK_WINDOW(dlg), "org.luxe.Input");
    luxe_add_about_button(GTK_WINDOW(dlg),
                          GTK_BUTTON(gtk_builder_get_object(builder, "about")),
                          "LuxeInput", VERSION, "Keyboard and mouse settings for Luxe.");
    gtk_window_set_application(GTK_WINDOW(dlg), application);

    mouse_accel = GTK_RANGE(gtk_builder_get_object(builder, "mouse_accel"));
    mouse_threshold = GTK_RANGE(gtk_builder_get_object(builder, "mouse_threshold"));
    mouse_left_handed = GTK_CHECK_BUTTON(gtk_builder_get_object(builder, "left_handed"));

    kb_delay = GTK_RANGE(gtk_builder_get_object(builder, "kb_delay"));
    kb_interval = GTK_RANGE(gtk_builder_get_object(builder, "kb_interval"));
    kb_beep = GTK_CHECK_BUTTON(gtk_builder_get_object(builder, "beep"));
    kb_layout = GTK_BUTTON(gtk_builder_get_object(builder, "keyboard_layout"));
    status_label = GTK_LABEL(gtk_builder_get_object(builder, "status_label"));
    gtk_widget_set_visible(GTK_WIDGET(status_label), FALSE);

    if (!have_x11()) {
        GtkWidget *controls[] = { GTK_WIDGET(mouse_accel), GTK_WIDGET(mouse_threshold),
            GTK_WIDGET(mouse_left_handed), GTK_WIDGET(kb_delay), GTK_WIDGET(kb_interval),
            GTK_WIDGET(kb_beep), NULL };
        for (guint i = 0; controls[i] != NULL; i++) {
            gtk_widget_set_sensitive(controls[i], FALSE);
            gtk_widget_set_tooltip_text(controls[i],
                "Live pointer and keyboard controls require an X11 session.");
        }
    }
#ifdef GDK_WINDOWING_X11
    else if (!have_xkb()) {
        GtkWidget *controls[] = { GTK_WIDGET(kb_delay), GTK_WIDGET(kb_interval),
            GTK_WIDGET(kb_beep), NULL };
        for (guint i = 0; controls[i] != NULL; i++) {
            gtk_widget_set_sensitive(controls[i], FALSE);
            gtk_widget_set_tooltip_text(controls[i], "Keyboard repeat controls require XKB support.");
        }
    }
#endif

    g_autofree gchar *program = detect_keymap_program();
    if (program == NULL)
    {
        /* Keep the optional capability visible and explain why it is unavailable. */
        gtk_button_set_label(kb_layout, "Keyboard layout unavailable");
        gtk_widget_set_sensitive(GTK_WIDGET(kb_layout), FALSE);
        gtk_widget_set_tooltip_text(GTK_WIDGET(kb_layout),
            "Install the optional lxkeymap utility to edit keyboard layouts.");
    }
    else
    {
        gtk_button_set_label(kb_layout, "Keyboard layout…");
    }

    ok_btn = lookup_widget(builder, "ok");
    cancel_btn = lookup_widget(builder, "cancel");

    g_object_unref(builder);

    /* read the config file */
    load_settings();

    /* init the UI */
    gtk_range_set_value(mouse_accel, (gdouble)accel / 10.0);
    accel = (int)(gtk_range_get_value(mouse_accel) * 10.0 + 0.5);
    gtk_range_set_value(mouse_threshold, threshold);
    threshold = (int)(gtk_range_get_value(mouse_threshold) + 0.5);
    gtk_check_button_set_active(mouse_left_handed, left_handed);

    gtk_range_set_value(kb_delay, delay);
    delay = (int)(gtk_range_get_value(kb_delay) + 0.5);
    gtk_range_set_value(kb_interval, interval);
    interval = (int)(gtk_range_get_value(kb_interval) + 0.5);
    gtk_check_button_set_active(kb_beep, beep);

    g_signal_connect(mouse_accel, "value-changed", G_CALLBACK(on_mouse_accel_changed), NULL);
    g_signal_connect(mouse_threshold, "value-changed", G_CALLBACK(on_mouse_threshold_changed), NULL);
    g_signal_connect(mouse_left_handed, "toggled", G_CALLBACK(on_left_handed_toggle), NULL);

    g_signal_connect(kb_delay, "value-changed", G_CALLBACK(on_kb_range_changed), &delay);
    g_signal_connect(kb_interval, "value-changed", G_CALLBACK(on_kb_range_changed), &interval);
    g_signal_connect(kb_beep, "toggled", G_CALLBACK(on_kb_beep_toggle), NULL);
    g_signal_connect(kb_layout, "clicked", G_CALLBACK(on_kb_layout_clicked), NULL);

    g_signal_connect(ok_btn, "clicked", G_CALLBACK(on_ok_clicked), NULL);
    g_signal_connect(cancel_btn, "clicked", G_CALLBACK(on_cancel_clicked), NULL);
    g_signal_connect(dlg, "close-request", G_CALLBACK(on_window_close), NULL);
}

static gboolean save_settings_to_files(GError **error)
{
    g_autofree char *path = NULL;
    g_autoptr(GKeyFile) kf = read_settings_file(&path, error);
    if (!kf) return FALSE;
    g_autofree char *directory = g_path_get_dirname(path);
    if (g_mkdir_with_parents(directory, 0700) != 0) {
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno),
                    "Cannot create settings directory: %s", g_strerror(errno));
        return FALSE;
    }
    g_key_file_set_integer(kf, "Mouse", "AccFactor", accel);
    g_key_file_set_integer(kf, "Mouse", "AccThreshold", threshold);
    g_key_file_set_boolean(kf, "Mouse", "LeftHanded", left_handed);
    g_key_file_set_integer(kf, "Keyboard", "Delay", delay);
    g_key_file_set_integer(kf, "Keyboard", "Interval", interval);
    g_key_file_set_boolean(kf, "Keyboard", "Beep", beep);
    return g_key_file_save_to_file(kf, path, error);
}

static void restore_original_settings(void)
{
    /* restore to original settings */

    /* keyboard */
    delay = old_delay;
    interval = old_interval;
    beep = old_beep;
#ifdef GDK_WINDOWING_X11
    if(have_x11())
        XkbSetAutoRepeatRate(xdisplay(), XkbUseCoreKbd, delay, interval);
#endif
#ifdef GDK_WINDOWING_X11
    /* Cancel must restore the bell exactly as Cancel restores the other
     * live X settings; the save path persists it separately. */
    if(have_x11())
        XChangeKeyboardControl(xdisplay(), KBBellPercent | KBBellPitch | KBBellDuration,
            &(XKeyboardControl){ .bell_percent = old_bell_percent,
                .bell_pitch = old_bell_pitch, .bell_duration = old_bell_duration });
#endif

    /* mouse */
    accel = old_accel;
    threshold = old_threshold;
    left_handed = old_left_handed;
#ifdef GDK_WINDOWING_X11
    if(have_x11()) {
        XChangePointerControl(xdisplay(), True, True, old_accel_num, old_accel_den, old_threshold);
        if (old_pointer_count > 0 && old_pointer_count <= sizeof old_pointer_map)
            XSetPointerMapping(xdisplay(), old_pointer_map, old_pointer_count);
    }
#endif
}

static void on_activate(GtkApplication* application, gpointer user_data)
{
    if (dlg) {
        gtk_window_present(GTK_WINDOW(dlg));
        return;
    }
    handled = FALSE;
    build_main_window(application);
    if(!dlg)
    {
        g_application_quit(G_APPLICATION(application));
        return;
    }

    gtk_window_present(GTK_WINDOW(dlg));
}

int main(int argc, char** argv)
{
#ifdef ENABLE_NLS
    bindtextdomain ( GETTEXT_PACKAGE, PACKAGE_LOCALE_DIR );
    bind_textdomain_codeset ( GETTEXT_PACKAGE, "UTF-8" );
    textdomain ( GETTEXT_PACKAGE );
#endif

    luxe_ui_init();
    GtkApplication* app = luxe_application_new("org.luxe.Input", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);

    return status;
}
