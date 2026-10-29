#include "../luxe-about.h"
#include "luxe-spacing.h"
#include "../luxe-dialogs.h"
/*
 *      lxrandr.c - Easy-to-use XRandR GUI frontend for LXDE project
 *
 *      Copyright (C) 2008 Hong Jen Yee(PCMan) <pcman.tw@gmail.com>
 *      Copyright (C) 2011 Julien Lavergne <julien.lavergne@gmail.com>
 *      Copyright (C) 2014 Andriy Grytsenko (LStranger) <andrej@rep.kiev.ua>
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

#include "command.h"
#include "window-placement.h"
/* Compiled-in About version must track the Luxe DEB_VERSION (bible rule);
 * CMake provides it as LUXE_VERSION. Undef first to avoid a redefinition
 * warning should any header define VERSION. */
#ifdef LUXE_VERSION
#undef VERSION
#define VERSION LUXE_VERSION
#endif

#include <gtk/gtk.h>
#include "luxe-gtk-ui.h"
#include <glib/gi18n.h>
#ifdef ENABLE_GTK4
#include <gdk/x11/gdkx.h>
#include <X11/Xatom.h>
#include <X11/extensions/Xrandr.h>
#endif

#include <locale.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <gio/gunixinputstream.h>

#ifndef LUXERANDR_QUERY_TIMEOUT
#define LUXERANDR_QUERY_TIMEOUT "5s"
#endif
#ifndef LUXERANDR_GUARD_PATH
#define LUXERANDR_GUARD_PATH "/usr/libexec/luxerandr-guard"
#endif

static GtkWidget* dlg = NULL;
static GtkDialog *confirmation = NULL;
static guint confirmation_source = 0;
static GString *rollback_command = NULL;
typedef struct {
    GSubprocess *guard;
    GDataInputStream *output;
    gboolean keep_requested;
    gboolean abandoned;
} DisplayTransaction;
static DisplayTransaction *transaction;
static GCancellable *query_cancel;
static gboolean query_pending;
static gboolean topology_changed;
static void reject_confirmation(void);

static const char* get_human_readable_name( Monitor* m )
{
    if( m == LVDS )
        return _("Laptop LCD Monitor");
    else if( g_str_has_prefix( m->name, "VGA" ) || g_str_has_prefix( m->name, "Analog" ) )
        return LVDS ? _("External VGA Monitor") : _("VGA Monitor");
    else if( g_str_has_prefix( m->name, "DVI" ) || g_str_has_prefix(m->name, "TMDS") || g_str_has_prefix(m->name, "Digital") || g_str_has_prefix(m->name, "LVDS") )
        return LVDS ? _("External DVI Monitor") : _("DVI Monitor");
    else if( g_str_has_prefix( m->name, "TV" ) || g_str_has_prefix(m->name, "S-Video") )
        return _("TV");
    else if( strcmp( m->name, "default" ) == 0 )
        return _( "Default Monitor");

    return m->name;
}

static gboolean parse_xrandr_output(const char *output)
{
    free_monitors();
    gchar **lines = g_strsplit(output, "\n", -1);
    g_autoptr(GRegex) token_splitter = g_regex_new("[ \\t]+", 0, 0, NULL);
    Monitor *monitor = NULL;
    for (gchar **line = lines; *line; line++) {
        gboolean indented = g_ascii_isspace((*line)[0]);
        gchar **tokens = g_regex_split(token_splitter, g_strstrip(*line), 0);
        guint count = g_strv_length(tokens);
        if (!indented) {
            monitor = NULL;
            if (count >= 2 && g_str_equal(tokens[1], "connected")) {
                monitor = g_new0(Monitor, 1);
                monitor->name = g_strdup(tokens[0]);
                monitor->active_mode = monitor->active_rate = -1;
                monitor->pref_mode = monitor->pref_rate = -1;
                monitors = g_slist_append(monitors, monitor);
                if (!LVDS && (g_str_has_prefix(monitor->name, "LVDS") ||
                    g_str_has_prefix(monitor->name, "PANEL") ||
                    g_str_has_prefix(monitor->name, "eDP") ||
                    g_str_has_prefix(monitor->name, "EDP") ||
                    g_str_has_prefix(monitor->name, "DSI")))
                    LVDS = monitor;
                for (guint i = 2; i < count; i++) {
                    if (!g_ascii_isdigit(tokens[i][0]) || !strchr(tokens[i], 'x'))
                        continue;
                    char *coords = strpbrk(tokens[i], "+-"), *end;
                    if (!coords) continue;
                    gint64 x = g_ascii_strtoll(coords, &end, 10);
                    if (*end != '+' && *end != '-') continue;
                    gint64 y = g_ascii_strtoll(end, &end, 10);
                    if (*end || x < G_MININT || x > G_MAXINT || y < G_MININT || y > G_MAXINT) continue;
                    monitor->x = x;
                    monitor->y = y;
                    monitor->has_position = TRUE;
                    if (x == 0 && y > 0) monitor->placement = PLACEMENT_BELOW;
                    else if (y == 0 && x > 0) monitor->placement = PLACEMENT_RIGHT;
                }
            }
        } else if (monitor && count >= 2 && *tokens[0]) {
            GPtrArray *mode = g_ptr_array_new_with_free_func(g_free);
            g_ptr_array_add(mode, g_strdup(tokens[0]));
            gint index = g_slist_length(monitor->mode_lines);
            for (guint i = 1; i < count; i++) {
                char *end;
                gdouble rate = g_ascii_strtod(tokens[i], &end);
                if (end != tokens[i] && rate > 0 &&
                    strspn(end, "*+") == strlen(end)) {
                    if (strchr(end, '*')) {
                        monitor->active_mode = index;
                        monitor->active_rate = mode->len - 1;
                    }
                    if (strchr(end, '+')) {
                        monitor->pref_mode = index;
                        monitor->pref_rate = mode->len - 1;
                    }
                    g_ptr_array_add(mode, g_strndup(tokens[i], end - tokens[i]));
                } else if (g_str_equal(tokens[i], "+") && mode->len > 1) {
                    monitor->pref_mode = index;
                    monitor->pref_rate = mode->len - 2;
                }
            }
            if (mode->len > 1) {
                g_ptr_array_add(mode, NULL);
                monitor->mode_lines = g_slist_append(monitor->mode_lines,
                    g_ptr_array_free(mode, FALSE));
            } else
                g_ptr_array_unref(mode);
        }
        g_strfreev(tokens);
    }
    g_strfreev(lines);
    return monitors != NULL;
}

