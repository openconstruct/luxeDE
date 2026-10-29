/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#include "../plugins/gtk/edit.c"

void _main_refresh(PluginData *data) { (void)data; }
void _show_error(const char *prefix, GError *error) { g_error("%s%s", prefix, error->message); }

static void test_copy(void)
{
    LuxeHotkeyAttr nested = { .name = "finalactions", .has_actions = TRUE, .has_value = TRUE };
    GList input = { .data = &nested };
    GList *copy = copy_options(&input);
    g_assert_true(options_equal(&input, copy));
    g_assert_true(copy->data != &nested);
    free_options(copy);
}

typedef struct {
    guint calls;
    guint fail_on_call;
    gboolean present;
    char *exec, *accel1, *accel2;
} BindingFixture;

static gboolean fixture_set_app(gpointer config, LuxeHotkeyApp *request, GError **error)
{
    BindingFixture *fixture = config;
    fixture->calls++;
    if (fixture->calls == fixture->fail_on_call) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED, "injected setter failure");
        return FALSE;
    }
    if (!request->accel1 && !request->accel2) {
        if (g_strcmp0(fixture->exec, request->exec) == 0)
            fixture->present = FALSE;
        return TRUE;
    }
    fixture->present = TRUE;
    g_free(fixture->exec); g_free(fixture->accel1); g_free(fixture->accel2);
    fixture->exec = g_strdup(request->exec);
    fixture->accel1 = g_strdup(request->accel1);
    fixture->accel2 = g_strdup(request->accel2);
    return TRUE;
}

static void test_binding_transaction_rollback(void)
{
    BindingFixture fixture = { .present = TRUE, .exec = g_strdup("old"),
        .accel1 = g_strdup("<Alt>a"), .accel2 = g_strdup("<Alt>b") };
    gpointer config = &fixture;
    LuxeHotkeyPluginInit callbacks = { .set_app_key = fixture_set_app };
    PluginData data = { .cb = &callbacks, .config = &config };
    LuxeHotkeyApp old = { .exec = "old", .accel1 = "<Alt>a",
        .accel2 = "<Alt>b" };
    LuxeHotkeyApp replacement = { .exec = "new", .accel1 = "<Alt>b",
        .accel2 = "<Alt>c" };
    GError *error = NULL;

    fixture.fail_on_call = 2;
    g_assert_false(replace_binding(&data, &old, &replacement,
        old.accel1, old.accel2, replacement.accel1, replacement.accel2,
        apply_app_binding, copy_app_binding, free_app_binding, &error));
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_FAILED);
    g_clear_error(&error);
    g_assert_cmpuint(fixture.calls, ==, 3);
    g_assert_true(fixture.present);
    g_assert_cmpstr(fixture.exec, ==, "old");
    g_assert_cmpstr(fixture.accel1, ==, "<Alt>a");
    g_assert_cmpstr(fixture.accel2, ==, "<Alt>b");

    fixture.calls = 0; fixture.fail_on_call = 0;
    replacement.accel1 = "<Control>x"; replacement.accel2 = NULL;
    g_assert_true(replace_binding(&data, &old, &replacement,
        old.accel1, old.accel2, replacement.accel1, replacement.accel2,
        apply_app_binding, copy_app_binding, free_app_binding, &error));
    g_assert_no_error(error);
    g_assert_cmpuint(fixture.calls, ==, 2);
    g_assert_true(fixture.present);
    g_assert_cmpstr(fixture.exec, ==, "new");
    g_assert_cmpstr(fixture.accel1, ==, "<Control>x");
    g_assert_null(fixture.accel2);
    g_free(fixture.exec); g_free(fixture.accel1); g_free(fixture.accel2);
}

static GList *templates(gpointer config, GError **error)
{
    static LuxeHotkeyAttr option = { .name = "prompt" };
    static GList list = { .data = &option };
    return &list;
}

static gboolean set_app(gpointer config, LuxeHotkeyApp *app, GError **error) { return TRUE; }

