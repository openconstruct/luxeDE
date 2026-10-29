/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#include "luxe-gtk-ui.h"
#include <stdarg.h>

static void scheme_changed(GSettings *settings, const char *key, gpointer unused)
{
    (void)key; (void)unused;
    g_autofree char *scheme = g_settings_get_string(settings, "color-scheme");
    g_object_set(gtk_settings_get_default(), "gtk-application-prefer-dark-theme",
                 !g_strcmp0(scheme, "prefer-dark"), NULL);
}

void luxe_ui_init(void)
{
    static gboolean initialized;
    luxe_gtk_init();
    if (initialized) return;
    initialized = TRUE;
    GSettingsSchemaSource *source = g_settings_schema_source_get_default();
    GSettingsSchema *schema = source ? g_settings_schema_source_lookup(source, "org.gnome.desktop.interface", TRUE) : NULL;
    if (schema) {
        if (g_settings_schema_has_key(schema, "color-scheme")) {
            GSettings *settings = g_settings_new_full(schema, NULL, NULL);
            scheme_changed(settings, NULL, NULL);
            g_signal_connect(settings, "changed::color-scheme", G_CALLBACK(scheme_changed), NULL);
            g_object_set_data_full(G_OBJECT(gtk_settings_get_default()), "luxe-interface-settings", settings, g_object_unref);
        }
        g_settings_schema_unref(schema);
    }
    GtkCssProvider *css = gtk_css_provider_new();
    gtk_css_provider_load_from_string(css,
        ".luxe-preferences { padding: 14px; }"
        ".luxe-group { border: 1px solid @borders; border-radius: 8px; }"
        ".luxe-group row { padding: 6px; }"
        ".luxe-section-title, .luxe-row-title { font-weight: bold; }"
        ".luxe-preferences .dim-label { opacity: 1; color: @insensitive_fg_color; }"
        ".luxe-toast { padding: 10px; margin: 14px; border: 1px solid @borders; border-radius: 8px; background: @theme_bg_color; color: @theme_fg_color; }");
    gtk_style_context_add_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);
}

GtkApplication *luxe_application_new(const char *id, GApplicationFlags flags)
{ luxe_ui_init(); return gtk_application_new(id, flags); }

GtkWidget *luxe_toolbar_new(void)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    g_object_set_data(G_OBJECT(box), "luxe-toolbar", GINT_TO_POINTER(1));
    return box;
}
void luxe_toolbar_set_content(GtkBox *box, GtkWidget *child)
{
    GtkWidget *old = g_object_get_data(G_OBJECT(box), "luxe-content");
    if (old) gtk_box_remove(box, old);
    g_object_set_data(G_OBJECT(box), "luxe-content", child);
    if (child) {
        gtk_widget_set_vexpand(child, TRUE);
        gtk_box_append(box, child);
        GtkWidget *bottom = g_object_get_data(G_OBJECT(box), "luxe-bottom");
        if (bottom) gtk_box_reorder_child_after(box, bottom, child);
    }
}
void luxe_toolbar_add_top_bar(GtkBox *box, GtkWidget *bar)
{
    if (GTK_IS_HEADER_BAR(bar)) {
        g_object_set_data_full(G_OBJECT(box), "luxe-titlebar", g_object_ref_sink(bar), g_object_unref);
    } else gtk_box_prepend(box, bar);
}
void luxe_toolbar_add_bottom_bar(GtkBox *box, GtkWidget *bar)
{ gtk_box_append(box, bar); g_object_set_data(G_OBJECT(box), "luxe-bottom", bar); }
void luxe_window_set_content(GtkWindow *window, GtkWidget *child)
{
    if (child && g_object_get_data(G_OBJECT(child), "luxe-toolbar")) {
        GtkWidget *header = g_object_get_data(G_OBJECT(child), "luxe-titlebar");
        if (header) gtk_window_set_titlebar(window, header);
    }
    gtk_window_set_child(window, child);
}
GtkWindow *luxe_dialog_new(void)
{ GtkWidget *w = gtk_window_new(); gtk_window_set_modal(GTK_WINDOW(w), TRUE); return GTK_WINDOW(w); }
void luxe_dialog_set_width(GtkWindow *w, int width)
{ int height; gtk_window_get_default_size(w, NULL, &height); gtk_window_set_default_size(w, width, height); }
void luxe_dialog_set_height(GtkWindow *w, int height)
{ int width; gtk_window_get_default_size(w, &width, NULL); gtk_window_set_default_size(w, width, height); }
void luxe_dialog_present(GtkWindow *window, GtkWidget *parent)
{
    GtkRoot *root = parent ? gtk_widget_get_root(parent) : NULL;
    if (GTK_IS_WINDOW(root) && GTK_WINDOW(root) != window) {
        gtk_window_set_transient_for(window, GTK_WINDOW(root));
        gtk_window_set_destroy_with_parent(window, TRUE);
        gtk_window_set_modal(window, TRUE);
    }
    gtk_window_present(window);
}
GtkWindow *luxe_window_get_visible_dialog(GtkWindow *parent)
{
    GListModel *windows = gtk_window_get_toplevels();
    GtkWindow *found = NULL;
    for (guint i = 0; i < g_list_model_get_n_items(windows); i++) {
        GtkWindow *w = g_list_model_get_item(windows, i);
        if (gtk_widget_get_visible(GTK_WIDGET(w)) && gtk_window_get_transient_for(w) == parent) found = w;
        g_object_unref(w);
    }
    return found; /* Borrowed, like gtk_application_get_active_window(). */
}