static gboolean same_topology(GSList *expected, GSList *observed)
{
    if (g_slist_length(expected) != g_slist_length(observed)) return FALSE;
    for (GSList *e=expected;e;e=e->next) {
        Monitor *old=e->data,*fresh=NULL;
        for (GSList *o=observed;o;o=o->next)
            if (g_strcmp0(old->name,((Monitor *)o->data)->name)==0) { fresh=o->data;break; }
        if (!fresh) return FALSE;
        if (old->has_position && fresh->has_position &&
            (old->x!=fresh->x || old->y!=fresh->y)) return FALSE;
        GSList *a=old->mode_lines,*b=fresh->mode_lines;
        for (;a&&b;a=a->next,b=b->next) {
            char **left=a->data,**right=b->data;
            for (guint i=0;left[i]||right[i];i++)
                if (g_strcmp0(left[i],right[i])!=0) return FALSE;
        }
        if (a||b) return FALSE;
    }
    return TRUE;
}

static char *query_xrandr_output(GCancellable *cancel)
{
    const char *argv[] = { "/usr/bin/timeout", "--kill-after=2s", LUXERANDR_QUERY_TIMEOUT,
                           "xrandr", "--query", NULL };
    g_autoptr(GSubprocessLauncher) launcher = g_subprocess_launcher_new(
        G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_SILENCE);
    g_subprocess_launcher_setenv(launcher, "LC_ALL", "C", TRUE);
    g_autoptr(GSubprocess) process = g_subprocess_launcher_spawnv(launcher, argv, NULL);
    if (!process) return NULL;
    GInputStream *stream = g_subprocess_get_stdout_pipe(process);
    if (!G_IS_UNIX_INPUT_STREAM(stream)) { g_subprocess_force_exit(process); g_subprocess_wait(process, NULL, NULL); return NULL; }
    int fd = g_unix_input_stream_get_fd(G_UNIX_INPUT_STREAM(stream));
    int flags = fcntl(fd, F_GETFL);
    gboolean ok = flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) >= 0;
    gboolean eof = FALSE;
    gint64 deadline = g_get_monotonic_time() + 7 * G_TIME_SPAN_SECOND;
    g_autoptr(GByteArray) output = g_byte_array_new();
    while (ok && !eof && (!cancel || !g_cancellable_is_cancelled(cancel)) && g_get_monotonic_time() < deadline) {
        GPollFD pollfd = {.fd=fd, .events=G_IO_IN | G_IO_HUP | G_IO_ERR};
        int ready = g_poll(&pollfd, 1, 100);
        if (ready < 0) { if (errno == EINTR) continue; ok = FALSE; break; }
        if (!ready) continue;
        guint8 block[16384];
        ssize_t n = read(fd, block, sizeof block);
        if (n > 0) {
            if ((gsize)n > 1024 * 1024 - output->len) ok = FALSE;
            else g_byte_array_append(output, block, (guint)n);
        } else if (n == 0) eof = TRUE;
        else if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) ok = FALSE;
    }
    if (!eof) ok = FALSE;
    if (!ok) {
        /* timeout forwards TERM to the owned command group and has its own
         * forced-kill fallback; close our pipe before reaping it. */
        g_subprocess_send_signal(process, SIGTERM);
        g_input_stream_close(stream, NULL, NULL);
    }
    if (!g_subprocess_wait(process, NULL, NULL) || !g_subprocess_get_successful(process)) ok = FALSE;
    if (ok && (!output->len || !g_utf8_validate((char *)output->data, output->len, NULL) ||
               memchr(output->data, 0, output->len))) ok = FALSE;
    if (!ok) return NULL;
    g_byte_array_append(output, (const guint8 *)"", 1);
    return (char *)g_byte_array_free(g_steal_pointer(&output), FALSE);
}

static void query_worker(GTask *task, gpointer source, gpointer data, GCancellable *cancel)
{
    (void)source; (void)data;
    char *output = query_xrandr_output(cancel);
    if (g_task_return_error_if_cancelled(task)) { g_free(output); return; }
    if (!output) g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_FAILED, "Display query failed or exceeded its limits.");
    else g_task_return_pointer(task, output, g_free);
}

static GString *snapshot_configuration(const char *output)
{
    /* Re-query immediately before applying, without replacing UI-owned
     * monitor objects. This retains exact offsets rather than inferred
     * right-of/above relationships in the rollback command. */
    GSList *current = monitors;
    Monitor *current_lvds = LVDS;
    topology_changed = FALSE;
    monitors = NULL;
    LVDS = NULL;
    GString *command = NULL;
    gboolean parsed = output && parse_xrandr_output(output);
    if (parsed && same_topology(current,monitors)) {
        for (GSList *l = monitors; l; l = l->next) {
            Monitor *m = l->data;
            m->try_mode = m->active_mode < 0 ? -1 : m->active_mode + 1;
            m->try_rate = m->active_rate + 1;
        }
        command = get_command_xrandr_restore();
    } else if (parsed) topology_changed = TRUE;
    free_monitors();
    monitors = current;
    LVDS = current_lvds;
    return command;
}

static gboolean current_topology_matches(const char *output)
{
    GSList *saved=monitors;
    Monitor *saved_lvds=LVDS;
    monitors=NULL;LVDS=NULL;
    gboolean parsed=output && parse_xrandr_output(output);
    gboolean matches=parsed && same_topology(saved,monitors);
    free_monitors();monitors=saved;LVDS=saved_lvds;
    return matches;
}

static void on_enable_toggled(GtkCheckButton *cb, Monitor* m)
{
    GSList *l;
    Monitor *fixed = LVDS && gtk_check_button_get_active(LVDS->enable) ? LVDS : NULL;
    int i;
    gboolean can_position;
    (void)cb;
    (void)m;

    for (l = monitors, i = 0; l; l = l->next)
    {
        Monitor *cur = (Monitor*)l->data;
        if (gtk_check_button_get_active(cur->enable))
        {
            i++;
            if (!fixed) fixed = cur;
        }
    }
    can_position = (i > 1);

    if (monitors->next != NULL) for (l = monitors; l; l = l->next)
    {
        Monitor *cur = (Monitor*)l->data;
        gtk_widget_set_sensitive(GTK_WIDGET(cur->pos_combo), can_position &&
            gtk_check_button_get_active(cur->enable) && (cur != fixed));
        if (!can_position || cur == fixed)
            gtk_combo_box_set_active(GTK_COMBO_BOX(cur->pos_combo), 0);
    }
}

