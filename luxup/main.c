/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later
 *
 * LuxUp: a light update notifier for the Luxe desktop.  It lives in the
 * system tray, checks the package channels that are actually installed
 * (APT, Flathub, Snap), and hands the pending updates to LuxuryStore
 * premarked for installation. */
#include <gtk/gtk.h>
#include "luxe-gtk-ui.h"
#include <string.h>
#include <unistd.h>

#ifndef GETTEXT_PACKAGE
#define GETTEXT_PACKAGE "luxup"
#endif
#include <glib/gi18n.h>

#include "luxe-about.h"
#include "luxe-dialogs.h"
#include "luxe-spacing.h"
#include "check.h"
#include "tray.h"

#define LUXUP_VERSION VERSION

typedef struct {
    GtkApplication *application;
    GtkWindow *window;
    GtkWidget *status;
    GtkWidget *detail;
    GtkWidget *list;
    GtkWidget *spinner;
    GtkWidget *check_button;
    GtkWidget *review_button;
#ifdef LUXUP_BEHAVIOR_TEST
    LuxUpTray *tray;
#endif
    LuxUpResult *result;
    GCancellable *cancellable;
    gboolean checking;
    gboolean background;
    gboolean refresh_on_launch;
    gboolean shutting_down;
    gboolean refresh_pending;
    GDBusProxy *service;
    GCancellable *service_calls;
    gboolean service_call_pending;
    gboolean service_connecting;
    gboolean service_check_queued;
    gboolean service_refresh_queued;
    gboolean service_force_queued;
    guint service_state_generation;
    guint service_generation;
    gboolean service_disconnected;
    gboolean service_attached;
    guint local_generation;
} LuxUpApp;

static LuxUpApp luxup;

static const char *channel_icon(int source)
{
    switch (source) {
        case LUXUP_SOURCE_FLATPAK:
            return "application-x-executable";
        case LUXUP_SOURCE_SNAP:
            return "package-x-generic";
        default:
            return "system-software-install";
    }
}

static void set_status(const char *text)
{
    gtk_label_set_text(GTK_LABEL(luxup.status), text);
}

static void clear_list(void)
{
    GtkWidget *child = gtk_widget_get_first_child(luxup.list);
    while (child) {
        GtkWidget *next = gtk_widget_get_next_sibling(child);
        gtk_list_box_remove(GTK_LIST_BOX(luxup.list), child);
        child = next;
    }
}

static GtkWidget *make_row(const LuxUpPackage *package)
{
    GtkWidget *row = gtk_list_box_row_new();
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, LUXE_SPACING_INNER);
    gtk_widget_set_margin_start(box, LUXE_SPACING_INNER);
    gtk_widget_set_margin_end(box, LUXE_SPACING_INNER);
    gtk_widget_set_margin_top(box, LUXE_SPACING_INNER);
    gtk_widget_set_margin_bottom(box, LUXE_SPACING_INNER);
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), box);

    GtkWidget *icon = gtk_image_new_from_icon_name(channel_icon(package->source));
    gtk_widget_set_valign(icon, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(box), icon);

    GtkWidget *labels = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_hexpand(labels, TRUE);
    gtk_box_append(GTK_BOX(box), labels);

    GtkWidget *name = gtk_label_new(package->name);
    gtk_label_set_xalign(GTK_LABEL(name), 0);
    gtk_label_set_ellipsize(GTK_LABEL(name), PANGO_ELLIPSIZE_END);
    gtk_widget_add_css_class(name, "heading");
    gtk_box_append(GTK_BOX(labels), name);

    char *detail = NULL;
    if (package->installed[0] && package->available[0])
        detail = g_strdup_printf("%s → %s", package->installed, package->available);
    else if (package->available[0])
        detail = g_strdup(package->available);
    else
        detail = g_strdup(luxup_source_name(package->source));
    GtkWidget *sub = gtk_label_new(detail);
    gtk_label_set_xalign(GTK_LABEL(sub), 0);
    gtk_label_set_ellipsize(GTK_LABEL(sub), PANGO_ELLIPSIZE_END);
    gtk_widget_add_css_class(sub, "dim-label");
    gtk_box_append(GTK_BOX(labels), sub);
    g_free(detail);

    const char *source = luxup_source_name(package->source);
    const char *scope_label = package->scope && g_str_equal(package->scope, "user") ? _("user") :
        package->scope && g_str_equal(package->scope, "system") ? _("system") : package->scope;
    g_autofree char *badge_text = package->scope && *package->scope
        ? g_strdup_printf("%s (%s)", source, scope_label)
        : g_strdup(source);
    GtkWidget *badge = gtk_label_new(badge_text);
    gtk_widget_add_css_class(badge, "dim-label");
    gtk_widget_set_valign(badge, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(box), badge);
    return row;
}

static guint result_count(const LuxUpResult *result)
{
    return result && result->packages ? result->packages->len : 0;
}

