/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <locale.h>
#include "settings-actions.h"
#include "luxe-spacing.h"
#include "luxe-gtk-ui.h"

/* Palette values stay in the global GTK theme. These scoped rules supply only
 * the group/row geometry formerly supplied by libadwaita preferences widgets. */
static const char layout_css[] =
    ".luxe-settings-page .dim-label { opacity: 1; color: @insensitive_fg_color; }"
    ".luxe-settings-intro { font-size: 20px; font-weight: bold; }"
    ".luxe-settings-section { font-weight: bold; }"
    ".luxe-settings-group { border: 1px solid @borders; border-radius: 8px; }"
    ".luxe-settings-group row { padding: 6px; border-radius: 0; }"
    ".luxe-settings-group row:first-child { border-radius: 7px 7px 0 0; }"
    ".luxe-settings-group row:last-child { border-radius: 0 0 7px 7px; }"
    ".luxe-settings-group row:only-child { border-radius: 7px; }"
    ".luxe-settings-group row:hover { background-color: alpha(@theme_fg_color, 0.08); }"
    ".luxe-settings-group row:focus { outline: 2px solid @theme_selected_bg_color; outline-offset: -2px; }"
    ".luxe-settings-row-title { font-weight: bold; }";

static GtkWidget *create_about_dialog(GtkWindow *parent)
{
    const char *authors[] = { "Jerry Howell (2026)", NULL };
    return g_object_new(GTK_TYPE_ABOUT_DIALOG,
        "transient-for", parent, "modal", TRUE,
        "program-name", _("luxe Settings"),
        "logo-icon-name", "gtk-preferences", "version", VERSION,
        "website", "https://github.com/openconstruct/luxeDE",
        "comments", _("Settings hub for the luxe desktop."),
        "authors", authors, "copyright", "Copyright © 2026 Jerry Howell",
        "license-type", GTK_LICENSE_GPL_2_0, NULL);
}

static void show_about(GSimpleAction *action, GVariant *parameter, gpointer data)
{
    (void)action; (void)parameter;
    GtkWindow *parent = gtk_application_get_active_window(GTK_APPLICATION(data));
    gtk_window_present(GTK_WINDOW(create_about_dialog(parent)));
}

typedef struct {
    GWeakRef parent;
} ActionWait;

static void show_action_error(GtkWindow *parent, const char *detail)
{
    GtkAlertDialog *dialog = gtk_alert_dialog_new(_("Could not open this setting"));
    gtk_alert_dialog_set_detail(dialog, detail);
    gtk_alert_dialog_show(dialog, parent);
    g_object_unref(dialog);
}

static void action_wait_done(GObject *source, GAsyncResult *result, gpointer user_data)
{
    ActionWait *wait = user_data;
    g_autoptr(GError) error = NULL;
    if (!g_subprocess_wait_check_finish(G_SUBPROCESS(source), result, &error)) {
        g_autoptr(GtkWindow) parent = g_weak_ref_get(&wait->parent);
        if (parent != NULL)
            show_action_error(parent, error->message);
    }
    g_weak_ref_clear(&wait->parent);
    g_free(wait);
}

static void launch_action(GtkListBox *list, GtkListBoxRow *row, gpointer data)
{
    (void)list; (void)data;
    const LuxeSettingsAction *action = g_object_get_data(G_OBJECT(row), "luxe-action");
    const char *program = g_object_get_data(G_OBJECT(row), "luxe-program");
    if (!luxe_settings_action_is_valid(action) || program == NULL)
        return;
    g_auto(GStrv) argv = g_strdupv((gchar **)action->argv);
    g_free(argv[0]);
    argv[0] = g_strdup(program);
    GtkRoot *root = gtk_widget_get_root(GTK_WIDGET(row));
    GtkWindow *parent = GTK_IS_WINDOW(root) ? GTK_WINDOW(root) : NULL;
    if (action->wait_for_exit) {
        g_autoptr(GError) error = NULL;
        g_autoptr(GSubprocess) process = g_subprocess_newv((const gchar * const *)argv,
                                                          G_SUBPROCESS_FLAGS_NONE,
                                                          &error);
        if (process == NULL) {
            show_action_error(parent, error->message);
            return;
        }
        ActionWait *wait = g_new0(ActionWait, 1);
        g_weak_ref_init(&wait->parent, parent);
        g_subprocess_wait_check_async(process, NULL, action_wait_done, wait);
        return;
    }
    GError *error = NULL;
    if (!g_spawn_async(NULL, argv, NULL, G_SPAWN_DEFAULT,
                       NULL, NULL, NULL, &error)) {
        show_action_error(parent, error->message);
        g_clear_error(&error);
    }
}