static void on_res_sel_changed( GtkComboBox* cb, Monitor* m )
{
    char** rate;
    int sel = gtk_combo_box_get_active( cb );
    char** mode_line = g_slist_nth_data( m->mode_lines, sel - 1 );
    gtk_list_store_clear( GTK_LIST_STORE(gtk_combo_box_get_model( GTK_COMBO_BOX(m->rate_combo) )) );
    gtk_combo_box_text_append_text( m->rate_combo, _("Auto") );
    if( sel >= 0 && mode_line && *mode_line )
    {
        for( rate = mode_line + 1; *rate; ++rate )
        {
            gtk_combo_box_text_append_text( m->rate_combo, *rate );
        }
    }
    gtk_combo_box_set_active( GTK_COMBO_BOX(m->rate_combo), 0 );
}

static void on_show_about( GSimpleAction* action, GVariant* param, gpointer data )
{
    GtkWindow* parent;
    (void)action;
    (void)param;
    parent = gtk_application_get_active_window( GTK_APPLICATION(data) );
    if( !parent )
        parent = GTK_WINDOW(dlg);
    luxe_show_about_full( parent, _( "LuxeRandR" ), VERSION,
                          _( "Monitor configuration tool for Luxe" ), _( "translator-credits" ) );
}

static void on_quit( GSimpleAction* action, GVariant* param, gpointer data )
{
    (void)action;
    (void)param;
    reject_confirmation();
    if (query_cancel) g_cancellable_cancel(query_cancel);
    g_application_quit( G_APPLICATION(data) );
}

static const GActionEntry app_actions[] =
{
    { .name = "about", .activate = on_show_about },
    { .name = "quit", .activate = on_quit }
};

/* Restored from upstream lxrandr (GPL-2+); the Luxe port stores plain
 * "WIDTHxHEIGHT" mode strings in mode_lines, so the command is built
 * directly from mode_lines instead of the combo's display model. */
static void prepare_try_values_from_GUI()
{
    GSList *l;

    for (l = monitors; l; l = l->next)
    {
        Monitor *m = (Monitor*)l->data;
        if (!gtk_check_button_get_active(m->enable))
            m->try_mode = -1;
        else
            m->try_mode = gtk_combo_box_get_active(GTK_COMBO_BOX(m->res_combo));
        m->try_rate = gtk_combo_box_get_active(GTK_COMBO_BOX(m->rate_combo));
        if (m->pos_combo)
            m->try_placement = gtk_combo_box_get_active(GTK_COMBO_BOX(m->pos_combo));
        else
            m->try_placement = PLACEMENT_DEFAULT;
        /* g_debug("mode %d rate %d placement %d",m->try_mode,m->try_rate,m->try_placement); */
    }
}

static gboolean save_configuration(GError **error)
{

    char* dirname;
    const char grp[] = "Desktop Entry";
    GKeyFile* kf;

    char* file, *data;
    gsize len;

    GString *cmd;

    prepare_try_values_from_GUI();
    cmd = get_desktop_exec_xrandr_info();
    if (!cmd)
    {
        g_set_error_literal(error, G_SPAWN_ERROR, G_SPAWN_ERROR_FAILED,
                            "The display configuration contains an unsafe or unavailable value");
        return FALSE;
    }

    /* Save only the settings represented by the last accepted layout. */
    for (GSList *l=monitors;l;l=l->next) {
        Monitor *m=l->data;
        if (m->try_mode!=m->active_mode ||
            (m->try_mode>=0 && (m->try_rate!=m->active_rate || m->try_placement!=m->placement))) {
            g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_BUSY,
                                _("Apply and confirm these display changes before saving them."));
            g_string_free(cmd,TRUE);
            return FALSE;
        }
    }

    /* create user autostart dir */
    dirname = g_build_filename(g_get_user_config_dir(), "autostart", NULL);
    g_mkdir_with_parents(dirname, 0700);
    g_free(dirname);

    kf = g_key_file_new();

    g_key_file_set_string( kf, grp, "Type", "Application" );
    g_key_file_set_string( kf, grp, "Name", _("LuxeRandR autostart") );
    g_key_file_set_string( kf, grp, "Comment", _("Start xrandr with settings done in LuxeRandR") );
    g_key_file_set_string( kf, grp, "Exec", cmd->str );
    g_key_file_set_string( kf, grp, "OnlyShowIn", "X-Luxe;LXDE;" );

    data = g_key_file_to_data(kf, &len, NULL);
    file = g_build_filename(  g_get_user_config_dir(), 
                              "autostart", 
                              "luxerandr-autostart.desktop", 
                              NULL );
    /* save it to user-specific autostart dir */
    g_debug("save to: %s", file);
    gboolean saved = g_file_set_contents(file, data, len, error);
    g_key_file_free (kf);
    g_free(file);
    g_free(data);
    g_string_free(cmd, TRUE);
    return saved;
}

static void clamp_windows_to_screen(void);
static void ask_apply_confirmation(void);

static void transaction_decide(DisplayTransaction *tx, char decision)
{
    if (!tx) return;
    GOutputStream *input = g_subprocess_get_stdin_pipe(tx->guard);
    if (!input) return;
    if (decision == 'K') {
        g_autoptr(GError) error = NULL;
        if (!g_output_stream_write_all(input, &decision, 1, NULL, NULL, &error))
            tx->keep_requested = FALSE;
    }
    /* EOF means Revert, and also covers a failed Keep write. */
    g_output_stream_close(input, NULL, NULL);
}

/* One modal confirmation owns one timeout. Keep, close, timeout and Quit
 * all complete the same state transition exactly once. */
static void on_apply_confirmation_response(GtkDialog *dialog, const char *response, gpointer data)
{
    (void)data;
    if (confirmation != dialog) return;
    if (confirmation_source) {
        g_source_remove(confirmation_source);
        confirmation_source = 0;
    }
    confirmation = NULL;
    if (transaction) {
        transaction->keep_requested = g_str_equal(response, "keep");
        transaction_decide(transaction, transaction->keep_requested ? 'K' : 'R');
    }
    g_object_unref(dialog);
}

static void reject_confirmation(void)
{
    if (!confirmation) return;
    GtkDialog *dialog = g_object_ref(confirmation);
    /* Force-closing emits the close response. Complete explicitly if a
     * presentation transition defers that signal; the callback is idempotent. */
    luxe_dialog_close(GTK_WINDOW(dialog));
    if (confirmation == dialog)
        on_apply_confirmation_response(dialog, "revert", NULL);
    g_object_unref(dialog);
}

static gboolean apply_confirmation_timeout( gpointer data )
{
    (void)data;
    confirmation_source = 0;
    reject_confirmation();
    return FALSE;
}