static void append_source_errors(GString *text, const LuxUpResult *result)
{
    const char *errors[] = {result->apt_refresh_error, result->apt_check_error,
                            result->flatpak_user_error, result->flatpak_system_error,
                            result->snap_check_error};
    for (guint i = 0; i < G_N_ELEMENTS(errors); i++) {
        if (!errors[i] || !*errors[i]) continue;
        if (text->len) g_string_append_c(text, '\n');
        g_string_append(text, errors[i]);
    }
}

static void update_ui(void)
{
    if (!luxup.window) return;
    guint total = result_count(luxup.result);
    gboolean failed = luxup.result && luxup.result->check_failed;
    gtk_widget_set_visible(luxup.spinner, luxup.checking || luxup.service_connecting);
    gtk_spinner_set_spinning(GTK_SPINNER(luxup.spinner), luxup.checking || luxup.service_connecting);
    gtk_widget_set_sensitive(luxup.check_button, !luxup.service_call_pending && !luxup.service_connecting && !luxup.service_disconnected);
    gtk_button_set_label(GTK_BUTTON(luxup.check_button),
                         luxup.checking ? _("Cancel") : _("Check Again"));
    gboolean can_review = total > 0 && !luxup.checking;
    gtk_widget_set_sensitive(luxup.review_button, can_review);
    GAction *review = g_action_map_lookup_action(G_ACTION_MAP(luxup.window), "review");
    if (review) g_simple_action_set_enabled(G_SIMPLE_ACTION(review), can_review);

    if (luxup.service_disconnected) {
        set_status(_("Update service disconnected."));
        gtk_label_set_text(GTK_LABEL(luxup.detail), _("Waiting for the update service to return."));
        clear_list();
        return;
    }
    if (luxup.service_connecting) {
        set_status(_("Connecting to the update service…"));
        gtk_label_set_text(GTK_LABEL(luxup.detail), "");
        return;
    }
    if (luxup.checking) {
        set_status(_("Checking for updates…"));
        gtk_label_set_text(GTK_LABEL(luxup.detail), "");
        return;
    }
    if (!luxup.result || (failed && total == 0)) {
        if (failed) {
            set_status(_("Could not check for updates."));
            g_autoptr(GString) errors = g_string_new(NULL);
            append_source_errors(errors, luxup.result);
            gtk_label_set_text(GTK_LABEL(luxup.detail), errors->len ? errors->str :
                               _("The update status is unknown. Check your network and package sources, then try again."));
        } else {
            set_status(_("Not checked yet."));
            gtk_label_set_text(GTK_LABEL(luxup.detail), "");
        }
        clear_list();
        return;
    }

    char *summary = g_strdup_printf("%s  ·  %s  ·  %s",
        luxup_source_name(LUXUP_SOURCE_APT), luxup_source_name(LUXUP_SOURCE_FLATPAK),
        luxup_source_name(LUXUP_SOURCE_SNAP));
    if (failed) {
        char *line = g_strdup_printf(ngettext("%u update found; some checks failed.", "%u updates found; some checks failed.", total), total);
        set_status(line);
        g_free(line);
    } else if (total == 0) {
        set_status(!luxup.result->apt_available
            ? _("No updates found in available sources; APT is unavailable.")
            : luxup_apt_lists_fresh(luxup.result, g_get_real_time()/G_USEC_PER_SEC)
            ? _("No updates found in recently refreshed lists.")
            : _("No updates found; APT list freshness is unknown or stale."));
    } else {
        char *line = g_strdup_printf(ngettext("%u update available.", "%u updates available.", total), total);
        set_status(line);
        g_free(line);
    }

    char *counts = g_strdup_printf("APT %u   ·   Flathub %u   ·   Snap %u",
        luxup_count(luxup.result, LUXUP_SOURCE_APT),
        luxup_count(luxup.result, LUXUP_SOURCE_FLATPAK),
        luxup_count(luxup.result, LUXUP_SOURCE_SNAP));
    GString *detail = g_string_new(summary);
    g_string_append_printf(detail, "\n%s", counts);
    if (failed)
        g_string_append(detail, _("\nSome sources could not be checked; counts may be incomplete."));
    if (!luxup.result->apt_available)
        g_string_append(detail, _("\nAPT is unavailable; other system package managers are not checked."));
    else if (!luxup_apt_lists_fresh(luxup.result, g_get_real_time()/G_USEC_PER_SEC))
        g_string_append(detail, _("\nAPT package lists may be out of date. Refresh to check current updates."));
    append_source_errors(detail, luxup.result);
    if (luxup.result->snap_available)
        g_string_append_printf(detail, "\n%s", !luxup.result->snap_hold_known
            ? _("Snap automatic refresh policy could not be determined.")
            : luxup.result->snap_auto_refresh_held
            ? _("Snap automatic refreshes are paused; updates require approval.")
            : _("Snap automatic refreshes are not held indefinitely."));
    gtk_label_set_text(GTK_LABEL(luxup.detail), detail->str);
    g_string_free(detail, TRUE);
    g_free(counts);
    g_free(summary);

    clear_list();
    for (guint i = 0; luxup.result->packages && i < luxup.result->packages->len; ++i)
        gtk_list_box_append(GTK_LIST_BOX(luxup.list),
                            make_row(g_ptr_array_index(luxup.result->packages, i)));
}

