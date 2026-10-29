/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#include "../src/setting.h"
static guint palette_calls;
static guint accelerator_rebuilds;
static gboolean counted_desktop_palette(GdkRGBA *fg, GdkRGBA *bg, GdkRGBA palette[16])
{
    palette_calls++;
    return setting_desktop_palette(fg, bg, palette);
}
static void counted_set_accels(GtkApplication *app, const char *action, const char * const *accels)
{
    if (g_str_equal(action, "win.new-window")) accelerator_rebuilds++;
    gtk_application_set_accels_for_action(app, action, accels);
}
#define setting_desktop_palette counted_desktop_palette
#define gtk_application_set_accels_for_action counted_set_accels
#define main terminal_program_main
#include "../src/luxeterm.c"
#undef main
#undef setting_desktop_palette
#undef gtk_application_set_accels_for_action
#include "../src/preferences.c"
#include <glib/gstdio.h>

static void pump(guint milliseconds)
{
    gint64 end = g_get_monotonic_time() + milliseconds * 1000;
    do {
        while (g_main_context_iteration(NULL, FALSE));
        g_usleep(1000);
    } while (g_get_monotonic_time() < end);
}

static gboolean child_has_marker(GPid pid, const char *marker)
{
    g_autofree char *path = g_strdup_printf("/proc/%ld/environ", (long)pid);
    g_autofree char *data = NULL;
    gsize length = 0;
    if (!g_file_get_contents(path, &data, &length, NULL)) return FALSE;
    for (gsize offset = 0; offset < length;) {
        gsize remaining = length - offset;
        gsize item = strnlen(data + offset, remaining);
        if (item == strlen(marker) && !memcmp(data + offset, marker, item)) return TRUE;
        if (item == remaining) break;
        offset += item + 1;
    }
    return FALSE;
}