static void ask_apply_confirmation( void )
{
    confirmation = GTK_DIALOG(g_object_ref_sink(luxe_choice_dialog_new(
        _("Keep these display settings?"),
        _("Confirm within 15 seconds, otherwise the previous state will be restored."))));
    luxe_choice_add_responses(confirmation, "keep", _("Keep"), "revert", _("Revert"), NULL);
    luxe_choice_set_default_response(confirmation, "revert");
    luxe_choice_set_close_response(confirmation, "revert");
    g_signal_connect(confirmation, "luxe-response", G_CALLBACK(on_apply_confirmation_response), NULL);
    confirmation_source = g_timeout_add(15000, apply_confirmation_timeout, NULL);
    luxe_dialog_present(GTK_WINDOW(confirmation), dlg);
}

static void close_confirmation_silently(void)
{
    if (confirmation_source) { g_source_remove(confirmation_source); confirmation_source = 0; }
    if (!confirmation) return;
    GtkDialog *dialog = confirmation;
    confirmation = NULL;
    luxe_dialog_close(GTK_WINDOW(dialog));
    g_object_unref(dialog);
}

static void transaction_read_next(DisplayTransaction *tx);
static void transaction_read_done(GObject *source, GAsyncResult *result, gpointer data)
{
    DisplayTransaction *tx = data;
    g_autoptr(GError) error = NULL;
    g_autofree char *line = g_data_input_stream_read_line_finish(G_DATA_INPUT_STREAM(source), result, NULL, &error);
    if (line && g_str_equal(line, "READY")) {
        if (!tx->abandoned && dlg) { ask_apply_confirmation(); }
        else transaction_decide(tx, 'R');
        transaction_read_next(tx);
        return;
    }
    close_confirmation_silently();
    if (!tx->abandoned && dlg) {
        if (line && g_str_equal(line, "KEPT") && tx->keep_requested) {
            clamp_windows_to_screen();
            for (GSList *l = monitors; l; l = l->next) {
                Monitor *m = l->data;
                if (m->try_placement != m->placement)
                    m->has_position = FALSE;
                m->active_mode = m->try_mode; m->active_rate = m->try_rate; m->placement = m->try_placement;
            }
        } else if (line && (g_str_equal(line, "REVERTED") ||
                            g_str_has_prefix(line, "APPLY_FAILED_RESTORED:"))) {
            for (GSList *l = monitors; l; l = l->next) {
                Monitor *m = l->data;
                m->try_mode = m->active_mode; m->try_rate = m->active_rate; m->try_placement = m->placement;
                gtk_check_button_set_active(m->enable, m->active_mode >= 0);
                gtk_combo_box_set_active(GTK_COMBO_BOX(m->res_combo), MAX(0, m->active_mode));
                gtk_combo_box_set_active(GTK_COMBO_BOX(m->rate_combo), MAX(0, m->active_rate));
                if (m->pos_combo) gtk_combo_box_set_active(GTK_COMBO_BOX(m->pos_combo), m->placement);
            }
            if (g_str_has_prefix(line, "APPLY_FAILED_RESTORED:")) {
                const char *reason=line+strlen("APPLY_FAILED_RESTORED:");
                const char *detail=g_str_equal(reason,"timeout")
                    ? _("The display command timed out. The previous layout was restored; check the X server and display connection.")
                    : g_str_equal(reason,"parent-disconnected")
                    ? _("The display settings window closed before the change finished. The previous layout was restored.")
                    : g_str_equal(reason,"process-group")
                    ? _("The display command could not be isolated safely. No new layout was kept; the previous layout was restored.")
                    : _("XRandR rejected the requested output or mode. Check that the selected mode is available; the previous layout was restored.");
                luxe_show_error(GTK_WINDOW(dlg),_("Display change failed"),detail);
            }
        } else {
            luxe_show_error(GTK_WINDOW(dlg), _("Display restore could not be confirmed"),
                            _("Check the display layout before continuing."));
        }
        gtk_widget_set_sensitive(dlg, TRUE);
    }
    if (transaction == tx) transaction = NULL;
    g_object_unref(tx->output);
    g_object_unref(tx->guard);
    g_free(tx);
}

static void transaction_read_next(DisplayTransaction *tx)
{
    g_data_input_stream_read_line_async(tx->output, G_PRIORITY_DEFAULT, NULL, transaction_read_done, tx);
}

/* After a resolution change the X server keeps client windows at their old
 * coordinates/sizes, so windows can end up outside the new screen area (WMs
 * generally only recover maximized/fullscreen windows). Clamp all ordinary
 * top-level windows back into the new screen so nothing is lost off-screen. */