static void set_tray_state(void)
{
#ifdef LUXUP_BEHAVIOR_TEST
    if (!luxup.tray)
        return;
    if (luxup.checking) {
        luxup_tray_set_state(luxup.tray, LUXUP_TRAY_CHECKING);
        luxup_tray_set_tooltip(luxup.tray, _("LuxUp — checking for updates…"));
        return;
    }
    /* Never report green when the last check failed or never completed. */
    guint total = result_count(luxup.result);
    if (!luxup.result || (luxup.result->check_failed && total == 0) ||
        (result_count(luxup.result) == 0 && !luxup_apt_lists_fresh(luxup.result, g_get_real_time()/G_USEC_PER_SEC))) {
        luxup_tray_set_state(luxup.tray, LUXUP_TRAY_UNKNOWN);
        luxup_tray_set_tooltip(luxup.tray, _("LuxUp — status unknown"));
        return;
    }
    if (total > 0) {
        luxup_tray_set_state(luxup.tray, LUXUP_TRAY_UPDATES);
        char *tip = luxup.result->check_failed
            ? g_strdup_printf(ngettext("LuxUp — %u update found; some checks failed", "LuxUp — %u updates found; some checks failed", total), total)
            : g_strdup_printf(ngettext("LuxUp — %u update available", "LuxUp — %u updates available", total), total);
        luxup_tray_set_tooltip(luxup.tray, tip);
        g_free(tip);
    } else {
        luxup_tray_set_state(luxup.tray, LUXUP_TRAY_UPTODATE);
        luxup_tray_set_tooltip(luxup.tray, _("LuxUp — up to date"));
    }
#endif
}

static void check_async(gboolean refresh);

static void check_finished(GObject *source G_GNUC_UNUSED, GAsyncResult *result, gpointer data)
{
    GError *error = NULL;
    LuxUpResult *checked = g_task_propagate_pointer(G_TASK(result), &error);
    if (GPOINTER_TO_UINT(data) != luxup.local_generation) {
        luxup_result_free(checked);
        g_clear_error(&error);
        return;
    }
    g_clear_object(&luxup.cancellable);
    luxup.checking = FALSE;
    if (luxup.shutting_down) {
        luxup_result_free(checked);
        g_clear_error(&error);
        return;
    }
    if (!g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
        if (checked && checked->apt_available && !checked->apt_refreshed_at && !checked->apt_refresh_error && luxup.result)
            checked->apt_refreshed_at = luxup.result->apt_refreshed_at;
        luxup_result_free(luxup.result);
        luxup.result = checked;
    }
    update_ui();
    set_tray_state();
    if (luxup.window && error && !g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
        set_status(_("Could not check for updates."));
        gtk_label_set_text(GTK_LABEL(luxup.detail), error->message);
    }
    g_clear_error(&error);
    if (luxup.refresh_pending) {
        luxup.refresh_pending = FALSE;
        check_async(TRUE);
    }
}

static void check_thread(GTask *task, gpointer source, gpointer data, GCancellable *cancellable)
{
    (void) source;
    LuxUpResult *checked = luxup_check(GPOINTER_TO_INT(data), cancellable);
    if (g_task_return_error_if_cancelled(task)) {
        luxup_result_free(checked);
        return;
    }
    g_task_return_pointer(task, checked, (GDestroyNotify) luxup_result_free);
}

static void service_request_refresh(void);
static void service_check_done(GObject *source, GAsyncResult *result, gpointer data)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(GVariant) reply = g_dbus_proxy_call_finish(G_DBUS_PROXY(source), result, &error);
    if (luxup.shutting_down || source != G_OBJECT(luxup.service) ||
        GPOINTER_TO_UINT(data) != luxup.service_generation) return;
    luxup.service_call_pending = FALSE;
    update_ui();
    if (!reply && luxup.window && !g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
        set_status(_("Could not check for updates."));
        gtk_label_set_text(GTK_LABEL(luxup.detail), error ? error->message : _("The update service did not respond."));
    }
    if (luxup.refresh_pending && !luxup.service_disconnected) {
        luxup.refresh_pending = FALSE;
        service_request_refresh();
    }
}