static GBytes *terminal_pixels(GtkWidget *widget)
{
    int width = gtk_widget_get_width(widget), height = gtk_widget_get_height(widget);
    g_assert_cmpint(width, >, 0); g_assert_cmpint(height, >, 0);
    g_autoptr(GdkPaintable) paintable = gtk_widget_paintable_new(widget);
    GtkSnapshot *snapshot = gtk_snapshot_new();
    gdk_paintable_snapshot(paintable, GDK_SNAPSHOT(snapshot), width, height);
    GskRenderNode *node = gtk_snapshot_free_to_node(snapshot);
    g_assert_nonnull(node);
    GtkNative *native = gtk_widget_get_native(widget);
    g_assert_nonnull(native);
    GskRenderer *renderer = gtk_native_get_renderer(native);
    graphene_rect_t bounds = GRAPHENE_RECT_INIT(0, 0, width, height);
    g_autoptr(GdkTexture) texture = gsk_renderer_render_texture(renderer, node, &bounds);
    gsk_render_node_unref(node);
    g_assert_nonnull(texture);
    g_assert_cmpint(gdk_texture_get_width(texture), ==, width);
    g_assert_cmpint(gdk_texture_get_height(texture), ==, height);
    gsize stride = (gsize)width * 4, size = stride * height;
    guchar *pixels = g_malloc(size);
    gdk_texture_download(texture, pixels, stride);
    return g_bytes_new_take(pixels, size);
}
static void test_arguments(void)
{
    g_autofree char *first_id = terminal_application_id(":10");
    g_autofree char *other_id = terminal_application_id(":11");
    g_assert_true(g_application_id_is_valid(first_id));
    g_assert_cmpstr(first_id, !=, other_id);
    CommandArguments args;
    char *invalid[][4] = {
        {"luxeterm", "-e", NULL}, {"luxeterm", "--command=", NULL},
        {"luxeterm", "--geometry=0x20", NULL}, {"luxeterm", "--geometry=80x24junk", NULL},
        {"luxeterm", "--geometry=999999999999x24", NULL}, {"luxeterm", "--title", NULL},
    };
    for (guint i = 0; i < G_N_ELEMENTS(invalid); i++) {
        g_assert_false(luxeterm_process_arguments(g_strv_length(invalid[i]), invalid[i], &args));
        g_strfreev(args.command);
    }
    char *valid[] = {"luxeterm", "--geometry=80x24", "-e", "/bin/printf", "%s", "a b", NULL};
    g_assert_true(luxeterm_process_arguments(6, valid, &args));
    g_assert_cmpint(args.geometry_columns, ==, 80);
    g_assert_cmpstr(args.command[0], ==, "/bin/printf");
    g_assert_cmpstr(args.command[1], ==, "/bin/printf");
    g_assert_cmpstr(args.command[3], ==, "a b");
    g_assert_null(args.command[4]);
    g_strfreev(args.command);
    char *login[] = {"luxeterm", "-l", "-e", "/bin/printf", "%s", "a b", NULL};
    g_assert_true(luxeterm_process_arguments(6, login, &args));
    g_assert_cmpstr(args.command[3], ==, "'/bin/printf' '%s' 'a b'");
    g_strfreev(args.command);
    g_autofree char *directory = g_dir_make_tmp("luxeterm-path-XXXXXX", NULL);
    g_assert_nonnull(directory);
    g_autofree char *program = g_build_filename(directory, "fixture-only-command", NULL);
    g_assert_true(g_file_set_contents(program, "#!/bin/sh\nexit 0\n", -1, NULL));
    g_assert_cmpint(g_chmod(program, 0700), ==, 0);
    g_autofree char *path_env = g_strdup_printf("PATH=%s", directory);
    const gchar *environment[] = {path_env, "SHELL=/bin/sh", NULL};
    char *single[] = {"luxeterm", "-e", "fixture-only-command", NULL};
    g_assert_true(process_arguments_for_env(3, single, &args, environment, directory));
    g_assert_cmpstr(args.command[0], ==, "fixture-only-command");
    g_assert_cmpstr(args.command[1], ==, "fixture-only-command");
    g_strfreev(args.command);
    const gchar *relative_env[] = {"PATH=.", "SHELL=/bin/sh", NULL};
    g_autofree char *working_option = g_strdup_printf("--working-directory=%s", directory);
    char *relative[] = {"luxeterm", working_option, "-e", "fixture-only-command", NULL};
    g_assert_true(process_arguments_for_env(4, relative, &args, relative_env, "/"));
    g_assert_cmpstr(args.command[0], ==, "fixture-only-command");
    g_strfreev(args.command);
    g_assert_cmpint(g_unlink(program), ==, 0);
    g_assert_cmpint(g_rmdir(directory), ==, 0);
}
static void test_settings(void)
{
    Setting *original = load_setting();
    Setting *copy = copy_setting(original);
    g_assert_true(original->zoom_in_accel != copy->zoom_in_accel);
    g_assert_true(original->keyfile != copy->keyfile);
    set_string(&copy->zoom_in_accel, "<Primary>F5");
    copy->follow_desktop_theme = FALSE;
    g_assert_cmpstr(original->zoom_in_accel, !=, copy->zoom_in_accel);
    g_assert_true(save_setting(copy, NULL));
    free_setting(&copy);
    load_setting();
    g_assert_false(get_setting()->follow_desktop_theme);
    g_autofree char *path = g_build_filename(g_get_user_config_dir(), "luxeterm", "luxeterm.conf", NULL);
    g_assert_true(g_file_set_contents(path, "[general]\n", -1, NULL));
    load_setting();
    g_assert_cmpstr(get_setting()->font_name, ==, "Monospace 10");
    g_assert_cmpstr(get_setting()->word_selection_characters, ==, "-A-Za-z0-9,./?%&#:_");
    g_assert_cmpint(get_setting()->scrollback, ==, 1000);
    g_assert_true(g_file_set_contents(path, "[general]\ngeometry_columns=-1\ngeometry_rows=oops\n[extra]\nkeep=me\n", -1, NULL));
    load_setting();
    g_assert_cmpint(get_setting()->geometry_columns, ==, 2);
    g_assert_cmpint(get_setting()->geometry_rows, ==, 24);
    g_assert_true(get_setting()->follow_desktop_theme);
    g_assert_true(save_setting(get_setting(), NULL));
    g_autofree char *text = NULL;
    g_assert_true(g_file_get_contents(path, &text, NULL, NULL));
    g_assert_nonnull(strstr(text, "keep=me"));
    g_assert_true(g_file_set_contents(path, "broken config", -1, NULL));
    GError *error = NULL;
    g_assert_false(save_setting(get_setting(), &error));
    g_clear_error(&error);
    g_unlink(path);
    g_assert_cmpint(symlink("/dev/null", path), ==, 0);
    g_assert_false(save_setting(get_setting(), &error));
    g_clear_error(&error);
    g_unlink(path);
    load_setting();
}
int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    luxe_ui_init();
    test_arguments(); test_settings();
    LXTermWindow context = {0};
    context.windows = g_ptr_array_new();
    context.app = GTK_APPLICATION(luxe_application_new("org.luxe.Term.Test", G_APPLICATION_NON_UNIQUE));
    g_assert_true(g_application_register(G_APPLICATION(context.app), NULL, NULL));
    g_signal_connect(gtk_settings_get_default(), "notify::gtk-theme-name",
        G_CALLBACK(terminal_desktop_theme_changed), &context);
    CommandArguments args;
    char *command[] = {"luxeterm", "--title=fixture", "--geometry=80x24", "-e", "/bin/sleep", "10", NULL};
    g_assert_true(luxeterm_process_arguments(6, command, &args));
    LuxeTerm *terminal = luxeterm_initialize(&context, &args);
    pump(150);
    Term *term = current_term(terminal);
    g_assert_cmpint(term->pid, >, 0);
    g_assert_true(get_setting()->follow_desktop_theme);
    const char *themes[]={"light","dark","nord","mocha","dracula","tokyo-night"};
    for(guint i=0;i<G_N_ELEMENTS(themes);i++) {
        g_autofree char *name=g_strdup_printf("Luxe-%s",themes[i]);
        g_object_set(gtk_settings_get_default(),"gtk-theme-name",name,NULL);
        pump(50);
        GdkRGBA fg,bg,colors[16],expected,rendered;
        g_assert_true(setting_desktop_palette(&fg,&bg,colors));
        g_autofree char *metadata=g_build_filename(LUXE_TEST_THEME_DATA,"bridges",themes[i],"metadata.ini",NULL);
        g_autoptr(GKeyFile) catalog=g_key_file_new();
        g_assert_true(g_key_file_load_from_file(catalog,metadata,G_KEY_FILE_NONE,NULL));
        g_autofree char *bg_text=g_key_file_get_string(catalog,"Theme","bg",NULL);
        g_autofree char *fg_text=g_key_file_get_string(catalog,"Theme","fg",NULL);
        g_assert_true(gdk_rgba_parse(&expected,bg_text));
        g_assert_cmpfloat_with_epsilon(bg.red,expected.red,.01);
        g_assert_cmpfloat_with_epsilon(bg.green,expected.green,.01);
        g_assert_cmpfloat_with_epsilon(bg.blue,expected.blue,.01);
        vte_terminal_get_color_background_for_draw(VTE_TERMINAL(term->vte),&rendered);
        g_assert_cmpfloat_with_epsilon(rendered.red,bg.red,.01);
        g_assert_cmpfloat_with_epsilon(rendered.green,bg.green,.01);
        g_assert_cmpfloat_with_epsilon(rendered.blue,bg.blue,.01);
        g_assert_true(gdk_rgba_parse(&expected,fg_text));
        g_assert_cmpfloat_with_epsilon(fg.red,expected.red,.01);
        g_assert_cmpfloat_with_epsilon(fg.green,expected.green,.01);
        g_assert_cmpfloat_with_epsilon(fg.blue,expected.blue,.01);
    }
    g_assert_true(gtk_widget_get_mapped(term->vte));
    glong columns = vte_terminal_get_column_count(VTE_TERMINAL(term->vte));
    glong rows = vte_terminal_get_row_count(VTE_TERMINAL(term->vte));
    vte_terminal_feed(VTE_TERMINAL(term->vte), "\033[?25l\033[2J\033[HVisible glyph fixture", -1);
    terminal_desktop_theme_changed(NULL, NULL, &context);
    pump(200);
    g_assert_cmpint(vte_terminal_get_column_count(VTE_TERMINAL(term->vte)), ==, columns);
    g_assert_cmpint(vte_terminal_get_row_count(VTE_TERMINAL(term->vte)), ==, rows);
    g_autoptr(GBytes) with_text = terminal_pixels(term->vte);
    vte_terminal_feed(VTE_TERMINAL(term->vte), "\033[2J\033[H", -1);
    pump(200);
    g_autoptr(GBytes) blank = terminal_pixels(term->vte);
    gsize with_size, blank_size;
    const guint8 *with_pixels = g_bytes_get_data(with_text, &with_size);
    const guint8 *blank_pixels = g_bytes_get_data(blank, &blank_size);
    g_assert_cmpuint(with_size, ==, blank_size);
    guint changed = 0;
    for (gsize i = 0; i < with_size; i += 4)
        if (memcmp(with_pixels + i, blank_pixels + i, 4)) changed++;
    g_assert_cmpuint(changed, >, 50); /* Actual glyph pixels survive a theme change. */
    gdk_rgba_parse(&get_setting()->background_color,"#123456");
    get_setting()->follow_desktop_theme=FALSE;
    terminal_settings_apply_to_all(terminal);
    g_object_set(gtk_settings_get_default(),"gtk-theme-name","Luxe-light",NULL);
    pump(50);
    GdkRGBA manual_bg;
    vte_terminal_get_color_background_for_draw(VTE_TERMINAL(term->vte),&manual_bg);
    g_assert_cmpfloat_with_epsilon(manual_bg.red,get_setting()->background_color.red,.01);
    g_assert_cmpfloat_with_epsilon(manual_bg.green,get_setting()->background_color.green,.01);
    g_assert_cmpfloat_with_epsilon(manual_bg.blue,get_setting()->background_color.blue,.01);
    get_setting()->follow_desktop_theme=TRUE;
    terminal_settings_apply_to_all(terminal);
    g_assert_nonnull(vte_terminal_get_word_char_exceptions(VTE_TERMINAL(term->vte)));
    char **accels = gtk_application_get_accels_for_action(context.app, "win.switch-tab(1)");
    g_assert_nonnull(accels[0]);
    g_strfreev(accels);
    terminal_preferences_dialog(NULL, NULL, terminal);
    GtkWindow *dialog = luxe_window_get_visible_dialog(GTK_WINDOW(terminal->window));
    g_assert_nonnull(dialog);
    Preferences *prefs = g_object_get_data(G_OBJECT(dialog), "preferences");
    GListModel *preset_model = gtk_drop_down_get_model(GTK_DROP_DOWN(prefs->preset));
    g_assert_cmpuint(g_list_model_get_n_items(preset_model), ==, color_preset_count);
    for (guint i = 0; i < color_preset_count; i++) {
        g_autoptr(GtkStringObject) item = g_list_model_get_item(preset_model, i);
        g_assert_cmpstr(gtk_string_object_get_string(item), ==, color_presets[i].name);
    }
    const char *range_keys[] = {SCROLLBACK, GEOMETRY_COLUMNS, GEOMETRY_ROWS};
    for (guint i = 0; i < G_N_ELEMENTS(range_keys); i++) {
        int minimum, maximum;
        g_assert_true(setting_integer_range(range_keys[i], &minimum, &maximum));
        GtkSpinButton *spin = g_object_get_data(G_OBJECT(dialog), range_keys[i]);
        g_assert_nonnull(spin);
        GtkAdjustment *adjustment = gtk_spin_button_get_adjustment(spin);
        g_assert_cmpfloat(gtk_adjustment_get_lower(adjustment), ==, minimum);
        g_assert_cmpfloat(gtk_adjustment_get_upper(adjustment), ==, maximum);
    }
    terminal_new_window_activate_event(NULL, NULL, terminal);
    pump(150);
    g_assert_cmpuint(context.windows->len, ==, 2);
    LuxeTerm *second = g_ptr_array_index(context.windows, 1);
    terminal_new_tab_activate_event(NULL, NULL, second);
    pump(150);
    g_assert_cmpuint(second->terms->len, ==, 2);
    Term *second_first = g_ptr_array_index(second->terms, 0);
    Term *second_term = current_term(second);
    palette_calls = 0;
    accelerator_rebuilds = 0;
    terminal_settings_apply_to_all(terminal);
    g_assert_cmpuint(palette_calls, ==, 1);
    g_assert_cmpuint(accelerator_rebuilds, ==, 1);
    vte_terminal_set_scrollback_lines(VTE_TERMINAL(term->vte), 123);
    vte_terminal_set_scrollback_lines(VTE_TERMINAL(second_term->vte), 234);
    vte_terminal_set_scrollback_lines(VTE_TERMINAL(second_first->vte), 345);
    palette_calls = 0;
    accelerator_rebuilds = 0;
    terminal_desktop_theme_changed(NULL, NULL, &context);
    g_assert_cmpuint(palette_calls, ==, 1);
    g_assert_cmpuint(accelerator_rebuilds, ==, 0);
    g_assert_cmpint(vte_terminal_get_scrollback_lines(VTE_TERMINAL(term->vte)), ==, 123);
    g_assert_cmpint(vte_terminal_get_scrollback_lines(VTE_TERMINAL(second_term->vte)), ==, 234);
    g_assert_cmpint(vte_terminal_get_scrollback_lines(VTE_TERMINAL(second_first->vte)), ==, 345);
    gtk_switch_set_active(GTK_SWITCH(prefs->follow), FALSE);
    terminal_preferences_dialog(NULL, NULL, second);
    g_assert_true(active_preferences == dialog);
    g_assert_true(g_object_get_data(G_OBJECT(active_preferences), "preferences") == prefs);
    g_assert_false(prefs->settings->follow_desktop_theme);
    gtk_switch_set_active(GTK_SWITCH(prefs->follow), TRUE);
    gtk_window_destroy(GTK_WINDOW(second->window));
    pump(150);
    g_assert_cmpuint(context.windows->len, ==, 1);
    g_assert_true(gtk_switch_get_active(GTK_SWITCH(prefs->follow)));
    g_assert_false(gtk_widget_get_sensitive(prefs->preset));
    gtk_switch_set_active(GTK_SWITCH(prefs->follow),FALSE);
    g_assert_true(gtk_widget_get_sensitive(prefs->preset));
    gtk_drop_down_set_selected(GTK_DROP_DOWN(prefs->preset), 3);
    g_assert_cmpstr(prefs->settings->color_preset, ==, "Solarized Dark");
    g_assert_cmpfloat(prefs->settings->background_color.red, ==, 0.0);
    luxe_dialog_close(dialog);
    pump(300);
    terminal_name_tab_activate_event(NULL, NULL, terminal);
    dialog = luxe_window_get_visible_dialog(GTK_WINDOW(terminal->window));
    g_assert_true(GTK_IS_DIALOG(dialog));
    g_object_ref(dialog);
    terminal_ref(term);
    get_setting()->disable_confirm = TRUE;
    terminal_close_tab_activate_event(NULL, NULL, terminal);
    pump(300);
    g_assert_true(term->closed);
    pump(100);
    g_object_unref(dialog);
    terminal_unref(term);
    get_setting()->disable_confirm = FALSE;
    /* Close immediately while VTE spawn is still pending. */
    g_assert_true(luxeterm_process_arguments(6, command, &args));
    terminal = luxeterm_initialize(&context, &args);
    gtk_window_destroy(GTK_WINDOW(terminal->window));
    pump(500);
    g_assert_cmpuint(context.windows->len, ==, 0);
    char *cat[] = {"luxeterm", "-e", "/bin/cat", NULL};
    g_assert_true(luxeterm_process_arguments(3, cat, &args));
    terminal = luxeterm_initialize(&context, &args);
    pump(150);
    term = current_term(terminal);
    g_assert_cmpint(term->pid, >, 0);
    /* All manual close routes must allow a running child to survive Cancel. */
    terminal_close_tab_activate_event(NULL, NULL, terminal);
    dialog = luxe_window_get_visible_dialog(GTK_WINDOW(terminal->window));
    g_assert_nonnull(dialog);
    luxe_choice_respond(GTK_DIALOG(dialog), "cancel"); pump(100);
    g_assert_false(term->closed);
    terminal_close_button_event(NULL, term);
    dialog = luxe_window_get_visible_dialog(GTK_WINDOW(terminal->window));
    g_assert_nonnull(dialog);
    luxe_choice_respond(GTK_DIALOG(dialog), "cancel"); pump(100);
    g_assert_false(term->closed);
    terminal_tab_middle_click_event(NULL, 1, 0, 0, term);
    dialog = luxe_window_get_visible_dialog(GTK_WINDOW(terminal->window));
    g_assert_nonnull(dialog);
    luxe_choice_respond(GTK_DIALOG(dialog), "cancel"); pump(100);
    g_assert_false(term->closed);
    gtk_window_close(GTK_WINDOW(terminal->window));
    dialog = luxe_window_get_visible_dialog(GTK_WINDOW(terminal->window));
    g_assert_nonnull(dialog);
    luxe_choice_respond(GTK_DIALOG(dialog), "cancel"); pump(100);
    g_assert_false(term->closed);
    g_assert_cmpint(kill(term->pid, 0), ==, 0);
    gdk_clipboard_set_text(gtk_widget_get_clipboard(term->vte), "luxe clipboard fixture\n");
    terminal_show_popup_menu(VTE_TERMINAL(term->vte), 10, 10, term);
    g_action_group_activate_action(G_ACTION_GROUP(terminal->window), "paste", NULL);
    pump(250);
    g_autofree char *contents = vte_terminal_get_text(VTE_TERMINAL(term->vte), NULL, NULL, NULL);
    g_assert_nonnull(strstr(contents, "luxe clipboard fixture"));
    terminal_preferences_dialog(NULL, NULL, terminal);
    dialog = luxe_window_get_visible_dialog(GTK_WINDOW(terminal->window));
    prefs = g_object_get_data(G_OBJECT(dialog), "preferences");
    set_string(&prefs->settings->tab_position, "bottom");
    apply_preferences(NULL, prefs);
    pump(300);
    g_assert_cmpint(gtk_notebook_get_tab_pos(GTK_NOTEBOOK(terminal->notebook)), ==, GTK_POS_BOTTOM);
    terminal_new_tab_activate_event(NULL, NULL, terminal);
    pump(200);
    g_assert_cmpuint(terminal->terms->len, ==, 2);
    Term *other = current_term(terminal);
    g_assert_true(other != term);
    terminal_ref(term);
    terminal_close_button_event(NULL, term);
    dialog = luxe_window_get_visible_dialog(GTK_WINDOW(terminal->window));
    g_assert_nonnull(dialog);
    /* The confirmed target remains the original tab even if selection changes. */
    gtk_notebook_set_current_page(GTK_NOTEBOOK(terminal->notebook), other->index);
    luxe_choice_respond(GTK_DIALOG(dialog), "close"); pump(200);
    g_assert_true(term->closed);
    g_assert_false(other->closed);
    terminal_unref(term);
    g_assert_cmpuint(terminal->terms->len, ==, 1);
    get_setting()->disable_confirm = TRUE;
    terminal_close_tab_activate_event(NULL, NULL, terminal);
    pump(300);
    g_assert_cmpuint(context.windows->len, ==, 0);
    g_assert_true(luxeterm_process_arguments(3, cat, &args));
    terminal = luxeterm_initialize(&context, &args);
    terminal_new_window_activate_event(NULL, NULL, terminal);
    pump(150);
    g_assert_cmpuint(context.windows->len, ==, 2);
    second = g_ptr_array_index(context.windows, 1);
    terminal_preferences_dialog(NULL, NULL, terminal);
    g_assert_nonnull(active_preferences);
    gtk_window_destroy(GTK_WINDOW(terminal->window));
    pump(200);
    g_assert_null(active_preferences);
    terminal_preferences_dialog(NULL, NULL, second);
    g_assert_nonnull(active_preferences);
    luxe_dialog_close(active_preferences);
    gtk_window_destroy(GTK_WINDOW(second->window));
    pump(200);
    g_assert_cmpuint(context.windows->len, ==, 0);
    get_setting()->disable_confirm = FALSE;
    char *finished[] = {"luxeterm", "-e", "/bin/true", NULL};
    g_assert_true(luxeterm_process_arguments(3, finished, &args));
    luxeterm_initialize(&context, &args);
    pump(700);
    g_assert_cmpuint(context.windows->len, ==, 0); /* A real child exit never asks. */
    char *shell_args[] = {"luxeterm", NULL};
    g_assert_true(luxeterm_process_arguments(1, shell_args, &args));
    g_auto(GStrv) shell_env = g_get_environ();
    shell_env = g_environ_setenv(shell_env, "SHELL", "/bin/sh", TRUE);
    shell_env = g_environ_setenv(shell_env, "HOME", "/tmp", TRUE);
    shell_env = g_environ_setenv(shell_env, "ENV", "/dev/null", TRUE);
    shell_env = g_environ_setenv(shell_env, "TERM", "xterm-256color", TRUE);
    shell_env = g_environ_setenv(shell_env, "PATH", "/usr/bin:/bin", TRUE);
    args.environment = (const gchar * const *)shell_env;
    terminal = luxeterm_initialize(&context, &args);
    pump(250);
    term = current_term(terminal);
    g_assert_true(term->interactive_shell);
    g_assert_false(terminal_tab_has_active_job(term));
    vte_terminal_feed_child(VTE_TERMINAL(term->vte), (const guint8 *)"sleep 5\n", -1);
    pump(250);
    g_assert_true(terminal_tab_has_active_job(term));
    terminal_request_close_tab(term);
    dialog = luxe_window_get_visible_dialog(GTK_WINDOW(terminal->window));
    g_assert_nonnull(dialog);
    luxe_choice_respond(GTK_DIALOG(dialog), "cancel");
    vte_terminal_feed_child(VTE_TERMINAL(term->vte), (const guint8 *)"\003", -1);
    pump(250);
    g_assert_false(terminal_tab_has_active_job(term));
    terminal_request_close_tab(term);
    g_assert_true(term->closed);
    pump(250);
    g_assert_cmpuint(context.windows->len, ==, 0);
    g_assert_true(luxeterm_process_arguments(3, cat, &args));
    terminal = luxeterm_initialize(&context, &args);
    pump(150);
    term = terminal_ref(current_term(terminal));
    GtkWidget *closing_window = g_object_ref(terminal->window);
    terminal_close_button_event(NULL, term);
    g_assert_nonnull(luxe_window_get_visible_dialog(GTK_WINDOW(closing_window)));
    g_assert_cmpint(kill(term->pid, SIGTERM), ==, 0); /* Only this fixture's child. */
    pump(700);
    g_assert_true(term->closed);
    terminal_unref(term);
    g_object_unref(closing_window);
    pump(100);
    g_assert_cmpuint(context.windows->len, ==, 0);
    g_auto(GStrv) launch_env = g_get_environ();
    launch_env = g_environ_setenv(launch_env, "LUXE_TEST_MARKER", "per-window", TRUE);
    g_assert_true(luxeterm_process_arguments(3, cat, &args));
    args.environment = (const gchar * const *)launch_env;
    terminal = luxeterm_initialize(&context, &args);
    pump(200);
    term = current_term(terminal);
    g_assert_true(child_has_marker(term->pid, "LUXE_TEST_MARKER=per-window"));
    terminal_new_tab_activate_event(NULL, NULL, terminal);
    pump(200);
    term = current_term(terminal);
    g_assert_true(child_has_marker(term->pid, "LUXE_TEST_MARKER=per-window"));
    terminal_new_window_activate_event(NULL, NULL, terminal);
    pump(200);
    second = g_ptr_array_index(context.windows, 1);
    g_assert_true(child_has_marker(current_term(second)->pid, "LUXE_TEST_MARKER=per-window"));
    gtk_window_destroy(GTK_WINDOW(second->window));
    gtk_window_destroy(GTK_WINDOW(terminal->window));
    pump(200);
    g_assert_cmpuint(context.windows->len, ==, 0);
    g_ptr_array_unref(context.windows);
    g_signal_handlers_disconnect_by_data(gtk_settings_get_default(),&context);
    g_object_unref(context.app);
    set_setting(NULL);
    return 0;
}