G_GNUC_BEGIN_IGNORE_DEPRECATIONS
static void clamp_windows_to_screen(void)
{
    GdkDisplay *display = gdk_display_get_default();
    if (!GDK_IS_X11_DISPLAY(display)) return;
    Display* dpy = GDK_DISPLAY_XDISPLAY(display);
    Window root = DefaultRootWindow(dpy);
    Atom client_list;
    Atom type;
    int fmt;
    unsigned long n, rest;
    unsigned char* data = NULL;
    unsigned long i;

    gdk_x11_display_error_trap_push(display);

    client_list = XInternAtom(dpy, "_NET_CLIENT_LIST", False);
    if (XGetWindowProperty(dpy, root, client_list, 0, 1024, False, XA_WINDOW,
                           &type, &fmt, &n, &rest, &data) != Success ||
        type != XA_WINDOW || fmt != 32 || n == 0)
    {
        if (data)
            XFree(data);
        gdk_x11_display_error_trap_pop_ignored(display);
        return;
    }
    int region_count=0;
    XRRMonitorInfo *monitor_info=XRRGetMonitors(dpy,root,True,&region_count);
    guint region_total=region_count>0?(guint)region_count:1;
    g_autofree LuxeRandRRect *regions=g_new0(LuxeRandRRect,region_total);
    if (monitor_info && region_count>0) {
        for (int r=0;r<region_count;r++)
            regions[r]=(LuxeRandRRect){monitor_info[r].x,monitor_info[r].y,
                                       monitor_info[r].width,monitor_info[r].height};
    } else {
        regions[0]=(LuxeRandRRect){0,0,WidthOfScreen(DefaultScreenOfDisplay(dpy)),
                                   HeightOfScreen(DefaultScreenOfDisplay(dpy))};
    }
    if (monitor_info) XRRFreeMonitors(monitor_info);
    for (i = 0; i < n; i++)
    {
        Window win = ((Window*)data)[i];
        Atom st_type;
        int st_fmt;
        unsigned long st_n, st_rest;
        unsigned char* st_data = NULL;
        gboolean skip = FALSE;
        XWindowAttributes attr;
        int abs_x = 0, abs_y = 0;
        Window child;
        Atom ext_type;
        int ext_fmt;
        unsigned long ext_n, ext_rest;
        unsigned char* ext_data = NULL;
        long left = 0, top = 0;
        int new_x, new_y;

        /* don't fight the WM over maximized/fullscreen windows */
        st_type = XInternAtom(dpy, "_NET_WM_STATE", False);
        if (XGetWindowProperty(dpy, win, st_type, 0, 64, False, XA_ATOM,
                               &st_type, &st_fmt, &st_n, &st_rest,
                               &st_data) == Success && st_data && st_type == XA_ATOM && st_fmt == 32)
        {
            unsigned long k;
            Atom max_v = XInternAtom(dpy, "_NET_WM_STATE_MAXIMIZED_VERT", False);
            Atom max_h = XInternAtom(dpy, "_NET_WM_STATE_MAXIMIZED_HORZ", False);
            Atom full = XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", False);
            for (k = 0; k < st_n; k++)
            {
                Atom a = ((Atom*)st_data)[k];
                if (a == max_v || a == max_h || a == full)
                    skip = TRUE;
            }
        }
        if (st_data) XFree(st_data);
        if (skip)
            continue;
        if (!XGetWindowAttributes(dpy, win, &attr) || attr.map_state == IsUnmapped)
            continue;
        if (!XTranslateCoordinates(dpy, win, root, 0, 0, &abs_x, &abs_y, &child))
            continue;
        /* account for the WM frame: client coords are relative to it;
         * _NET_FRAME_EXTENTS order: left, right, top, bottom */
        ext_type = XInternAtom(dpy, "_NET_FRAME_EXTENTS", False);
        if (XGetWindowProperty(dpy, win, ext_type, 0, 4, False, XA_CARDINAL,
                               &ext_type, &ext_fmt, &ext_n, &ext_rest,
                               &ext_data) == Success && ext_data &&
            ext_type == XA_CARDINAL && ext_fmt == 32 && ext_n >= 4)
        {
            unsigned long *extents = (unsigned long *)ext_data;
            if (extents[0] <= 65535 && extents[2] <= 65535) {
                left = extents[0];
                top = extents[2];
            }
        }
        if (ext_data) XFree(ext_data);
        if (!luxerandr_recovery_position(abs_x-left,abs_y-top,attr.width,attr.height,
                                         regions,region_total,120,32,&new_x,&new_y))
            continue;
        if (new_x != abs_x - left || new_y != abs_y - top)
        {
            /* Ask the WM to move the frame in root coordinates. Moving
             * a reparented client directly uses frame-relative coordinates. */
            XEvent event = { 0 };
            event.xclient.type = ClientMessage;
            event.xclient.window = win;
            event.xclient.message_type = XInternAtom(dpy, "_NET_MOVERESIZE_WINDOW", False);
            event.xclient.format = 32;
            event.xclient.data.l[0] = NorthWestGravity | (1 << 8) | (1 << 9) | (1 << 12);
            event.xclient.data.l[1] = new_x;
            event.xclient.data.l[2] = new_y;
            XSendEvent(dpy, root, False, SubstructureRedirectMask | SubstructureNotifyMask, &event);
        }
    }
    XFree(data);
    XFlush(dpy);
    gdk_x11_display_error_trap_pop_ignored(display);
}
G_GNUC_END_IGNORE_DEPRECATIONS

static void free_command(gpointer command)
{
    if (command) g_string_free(command, TRUE);
}

static void apply_query_done(GObject *source, GAsyncResult *result, gpointer data)
{
    (void)source; (void)data;
    g_autoptr(GError) error = NULL;
    g_autofree char *output = g_task_propagate_pointer(G_TASK(result), &error);
    GString *cmd = g_task_get_task_data(G_TASK(result));
    query_pending = FALSE;
    g_clear_object(&query_cancel);
    if (!dlg || g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) return;
    rollback_command = snapshot_configuration(output);
    if (!rollback_command) {
        luxe_show_error(GTK_WINDOW(dlg),
            topology_changed ? _("Display configuration changed") : _("Cannot read current layout"),
            topology_changed
                ? _("An output or mode changed while this window was open. Close and reopen LuxeRandR before applying changes.")
                : _("No changes were applied because a safe rollback could not be prepared."));
        gtk_widget_set_sensitive(dlg, TRUE);
        return;
    }
    g_clear_error(&error);
    GSubprocess *guard = g_subprocess_new(G_SUBPROCESS_FLAGS_STDIN_PIPE |
        G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_SILENCE,
        &error, LUXERANDR_GUARD_PATH, cmd->str, rollback_command->str, NULL);
    if (!guard) {
        luxe_show_error(GTK_WINDOW(dlg), _("Display change failed"), error->message);
        gtk_widget_set_sensitive(dlg, TRUE);
    } else {
        transaction = g_new0(DisplayTransaction, 1);
        transaction->guard = guard;
        transaction->output = g_data_input_stream_new(g_subprocess_get_stdout_pipe(guard));
        transaction_read_next(transaction);
    }
    g_string_free(rollback_command, TRUE);
    rollback_command = NULL;
}

static void set_xrandr_info(void)
{
    if (transaction || query_pending) return;
    prepare_try_values_from_GUI();
    GString *cmd = get_command_xrandr_info();
    if (!cmd || !GDK_IS_X11_DISPLAY(gdk_display_get_default())) {
        free_command(cmd);
        luxe_show_error(GTK_WINDOW(dlg), _("Display change failed"),
                        _("The display configuration contains an unsafe or unavailable value."));
        return;
    }
    query_pending = TRUE;
    query_cancel = g_cancellable_new();
    gtk_widget_set_sensitive(dlg, FALSE);
    GTask *task = g_task_new(NULL, query_cancel, apply_query_done, NULL);
    g_task_set_task_data(task, cmd, free_command);
    g_task_run_in_thread(task, query_worker);
    g_object_unref(task);
}

static void choose_max_resolution( Monitor* m )
{
    if( gtk_tree_model_iter_n_children( gtk_combo_box_get_model(GTK_COMBO_BOX(m->res_combo)), NULL ) > 1 )
        gtk_combo_box_set_active( GTK_COMBO_BOX(m->res_combo), 1 );
    if (m->pos_combo)
        gtk_combo_box_set_active(GTK_COMBO_BOX(m->pos_combo), PLACEMENT_DEFAULT);
}