static void service_request_refresh(void)
{
    if (luxup.shutting_down || !luxup.service_attached || luxup.service_disconnected) return;
    if (luxup.service_call_pending) { luxup.refresh_pending = TRUE; return; }
    if (!luxup.service_calls) luxup.service_calls = g_cancellable_new();
    luxup.service_call_pending = TRUE;
    update_ui();
    g_dbus_proxy_call(luxup.service, "Check", g_variant_new("(b)", TRUE),
                      G_DBUS_CALL_FLAGS_NONE, 3000, luxup.service_calls, service_check_done,
                      GUINT_TO_POINTER(luxup.service_generation));
}

static void check_async(gboolean refresh)
{
    if (luxup.shutting_down) return;
    if (luxup.service_disconnected) return;
    if (luxup.service_connecting) {
        luxup.service_check_queued = TRUE;
        luxup.service_refresh_queued |= refresh;
        update_ui();
        return;
    }
    if (luxup.service && luxup.service_attached) {
        if (luxup.service_call_pending) return;
        if (!luxup.service_calls) luxup.service_calls = g_cancellable_new();
        luxup.service_call_pending = TRUE;
        update_ui();
        g_dbus_proxy_call(luxup.service,
            luxup.checking?"Cancel":"Check",luxup.checking?NULL:g_variant_new("(b)",refresh),
            G_DBUS_CALL_FLAGS_NONE,3000,luxup.service_calls,service_check_done,
            GUINT_TO_POINTER(luxup.service_generation));
        return;
    }
    /* A second request while checking cancels the in-flight check so the
     * cancel path (button label switches to Cancel) is effective. */
    if (luxup.checking) {
        if (luxup.cancellable)
            g_cancellable_cancel(luxup.cancellable);
        return;
    }
    luxup.checking = TRUE;
    if (luxup.cancellable)
        g_cancellable_cancel(luxup.cancellable);
    g_clear_object(&luxup.cancellable);
    luxup.cancellable = g_cancellable_new();
    GCancellable *cancellable = g_object_ref(luxup.cancellable);
    update_ui();
    set_tray_state();

    luxup.local_generation++;
    GTask *task = g_task_new(NULL, cancellable, check_finished, GUINT_TO_POINTER(luxup.local_generation));
    g_task_set_task_data(task, GINT_TO_POINTER(refresh), NULL);
    g_task_run_in_thread(task, check_thread);
    g_object_unref(task);
    g_object_unref(cancellable);
}

static void review_in_luxurystore(GtkWindow *parent)
{
    GError *error = NULL;
    char *path = g_find_program_in_path("luxurystore");
    if (!path) {
        luxe_show_error(parent, _("LuxuryStore is not installed"),
                        _("Install the LuxuryStore package to review and apply updates."));
        return;
    }
    GSubprocess *process = g_subprocess_new(G_SUBPROCESS_FLAGS_NONE, &error, path, "--mark-updates", NULL);
    g_free(path);
    if (!process) {
        luxe_show_error(parent, _("Could not open LuxuryStore"), error ? error->message : NULL);
        g_clear_error(&error);
        return;
    }
    g_object_unref(process);
}

static void show_about(GtkWindow *parent)
{
    luxe_show_about(parent, "LuxUp", LUXUP_VERSION,
                    _("Light update notifier for the Luxe desktop. Checks APT, Flathub and the Snap Store, pauses automatic Snap refreshes, then opens pending updates in LuxuryStore for review."));
}

static void build_window(GtkApplication *application);

static void open_window(void)
{
    if (!luxup.window)
        build_window(luxup.application);
    update_ui();
    gtk_window_present(GTK_WINDOW(luxup.window));
}

#ifdef LUXUP_BEHAVIOR_TEST
static void tray_activated(LuxUpTray *tray, guint button, gpointer data)
{
    (void) tray;
    (void) data;
    if (button == 2)
        check_async(TRUE);
    else
        open_window();
}
#endif