static GtkWidget *label_new(const char *text, const char *css)
{
    GtkWidget *label = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(label), 0);
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    if (css) gtk_widget_add_css_class(label, css);
    return label;
}
GtkWidget *luxe_preferences_page_new(void)
{
    GtkWidget *scroller = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_add_css_class(box, "luxe-preferences");
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), box);
    return scroller;
}
static GtkBox *page_box(GtkScrolledWindow *page)
{
    GtkWidget *child = gtk_scrolled_window_get_child(page);
    if (GTK_IS_VIEWPORT(child)) child = gtk_viewport_get_child(GTK_VIEWPORT(child));
    return GTK_BOX(child);
}
void luxe_preferences_page_add(GtkScrolledWindow *page, GtkBox *group)
{ gtk_box_append(page_box(page), GTK_WIDGET(group)); }
void luxe_preferences_page_remove(GtkScrolledWindow *page, GtkBox *group)
{ gtk_box_remove(page_box(page), GTK_WIDGET(group)); }
static void set_box_label(GtkBox *box, const char *key, const char *text, const char *css)
{
    GtkWidget *label = g_object_get_data(G_OBJECT(box), key);
    if (!label) {
        label = label_new(text, css);
        gtk_box_prepend(box, label);
        g_object_set_data(G_OBJECT(box), key, label);
    } else gtk_label_set_text(GTK_LABEL(label), text);
}
void luxe_preferences_page_set_title(GtkScrolledWindow *page, const char *text)
{ set_box_label(page_box(page), "luxe-title", text, "luxe-section-title"); }
void luxe_preferences_page_set_description(GtkScrolledWindow *page, const char *text)
{ set_box_label(page_box(page), "luxe-description", text, "dim-label"); }
GtkWidget *luxe_preferences_group_new(void)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    GtkWidget *list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_NONE);
    gtk_widget_add_css_class(list, "luxe-group");
    gtk_box_append(GTK_BOX(box), list);
    g_object_set_data(G_OBJECT(box), "luxe-list", list);
    return box;
}
void luxe_preferences_group_set_title(GtkBox *box, const char *text)
{ set_box_label(box, "luxe-title", text, "luxe-section-title"); }
void luxe_preferences_group_set_description(GtkBox *box, const char *text)
{ set_box_label(box, "luxe-description", text, "dim-label"); }
GtkWidget *luxe_action_row_new(void)
{
    GtkWidget *row = gtk_list_box_row_new(), *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *labels = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_hexpand(labels, TRUE);
    gtk_box_append(GTK_BOX(box), labels);
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), box);
    g_object_set_data(G_OBJECT(row), "luxe-labels", labels);
    GtkWidget *title = label_new(NULL, "luxe-row-title"), *subtitle = label_new(NULL, "dim-label");
    gtk_box_append(GTK_BOX(labels), title); gtk_box_append(GTK_BOX(labels), subtitle);
    gtk_widget_set_visible(subtitle, FALSE);
    g_object_set_data(G_OBJECT(row), "luxe-title", title);
    g_object_set_data(G_OBJECT(row), "luxe-subtitle", subtitle);
    gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(row), FALSE);
    return row;
}
void luxe_row_set_title(GtkWidget *widget, const char *text)
{
    if (GTK_IS_LIST_BOX_ROW(widget)) gtk_label_set_text(GTK_LABEL(g_object_get_data(G_OBJECT(widget), "luxe-title")), text);
    else {
        g_object_set_data_full(G_OBJECT(widget), "luxe-row-title", g_strdup(text), g_free);
        gtk_widget_set_tooltip_text(widget, text);
        gtk_accessible_update_property(GTK_ACCESSIBLE(widget), GTK_ACCESSIBLE_PROPERTY_LABEL, text, -1);
    }
}
void luxe_row_set_use_markup(GtkWidget *widget, gboolean markup)
{ if (GTK_IS_LIST_BOX_ROW(widget)) gtk_label_set_use_markup(GTK_LABEL(g_object_get_data(G_OBJECT(widget), "luxe-title")), markup); }
void luxe_row_set_subtitle(GtkWidget *row, const char *text)
{
    if (!GTK_IS_LIST_BOX_ROW(row)) {
        g_object_set_data_full(G_OBJECT(row), "luxe-row-subtitle", g_strdup(text), g_free);
        gtk_accessible_update_property(GTK_ACCESSIBLE(row), GTK_ACCESSIBLE_PROPERTY_DESCRIPTION, text, -1);
        return;
    }
    GtkWidget *label = g_object_get_data(G_OBJECT(row), "luxe-subtitle");
    gtk_label_set_text(GTK_LABEL(label), text); gtk_widget_set_visible(label, text && *text);
}
void luxe_action_row_add_suffix(GtkListBoxRow *row, GtkWidget *child)
{ gtk_widget_set_valign(child, GTK_ALIGN_CENTER); gtk_box_append(GTK_BOX(gtk_list_box_row_get_child(row)), child); }
static void row_activate(GtkListBoxRow *row, gpointer widget)
{ (void)row; if (!gtk_widget_activate(widget)) gtk_widget_grab_focus(widget); }
void luxe_action_row_set_activatable_widget(GtkListBoxRow *row, GtkWidget *widget)
{ gtk_list_box_row_set_activatable(row, TRUE); g_signal_connect(row, "activate", G_CALLBACK(row_activate), widget); }
void luxe_preferences_group_add(GtkBox *box, GtkWidget *widget)
{
    if (!GTK_IS_LIST_BOX_ROW(widget)) {
        const char *title = g_object_get_data(G_OBJECT(widget), "luxe-row-title");
        if (title) {
            GtkWidget *row = luxe_action_row_new();
            luxe_row_set_title(row, title);
            luxe_row_set_subtitle(row, g_object_get_data(G_OBJECT(widget), "luxe-row-subtitle"));
            luxe_action_row_add_suffix(GTK_LIST_BOX_ROW(row), widget);
            luxe_action_row_set_activatable_widget(GTK_LIST_BOX_ROW(row), widget);
            widget = row;
        }
    }
    gtk_list_box_append(GTK_LIST_BOX(g_object_get_data(G_OBJECT(box), "luxe-list")), widget);
}
GtkWidget *luxe_combo_row_new(void) { return gtk_drop_down_new(NULL, NULL); }
GtkWidget *luxe_switch_row_new(void) { return gtk_switch_new(); }
GtkWidget *luxe_spin_row_new(GtkAdjustment *a, double climb, guint digits) { return gtk_spin_button_new(a, climb, digits); }
GtkWidget *luxe_spin_row_new_with_range(double min, double max, double step) { return gtk_spin_button_new_with_range(min, max, step); }
GtkWidget *luxe_entry_row_new(void) { return gtk_entry_new(); }
GtkWidget *luxe_switcher_bar_new(void)
{ GtkWidget *w = gtk_revealer_new(); gtk_revealer_set_child(GTK_REVEALER(w), gtk_stack_switcher_new()); return w; }
void luxe_switcher_bar_set_stack(GtkRevealer *bar, GtkStack *stack)
{ gtk_stack_switcher_set_stack(GTK_STACK_SWITCHER(gtk_revealer_get_child(bar)), stack); }
void luxe_switcher_set_policy(GtkStackSwitcher *switcher, int policy) { (void)switcher; (void)policy; }
GtkStackPage *luxe_stack_add_titled_with_icon(GtkStack *stack, GtkWidget *child, const char *name, const char *title, const char *icon)
{ GtkStackPage *page = gtk_stack_add_titled(stack, child, name, title); gtk_stack_page_set_icon_name(page, icon); return page; }
static void status_label(GtkBox *box, const char *key, const char *css, const char *text)
{
    for (GtkWidget *child = gtk_widget_get_first_child(GTK_WIDGET(box)); child; child = gtk_widget_get_next_sibling(child))
        if (gtk_widget_has_css_class(child, css)) { g_object_set_data(G_OBJECT(box), key, child); break; }
    set_box_label(box, key, text, css);
}
void luxe_status_set_title(GtkBox *box, const char *text) { status_label(box, "luxe-title", "luxe-status-title", text); }
void luxe_status_set_description(GtkBox *box, const char *text) { status_label(box, "luxe-description", "luxe-status-description", text); }
GtkSettings *luxe_style_settings(void) { return gtk_settings_get_default(); }
gboolean luxe_style_get_dark(GtkSettings *s) { gboolean dark = FALSE; g_object_get(s, "gtk-application-prefer-dark-theme", &dark, NULL); return dark; }
gboolean luxe_style_get_high_contrast(GtkSettings *s)
{ g_autofree char *theme = NULL; g_object_get(s, "gtk-theme-name", &theme, NULL); return theme && g_str_has_prefix(theme, "HighContrast"); }
void luxe_style_set_color_scheme(GtkSettings *s, LuxeColorScheme scheme)
{ g_object_set(s, "gtk-application-prefer-dark-theme", scheme == LUXE_COLOR_SCHEME_FORCE_DARK || scheme == LUXE_COLOR_SCHEME_PREFER_DARK, NULL); }