static GtkEventController *controller(GtkWidget *widget, GType type)
{
    GListModel *list = gtk_widget_observe_controllers(widget);
    GtkEventController *result = NULL;
    for (guint i = 0; i < g_list_model_get_n_items(list); i++) {
        GtkEventController *item = g_list_model_get_item(list, i);
        if (G_TYPE_CHECK_INSTANCE_TYPE(item, type)) {
            result = item;
            break;
        }
        g_object_unref(item);
    }
    g_object_unref(list);
    g_assert_nonnull(result);
    return result;
}

static void press(GtkEventController *keys, guint key, GdkModifierType state)
{
    gboolean handled = FALSE;
    g_signal_emit_by_name(keys, "key-pressed", key, 0u, state, &handled);
    g_assert_true(handled);
}

static void test_key_capture(void)
{
    PluginData data = {0};
    GtkWidget *window = gtk_window_new();
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    GtkWidget *frame = key_frame_new(&data, "Shortcut", "<Alt>F1", &data.edit_key1);
    data.edit_key2 = key_button_new(&data, NULL);
    data.edit_exec = GTK_ENTRY(gtk_entry_new());
    data.edit_apply_button = gtk_button_new();
    gtk_box_append(GTK_BOX(box), frame);
    gtk_box_append(GTK_BOX(box), data.edit_key2);
    gtk_box_append(GTK_BOX(box), GTK_WIDGET(data.edit_exec));
    gtk_box_append(GTK_BOX(box), data.edit_apply_button);
    gtk_window_set_child(GTK_WINDOW(window), box);
    gtk_window_present(GTK_WINDOW(window));
    while (g_main_context_iteration(NULL, FALSE));
    GtkEventController *keys = controller(data.edit_key1, GTK_TYPE_EVENT_CONTROLLER_KEY);
    GtkEventController *focus = controller(data.edit_key1, GTK_TYPE_EVENT_CONTROLLER_FOCUS);
    g_assert_cmpint(gtk_event_controller_get_propagation_phase(keys), ==, GTK_PHASE_CAPTURE);
    const guint bare[] = { GDK_KEY_a, GDK_KEY_space, GDK_KEY_Tab, GDK_KEY_Escape,
        GDK_KEY_BackSpace, GDK_KEY_Return, GDK_KEY_F1, GDK_KEY_Search, GDK_KEY_Caps_Lock };
    for (guint i = 0; i < G_N_ELEMENTS(bare); i++) {
        gtk_widget_grab_focus(data.edit_key1);
        press(keys, bare[i], 0);
        char *expected = gtk_accelerator_name(bare[i], 0);
        g_assert_cmpstr(g_object_get_data(G_OBJECT(data.edit_key1), "accelerator_name"), ==, expected);
        g_free(expected);
    }
    const guint modifiers[] = { GDK_KEY_Super_L, GDK_KEY_Super_R, GDK_KEY_Meta_L,
        GDK_KEY_Hyper_L, GDK_KEY_Control_L, GDK_KEY_Shift_L, GDK_KEY_Alt_L,
        GDK_KEY_ISO_Level3_Shift };
    for (guint i = 0; i < G_N_ELEMENTS(modifiers); i++) {
        gtk_widget_grab_focus(data.edit_key1);
        char *previous = g_strdup(g_object_get_data(G_OBJECT(data.edit_key1), "accelerator_name"));
        /* Search/Super can include its own modifier bit on key-press. */
        press(keys, modifiers[i], modifier_mask(modifiers[i]));
        g_assert_cmpstr(g_object_get_data(G_OBJECT(data.edit_key1), "accelerator_name"), ==, previous);
        g_signal_emit_by_name(keys, "key-released", modifiers[i], 0u, 0);
        char *expected = gtk_accelerator_name(modifiers[i], 0);
        g_assert_cmpstr(g_object_get_data(G_OBJECT(data.edit_key1), "accelerator_name"), ==, expected);
        g_free(previous);
        g_free(expected);
    }
    gtk_widget_grab_focus(data.edit_key1);
    press(keys, GDK_KEY_Control_L, 0);
    press(keys, GDK_KEY_a, GDK_CONTROL_MASK);
    g_signal_emit_by_name(keys, "key-released", GDK_KEY_Control_L, 0u, 0);
    g_assert_cmpstr(g_object_get_data(G_OBJECT(data.edit_key1), "accelerator_name"), ==, "<Control>a");
    gtk_widget_grab_focus(data.edit_key1);
    press(keys, GDK_KEY_Control_L, 0);
    press(keys, GDK_KEY_Alt_L, GDK_CONTROL_MASK);
    g_signal_emit_by_name(keys, "key-released", GDK_KEY_Alt_L, 0u, GDK_CONTROL_MASK);
    g_signal_emit_by_name(keys, "key-released", GDK_KEY_Control_L, 0u, 0);
    g_assert_cmpstr(g_object_get_data(G_OBJECT(data.edit_key1), "accelerator_name"), ==, "<Control>a");
    press(keys, GDK_KEY_Super_L, 0);
    g_signal_emit_by_name(focus, "leave");
    g_signal_emit_by_name(keys, "key-released", GDK_KEY_Super_L, 0u, 0);
    g_assert_cmpstr(g_object_get_data(G_OBJECT(data.edit_key1), "accelerator_name"), ==, "<Control>a");
    GtkWidget *clear = gtk_widget_get_last_child(gtk_frame_get_child(GTK_FRAME(frame)));
    g_signal_emit_by_name(clear, "clicked");
    g_assert_null(g_object_get_data(G_OBJECT(data.edit_key1), "accelerator_name"));
    g_assert_false(gtk_widget_get_sensitive(data.edit_apply_button));
    press(keys, GDK_KEY_Tab, 0);
    g_assert_true(gtk_widget_get_sensitive(data.edit_apply_button));
    g_object_unref(keys);
    g_object_unref(focus);
    gtk_window_destroy(GTK_WINDOW(window));
}