static void build_window(GtkApplication *application)
{
    GtkWidget *window = gtk_application_window_new(application);
    luxup.window = GTK_WINDOW(window);
    gtk_window_set_title(luxup.window, "LuxUp");
    gtk_window_set_default_size(luxup.window, 520, 440);
    gtk_window_set_hide_on_close(luxup.window, FALSE);
    g_object_add_weak_pointer(G_OBJECT(window),(gpointer *)&luxup.window);
    gtk_window_set_icon_name(luxup.window, "org.luxe.LuxUp");

    GtkWidget *header = gtk_header_bar_new();
    GtkWidget *title = gtk_label_new("LuxUp");
    gtk_widget_add_css_class(title, "title");
    gtk_header_bar_set_title_widget(GTK_HEADER_BAR(header), title);

    GMenu *menu = g_menu_new();
    g_menu_append(menu, _("Check for Updates"), "win.check");
    g_menu_append(menu, _("Open in LuxuryStore"), "win.review");
    g_menu_append(menu, _("About LuxUp"), "win.about");
    g_menu_append(menu, _("Quit"), "app.quit");
    GtkWidget *menu_button = gtk_menu_button_new();
    gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(menu_button), "open-menu-symbolic");
    gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(menu_button), G_MENU_MODEL(menu));
    gtk_widget_set_tooltip_text(menu_button, _("Main Menu"));
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), menu_button);
    g_object_unref(menu);

    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *toolbar = luxe_toolbar_new();
    luxe_toolbar_add_top_bar(GTK_BOX(toolbar), header);
    luxe_toolbar_set_content(GTK_BOX(toolbar), outer);
    luxe_window_set_content(GTK_WINDOW(window), toolbar);
    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, LUXE_SPACING_INNER);
    gtk_widget_set_margin_start(content, LUXE_SPACING_OUTER);
    gtk_widget_set_margin_end(content, LUXE_SPACING_OUTER);
    gtk_widget_set_margin_top(content, LUXE_SPACING_OUTER);
    gtk_widget_set_margin_bottom(content, LUXE_SPACING_OUTER);
    gtk_widget_set_vexpand(content, TRUE);
    gtk_box_append(GTK_BOX(outer), content);

    GtkWidget *heading = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, LUXE_SPACING_INNER);
    luxup.spinner = gtk_spinner_new();
    gtk_box_append(GTK_BOX(heading), luxup.spinner);
    luxup.status = gtk_label_new(_("Not checked yet."));
    gtk_label_set_xalign(GTK_LABEL(luxup.status), 0);
    gtk_label_set_wrap(GTK_LABEL(luxup.status), TRUE);
    gtk_widget_add_css_class(luxup.status, "heading");
    gtk_box_append(GTK_BOX(heading), luxup.status);
    gtk_box_append(GTK_BOX(content), heading);

    luxup.detail = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(luxup.detail), 0);
    gtk_label_set_wrap(GTK_LABEL(luxup.detail), TRUE);
    gtk_widget_add_css_class(luxup.detail, "dim-label");
    gtk_box_append(GTK_BOX(content), luxup.detail);

    GtkWidget *frame = gtk_frame_new(NULL);
    gtk_widget_set_vexpand(frame, TRUE);
    luxup.list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(luxup.list), GTK_SELECTION_NONE);
    gtk_list_box_set_show_separators(GTK_LIST_BOX(luxup.list), TRUE);
    GtkWidget *scrolled = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), luxup.list);
    gtk_frame_set_child(GTK_FRAME(frame), scrolled);
    gtk_box_append(GTK_BOX(content), frame);

    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, LUXE_SPACING_INNER);
    luxup.check_button = gtk_button_new_with_label(_("Check Again"));
    gtk_box_append(GTK_BOX(actions), luxup.check_button);
    GtkWidget *spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(spacer, TRUE);
    gtk_box_append(GTK_BOX(actions), spacer);
    luxup.review_button = gtk_button_new_with_label(_("Review in LuxuryStore"));
    gtk_widget_add_css_class(luxup.review_button, "suggested-action");
    gtk_box_append(GTK_BOX(actions), luxup.review_button);
    gtk_box_append(GTK_BOX(content), actions);

    g_signal_connect_swapped(luxup.check_button, "clicked", G_CALLBACK(check_async), GINT_TO_POINTER(TRUE));
    g_signal_connect_swapped(luxup.review_button, "clicked", G_CALLBACK(review_in_luxurystore), luxup.window);

    GSimpleAction *check_action = g_simple_action_new("check", NULL);
    g_signal_connect_swapped(check_action, "activate", G_CALLBACK(check_async), GINT_TO_POINTER(TRUE));
    g_action_map_add_action(G_ACTION_MAP(luxup.window), G_ACTION(check_action));
    g_object_unref(check_action);
    GSimpleAction *review_action = g_simple_action_new("review", NULL);
    g_signal_connect_swapped(review_action, "activate", G_CALLBACK(review_in_luxurystore), luxup.window);
    g_action_map_add_action(G_ACTION_MAP(luxup.window), G_ACTION(review_action));
    g_object_unref(review_action);
    GSimpleAction *about_action = g_simple_action_new("about", NULL);
    g_signal_connect_swapped(about_action, "activate", G_CALLBACK(show_about), luxup.window);
    g_action_map_add_action(G_ACTION_MAP(luxup.window), G_ACTION(about_action));
    g_object_unref(about_action);
}

static void activate(GtkApplication *application, gpointer data)
{
    (void) data;
    (void) application;
    if (luxup.background) {
        luxup.background = FALSE;
        if (!luxup.checking) check_async(luxup.refresh_on_launch);
        return;
    }
    open_window();
    if (!luxup.result && !luxup.checking)
        check_async(luxup.refresh_on_launch);
}