static GtkWidget *create_action_row(const LuxeSettingsAction *action)
{
    GtkWidget *row = gtk_list_box_row_new();
    g_autofree char *program = g_find_program_in_path(action->argv[0]);
    gboolean available = program != NULL;
    g_object_set_data(G_OBJECT(row), "luxe-action", (gpointer)action);
    g_object_set_data_full(G_OBJECT(row), "luxe-program", g_steal_pointer(&program), g_free);
    gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(row), available);
    gtk_widget_set_sensitive(row, available);
    if (!available)
        gtk_widget_set_tooltip_text(row, _("This setting is unavailable because its program is not installed."));
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, LUXE_SPACING_OUTER);
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), box);
    GtkWidget *icon = gtk_image_new_from_icon_name(action->icon_name);
    gtk_image_set_pixel_size(GTK_IMAGE(icon), 24);
    gtk_box_append(GTK_BOX(box), icon);
    GtkWidget *labels = gtk_box_new(GTK_ORIENTATION_VERTICAL, LUXE_SPACING_INNER);
    gtk_widget_set_hexpand(labels, TRUE);
    gtk_box_append(GTK_BOX(box), labels);
    const char *texts[] = { _(action->title), available ? _(action->subtitle) : _("Not installed") };
    for (guint i = 0; i < G_N_ELEMENTS(texts); i++) {
        GtkWidget *label = gtk_label_new(texts[i]);
        gtk_label_set_xalign(GTK_LABEL(label), 0);
        gtk_label_set_wrap(GTK_LABEL(label), TRUE);
        gtk_widget_add_css_class(label, i ? "dim-label" : "luxe-settings-row-title");
        gtk_box_append(GTK_BOX(labels), label);
    }
    GtkWidget *arrow = gtk_image_new_from_icon_name("go-next-symbolic");
    gtk_widget_add_css_class(arrow, "dim-label");
    gtk_box_append(GTK_BOX(box), arrow);
    return row;
}

static GtkWidget *create_preferences_page(void)
{
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, LUXE_SPACING_SECTION);
    gtk_widget_add_css_class(page, "luxe-settings-page");
    gtk_widget_set_margin_start(page, LUXE_SPACING_OUTER);
    gtk_widget_set_margin_end(page, LUXE_SPACING_OUTER);
    gtk_widget_set_margin_top(page, LUXE_SPACING_OUTER);
    gtk_widget_set_margin_bottom(page, LUXE_SPACING_OUTER);
    GtkWidget *intro = gtk_label_new(_("Configure your desktop"));
    gtk_label_set_xalign(GTK_LABEL(intro), 0);
    gtk_widget_add_css_class(intro, "luxe-settings-intro");
    gtk_box_append(GTK_BOX(page), intro);
    GtkWidget *subtitle = gtk_label_new(_("Configure the luxe desktop from one place."));
    gtk_label_set_xalign(GTK_LABEL(subtitle), 0);
    gtk_label_set_wrap(GTK_LABEL(subtitle), TRUE);
    gtk_widget_add_css_class(subtitle, "dim-label");
    gtk_box_append(GTK_BOX(page), subtitle);
    const char *section = NULL;
    GtkWidget *list = NULL;
    for (guint i = 0; i < luxe_settings_actions_count; i++) {
        const LuxeSettingsAction *action = &luxe_settings_actions[i];
        if (!luxe_settings_action_is_valid(action))
            continue;
        if (g_strcmp0(section, action->section) != 0) {
            GtkWidget *group = gtk_box_new(GTK_ORIENTATION_VERTICAL, LUXE_SPACING_INNER);
            GtkWidget *label = gtk_label_new(_(action->section));
            gtk_label_set_xalign(GTK_LABEL(label), 0);
            gtk_widget_add_css_class(label, "luxe-settings-section");
            gtk_box_append(GTK_BOX(group), label);
            list = gtk_list_box_new();
            gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_NONE);
            gtk_list_box_set_activate_on_single_click(GTK_LIST_BOX(list), TRUE);
            gtk_widget_add_css_class(list, "luxe-settings-group");
            g_signal_connect(list, "row-activated", G_CALLBACK(launch_action), NULL);
            gtk_box_append(GTK_BOX(group), list);
            gtk_box_append(GTK_BOX(page), group);
            section = action->section;
        }
        gtk_list_box_append(GTK_LIST_BOX(list), create_action_row(action));
    }
    return page;
}

