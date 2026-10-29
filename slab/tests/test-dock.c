/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#define main slab_main
#include "../src/main.c"
#undef main
#include <glib/gstdio.h>

static void drain(void)
{
    gint64 until = g_get_monotonic_time() + 100000;
    do { while (g_main_context_iteration(NULL, FALSE)); g_usleep(1000); }
    while (g_get_monotonic_time() < until);
}
static void manual_refresh(Dock *d)
{
    if (d->refresh) { g_source_remove(d->refresh); d->refresh = 0; }
    refresh(d);
}
static gboolean event_heartbeat(gpointer data) { ++*(guint *)data; return G_SOURCE_CONTINUE; }
int main(void)
{
    /* RunNestedX provides private XDG directories and display. */
    g_autofree char *apps_dir = g_build_filename(g_get_user_data_dir(), "applications", NULL);
    g_assert_cmpint(g_mkdir_with_parents(apps_dir, 0700), ==, 0);
    g_autofree char *desktop = g_build_filename(apps_dir, "slab-test.desktop", NULL);
    g_assert_true(g_file_set_contents(desktop,
        "[Desktop Entry]\nType=Application\nName=Slab Test\nExec=/bin/true\nIcon=utilities-terminal\nStartupWMClass=SlabFixture\n", -1, NULL));
    g_autofree char *pins_dir = g_build_filename(g_get_user_config_dir(), "slab", NULL);
    g_assert_cmpint(g_mkdir_with_parents(pins_dir, 0700), ==, 0);
    g_autofree char *pins_path = g_build_filename(pins_dir, "pins.txt", NULL);
    g_assert_true(g_file_set_contents(pins_path, "", 0, NULL));
    luxe_gtk_init(); Dock d = {0};
    d.app = gtk_application_new("org.luxe.Slab.Test", G_APPLICATION_NON_UNIQUE);
    g_assert_true(g_application_register(G_APPLICATION(d.app), NULL, NULL));
    activate(d.app, &d); drain();
    guint inventory_before_preferences = d.inventory_count;
    g_settings_set_int(d.preferences, "gap", 8);
    g_settings_set_int(d.preferences, "padding", 5);
    g_settings_set_int(d.preferences, "margin", 6);
    g_settings_set_int(d.preferences, "opacity", 80);
    g_settings_set_boolean(d.preferences, "animations", TRUE); drain();
    g_assert_cmpint(gtk_box_get_spacing(d.tasks), ==, 8);
    g_assert_cmpint(gtk_widget_get_margin_start(gtk_window_get_child(d.window)), ==, 6);
    g_assert_cmpfloat(gtk_widget_get_opacity(GTK_WIDGET(d.window)), ==, 1.0);
    g_assert_cmpuint(d.inventory_count, ==, inventory_before_preferences);
    g_autofree char *style = gtk_css_provider_to_string(d.style);
    g_assert_nonnull(strstr(style, "alpha(@theme_bg_color"));
    d.autohide = FALSE;
    g_assert_true(contains(&d, d.xid, "_NET_WM_WINDOW_TYPE", "_NET_WM_WINDOW_TYPE_DOCK"));
    Window windows[2];
    for (int i = 0; i < 2; ++i) {
        windows[i] = XCreateSimpleWindow(d.display, d.root, 0, 0, 100, 100, 0, 0, 0);
        const char klass[] = "fixture\0SlabFixture\0";
        XChangeProperty(d.display, windows[i], atom(&d, "WM_CLASS"), XA_STRING, 8,
                        PropModeReplace, (const unsigned char *)klass, sizeof klass - 1);
        XChangeProperty(d.display, windows[i], atom(&d, "_NET_WM_NAME"), atom(&d, "UTF8_STRING"), 8,
                        PropModeReplace, (const unsigned char *)"Test window", 11);
    }
    XChangeProperty(d.display, d.root, atom(&d, "_NET_CLIENT_LIST"), XA_WINDOW, 32,
                    PropModeReplace, (unsigned char *)windows, 2);
    manual_refresh(&d);
    g_assert_cmpuint(d.groups->len, ==, 1);
    Group *g = g_ptr_array_index(d.groups, 0);
    g_assert_cmpstr(g->key, ==, "slab-test.desktop"); g_assert_cmpuint(g->windows->len, ==, 2);
    g_assert_cmpuint(next_window(g, windows[0]), ==, windows[1]);
    g_assert_cmpuint(next_window(g, windows[1]), ==, windows[0]);
    /* Title storms must not rebuild the full client inventory. */
    drain();
    guint inventory_before_titles = d.inventory_count;
    guint heartbeat = 0; guint heartbeat_source = g_timeout_add(1, event_heartbeat, &heartbeat);
    for (guint i = 0; i < 1000; ++i) {
        char title[32]; g_snprintf(title, sizeof title, "Title %u", i);
        XChangeProperty(d.display, windows[0], atom(&d, "_NET_WM_NAME"), atom(&d, "UTF8_STRING"), 8,
                        PropModeReplace, (unsigned char *)title, strlen(title));
    }
    XFlush(d.display); drain();
    g_source_remove(heartbeat_source);
    g_assert_cmpuint(d.inventory_count, ==, inventory_before_titles);
    g_assert_cmpuint(heartbeat, >, 0);
    g_assert_cmpuint(d.max_event_batch, <=, 128);
    GtkWidget *original_button = g->button;
    XChangeProperty(d.display, d.root, atom(&d, "_NET_ACTIVE_WINDOW"), XA_WINDOW, 32,
                    PropModeReplace, (unsigned char *)windows, 1);
    manual_refresh(&d);
    g = g_ptr_array_index(d.groups, 0);
    g_assert_true(original_button == g->button);
    g_assert_true(gtk_widget_has_css_class(g->button, "suggested-action"));
    /* A desktop file update must refresh metadata without replacing the button. */
    g_autofree char *marker = g_build_filename(g_get_user_data_dir(), "launch-marker", NULL);
    g_autofree char *command = g_build_filename(g_get_user_data_dir(), "launch-fixture", NULL);
    g_autofree char *script = g_strdup_printf("#!/bin/sh\nprintf updated > '%s'\n", marker);
    g_assert_true(g_file_set_contents(command, script, -1, NULL));
    g_assert_cmpint(g_chmod(command, 0700), ==, 0);
    g_autofree char *updated = g_strdup_printf("[Desktop Entry]\nType=Application\nName=Updated Test\nExec=%s\nIcon=edit-copy\nStartupWMClass=SlabFixture\n", command);
    g_assert_true(g_file_set_contents(desktop, updated, -1, NULL));
    gtk_window_set_focus(d.window, original_button);
    drain(); apps_changed(NULL, &d); manual_refresh(&d);
    g = g_ptr_array_index(d.groups, 0);
    g_assert_true(original_button == g->button);
    g_assert_true(gtk_window_get_focus(d.window) == original_button);
    g_assert_cmpstr(g->name, ==, "Updated Test");
    g_assert_cmpstr(g_app_info_get_executable(G_APP_INFO(g->app)), ==, command);
    GIcon *icon = gtk_image_get_gicon(GTK_IMAGE(g->image));
    g_autofree char *icon_name = icon ? g_icon_to_string(icon) : NULL;
    g_assert_cmpstr(icon_name, ==, "edit-copy");
    launch_app(g);
    for (int i = 0; i < 50 && !g_file_test(marker, G_FILE_TEST_EXISTS); ++i) drain();
    g_assert_true(g_file_test(marker, G_FILE_TEST_EXISTS));
    /* NoDisplay affects menus, not an already-running or deliberately pinned app. */
    g_autofree char *no_display = g_strconcat(updated, "NoDisplay=true\n", NULL);
    g_assert_true(g_file_set_contents(desktop, no_display, -1, NULL));
    drain(); apps_changed(NULL, &d); manual_refresh(&d);
    g = g_ptr_array_index(d.groups, 0);
    g_assert_true(g->button == original_button);
    g_assert_nonnull(g->app);
    g_autofree char *hidden = g_strconcat(updated, "Hidden=true\n", NULL);
    g_assert_true(g_file_set_contents(desktop, hidden, -1, NULL));
    drain(); apps_changed(NULL, &d); manual_refresh(&d);
    g_assert_cmpuint(d.groups->len, ==, 1);
    g = g_ptr_array_index(d.groups, 0);
    g_assert_null(g->app);
    g_assert_true(g_str_has_prefix(g->key, "class:"));
    g_assert_true(g_file_set_contents(desktop, updated, -1, NULL));
    drain(); apps_changed(NULL, &d); manual_refresh(&d);
    g = g_ptr_array_index(d.groups, 0);
    g_assert_nonnull(g->app);
    /* A removed entry falls back to the running window; reinstall restores it. */
    g_assert_cmpint(g_unlink(desktop), ==, 0);
    drain(); apps_changed(NULL, &d); manual_refresh(&d);
    g_assert_cmpuint(d.groups->len, ==, 1);
    g = g_ptr_array_index(d.groups, 0);
    g_assert_null(g->app);
    g_assert_true(g_str_has_prefix(g->key, "class:"));
    g_assert_true(g_file_set_contents(desktop, updated, -1, NULL));
    drain(); apps_changed(NULL, &d); manual_refresh(&d);
    g_assert_cmpuint(d.groups->len, ==, 1);
    g = g_ptr_array_index(d.groups, 0);
    g_assert_nonnull(g->app);
    g_assert_cmpstr(g_app_info_get_executable(G_APP_INFO(g->app)), ==, command);
    /* Two windows paint exactly two opposite dots, never a numeric label. */
    g_assert_true(GTK_IS_DRAWING_AREA(g->count));
    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 36, 36);
    cairo_t *cr = cairo_create(surface);
    draw_indicators(GTK_DRAWING_AREA(g->count), cr, 36, 36, g);
    cairo_surface_flush(surface);
    unsigned char *pixels = cairo_image_surface_get_data(surface);
    int stride = cairo_image_surface_get_stride(surface);
    g_assert_cmpuint(*(guint32 *)(pixels + 3 * stride + 18 * 4), !=, 0);
    g_assert_cmpuint(*(guint32 *)(pixels + 33 * stride + 18 * 4), !=, 0);
    g_assert_cmpuint(*(guint32 *)(pixels + 18 * stride + 18 * 4), ==, 0);
    cairo_destroy(cr); cairo_surface_destroy(surface);
    /* Drag and drop callback persists and deduplicates the running application. */
    GValue value = G_VALUE_INIT; g_value_init(&value, G_TYPE_STRING);
    g_value_set_string(&value, "slab-test.desktop");
    d.dragging = TRUE;
    g_assert_true(drop(NULL, &value, 0, 0, &d));
    manual_refresh(&d); g_assert_true(d.dirty); /* never destroy an active drag source */
    d.dragging = FALSE; manual_refresh(&d);
    g_assert_cmpuint(d.groups->len, ==, 1); g_assert_cmpuint(d.pins->len, ==, 1);
    g = g_ptr_array_index(d.groups, 0); g_assert_true(g->pinned);
    g_assert_true(drop(NULL, &value, 0, 0, &d)); g_assert_cmpuint(d.pins->len, ==, 1);
    g_assert_false(pin_at(&d, "not-installed.desktop", 0));
    g_value_set_string(&value, "../../untrusted.desktop"); g_assert_false(drop(NULL, &value, 0, 0, &d));
    g_value_unset(&value);
    g_autoptr(GPtrArray) loaded = slab_pins_load(d.pins_path, NULL);
    g_assert_cmpstr(g_ptr_array_index(loaded, 0), ==, "slab-test.desktop");
    /* Installed file-list drops resolve by path, not basename. */
    g_autoptr(GFile) file = g_file_new_for_path(desktop);
    GFile *array[] = {file}; g_value_init(&value, GDK_TYPE_FILE_LIST);
    g_value_take_boxed(&value, gdk_file_list_new_from_array(array, 1));
    g_autofree char *id = drop_id(&d, &value); g_assert_cmpstr(id, ==, "slab-test.desktop"); g_value_unset(&value);
    g_autoptr(GFile) foreign = g_file_new_for_path("/tmp/slab-test.desktop");
    array[0] = foreign; g_value_init(&value, GDK_TYPE_FILE_LIST);
    g_value_take_boxed(&value, gdk_file_list_new_from_array(array, 1));
    g_assert_null(drop_id(&d, &value)); g_value_unset(&value);
    /* Keyboard context menu and unpin preserve the running group. */
    manual_refresh(&d); g = g_ptr_array_index(d.groups, 0);
    g_assert_true(menu_key(NULL, GDK_KEY_F10, 0, GDK_SHIFT_MASK, g));
    g_assert_nonnull(d.popover); pin_toggle(NULL, g); drain();
    g_assert_null(d.popover); manual_refresh(&d);
    g_assert_cmpuint(d.pins->len, ==, 0); g_assert_cmpuint(d.groups->len, ==, 1);
    Atom skip = atom(&d, "_NET_WM_STATE_SKIP_TASKBAR");
    XChangeProperty(d.display, windows[0], atom(&d, "_NET_WM_STATE"), XA_ATOM, 32,
                    PropModeReplace, (unsigned char *)&skip, 1);
    manual_refresh(&d); g = g_ptr_array_index(d.groups, 0); g_assert_cmpuint(g->windows->len, ==, 1);
    /* No WM in this fixture: focus remains on the root, so autohide can run. */
    d.autohide = TRUE; d.hovered = TRUE;
    if (d.hide_timer) { g_source_remove(d.hide_timer); d.hide_timer = 0; }
    d.dragging = TRUE; hide_dock(&d); g_assert_false(d.hidden); d.dragging = FALSE;
    d.hovered = FALSE; hide_dock(&d); g_assert_true(d.hidden);
    XWindowAttributes attributes; XGetWindowAttributes(d.display, d.edge, &attributes);
    g_assert_cmpint(attributes.map_state, ==, IsViewable);
    show_dock(&d); g_assert_false(d.hidden); g_assert_true(gtk_widget_get_visible(GTK_WIDGET(d.window)));
    drain();
    XGetWindowAttributes(d.display, d.edge, &attributes); g_assert_cmpint(attributes.map_state, ==, IsUnmapped);
    unsigned long hostile_area[] = { G_MAXUINT, G_MAXUINT, G_MAXUINT, G_MAXUINT };
    XChangeProperty(d.display, d.root, atom(&d, "_NET_WORKAREA"), XA_CARDINAL, 32,
                    PropModeReplace, (unsigned char *)hostile_area, 4);
    position(&d);
    g_assert_cmpint(d.last_x, >=, 0); g_assert_cmpint(d.last_y, >=, 0);
    g_assert_cmpint(d.last_x, <, DisplayWidth(d.display, DefaultScreen(d.display)));
    g_assert_cmpint(d.last_y, <, DisplayHeight(d.display, DefaultScreen(d.display)));
    for (int i = 0; i < 2; ++i) XDestroyWindow(d.display, windows[i]);
    XDeleteProperty(d.display, d.root, atom(&d, "_NET_CLIENT_LIST")); manual_refresh(&d);
    g_assert_cmpuint(d.groups->len, ==, 0);
    cleanup(&d); g_object_unref(d.app); return 0;
}