static void ui_package_free(gpointer data)
{LuxUpPackage *p=data;g_free(p->name);g_free(p->installed);g_free(p->available);g_free(p->scope);g_free(p->ref);g_free(p);}
static void service_state(GVariant *values)
{
    gboolean has_result=FALSE;
    g_variant_lookup(values,"has-result","b",&has_result);
    g_variant_lookup(values,"checking","b",&luxup.checking);
    luxup_result_free(luxup.result);luxup.result=NULL;
    if(has_result) {
        luxup.result=g_new0(LuxUpResult,1);
        /* Older services lack this field; retain their former APT behavior. */
        luxup.result->apt_available=TRUE;
        luxup.result->packages=g_ptr_array_new_with_free_func(ui_package_free);
        g_variant_lookup(values,"failed","b",&luxup.result->check_failed);
        g_variant_lookup(values,"apt-refreshed-at","x",&luxup.result->apt_refreshed_at);
        g_variant_lookup(values,"apt-available","b",&luxup.result->apt_available);
        g_variant_lookup(values,"snap-available","b",&luxup.result->snap_available);
        g_variant_lookup(values,"snap-held","b",&luxup.result->snap_auto_refresh_held);
        g_variant_lookup(values,"snap-hold-known","b",&luxup.result->snap_hold_known);
        const char *error=NULL;
        if(g_variant_lookup(values,"error","&s",&error)&&*error)luxup.result->apt_refresh_error=g_strdup(error);
        if(g_variant_lookup(values,"apt-check-error","&s",&error)&&*error)luxup.result->apt_check_error=g_strdup(error);
        if(g_variant_lookup(values,"flatpak-user-error","&s",&error)&&*error)luxup.result->flatpak_user_error=g_strdup(error);
        if(g_variant_lookup(values,"flatpak-system-error","&s",&error)&&*error)luxup.result->flatpak_system_error=g_strdup(error);
        if(g_variant_lookup(values,"snap-check-error","&s",&error)&&*error)luxup.result->snap_check_error=g_strdup(error);
        g_autoptr(GVariant) rows=g_variant_lookup_value(values,"packages-v2",G_VARIANT_TYPE("a(isssss)"));
        gboolean v2 = rows != NULL;
        if (!rows) rows=g_variant_lookup_value(values,"packages",G_VARIANT_TYPE("a(isss)"));
        if(rows) {
            GVariantIter iter;int source;const char *name,*installed,*available,*scope,*ref;
            g_variant_iter_init(&iter,rows);
            while(v2 ? g_variant_iter_loop(&iter,"(i&s&s&s&s&s)",&source,&name,&installed,&available,&scope,&ref)
                     : g_variant_iter_loop(&iter,"(i&s&s&s)",&source,&name,&installed,&available)) {
                LuxUpPackage *p=g_new0(LuxUpPackage,1);p->source=source;p->name=g_strdup(name);p->installed=g_strdup(installed);p->available=g_strdup(available);
                if (v2) {p->scope=g_strdup(scope);p->ref=g_strdup(ref);}
                g_ptr_array_add(luxup.result->packages,p);
            }
        }
    }
    update_ui();
}
static void service_changed(GDBusProxy *proxy,const char *sender,const char *name,GVariant *args,gpointer unused)
{
    (void)unused;
    g_autofree char *owner = g_dbus_proxy_get_name_owner(proxy);
    if (luxup.shutting_down || proxy != luxup.service || !owner || g_strcmp0(owner, sender)) return;
    if(g_str_equal(name,"Changed")){luxup.service_state_generation++;g_autoptr(GVariant) values=g_variant_get_child_value(args,0);service_state(values);}
}
typedef struct { guint service_generation, state_generation; } InitialStateRequest;
static void initial_state_done(GObject *source, GAsyncResult *result, gpointer data);

static void request_initial_state(void)
{
    InitialStateRequest *request = g_new(InitialStateRequest, 1);
    request->service_generation = luxup.service_generation;
    request->state_generation = luxup.service_state_generation;
    g_dbus_proxy_call(luxup.service, "GetState", NULL, G_DBUS_CALL_FLAGS_NONE, 3000,
                      luxup.service_calls, initial_state_done, request);
}

static void finish_service_connection(void)
{
    luxup.service_connecting = FALSE;
    update_ui();
    if (luxup.service_check_queued) {
        gboolean refresh = luxup.service_refresh_queued;
        gboolean force = luxup.service_force_queued;
        luxup.service_check_queued = FALSE;
        luxup.service_refresh_queued = FALSE;
        luxup.service_force_queued = FALSE;
        /* Activation queued a check before the service state was known. An
         * existing result or active check already satisfies that request. */
        if (force && luxup.service_attached && luxup.checking) {
            service_request_refresh();
        } else if (force || !luxup.service_attached || (!luxup.result && !luxup.checking))
            check_async(refresh);
    }
}

static void initial_state_done(GObject *source, GAsyncResult *result, gpointer data)
{
    g_autofree InitialStateRequest *request = data;
    g_autoptr(GError) error = NULL;
    g_autoptr(GVariant) reply = g_dbus_proxy_call_finish(G_DBUS_PROXY(source), result, &error);
    if (luxup.shutting_down || source != G_OBJECT(luxup.service) ||
        request->service_generation != luxup.service_generation) return;
    if (reply && request->state_generation == luxup.service_state_generation) {
        g_autoptr(GVariant) values = g_variant_get_child_value(reply, 0);
        service_state(values);
    }
    finish_service_connection();
}