static GtkWidget *create_settings_content(void)
{
    GtkWidget *scroller = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller),
                                  create_preferences_page());
    return scroller;
}

static void refresh_available_settings(GtkButton *button, gpointer data)
{
    (void)data;
    GtkRoot *root = gtk_widget_get_root(GTK_WIDGET(button));
    if (GTK_IS_WINDOW(root))
        gtk_window_set_child(GTK_WINDOW(root), create_settings_content());
}

static void startup(GApplication *application)
{
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_string(provider, layout_css);
    gtk_style_context_add_provider_for_display(gdk_display_get_default(),
        GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_set_data_full(G_OBJECT(application), "luxe-layout-css", provider, g_object_unref);
    const GActionEntry actions[] = { { "about", show_about, NULL, NULL, NULL, {0} } };
    g_action_map_add_action_entries(G_ACTION_MAP(application), actions, G_N_ELEMENTS(actions), application);
}

static void activate(GApplication *application)
{
    GtkWindow *existing = gtk_application_get_active_window(GTK_APPLICATION(application));
    if (existing) {
        gtk_window_present(existing);
        return;
    }
    GtkWidget *window = gtk_application_window_new(GTK_APPLICATION(application));
    gtk_window_set_title(GTK_WINDOW(window), "luxe Settings");
    gtk_window_set_icon_name(GTK_WINDOW(window), "gtk-preferences");
    gtk_window_set_default_size(GTK_WINDOW(window), 640, 680);
    GtkWidget *header = gtk_header_bar_new();
    GtkWidget *title = gtk_label_new(_("luxe Settings"));
    gtk_widget_add_css_class(title, "luxe-settings-row-title");
    gtk_header_bar_set_title_widget(GTK_HEADER_BAR(header), title);
    GtkWidget *menu_button = gtk_menu_button_new();
    g_autoptr(GMenu) menu = g_menu_new();
    g_menu_append(menu, _("About luxe Settings"), "app.about");
    gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(menu_button), "open-menu-symbolic");
    gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(menu_button), G_MENU_MODEL(menu));
    gtk_widget_set_tooltip_text(menu_button, _("Main menu"));
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), menu_button);
    GtkWidget *refresh = gtk_button_new_from_icon_name("view-refresh-symbolic");
    g_object_set_data(G_OBJECT(refresh), "luxe-refresh", GINT_TO_POINTER(1));
    gtk_widget_set_tooltip_text(refresh, _("Refresh available settings"));
    gtk_accessible_update_property(GTK_ACCESSIBLE(refresh),
        GTK_ACCESSIBLE_PROPERTY_LABEL, _("Refresh available settings"), -1);
    g_signal_connect(refresh, "clicked", G_CALLBACK(refresh_available_settings), NULL);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), refresh);
    gtk_window_set_titlebar(GTK_WINDOW(window), header);
    gtk_window_set_child(GTK_WINDOW(window), create_settings_content());
    gtk_window_present(GTK_WINDOW(window));
}

int main(int argc, char **argv)
{
    setlocale(LC_ALL, "");
    bindtextdomain(GETTEXT_PACKAGE, PACKAGE_LOCALE_DIR);
    bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");
    textdomain(GETTEXT_PACKAGE);
    luxe_ui_init();
    g_autoptr(GtkApplication) application = gtk_application_new(
        "org.luxe.Settings", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect_after(application, "startup", G_CALLBACK(startup), NULL);
    g_signal_connect(application, "activate", G_CALLBACK(activate), NULL);
    return g_application_run(G_APPLICATION(application), argc, argv);
}