static void on_quick_option( GtkCheckButton* btn, gpointer data )
{
    if (!gtk_check_button_get_active(btn) || transaction || query_pending) return;
    GSList* l;
    int option = GPOINTER_TO_INT(data);
    switch( option )
    {
    case 1: // turn on both
        for( l = monitors; l; l = l->next )
        {
            Monitor* m = (Monitor*)l->data;
            choose_max_resolution( m );
            gtk_check_button_set_active( m->enable, TRUE );
        }
        break;
    case 2: // external monitor only
        for( l = monitors; l; l = l->next )
        {
            Monitor* m = (Monitor*)l->data;
            choose_max_resolution( m );
            gtk_check_button_set_active( m->enable, m != LVDS );
        }
        break;
    case 3: // laptop panel - LVDS only
        for( l = monitors; l; l = l->next )
        {
            Monitor* m = (Monitor*)l->data;
            choose_max_resolution( m );
            gtk_check_button_set_active( m->enable, m == LVDS );
        }
        break;
    case 4: // external right of LVDS
        for( l = monitors; l; l = l->next )
        {
            Monitor* m = (Monitor*)l->data;
            choose_max_resolution( m );
            gtk_check_button_set_active(m->enable, TRUE);
            if (m != LVDS)
                gtk_combo_box_set_active(GTK_COMBO_BOX(m->pos_combo), PLACEMENT_RIGHT);
        }
        break;
    case 5: // external above of LVDS
        for( l = monitors; l; l = l->next )
        {
            Monitor* m = (Monitor*)l->data;
            choose_max_resolution( m );
            gtk_check_button_set_active(m->enable, TRUE);
            if (m != LVDS)
                gtk_combo_box_set_active(GTK_COMBO_BOX(m->pos_combo), PLACEMENT_ABOVE);
        }
        break;
    default:
        return;
    }
//    gtk_dialog_response( GTK_DIALOG(dlg), GTK_RESPONSE_OK );
//    set_xrandr_info();
}

/* In GTK4 the main window is a GtkApplicationWindow (not a GtkDialog), so
 * Apply/Save are ordinary buttons. The window simply stays open after
 * Apply — no need for the old g_signal_stop_emission "block the response"
 * hack. Info/warning popups are fire-and-forget modal alert dialogs. */
static void on_apply_clicked( GtkButton* btn, gpointer user_data )
{
    (void)btn;
    if (transaction || query_pending) return;
    GtkWindow* window = user_data;
    GSList* l;
    for( l = monitors; l; l = l->next )
    {
        Monitor* m = (Monitor*)l->data;
        if( gtk_check_button_get_active( m->enable ) )
            break;
    }
    if (l != NULL)
        set_xrandr_info();
    else
    {
        luxe_show_message( window,
                          _("You cannot turn off all monitors. "
                            "Otherwise, you will not be able to "
                            "turn them on again since this tool "
                            "is not accessible without monitor."), NULL );
    }
}

static void save_query_done(GObject *source,GAsyncResult *result,gpointer data)
{
    (void)source;(void)data;
    g_autoptr(GError) query_error=NULL;
    g_autofree char *output=g_task_propagate_pointer(G_TASK(result),&query_error);
    query_pending=FALSE;
    g_clear_object(&query_cancel);
    if (!dlg) return;
    if (!output || !current_topology_matches(output)) {
        luxe_show_error(GTK_WINDOW(dlg),_("Display configuration changed"),
            _("The connected outputs or available modes changed. Close and reopen LuxeRandR before saving."));
        gtk_widget_set_sensitive(dlg,TRUE);
        return;
    }
    GError *error=NULL;
    if (save_configuration(&error))
        luxe_show_message(GTK_WINDOW(dlg),_("Configuration Saved"),NULL);
    else {
        luxe_show_error(GTK_WINDOW(dlg),_("Could not save the display configuration"),error?error->message:NULL);
        g_clear_error(&error);
    }
    gtk_widget_set_sensitive(dlg,TRUE);
}

static void on_save_clicked( GtkButton* btn, gpointer user_data )
{
    (void)btn;
    if (transaction || query_pending) return;
    (void)user_data;
    query_pending=TRUE;
    query_cancel=g_cancellable_new();
    gtk_widget_set_sensitive(dlg,FALSE);
    GTask *task=g_task_new(NULL,query_cancel,save_query_done,NULL);
    g_task_run_in_thread(task,query_worker);
    g_object_unref(task);
}

static GtkApplication* app = NULL;

static gboolean on_window_close(GtkWindow *window, gpointer data)
{
    (void)window; (void)data;
    reject_confirmation();
    if (query_cancel) g_cancellable_cancel(query_cancel);
    if (transaction) {
        transaction->abandoned = TRUE;
        transaction_decide(transaction, 'R');
    }
    dlg = NULL;
    return FALSE;
}