static void service_owner_changed(GObject *object, GParamSpec *pspec, gpointer data)
{
    (void)pspec; (void)data;
    if (luxup.shutting_down || object != G_OBJECT(luxup.service)) return;
    g_autofree char *owner = g_dbus_proxy_get_name_owner(luxup.service);
    if (!owner && !luxup.service_attached) return;
    luxup.service_generation++;
    luxup.local_generation++;
    if (luxup.cancellable) g_cancellable_cancel(luxup.cancellable);
    g_clear_object(&luxup.cancellable);
    luxup.refresh_pending = FALSE;
    if (luxup.service_calls) g_cancellable_cancel(luxup.service_calls);
    g_clear_object(&luxup.service_calls);
    luxup.service_call_pending = FALSE;
    luxup.checking = FALSE;
    luxup_result_free(luxup.result); luxup.result = NULL;
    if (!owner) {
        luxup.service_disconnected = TRUE;
        luxup.service_connecting = FALSE;
        update_ui();
        return;
    }
    luxup.service_attached = TRUE;
    luxup.service_disconnected = FALSE;
    luxup.service_connecting = TRUE;
    luxup.service_check_queued = TRUE;
    luxup.service_refresh_queued = FALSE;
    luxup.service_calls = g_cancellable_new();
    update_ui();
    request_initial_state();
}

static void service_connected(GObject *source, GAsyncResult *result, gpointer data)
{
    (void)source; (void)data;
    g_autoptr(GError) error = NULL;
    GDBusProxy *proxy = g_dbus_proxy_new_for_bus_finish(result, &error);
    if (luxup.shutting_down) { g_clear_object(&proxy); return; }
    luxup.service = proxy;
    if (!proxy) { finish_service_connection(); return; }
    g_signal_connect(proxy, "g-signal", G_CALLBACK(service_changed), NULL);
    g_signal_connect(proxy, "notify::g-name-owner", G_CALLBACK(service_owner_changed), NULL);
    g_autofree char *owner = g_dbus_proxy_get_name_owner(proxy);
    if (!owner) { finish_service_connection(); return; }
    luxup.service_attached = TRUE;
    request_initial_state();
}
static void quit_done(GObject *source,GAsyncResult *result,gpointer data)
{
    g_autoptr(GVariant) reply=g_dbus_proxy_call_finish(G_DBUS_PROXY(source),result,NULL);
    GApplication *app=data;g_application_quit(app);g_application_release(app);g_object_unref(app);
}
static void quit_app(GtkApplication *application)
{
    if(luxup.service && luxup.service_attached && !luxup.service_disconnected) {
        g_application_hold(G_APPLICATION(application));
        g_dbus_proxy_call(luxup.service,"Quit",NULL,G_DBUS_CALL_FLAGS_NONE,3000,NULL,quit_done,g_object_ref(application));
    } else g_application_quit(G_APPLICATION(application));
}
static void startup(GtkApplication *application, gpointer data)
{
    (void) data;
#ifdef LUXUP_BEHAVIOR_TEST
    /* The resident tray keeps the service alive even before any window exists. */
    g_application_hold(G_APPLICATION(application));
    if (!luxup.tray)
        luxup.tray = luxup_tray_new(tray_activated, NULL);
#else
    luxup.service_connecting = TRUE;
    luxup.service_calls = g_cancellable_new();
    g_dbus_proxy_new_for_bus(G_BUS_TYPE_SESSION,G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES,
        NULL,"org.luxe.LuxUp.Service","/org/luxe/LuxUp","org.luxe.LuxUp.Service",
        luxup.service_calls,service_connected,NULL);
#endif
    GSimpleAction *quit = g_simple_action_new("quit", NULL);
    g_signal_connect_swapped(quit, "activate", G_CALLBACK(quit_app), application);
    g_action_map_add_action(G_ACTION_MAP(application), G_ACTION(quit));
    g_object_unref(quit);
}