/* Only choice dialogs need a string response signal. The presentation and
 * button activation remain native GtkDialog; no parallel widget toolkit. */
typedef struct { GtkDialog parent; } LuxeChoiceDialog;
typedef GtkDialogClass LuxeChoiceDialogClass;
G_DEFINE_TYPE(LuxeChoiceDialog, luxe_choice_dialog, GTK_TYPE_DIALOG)
static guint choice_signal;
static int response_id(GtkDialog *d, const char *name)
{ return GPOINTER_TO_INT(g_object_get_data(G_OBJECT(d), name)); }
static const char *response_name(GtkDialog *d, int id)
{
    g_autofree char *key = g_strdup_printf("luxe-id-%d", id);
    const char *name = g_object_get_data(G_OBJECT(d), key);
    if (!name) name = g_object_get_data(G_OBJECT(d), "luxe-close-response");
    return name ? name : "close";
}
static void choice_response(GtkDialog *d, int id, gpointer unused)
{
    (void)unused;
    g_object_ref(d);
    const char *name = response_name(d, id);
    g_autofree char *answer = g_strdup(name);
    GTask *task = g_object_steal_data(G_OBJECT(d), "luxe-choice-task");
    g_signal_emit(d, choice_signal, 0, answer);
    if (task) {
        GCancellable *cancel = g_task_get_cancellable(task);
        gulong handler = (gulong)GPOINTER_TO_SIZE(g_object_steal_data(G_OBJECT(d), "luxe-cancel-handler"));
        if (handler && cancel) g_cancellable_disconnect(cancel, handler);
        g_task_set_check_cancellable(task, FALSE);
        g_task_return_pointer(task, g_strdup(answer), g_free);
        gtk_widget_set_visible(GTK_WIDGET(d), FALSE);
        g_object_unref(task);
    } else gtk_window_destroy(GTK_WINDOW(d));
    g_object_unref(d);
}
static gboolean choice_close(GtkWindow *w, gpointer unused)
{ (void)unused; gtk_dialog_response(GTK_DIALOG(w), GTK_RESPONSE_DELETE_EVENT); return TRUE; }
static void luxe_choice_dialog_class_init(LuxeChoiceDialogClass *klass)
{ choice_signal = g_signal_new("luxe-response", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_STRING); }
static void luxe_choice_dialog_init(LuxeChoiceDialog *d)
{ g_signal_connect(d, "response", G_CALLBACK(choice_response), NULL); g_signal_connect(d, "close-request", G_CALLBACK(choice_close), NULL); }
GtkWindow *luxe_choice_dialog_new(const char *heading, const char *body)
{
    GtkWidget *widget = g_object_new(luxe_choice_dialog_get_type(), "modal", TRUE, "title", heading, "default-width", 440, NULL);
    GtkBox *box = GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(widget)));
    gtk_widget_add_css_class(GTK_WIDGET(box), "luxe-preferences");
    GtkWidget *title = label_new(heading, "luxe-section-title"), *detail = label_new(body, NULL);
    gtk_box_append(box, title); gtk_box_append(box, detail);
    g_object_set_data(G_OBJECT(widget), "luxe-body", detail);
    return GTK_WINDOW(widget);
}
void luxe_choice_add_response(GtkDialog *d, const char *name, const char *label)
{
    int id = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(d), "luxe-next-id")) + 1;
    g_object_set_data(G_OBJECT(d), "luxe-next-id", GINT_TO_POINTER(id));
    g_object_set_data(G_OBJECT(d), name, GINT_TO_POINTER(id));
    g_autofree char *key = g_strdup_printf("luxe-id-%d", id);
    g_object_set_data_full(G_OBJECT(d), key, g_strdup(name), g_free);
    GtkWidget *button = gtk_dialog_add_button(d, label, id);
    gtk_widget_set_receives_default(button, TRUE);
}
void luxe_choice_add_responses(GtkDialog *d, ...)
{
    va_list args; va_start(args, d);
    const char *name;
    while ((name = va_arg(args, const char *))) { const char *label = va_arg(args, const char *); luxe_choice_add_response(d, name, label); }
    va_end(args);
}
void luxe_choice_set_close_response(GtkDialog *d, const char *name)
{ g_object_set_data_full(G_OBJECT(d), "luxe-close-response", g_strdup(name), g_free); }
void luxe_choice_set_default_response(GtkDialog *d, const char *name)
{ gtk_dialog_set_default_response(d, response_id(d, name)); }
void luxe_choice_respond(GtkDialog *d, const char *name) { gtk_dialog_response(d, response_id(d, name)); }
void luxe_choice_set_response_appearance(GtkDialog *d, const char *name, LuxeResponseAppearance appearance)
{
    GtkWidget *button = gtk_dialog_get_widget_for_response(d, response_id(d, name));
    if (!button) return;
    gtk_widget_remove_css_class(button, "destructive-action"); gtk_widget_remove_css_class(button, "suggested-action");
    if (appearance != LUXE_RESPONSE_DEFAULT) gtk_widget_add_css_class(button, appearance == LUXE_RESPONSE_DESTRUCTIVE ? "destructive-action" : "suggested-action");
    g_object_set_data(G_OBJECT(button), "luxe-response-appearance", GINT_TO_POINTER(appearance));
}
LuxeResponseAppearance luxe_choice_get_response_appearance(GtkDialog *d, const char *name)
{
    GtkWidget *button = gtk_dialog_get_widget_for_response(d, response_id(d, name));
    return button ? GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "luxe-response-appearance")) : LUXE_RESPONSE_DEFAULT;
}
void luxe_choice_set_body(GtkDialog *d, const char *body) { gtk_label_set_text(GTK_LABEL(g_object_get_data(G_OBJECT(d), "luxe-body")), body); }
void luxe_choice_set_extra_child(GtkDialog *d, GtkWidget *child)
{
    GtkWidget *old = g_object_get_data(G_OBJECT(d), "luxe-extra");
    if (old) gtk_box_remove(GTK_BOX(gtk_dialog_get_content_area(d)), old);
    if (child) gtk_box_append(GTK_BOX(gtk_dialog_get_content_area(d)), child);
    g_object_set_data(G_OBJECT(d), "luxe-extra", child);
}
GtkWidget *luxe_choice_get_extra_child(GtkDialog *d) { return g_object_get_data(G_OBJECT(d), "luxe-extra"); }
static gboolean choice_cancel_idle(gpointer d) { luxe_dialog_close(GTK_WINDOW(d)); return G_SOURCE_REMOVE; }
static void choice_cancelled(GCancellable *c, gpointer d)
{ (void)c; g_idle_add_full(G_PRIORITY_DEFAULT, choice_cancel_idle, g_object_ref(d), g_object_unref); }
void luxe_choice_choose(GtkDialog *d, GtkWindow *parent, GCancellable *cancel, GAsyncReadyCallback callback, gpointer data)
{
    GTask *task = g_task_new(d, cancel, callback, data);
    g_object_set_data_full(G_OBJECT(d), "luxe-choice-task", task, g_object_unref);
    if (cancel) {
        gulong handler = g_cancellable_connect(cancel, G_CALLBACK(choice_cancelled), d, NULL);
        g_object_set_data(G_OBJECT(d), "luxe-cancel-handler", GSIZE_TO_POINTER(handler));
    }
    luxe_dialog_present(GTK_WINDOW(d), GTK_WIDGET(parent));
}
const char *luxe_choice_choose_finish(GtkDialog *d, GAsyncResult *result)
{
    char *answer = g_task_propagate_pointer(G_TASK(result), NULL);
    g_object_set_data_full(G_OBJECT(d), "luxe-choice-answer", answer, g_free);
    gtk_window_destroy(GTK_WINDOW(d));
    return answer ? answer : "close";
}
void luxe_dialog_close(GtkWindow *w)
{ if (G_TYPE_CHECK_INSTANCE_TYPE(w, luxe_choice_dialog_get_type())) gtk_dialog_response(GTK_DIALOG(w), GTK_RESPONSE_DELETE_EVENT); else gtk_window_destroy(w); }