static void build_main_window( GtkApplication* application )
{
    GtkWidget *notebook, *vbox, *frame, *label, *hbox, *check, *btn;
    GtkWidget *header, *toolbar_view, *menu_btn;
    GMenu *menu;
    GSList* l;
    Monitor *fixed;
    int i;
    gboolean can_position;

    dlg = gtk_application_window_new( GTK_APPLICATION(application) );
    g_object_add_weak_pointer(G_OBJECT(dlg), (gpointer *)&dlg);
    g_signal_connect(dlg, "close-request", G_CALLBACK(on_window_close), NULL);
    gtk_window_set_default_size(GTK_WINDOW(dlg), 780, -1);
    gtk_window_set_title( GTK_WINDOW(dlg), _("LuxeRandR — Display Settings") );

    /* Set icon name for main window so it displays in the panel. */
    gtk_window_set_icon_name(GTK_WINDOW(dlg), "org.luxe.Randr");

    /* R2 headerbar chrome hosted in GtkBox (never as a window
     * titlebar): Save/Apply stay as header buttons, secondary commands
     * (About/Quit) live in the hamburger menu. */
    header = gtk_header_bar_new();

    btn = gtk_button_new_with_mnemonic( _("_Save") );
    gtk_widget_set_tooltip_text( btn, _("Save settings to autostart") );
    g_signal_connect( btn, "clicked", G_CALLBACK(on_save_clicked), dlg );
    gtk_header_bar_pack_end( GTK_HEADER_BAR(header), btn );

    btn = gtk_button_new_with_mnemonic( _("_Apply") );
    gtk_widget_add_css_class( btn, "suggested-action" );
    gtk_widget_set_tooltip_text( btn, _("Apply settings now") );
    g_signal_connect( btn, "clicked", G_CALLBACK(on_apply_clicked), dlg );
    gtk_header_bar_pack_end( GTK_HEADER_BAR(header), btn );

    menu = g_menu_new();
    g_menu_append( menu, _("About"), "app.about" );
    g_menu_append( menu, _("Quit"), "app.quit" );
    menu_btn = gtk_menu_button_new();
    gtk_menu_button_set_icon_name( GTK_MENU_BUTTON(menu_btn), "open-menu-symbolic" );
    gtk_widget_set_tooltip_text( menu_btn, _("Main menu") );
    gtk_menu_button_set_menu_model( GTK_MENU_BUTTON(menu_btn), G_MENU_MODEL(menu) );
    g_object_unref( menu );
    gtk_header_bar_pack_end( GTK_HEADER_BAR(header), menu_btn );

    vbox = gtk_box_new( GTK_ORIENTATION_VERTICAL, LUXE_SPACING_INNER );
    gtk_widget_set_margin_start( vbox, LUXE_SPACING_OUTER );
    gtk_widget_set_margin_end( vbox, LUXE_SPACING_OUTER );
    gtk_widget_set_margin_top( vbox, LUXE_SPACING_OUTER );
    gtk_widget_set_margin_bottom( vbox, LUXE_SPACING_OUTER );
    gtk_widget_set_vexpand( vbox, TRUE );

    /* GtkApplicationWindow hosts the header bar in an GtkBox,
     * never via gtk_window_set_titlebar. */
    toolbar_view = luxe_toolbar_new();
    luxe_toolbar_add_top_bar( GTK_BOX(toolbar_view), header );
    luxe_toolbar_set_content( GTK_BOX(toolbar_view), vbox );
    luxe_window_set_content( GTK_WINDOW(dlg), toolbar_view );

    notebook = gtk_notebook_new();
    gtk_box_append( GTK_BOX(vbox), notebook );
    gtk_widget_set_vexpand( notebook, TRUE );

    // If this is a laptop and there is an external monitor, offer quick options
    if( LVDS && g_slist_length( monitors ) == 2 )
    {
        GtkWidget* qbox = gtk_box_new( GTK_ORIENTATION_VERTICAL, LUXE_SPACING_INNER );
        gtk_widget_set_margin_start( qbox, LUXE_SPACING_OUTER );
        gtk_widget_set_margin_end( qbox, LUXE_SPACING_OUTER );
        gtk_widget_set_margin_top( qbox, LUXE_SPACING_OUTER );
        gtk_widget_set_margin_bottom( qbox, LUXE_SPACING_OUTER );

        /* GTK4 removed GtkRadioButton; grouped GtkCheckButtons are the
         * replacement and preserve the original radio behavior. */
        btn = gtk_check_button_new_with_label(_("Show the same screen on both laptop LCD and external monitor"));
        g_signal_connect( btn, "toggled", G_CALLBACK(on_quick_option), GINT_TO_POINTER(1) );
        gtk_box_append( GTK_BOX(qbox), btn );

        GtkWidget* first_opt = btn;
        btn = gtk_check_button_new_with_label(_("Turn off laptop LCD and use external monitor only"));
        gtk_check_button_set_group( GTK_CHECK_BUTTON(btn), GTK_CHECK_BUTTON(first_opt) );
        g_signal_connect( btn, "toggled", G_CALLBACK(on_quick_option), GINT_TO_POINTER(2) );
        gtk_box_append( GTK_BOX(qbox), btn );

        btn = gtk_check_button_new_with_label(_("Turn off external monitor and use laptop LCD only"));
        gtk_check_button_set_group( GTK_CHECK_BUTTON(btn), GTK_CHECK_BUTTON(first_opt) );
        g_signal_connect( btn, "toggled", G_CALLBACK(on_quick_option), GINT_TO_POINTER(3) );
        gtk_box_append( GTK_BOX(qbox), btn );

        btn = gtk_check_button_new_with_label(_("Place external monitor to the right of laptop LCD"));
        gtk_check_button_set_group( GTK_CHECK_BUTTON(btn), GTK_CHECK_BUTTON(first_opt) );
        g_signal_connect( btn, "toggled", G_CALLBACK(on_quick_option), GINT_TO_POINTER(4) );
        gtk_box_append( GTK_BOX(qbox), btn );

        btn = gtk_check_button_new_with_label(_("Place external monitor above of laptop LCD"));
        gtk_check_button_set_group( GTK_CHECK_BUTTON(btn), GTK_CHECK_BUTTON(first_opt) );
        g_signal_connect( btn, "toggled", G_CALLBACK(on_quick_option), GINT_TO_POINTER(5) );
        gtk_box_append( GTK_BOX(qbox), btn );

        gtk_notebook_append_page( GTK_NOTEBOOK(notebook), qbox, gtk_label_new( _("Quick Options") ) );
    }
    else
    {
        gtk_notebook_set_show_tabs( GTK_NOTEBOOK(notebook), FALSE );
    }

    vbox = gtk_box_new( GTK_ORIENTATION_VERTICAL, LUXE_SPACING_SECTION );
    gtk_widget_set_margin_start( vbox, LUXE_SPACING_OUTER );
    gtk_widget_set_margin_end( vbox, LUXE_SPACING_OUTER );
    gtk_widget_set_margin_top( vbox, LUXE_SPACING_OUTER );
    gtk_widget_set_margin_bottom( vbox, LUXE_SPACING_OUTER );
    gtk_notebook_append_page( GTK_NOTEBOOK(notebook), vbox, gtk_label_new(_("Advanced")) );

    label = gtk_label_new("");
    gtk_label_set_xalign( GTK_LABEL(label), 0.0 );
    gtk_label_set_yalign( GTK_LABEL(label), 0.5 );
    gtk_label_set_markup( GTK_LABEL(label), ngettext( "The following monitor is detected:",
                                    "The following monitors are detected:",
                                    g_slist_length(monitors) ) );
    gtk_box_append( GTK_BOX(vbox), label );

    for (l = monitors, i = 0; l; l = l->next)
        if (((Monitor*)l->data)->active_mode >= 0)
            i++;
    can_position = (i > 1);

    /* correct placements */
    fixed = LVDS ? LVDS : monitors->data;
    if (fixed->placement != PLACEMENT_DEFAULT)
    {
        for (l = monitors, i = 0; l; l = l->next)
        {
            Monitor *m = (Monitor*)l->data;

            if (m->placement != PLACEMENT_DEFAULT)
                continue; /* FIXME: how to handle corners? */
            switch (fixed->placement)
            {
            case PLACEMENT_LEFT:
                m->placement = PLACEMENT_RIGHT;
                break;
            case PLACEMENT_RIGHT:
                m->placement = PLACEMENT_LEFT;
                break;
            case PLACEMENT_ABOVE:
                m->placement = PLACEMENT_BELOW;
                break;
            case PLACEMENT_BELOW:
                m->placement = PLACEMENT_ABOVE;
                break;
            case PLACEMENT_DEFAULT: ;
            }
        }
        fixed->placement = PLACEMENT_DEFAULT;
    }

    for( l = monitors, i = 0; l; l = l->next, ++i )
    {
        Monitor* m = (Monitor*)l->data;
        GSList* mode_line;

        frame = gtk_frame_new( get_human_readable_name(m) );
        gtk_box_append( GTK_BOX(vbox), frame );

        hbox = gtk_box_new( GTK_ORIENTATION_HORIZONTAL, LUXE_SPACING_INNER );
        gtk_widget_set_margin_start( hbox, LUXE_SPACING_INNER );
        gtk_widget_set_margin_end( hbox, LUXE_SPACING_INNER );
        gtk_widget_set_margin_top( hbox, LUXE_SPACING_INNER );
        gtk_widget_set_margin_bottom( hbox, LUXE_SPACING_INNER );
        gtk_frame_set_child( GTK_FRAME(frame), hbox );

        check = gtk_check_button_new_with_label( _("Turn On") );
        m->enable = GTK_CHECK_BUTTON(check);
        gtk_check_button_set_active(GTK_CHECK_BUTTON(check), m->active_mode >= 0);

        // turn off screen is not allowed since there should be at least one monitor available.
        if( g_slist_length( monitors ) == 1 )
            gtk_widget_set_sensitive( GTK_WIDGET(m->enable), FALSE );
        else
            g_signal_connect(m->enable, "toggled", G_CALLBACK(on_enable_toggled), m);

        gtk_box_append( GTK_BOX(hbox), check );

        if (monitors->next != NULL) /* g_slist_length(monitors) > 0 */
        {
            label = gtk_label_new(_("Position:"));
            gtk_box_append(GTK_BOX(hbox), label);
            m->pos_combo = GTK_COMBO_BOX_TEXT(gtk_combo_box_text_new());
            gtk_combo_box_text_append_text(m->pos_combo, _("Default"));
            gtk_combo_box_text_append_text(m->pos_combo, _("On right"));
            gtk_combo_box_text_append_text(m->pos_combo, _("Above"));
            gtk_combo_box_text_append_text(m->pos_combo, _("On left"));
            gtk_combo_box_text_append_text(m->pos_combo, _("Below"));
            gtk_combo_box_set_active(GTK_COMBO_BOX(m->pos_combo), m->placement);
            if (m == fixed || !can_position || m->active_mode < 0)
                gtk_widget_set_sensitive(GTK_WIDGET(m->pos_combo), FALSE);
            gtk_box_append(GTK_BOX(hbox), GTK_WIDGET(m->pos_combo));
        }

        label = gtk_label_new( _("Resolution:") );
        gtk_box_append( GTK_BOX(hbox), label );

        m->res_combo = GTK_COMBO_BOX_TEXT(gtk_combo_box_text_new());
        g_signal_connect( m->res_combo, "changed", G_CALLBACK(on_res_sel_changed), m );
        gtk_box_append( GTK_BOX(hbox), GTK_WIDGET(m->res_combo) );

        label = gtk_label_new( _("Refresh Rate:") );
        gtk_box_append( GTK_BOX(hbox), label );

        m->rate_combo = GTK_COMBO_BOX_TEXT(gtk_combo_box_text_new());
        gtk_box_append( GTK_BOX(hbox), GTK_WIDGET(m->rate_combo) );

        gtk_combo_box_text_append_text( m->res_combo, _("Auto") );
        for( mode_line = m->mode_lines; mode_line; mode_line = mode_line->next )
        {
            char** strv = (char**)mode_line->data;
            gtk_combo_box_text_append_text( m->res_combo, strv[0] );
        }

        m->active_rate++;
        gtk_combo_box_set_active( GTK_COMBO_BOX(m->res_combo), m->active_mode + 1 );
        gtk_combo_box_set_active( GTK_COMBO_BOX(m->rate_combo), m->active_rate );
        if (m->active_mode >= 0)
            m->active_mode++; /* let it stay -1 for inactive button */
    }

    gtk_window_present( GTK_WINDOW(dlg) );
}