static void test_dialog(void)
{
    LuxeHotkeyPluginInit callbacks = { .get_app_options = templates, .set_app_key = set_app };
    gpointer config = NULL;
    PluginData data = { .cb = &callbacks, .config = &config };
    GtkWidget *parent = gtk_window_new();
    data.notebook = GTK_NOTEBOOK(gtk_notebook_new());
    data.apps = GTK_TREE_VIEW(gtk_tree_view_new());
    data.current_page = data.apps;
    GtkListStore *model = gtk_list_store_new(4, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_POINTER);
    gtk_tree_view_set_model(data.apps, GTK_TREE_MODEL(model));
    g_object_unref(model);
    gtk_notebook_append_page(data.notebook, GTK_WIDGET(data.apps), gtk_label_new("Programs"));
    luxe_window_set_content(GTK_WINDOW(parent), GTK_WIDGET(data.notebook));
    gtk_window_present(GTK_WINDOW(parent));
    for (int i = 0; i < 3; i++) {
        _edit_action(&data, NULL);
        g_assert_nonnull(data.edit_window);
        g_assert_true(GTK_IS_WINDOW(data.edit_window));
        while (g_main_context_iteration(NULL, FALSE));
        on_cancel_btn(NULL, &data);
        g_assert_null(data.edit_window);
        g_assert_null(data.edit_options_copy);
    }
    gtk_tree_view_set_model(data.apps, NULL);
    gtk_window_destroy(GTK_WINDOW(parent));
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    luxe_ui_init();
    g_test_add_func("/edit/copy-flags", test_copy);
    g_test_add_func("/edit/binding-transaction-rollback", test_binding_transaction_rollback);
    g_test_add_func("/edit/dialog-cleanup", test_dialog);
    g_test_add_func("/edit/single-key-and-chord-capture", test_key_capture);
    return g_test_run();
}