GtkWindow *luxe_about_new(void) { return GTK_WINDOW(gtk_about_dialog_new()); }
void luxe_about_add_credit_section(GtkAboutDialog *d, const char *name, const char **people)
{ gtk_about_dialog_add_credit_section(d, name, people); }
void luxe_about_add_legal_section(GtkAboutDialog *d, const char *name, const char *copyright, GtkLicense type, const char *text)
{
    (void)type;
    const char *old = g_object_get_data(G_OBJECT(d), "luxe-legal-text");
    char *legal = g_strdup_printf("%s%s%s\n%s\n%s", old ? old : "", old ? "\n\n" : "", name, copyright ? copyright : "", text ? text : "");
    g_object_set_data_full(G_OBJECT(d), "luxe-legal-text", legal, g_free);
    const char *people[] = { text ? text : "", NULL };
    gtk_about_dialog_add_credit_section(d, name, people);
}
const char *luxe_about_get_license(GtkAboutDialog *d)
{ const char *legal = g_object_get_data(G_OBJECT(d), "luxe-legal-text"); return legal ? legal : gtk_about_dialog_get_license(d); }
LuxeToast *luxe_toast_new(const char *text)
{ LuxeToast *t = g_new0(LuxeToast, 1); t->message = g_strdup(text); t->timeout = 3; return t; }
void luxe_toast_set_timeout(LuxeToast *t, guint timeout) { t->timeout = timeout; }
static gboolean toast_expired(gpointer label)
{
    GtkWidget *parent = gtk_widget_get_parent(label);
    if (GTK_IS_OVERLAY(parent)) gtk_overlay_remove_overlay(GTK_OVERLAY(parent), label);
    return G_SOURCE_REMOVE;
}
void luxe_toast_overlay_add_toast(GtkOverlay *overlay, LuxeToast *t)
{
    GtkWidget *label = label_new(t->message, "luxe-toast");
    gtk_widget_set_halign(label, GTK_ALIGN_CENTER); gtk_widget_set_valign(label, GTK_ALIGN_END);
    gtk_overlay_add_overlay(overlay, label);
    g_timeout_add_seconds_full(G_PRIORITY_DEFAULT, MAX(t->timeout, 1), toast_expired, g_object_ref(label), g_object_unref);
    g_free(t->message); g_free(t);
}