static int print_check(gboolean refresh)
{
    LuxUpResult *result = luxup_check(refresh, NULL);
    guint total = result_count(result);
    g_print("APT\t%u\nFlathub\t%u\nSnap\t%u\n",
            luxup_count(result, LUXUP_SOURCE_APT),
            luxup_count(result, LUXUP_SOURCE_FLATPAK),
            luxup_count(result, LUXUP_SOURCE_SNAP));
    if (!result->apt_available) g_print("APT\tunavailable; other system package managers are not checked\n");
    if (result->snap_available)
        g_print("Snap auto-refresh\t%s\n", !result->snap_hold_known ? "unknown" :
                result->snap_auto_refresh_held ? "held indefinitely" : "not held indefinitely");
    for (guint i = 0; result->packages && i < result->packages->len; ++i) {
        LuxUpPackage *p = g_ptr_array_index(result->packages, i);
        g_print("%s\t%s\t%s\t%s\n", luxup_source_name(p->source), p->name,
                p->installed, p->available);
    }
    if (result->apt_refresh_error)
        g_printerr("APT refresh: %s\n", result->apt_refresh_error);
    if (result->apt_check_error)
        g_printerr("APT check: %s\n", result->apt_check_error);
    if (result->flatpak_user_error) g_printerr("%s\n", result->flatpak_user_error);
    if (result->flatpak_system_error) g_printerr("%s\n", result->flatpak_system_error);
    if (result->snap_check_error) g_printerr("%s\n", result->snap_check_error);
    gboolean failed = result->check_failed;
    luxup_result_free(result);
    g_print("%u\n", total);
    return failed ? 1 : 0;
}

static int command_line(GtkApplication *application, GApplicationCommandLine *cmdline, gpointer data)
{
    (void) data;
    int argc = 0;
    char **argv = g_application_command_line_get_arguments(cmdline, &argc);
    gboolean refresh = FALSE;
    gboolean background = FALSE;
    gboolean no_refresh = FALSE;
    for (int i = 1; i < argc; ++i) {
        if (g_strcmp0(argv[i], "--refresh") == 0)
            refresh = TRUE;
        else if (g_strcmp0(argv[i], "--background") == 0)
            background = TRUE;
        else if (g_strcmp0(argv[i], "--no-refresh") == 0)
            no_refresh = TRUE;
    }
    g_strfreev(argv);
    if (background)
        luxup.background = TRUE;
    gboolean was_checking = luxup.checking;
    gboolean had_result = luxup.result != NULL;
    luxup.refresh_on_launch = refresh || !no_refresh;
    if (refresh && luxup.service_connecting) {
        luxup.service_check_queued = TRUE;
        luxup.service_refresh_queued = TRUE;
        luxup.service_force_queued = TRUE;
    }
    g_application_activate(G_APPLICATION(application));
    /* Forward --refresh through the single-instance path so a second
     * invocation with --refresh triggers a recheck. */
    if (refresh && luxup.service_attached && !luxup.service_connecting) {
        service_request_refresh();
    } else if (refresh && was_checking && !luxup.service_connecting) {
        luxup.refresh_pending = TRUE;
        if (luxup.cancellable) g_cancellable_cancel(luxup.cancellable);
    } else if (refresh && had_result && !luxup.checking)
        check_async(TRUE);
    return 0;
}

int main(int argc, char **argv)
{
    gboolean check_only = FALSE;
    gboolean refresh = FALSE;
    gboolean no_refresh = FALSE;
    for (int i = 1; i < argc; ++i) {
        if (g_strcmp0(argv[i], "--background") == 0)
            luxup.background = TRUE;
        else if (g_strcmp0(argv[i], "--check") == 0)
            check_only = TRUE;
        else if (g_strcmp0(argv[i], "--refresh") == 0)
            refresh = TRUE;
        else if (g_strcmp0(argv[i], "--no-refresh") == 0)
            no_refresh = TRUE;
    }
    luxup.refresh_on_launch = !no_refresh;

    if (check_only) {
        return print_check(refresh);
    }
#ifndef LUXUP_BEHAVIOR_TEST
    if(luxup.background) {
        argv[0]=LUXUP_SERVICE;
        execv(LUXUP_SERVICE,argv);
        g_printerr("Cannot start LuxUp service: %s\n",g_strerror(errno));return 1;
    }
#endif

    GtkApplication *application = GTK_APPLICATION(luxe_application_new("org.luxe.LuxUp", G_APPLICATION_HANDLES_COMMAND_LINE));
    luxup.application = application;
    g_signal_connect(application, "startup", G_CALLBACK(startup), NULL);
    g_signal_connect(application, "activate", G_CALLBACK(activate), NULL);
    g_signal_connect(application, "command-line", G_CALLBACK(command_line), NULL);
    int status = g_application_run(G_APPLICATION(application), argc, argv);
    luxup.shutting_down = TRUE;
    if (luxup.cancellable) g_cancellable_cancel(luxup.cancellable);
    if (luxup.service_calls) g_cancellable_cancel(luxup.service_calls);
    while (luxup.cancellable && luxup.checking) g_main_context_iteration(NULL, TRUE);
    if (luxup.window) gtk_window_destroy(luxup.window);
    luxup.window = NULL;
    if (luxup.result)
        luxup_result_free(luxup.result);
#ifdef LUXUP_BEHAVIOR_TEST
    if (luxup.tray) luxup_tray_free(luxup.tray);
#endif
    g_clear_object(&luxup.cancellable);
    g_clear_object(&luxup.service_calls);
    g_clear_object(&luxup.service);
    g_object_unref(application);
    return status;
}