static void on_startup_error_dismissed( gint button_index, gpointer data )
{
    (void)button_index;
    g_application_release(G_APPLICATION(data));
    g_application_quit( G_APPLICATION(data) );
}

static void startup_query_done(GObject *source, GAsyncResult *result, gpointer data)
{
    (void)data;
    GApplication *application = G_APPLICATION(source);
    g_autoptr(GError) error = NULL;
    g_autofree char *output = g_task_propagate_pointer(G_TASK(result), &error);
    query_pending = FALSE;
    g_clear_object(&query_cancel);
    if (g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
        g_application_release(application);
        return;
    }
    if (!output || !parse_xrandr_output(output)) {
        luxe_ask(NULL, _("Unable to get monitor information!"), NULL,
                 (const char *const[]){_("OK"), NULL}, -1, 0,
                 on_startup_error_dismissed, application);
        return;
    }
    build_main_window(GTK_APPLICATION(application));
    g_application_release(application);
}

static void on_activate( GtkApplication* application, gpointer data )
{
    (void) data;
    if (dlg) {
        gtk_window_present(GTK_WINDOW(dlg));
        return;
    }
    if (query_pending) return;
    g_action_map_add_action_entries( G_ACTION_MAP(application), app_actions,
                                     G_N_ELEMENTS(app_actions), application );
    gtk_application_set_accels_for_action( GTK_APPLICATION(application), "app.quit",
                                           (const char *const[]){ "<Control>q", NULL } );
    g_application_hold(G_APPLICATION(application));
    if (!GDK_IS_X11_DISPLAY(gdk_display_get_default()))
    {
        luxe_ask( NULL,
                  _("Unable to get monitor information!"),
                  NULL,
                  (const char *const[]){_("OK"), NULL},
                  -1, 0, on_startup_error_dismissed, application );
        return;
    }

    query_pending = TRUE;
    query_cancel = g_cancellable_new();
    GTask *task = g_task_new(application, query_cancel, startup_query_done, NULL);
    g_task_run_in_thread(task, query_worker);
    g_object_unref(task);
}

int main(int argc, char** argv)
{
    signal(SIGPIPE, SIG_IGN);
#ifdef ENABLE_NLS
    bindtextdomain ( GETTEXT_PACKAGE, PACKAGE_LOCALE_DIR );
    bind_textdomain_codeset ( GETTEXT_PACKAGE, "UTF-8" );
    textdomain ( GETTEXT_PACKAGE );
#endif

    luxe_ui_init();
    app = GTK_APPLICATION( luxe_application_new( "org.luxe.Randr", G_APPLICATION_DEFAULT_FLAGS ) );
    g_signal_connect( app, "activate", G_CALLBACK(on_activate), NULL );
    int status = g_application_run( G_APPLICATION(app), argc, argv );
    g_clear_object( &app );
    free_monitors();

    return status;
}
