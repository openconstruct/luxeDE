/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#include <gtk/gtk.h>
#include <gdk/x11/gdkx.h>
#include <gio/gdesktopappinfo.h>
#include <glib-unix.h>
#include <X11/Xatom.h>
#include <math.h>
#include "luxe-renderer.h"
#include "pins.h"
#include "preferences.h"
#ifndef SLAB_VERSION
#define SLAB_VERSION "0.2.9~testing1"
#endif

typedef struct Dock Dock;
typedef struct {
    Dock *dock;
    char *key, *name;
    GDesktopAppInfo *app;
    GArray *windows;
    GtkWidget *button, *count, *image;
    gboolean pinned;
} Group;
struct Dock {
    GtkApplication *app;
    GtkWindow *window;
    GtkBox *tasks;
    GtkScrolledWindow *scroll;
    GtkWidget *popover;
    GSettings *preferences;
    GtkCssProvider *style;
    Display *display;
    Window root, xid, edge, active;
    Atom a_client_list, a_active_window, a_wm_name, a_wm_state, a_wm_class;
    Atom a_gtk_application_id, a_workarea, a_current_desktop, a_client_stacking, a_utf8_string;
    guint source, refresh, hide_timer;
#ifdef SLAB_TESTING
    guint inventory_count;
    guint max_event_batch;
#endif
    int last_x, last_y;
    gboolean positioned, hidden, hovered, dragging, autohide, dirty, config_ok, closing;
    char *pins_path;
    GPtrArray *pins, *groups;
    GList *apps;
    GHashTable *app_index;
    GAppInfoMonitor *monitor;
    gulong monitor_handler;
};
static gboolean refresh(gpointer data);
static void position(Dock *d);
static void schedule_hide(Dock *d);

static Atom atom(Dock *d, const char *name) { return XInternAtom(d->display, name, False); }
/* Windows can disappear between list and property reads. */
static int xerror(Display *display, XErrorEvent *event)
{
    if (event->error_code != BadWindow && event->error_code != BadDrawable) {
        char message[256]; XGetErrorText(display, event->error_code, message, sizeof message);
        g_warning("slab X11 request %u: %s", event->request_code, message);
    }
    return 0;
}
static unsigned char *property(Dock *d, Window w, const char *name, Atom type, int format, unsigned long *count)
{
    Atom actual; int bits; unsigned long remaining; unsigned char *data = NULL;
    *count = 0;
    if (XGetWindowProperty(d->display, w, atom(d, name), 0, 4096, False,
        type, &actual, &bits, count, &remaining, &data) != Success || actual != type || bits != format || remaining) {
        if (data) XFree(data);
        *count = 0; return NULL;
    }
    return data;
}
static char *string_property(Dock *d, Window w, const char *name, Atom type)
{
    unsigned long n; unsigned char *raw = property(d, w, name, type, 8, &n);
    char *value = raw ? g_utf8_make_valid((char *)raw, n) : NULL;
    if (raw) XFree(raw);
    return value;
}
static gboolean contains(Dock *d, Window w, const char *name, const char *value)
{
    unsigned long n; Atom *items = (Atom *)property(d, w, name, XA_ATOM, 32, &n);
    gboolean found = FALSE; Atom wanted = atom(d, value);
    for (unsigned long i = 0; i < n; ++i) found |= items[i] == wanted;
    if (items) XFree(items);
    return found;
}
static void queue_refresh(Dock *d)
{
    d->dirty = TRUE;
    if (!d->closing && !d->refresh && !d->dragging && !d->popover) d->refresh = g_idle_add(refresh, d);
}
static void group_free(gpointer data)
{
    Group *g = data;
    g_free(g->key); g_free(g->name); g_clear_object(&g->app); g_array_unref(g->windows); g_free(g);
}
static Group *group_new(Dock *d, const char *key, const char *name, GDesktopAppInfo *app)
{
    Group *g = g_new0(Group, 1);
    g->dock = d; g->key = g_strdup(key); g->name = g_strdup(name);
    g->app = app ? g_object_ref(app) : NULL;
    g->windows = g_array_new(FALSE, FALSE, sizeof(Window)); return g;
}
static void index_key(Dock *d, const char *key, GDesktopAppInfo *info)
{
    if (!key || !*key) return;
    char *lower = g_ascii_strdown(key, -1);
    if (!g_hash_table_contains(d->app_index, lower)) g_hash_table_insert(d->app_index, lower, info);
    else g_free(lower);
}
static void index_apps(Dock *d)
{
    if (d->app_index) g_hash_table_unref(d->app_index);
    g_list_free_full(d->apps, g_object_unref);
    d->apps = g_app_info_get_all();
    d->app_index = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    /* Explicit identifiers precede executable-name fallback. */
    for (GList *l = d->apps; l; l = l->next) {
        if (!G_IS_DESKTOP_APP_INFO(l->data) || g_desktop_app_info_get_is_hidden(l->data)) continue;
        GDesktopAppInfo *info = l->data;
        const char *id = g_app_info_get_id(G_APP_INFO(info)); index_key(d, id, info);
        if (id && g_str_has_suffix(id, ".desktop")) {
            g_autofree char *stem = g_strndup(id, strlen(id) - 8); index_key(d, stem, info);
        }
        index_key(d, g_desktop_app_info_get_startup_wm_class(info), info);
    }
    for (GList *l = d->apps; l; l = l->next) {
        if (!G_IS_DESKTOP_APP_INFO(l->data) || g_desktop_app_info_get_is_hidden(l->data)) continue;
        const char *exe = g_app_info_get_executable(l->data);
        if (exe) { g_autofree char *base = g_path_get_basename(exe); index_key(d, base, l->data); }
    }
}
static GDesktopAppInfo *lookup(Dock *d, const char *key)
{
    if (!key) return NULL;
    g_autofree char *lower = g_ascii_strdown(key, -1); return g_hash_table_lookup(d->app_index, lower);
}
static GDesktopAppInfo *window_app(Dock *d, Window w, char **fallback)
{
    g_autofree char *gtk_id = string_property(d, w, "_GTK_APPLICATION_ID", atom(d, "UTF8_STRING"));
    GDesktopAppInfo *info = lookup(d, gtk_id);
    unsigned long n; unsigned char *raw = property(d, w, "WM_CLASS", XA_STRING, 8, &n);
    g_autofree char *instance = NULL, *klass = NULL;
    if (raw && n) {
        size_t first = strnlen((char *)raw, n); instance = g_strndup((char *)raw, first);
        if (first + 1 < n) klass = g_strndup((char *)raw + first + 1, strnlen((char *)raw + first + 1, n - first - 1));
    }
    if (raw) XFree(raw);
    if (!info) info = lookup(d, klass);
    if (!info) info = lookup(d, instance);
    if (klass && *klass) *fallback = g_ascii_strdown(klass, -1);
    else if (instance && *instance) *fallback = g_ascii_strdown(instance, -1);
    else *fallback = g_strdup_printf("window-%lu", w);
    return info;
}
static Window next_window(Group *g, Window active)
{
    if (!g->windows->len) return None;
    for (guint i = 0; i < g->windows->len; ++i)
        if (g_array_index(g->windows, Window, i) == active)
            return g_array_index(g->windows, Window, (i + 1) % g->windows->len);
    return g_array_index(g->windows, Window, 0);
}
static void request_focus(Dock *d, Window target)
{
    XEvent event = {0}; event.xclient.type = ClientMessage; event.xclient.window = target;
    event.xclient.message_type = atom(d, "_NET_ACTIVE_WINDOW"); event.xclient.format = 32;
    event.xclient.data.l[0] = 2;
    event.xclient.data.l[1] = gdk_x11_get_server_time(gtk_native_get_surface(GTK_NATIVE(d->window)));
    event.xclient.data.l[2] = d->active;
    XSendEvent(d->display, d->root, False, SubstructureRedirectMask | SubstructureNotifyMask, &event);
    XFlush(d->display);
}
static void show_error(Dock *d, const char *message)
{
    GtkAlertDialog *dialog = gtk_alert_dialog_new("slab could not complete that change");
    gtk_alert_dialog_set_detail(dialog, message); gtk_alert_dialog_show(dialog, d->window); g_object_unref(dialog);
}
static void launch_app(Group *g)
{
    if (!g->app) return;
    GdkAppLaunchContext *context = gdk_display_get_app_launch_context(gtk_widget_get_display(GTK_WIDGET(g->dock->window)));
    GError *error = NULL;
    if (!g_app_info_launch(G_APP_INFO(g->app), NULL, G_APP_LAUNCH_CONTEXT(context), &error)) {
        show_error(g->dock, error->message); g_clear_error(&error);
    }
    g_object_unref(context);
}
static void clicked(GtkButton *button, gpointer data)
{
    (void)button; Group *g = data; Window target = next_window(g, g->dock->active);
    if (target) request_focus(g->dock, target); else launch_app(g);
}
static gboolean commit_pins(Dock *d, GPtrArray *pins)
{
    GError *error = NULL;
    gboolean ok = d->config_ok && slab_pins_save_if_unchanged(d->pins_path, d->pins, pins, &error);
    if (ok) { g_ptr_array_unref(d->pins); d->pins = pins; queue_refresh(d); }
    else {
        show_error(d, error ? error->message : "Repair pins.txt and restart slab before modifying pins.");
        g_clear_error(&error); g_ptr_array_unref(pins);
    }
    return ok;
}
static gboolean pin_at(Dock *d, const char *id, guint index)
{
    if (!slab_id_valid(id)) return FALSE;
    g_autoptr(GDesktopAppInfo) info = g_desktop_app_info_new(id);
    if (!info || g_desktop_app_info_get_is_hidden(info)) return FALSE;
    if (slab_pin_index(d->pins, id) < 0 && d->pins->len >= SLAB_MAX_PINS) return FALSE;
    return commit_pins(d, slab_pins_move(d->pins, id, index));
}
static void menu_closed(GtkPopover *popover, gpointer data)
{
    Dock *d = data; d->popover = NULL; gtk_widget_unparent(GTK_WIDGET(popover));
    if (d->dirty) queue_refresh(d);
    schedule_hide(d);
}
static void pin_toggle(GtkButton *button, gpointer data)
{
    (void)button; Group *g = data; Dock *d = g->dock;
    const char *id = g_app_info_get_id(G_APP_INFO(g->app));
    if (!g->pinned) pin_at(d, id, d->pins->len);
    else {
        GPtrArray *pins = g_ptr_array_new_with_free_func(g_free);
        for (guint i = 0; i < d->pins->len; ++i)
            if (!g_str_equal(id, g_ptr_array_index(d->pins, i))) g_ptr_array_add(pins, g_strdup(g_ptr_array_index(d->pins, i)));
        commit_pins(d, pins);
    }
    gtk_popover_popdown(GTK_POPOVER(d->popover));
}
static void new_window(GtkButton *button, gpointer data)
{
    (void)button; Group *g = data; launch_app(g); gtk_popover_popdown(GTK_POPOVER(g->dock->popover));
}
static void selected_window(GtkButton *button, gpointer data)
{
    Dock *d = data;
    request_focus(d, (Window)(guintptr)g_object_get_data(G_OBJECT(button), "window"));
    gtk_popover_popdown(GTK_POPOVER(d->popover));
}
static void open_menu(Group *g)
{
    Dock *d = g->dock; if (d->popover) return;
    d->popover = gtk_popover_new(); gtk_widget_set_parent(d->popover, g->button);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_margin_start(box, 10); gtk_widget_set_margin_end(box, 10);
    gtk_widget_set_margin_top(box, 10); gtk_widget_set_margin_bottom(box, 10);
    if (g->app) {
        GtkWidget *pin = gtk_button_new_with_label(g->pinned ? "Unpin" : "Keep in dock");
        g_signal_connect(pin, "clicked", G_CALLBACK(pin_toggle), g); gtk_box_append(GTK_BOX(box), pin);
        GtkWidget *launch = gtk_button_new_with_label("Launch application");
        g_signal_connect(launch, "clicked", G_CALLBACK(new_window), g); gtk_box_append(GTK_BOX(box), launch);
    }
    for (guint i = 0; i < g->windows->len; ++i) {
        Window w = g_array_index(g->windows, Window, i);
        g_autofree char *title = string_property(d, w, "_NET_WM_NAME", atom(d, "UTF8_STRING"));
        GtkWidget *button = gtk_button_new_with_label(title ? title : "Window");
        GtkLabel *label = GTK_LABEL(gtk_button_get_child(GTK_BUTTON(button)));
        gtk_label_set_ellipsize(label, PANGO_ELLIPSIZE_END); gtk_label_set_max_width_chars(label, 36);
        g_object_set_data(G_OBJECT(button), "window", (gpointer)(guintptr)w);
        g_signal_connect(button, "clicked", G_CALLBACK(selected_window), d); gtk_box_append(GTK_BOX(box), button);
    }
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(scroll), 320);
    gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(scroll), TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), box);
    gtk_popover_set_child(GTK_POPOVER(d->popover), scroll);
    g_signal_connect(d->popover, "closed", G_CALLBACK(menu_closed), d); gtk_popover_popup(GTK_POPOVER(d->popover));
}
static void secondary(GtkGestureClick *gesture, int presses, double x, double y, gpointer data)
{
    (void)gesture; (void)presses; (void)x; (void)y; open_menu(data);
}
static gboolean menu_key(GtkEventControllerKey *controller, guint key, guint code, GdkModifierType state, gpointer data)
{
    (void)controller; (void)code;
    if (key == GDK_KEY_Menu || (key == GDK_KEY_F10 && (state & GDK_SHIFT_MASK))) { open_menu(data); return TRUE; }
    return FALSE;
}
static GdkContentProvider *drag_prepare(GtkDragSource *source, double x, double y, gpointer data)
{
    (void)source; (void)x; (void)y; Group *g = data;
    return g->app ? gdk_content_provider_new_typed(G_TYPE_STRING, g_app_info_get_id(G_APP_INFO(g->app))) : NULL;
}
static void drag_begin(GtkDragSource *source, GdkDrag *drag, gpointer data)
{
    (void)source; (void)drag; Group *g = data; g->dock->dragging = TRUE;
}
static void drag_end(GtkDragSource *source, GdkDrag *drag, gboolean remove, gpointer data)
{
    (void)source; (void)drag; (void)remove; Group *g = data; Dock *d = g->dock;
    d->dragging = FALSE; if (d->dirty) queue_refresh(d); schedule_hide(d);
}
/* Only installed desktop entries are accepted, never arbitrary dropped commands. */
static char *drop_id(Dock *d, const GValue *value)
{
    if (G_VALUE_HOLDS_STRING(value)) {
        const char *id = g_value_get_string(value); return slab_id_valid(id) ? g_strdup(id) : NULL;
    }
    if (!G_VALUE_HOLDS(value, GDK_TYPE_FILE_LIST)) return NULL;
    GSList *files = gdk_file_list_get_files(g_value_get_boxed(value));
    if (!files || files->next) return NULL;
    for (GList *l = d->apps; l; l = l->next) {
        if (!G_IS_DESKTOP_APP_INFO(l->data)) continue;
        const char *path = g_desktop_app_info_get_filename(l->data); if (!path) continue;
        g_autoptr(GFile) installed = g_file_new_for_path(path);
        if (g_file_equal(installed, files->data)) return g_strdup(g_app_info_get_id(l->data));
    }
    return NULL;
}
static gboolean drop(GtkDropTarget *target, const GValue *value, double x, double y, gpointer data)
{
    Dock *d = data;
    if (target) {
        graphene_point_t point = GRAPHENE_POINT_INIT(x, y), local;
        if (!gtk_widget_compute_point(gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(target)),
            GTK_WIDGET(d->tasks), &point, &local)) return FALSE;
        x = local.x;
    }
    g_autofree char *id = drop_id(d, value); if (!id) return FALSE;
    guint index = d->pins->len;
    for (guint i = 0; i < d->groups->len; ++i) {
        Group *g = g_ptr_array_index(d->groups, i); graphene_rect_t bounds;
        if (g->pinned && gtk_widget_compute_bounds(g->button, GTK_WIDGET(d->tasks), &bounds) &&
            x < bounds.origin.x + bounds.size.width / 2) {
            index = slab_pin_index(d->pins, g_app_info_get_id(G_APP_INFO(g->app))); break;
        }
    }
    return pin_at(d, id, index);
}
static void draw_indicators(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer data)
{
    Group *g = data; guint count = g->windows->len;
    if (!count) return;
    GdkRGBA color;
    GtkStyleContext *context = gtk_widget_get_style_context(GTK_WIDGET(area));
    gboolean active = gtk_widget_has_css_class(g->button, "suggested-action");
    gtk_style_context_get_color(context, &color);
    if (active) gtk_style_context_lookup_color(context, "theme_selected_fg_color", &color);
    double radius = MAX(1.0, MIN(width, height) / 2.0 - 3.0);
    double dot = MIN(active ? 2.5 : 2.0, G_PI * radius / MAX(1u, count) * 0.75);
    for (guint i = 0; i < count; ++i) {
        double angle = -G_PI / 2 + 2 * G_PI * i / count;
        double x = width / 2.0 + radius * cos(angle), y = height / 2.0 + radius * sin(angle);
        cairo_arc(cr, x, y, dot, 0, 2 * G_PI);
        gdk_cairo_set_source_rgba(cr, &color); cairo_fill(cr);
    }
}
static void update_group_icon(Group *g)
{
    GIcon *icon = g->app ? g_app_info_get_icon(G_APP_INFO(g->app)) : NULL;
    if (icon) gtk_image_set_from_gicon(GTK_IMAGE(g->image), icon);
    else gtk_image_set_from_icon_name(GTK_IMAGE(g->image), "application-x-executable");
}
static void make_button(Group *g)
{
    Dock *d = g->dock;
    g->button = gtk_button_new(); gtk_widget_add_css_class(g->button, "flat");
    gtk_widget_add_css_class(g->button, "slab-item");
    GtkWidget *column = gtk_overlay_new();
    GtkWidget *image = gtk_image_new();
    g->image = image;
    update_group_icon(g);
    gtk_image_set_pixel_size(GTK_IMAGE(image), d->preferences ? g_settings_get_int(d->preferences, "icon-size") : 20);
    gtk_widget_set_margin_top(image, 8); gtk_widget_set_margin_bottom(image, 8);
    gtk_widget_set_margin_start(image, 8); gtk_widget_set_margin_end(image, 8);
    gtk_overlay_set_child(GTK_OVERLAY(column), image);
    g->count = gtk_drawing_area_new();
    gtk_widget_set_can_target(g->count, FALSE);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(g->count), draw_indicators, g, NULL);
    gtk_overlay_add_overlay(GTK_OVERLAY(column), g->count);
    gtk_button_set_child(GTK_BUTTON(g->button), column);
    g_signal_connect(g->button, "clicked", G_CALLBACK(clicked), g);
    GtkGesture *secondary_click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(secondary_click), GDK_BUTTON_SECONDARY);
    g_signal_connect(secondary_click, "pressed", G_CALLBACK(secondary), g);
    gtk_widget_add_controller(g->button, GTK_EVENT_CONTROLLER(secondary_click));
    GtkEventController *keys = gtk_event_controller_key_new();
    g_signal_connect(keys, "key-pressed", G_CALLBACK(menu_key), g); gtk_widget_add_controller(g->button, keys);
    if (g->app) {
        GtkDragSource *source = gtk_drag_source_new(); gtk_drag_source_set_actions(source, GDK_ACTION_COPY | GDK_ACTION_MOVE);
        g_signal_connect(source, "prepare", G_CALLBACK(drag_prepare), g);
        g_signal_connect(source, "drag-begin", G_CALLBACK(drag_begin), g);
        g_signal_connect(source, "drag-end", G_CALLBACK(drag_end), g);
        gtk_widget_add_controller(g->button, GTK_EVENT_CONTROLLER(source));
    }
    gtk_box_append(g->dock->tasks, g->button);
}
static gboolean refresh(gpointer data)
{
    Dock *d = data; d->refresh = 0;
#ifdef SLAB_TESTING
    d->inventory_count++;
#endif
    if (d->dragging || d->popover) { d->dirty = TRUE; return G_SOURCE_REMOVE; }
    d->dirty = FALSE;
    unsigned long n, active_n;
    Window *windows = (Window *)property(d, d->root, "_NET_CLIENT_LIST", XA_WINDOW, 32, &n);
    Window *active = (Window *)property(d, d->root, "_NET_ACTIVE_WINDOW", XA_WINDOW, 32, &active_n);
    /* Keep the last client when the user focuses the dock to cycle its group. */
    if (active_n && active[0] != d->xid) d->active = active[0];
    if (active) XFree(active);
    GPtrArray *next = g_ptr_array_new_with_free_func(group_free);
    GHashTable *by_key = g_hash_table_new(g_str_hash, g_str_equal);
    for (guint i = 0; i < d->pins->len; ++i) {
        const char *id = g_ptr_array_index(d->pins, i); GDesktopAppInfo *app = lookup(d, id);
        if (!app) continue;
        Group *g = group_new(d, id, g_app_info_get_display_name(G_APP_INFO(app)), app);
        g->pinned = TRUE; g_ptr_array_add(next, g); g_hash_table_insert(by_key, g->key, g);
    }
    for (unsigned long i = 0; i < MIN(n, 512); ++i) {
        Window w = windows[i]; if (w == d->xid || w == d->edge) continue;
        XSelectInput(d->display, w, PropertyChangeMask);
        if (contains(d, w, "_NET_WM_STATE", "_NET_WM_STATE_SKIP_TASKBAR") ||
            contains(d, w, "_NET_WM_WINDOW_TYPE", "_NET_WM_WINDOW_TYPE_DOCK") ||
            contains(d, w, "_NET_WM_WINDOW_TYPE", "_NET_WM_WINDOW_TYPE_DESKTOP")) continue;
        g_autofree char *fallback = NULL; GDesktopAppInfo *app = window_app(d, w, &fallback);
        g_autofree char *key = app ? g_strdup(g_app_info_get_id(G_APP_INFO(app))) : g_strconcat("class:", fallback, NULL);
        Group *g = g_hash_table_lookup(by_key, key);
        if (!g) {
            g_autofree char *title = string_property(d, w, "_NET_WM_NAME", atom(d, "UTF8_STRING"));
            g = group_new(d, key, app ? g_app_info_get_display_name(G_APP_INFO(app)) : (title ? title : fallback), app);
            g_ptr_array_add(next, g); g_hash_table_insert(by_key, g->key, g);
        }
        g_array_append_val(g->windows, w);
    }
    if (windows) XFree(windows);
    g_hash_table_unref(by_key);
    /* Preserve widgets/focus for activation and count updates. */
    gboolean same = next->len == d->groups->len;
    for (guint i = 0; same && i < next->len; ++i)
        same = g_str_equal(((Group *)g_ptr_array_index(next, i))->key, ((Group *)g_ptr_array_index(d->groups, i))->key);
    g_autofree char *focus_key = NULL;
    if (!same) {
        for (guint i = 0; i < d->groups->len; ++i) {
            Group *g = g_ptr_array_index(d->groups, i);
            if (gtk_widget_has_focus(g->button)) focus_key = g_strdup(g->key);
        }
        GtkWidget *child;
        while ((child = gtk_widget_get_first_child(GTK_WIDGET(d->tasks)))) gtk_box_remove(d->tasks, child);
        g_ptr_array_unref(d->groups); d->groups = next;
        for (guint i = 0; i < next->len; ++i) make_button(g_ptr_array_index(next, i));
    } else {
        for (guint i = 0; i < next->len; ++i) {
            Group *old = g_ptr_array_index(d->groups, i), *fresh = g_ptr_array_index(next, i);
            g_array_unref(old->windows); old->windows = g_array_ref(fresh->windows);
            g_free(old->name); old->name = g_strdup(fresh->name); old->pinned = fresh->pinned;
            if (old->app != fresh->app) {
                g_set_object(&old->app, fresh->app);
                update_group_icon(old);
            }
        }
        g_ptr_array_unref(next);
    }
    for (guint i = 0; i < d->groups->len; ++i) {
        Group *g = g_ptr_array_index(d->groups, i);
        gboolean selected = FALSE;
        for (guint j = 0; j < g->windows->len; ++j) selected |= g_array_index(g->windows, Window, j) == d->active;
        if (selected) gtk_widget_add_css_class(g->button, "suggested-action");
        else gtk_widget_remove_css_class(g->button, "suggested-action");
        gtk_widget_queue_draw(g->count);
        g_autofree char *tip = g_strdup_printf("%s — %u running window%s", g->name, g->windows->len, g->windows->len == 1 ? "" : "s");
        gtk_widget_set_tooltip_text(g->button, tip);
        gtk_accessible_update_property(GTK_ACCESSIBLE(g->button), GTK_ACCESSIBLE_PROPERTY_LABEL, tip, -1);
        if (focus_key && g_str_equal(focus_key, g->key)) gtk_widget_grab_focus(g->button);
    }
    int natural;
    gtk_widget_measure(GTK_WIDGET(d->tasks), GTK_ORIENTATION_HORIZONTAL, -1, NULL, &natural, NULL, NULL);
    gtk_scrolled_window_set_min_content_width(d->scroll,
        MAX(48, MIN(natural, gtk_scrolled_window_get_max_content_width(d->scroll))));
    gtk_window_set_default_size(d->window,
        MIN(natural, gtk_scrolled_window_get_max_content_width(d->scroll)) + 10, -1);
    position(d); return G_SOURCE_REMOVE;
}
static void position(Dock *d)
{
    unsigned long n, desk_n;
    unsigned long *desktops = (unsigned long *)property(d, d->root, "_NET_CURRENT_DESKTOP", XA_CARDINAL, 32, &desk_n);
    unsigned long desk = desk_n ? desktops[0] : 0; if (desktops) XFree(desktops);
    unsigned long *area = (unsigned long *)property(d, d->root, "_NET_WORKAREA", XA_CARDINAL, 32, &n);
    int x = 0, y = 0, width = DisplayWidth(d->display, DefaultScreen(d->display));
    int height = DisplayHeight(d->display, DefaultScreen(d->display));
    if (area && desk < n / 4) {
        gint64 area_x = (gint32)(guint32)area[desk * 4];
        gint64 area_y = (gint32)(guint32)area[desk * 4 + 1];
        gint64 area_width = (gint64)(guint32)area[desk * 4 + 2];
        gint64 area_height = (gint64)(guint32)area[desk * 4 + 3];
        gint64 root_width = width, root_height = height;
        if (area_x >= 0 && area_y >= 0 && area_width > 0 && area_height > 0 &&
            area_width <= 32768 && area_height <= 32768 &&
            area_x + area_width <= root_width && area_y + area_height <= root_height) {
            x = (int)area_x; y = (int)area_y;
            width = (int)area_width; height = (int)area_height;
        }
    }
    if (area) XFree(area);
    int scale = gtk_widget_get_scale_factor(GTK_WIDGET(d->window));
    int limit = MAX(48, MIN(560, width / scale - 110));
    if (gtk_scrolled_window_get_max_content_width(d->scroll) != limit) {
        if (gtk_scrolled_window_get_min_content_width(d->scroll) > limit)
            gtk_scrolled_window_set_min_content_width(d->scroll, limit);
        gtk_scrolled_window_set_max_content_width(d->scroll, limit);
    }
    /* GTK reports zero allocation after hiding. The native window retains its
     * dimensions: use them so background refreshes cannot collapse the edge. */
    XWindowAttributes allocation;
    if (!XGetWindowAttributes(d->display, d->xid, &allocation)) return;
    int w = MAX(1, allocation.width);
    int h = MAX(1, allocation.height);
    x += MAX(0, (width - w) / 2); int dock_y = y + MAX(0, height - h - 6 * scale);
    if (d->edge) {
        int trigger_width = MIN(width, MAX(w, 96 * scale));
        int trigger_height = MIN(height, 12 * scale);
        XMoveResizeWindow(d->display, d->edge, x + (w - trigger_width) / 2,
            y + height - trigger_height, trigger_width, trigger_height);
        if (d->hidden) XRaiseWindow(d->display, d->edge);
    }
    int actual_x = 0, actual_y = 0; Window child;
    XTranslateCoordinates(d->display, d->xid, d->root, 0, 0, &actual_x, &actual_y, &child);
    if (!d->positioned || x != actual_x || dock_y != actual_y) {
        d->positioned = TRUE; d->last_x = x; d->last_y = dock_y;
        unsigned long wm_n;
        unsigned char *wm = property(d, d->root, "_NET_SUPPORTING_WM_CHECK", XA_WINDOW, 32, &wm_n);
        if (wm_n) {
            /* Move the managed frame in root coordinates, not its reparented child. */
            XEvent event = {0}; event.xclient.type = ClientMessage; event.xclient.window = d->xid;
            event.xclient.message_type = atom(d, "_NET_MOVERESIZE_WINDOW"); event.xclient.format = 32;
            event.xclient.data.l[0] = StaticGravity | (1 << 8) | (1 << 9) | (2 << 12);
            event.xclient.data.l[1] = x; event.xclient.data.l[2] = dock_y;
            XSendEvent(d->display, d->root, False, SubstructureRedirectMask | SubstructureNotifyMask, &event);
        } else XMoveWindow(d->display, d->xid, x, dock_y);
        if (wm) XFree(wm);
    }
    XFlush(d->display);
}
static void show_dock(Dock *d)
{
    if (d->hide_timer) { g_source_remove(d->hide_timer); d->hide_timer = 0; }
    d->hidden = FALSE; XUnmapWindow(d->display, d->edge);
    gdk_x11_surface_set_user_time(gtk_native_get_surface(GTK_NATIVE(d->window)), 0);
    gtk_widget_set_visible(GTK_WIDGET(d->window), TRUE); position(d); schedule_hide(d);
}
static gboolean hide_dock(gpointer data)
{
    Dock *d = data; d->hide_timer = 0;
    if (!d->autohide || d->dragging || d->popover ||
        gtk_window_is_active(d->window)) return G_SOURCE_REMOVE;
    /* GTK leave can precede arrival at the reveal edge. Keep the short bridge
     * below the dock usable, without a permanent pointer-polling timer. */
    Window root, child; int rx, ry, wx, wy; unsigned int mask;
    if (XQueryPointer(d->display, d->root, &root, &child, &rx, &ry, &wx, &wy, &mask)) {
        int scale = gtk_widget_get_scale_factor(GTK_WIDGET(d->window));
        int width = gtk_widget_get_width(GTK_WIDGET(d->window)) * scale;
        int height = gtk_widget_get_height(GTK_WIDGET(d->window)) * scale;
        if (rx >= d->last_x && rx < d->last_x + width &&
            ry >= d->last_y && ry < d->last_y + height + 6 * scale) {
            schedule_hide(d); return G_SOURCE_REMOVE;
        }
    }
    position(d); d->hidden = TRUE; gtk_widget_set_visible(GTK_WIDGET(d->window), FALSE);
    XMapRaised(d->display, d->edge); XFlush(d->display); return G_SOURCE_REMOVE;
}
static void schedule_hide(Dock *d)
{
    if (!d->closing && !d->hidden && d->autohide && !d->hide_timer && !d->dragging && !d->popover)
        d->hide_timer = g_timeout_add(700, hide_dock, d);
}
static void pointer_enter(GtkEventControllerMotion *controller, double x, double y, gpointer data)
{
    (void)controller; (void)x; (void)y; Dock *d = data; d->hovered = TRUE;
    if (d->hide_timer) { g_source_remove(d->hide_timer); d->hide_timer = 0; }
    schedule_hide(d); /* Validate actual pointer position after remap/move. */
}
static void pointer_leave(GtkEventControllerMotion *controller, gpointer data)
{
    (void)controller; Dock *d = data; d->hovered = FALSE; schedule_hide(d);
}
static void active_changed(GObject *object, GParamSpec *pspec, gpointer data)
{
    (void)object; (void)pspec; schedule_hide(data);
}
static GdkDragAction drop_enter(GtkDropTarget *target, double x, double y, gpointer data)
{
    (void)target; (void)x; (void)y; Dock *d = data; d->hovered = TRUE; return GDK_ACTION_COPY;
}
static void drop_leave(GtkDropTarget *target, gpointer data)
{
    (void)target; Dock *d = data; d->hovered = FALSE; schedule_hide(d);
}
static gboolean events(gint fd, GIOCondition condition, gpointer data)
{
    Dock *d = data; (void)fd;
    if (condition & (G_IO_HUP | G_IO_ERR)) { d->source = 0; g_application_quit(G_APPLICATION(d->app)); return G_SOURCE_REMOVE; }
    guint handled = 0;
    while (handled < 128 && XPending(d->display)) {
        XEvent event; XNextEvent(d->display, &event);
        handled++;
        if (event.type == PropertyNotify) {
            Atom a = event.xproperty.atom;
            if (event.xproperty.window == d->root && a == d->a_client_list) queue_refresh(d);
            else if (event.xproperty.window == d->root && a == d->a_active_window) {
                unsigned long count = 0;
                Window *active = (Window *)property(d, d->root, "_NET_ACTIVE_WINDOW", XA_WINDOW, 32, &count);
                if (count && active[0] != d->xid) d->active = active[0];
                if (active) XFree(active);
                for (guint i = 0; i < d->groups->len; ++i) {
                    Group *g = g_ptr_array_index(d->groups, i); gboolean selected = FALSE;
                    for (guint j = 0; j < g->windows->len; ++j)
                        selected |= g_array_index(g->windows, Window, j) == d->active;
                    if (selected) gtk_widget_add_css_class(g->button, "suggested-action");
                    else gtk_widget_remove_css_class(g->button, "suggested-action");
                }
            } else if (a == d->a_wm_name) {
                for (guint i = 0; i < d->groups->len; ++i) {
                    Group *g = g_ptr_array_index(d->groups, i);
                    if (g->app || g->windows->len != 1 ||
                        g_array_index(g->windows, Window, 0) != event.xproperty.window) continue;
                    unsigned long title_n = 0;
                    unsigned char *title_bytes = property(d, event.xproperty.window, "_NET_WM_NAME", d->a_utf8_string, 8, &title_n);
                    g_autofree char *title = title_bytes ? g_utf8_make_valid((char *)title_bytes, title_n) : NULL;
                    if (title_bytes) XFree(title_bytes);
                    if (title && *title) {
                        g_free(g->name); g->name = g_strdup(title);
                        g_autofree char *tip = g_strdup_printf("%s — 1 running window", g->name);
                        gtk_widget_set_tooltip_text(g->button, tip);
                        gtk_accessible_update_property(GTK_ACCESSIBLE(g->button), GTK_ACCESSIBLE_PROPERTY_LABEL, tip, -1);
                    }
                    break;
                }
            } else if (a == d->a_wm_state || a == d->a_wm_class || a == d->a_gtk_application_id) queue_refresh(d);
            if (event.xproperty.window == d->root && (a == d->a_workarea || a == d->a_current_desktop)) position(d);
            if (d->hidden && event.xproperty.window == d->root && a == d->a_client_stacking) XRaiseWindow(d->display, d->edge);
        } else if (event.type == ConfigureNotify && event.xconfigure.window == d->xid) position(d);
        else if (event.type == EnterNotify && event.xcrossing.window == d->edge) show_dock(d);
    }
#ifdef SLAB_TESTING
    d->max_event_batch = MAX(d->max_event_batch, handled);
#endif
    return G_SOURCE_CONTINUE;
}
/* Xlib can buffer events while synchronous property reads drain its fd. A
 * plain fd watch would leave those events stranded until unrelated traffic. */
typedef struct { GSource source; GPollFD poll; Dock *dock; } XSource;
static gboolean x_prepare(GSource *source, gint *timeout)
{
    XSource *xs = (XSource *)source; *timeout = -1;
    return XPending(xs->dock->display) > 0;
}
static gboolean x_check(GSource *source)
{
    XSource *xs = (XSource *)source;
    return xs->poll.revents || XPending(xs->dock->display) > 0;
}
static gboolean x_dispatch(GSource *source, GSourceFunc callback, gpointer data)
{
    (void)callback; (void)data; XSource *xs = (XSource *)source;
    return events(xs->poll.fd, xs->poll.revents, xs->dock);
}
static guint watch_x(Dock *d)
{
    static GSourceFuncs funcs = {.prepare = x_prepare, .check = x_check, .dispatch = x_dispatch};
    GSource *source = g_source_new(&funcs, sizeof(XSource)); XSource *xs = (XSource *)source;
    xs->dock = d; xs->poll.fd = ConnectionNumber(d->display); xs->poll.events = G_IO_IN | G_IO_HUP | G_IO_ERR;
    g_source_add_poll(source, &xs->poll);
    guint id = g_source_attach(source, NULL); g_source_unref(source); return id;
}
static void apps_changed(GAppInfoMonitor *monitor, gpointer data)
{
    (void)monitor; Dock *d = data; index_apps(d); queue_refresh(d);
}
static void preferences_changed(GSettings *settings, const char *key, gpointer data)
{
    Dock *d = data;
    gboolean all = key == NULL;
    if (all || g_str_equal(key, "autohide")) {
        d->autohide = g_settings_get_boolean(settings, "autohide");
        if (!d->autohide) show_dock(d); else schedule_hide(d);
    }
    if (all || g_str_equal(key, "margin")) {
        int margin = g_settings_get_int(settings, "margin"); GtkWidget *box = gtk_window_get_child(d->window);
        gtk_widget_set_margin_start(box, margin); gtk_widget_set_margin_end(box, margin);
        gtk_widget_set_margin_top(box, margin); gtk_widget_set_margin_bottom(box, margin);
    }
    if (all || g_str_equal(key, "gap"))
        gtk_box_set_spacing(d->tasks, g_settings_get_int(settings, "gap"));
    if (all || g_str_equal(key, "opacity") || g_str_equal(key, "padding") || g_str_equal(key, "animations")) {
        /* Only the background is translucent; never fade the window's children. */
        int opacity = g_settings_get_int(settings, "opacity");
        g_autofree char *css = g_strdup_printf(
            "window.slab {background-image:none; background-color:alpha(@theme_bg_color,%d.%02d); color:@theme_fg_color; border:none; box-shadow:none;}"
            "window.slab .slab-item {min-width:0; min-height:0; padding:%dpx; border-radius:3px; transition: %s;}"
            "window.slab .slab-count {font-size:8px; min-height:0; padding:0;}",
            opacity / 100, opacity % 100, g_settings_get_int(settings, "padding"),
            g_settings_get_boolean(settings, "animations") ? "background-color 100ms ease-out" : "none");
        gtk_css_provider_load_from_data(d->style, css, -1);
    }
    if (all || g_str_equal(key, "icon-size"))
        for (guint i = 0; i < d->groups->len; ++i) {
            Group *g = g_ptr_array_index(d->groups, i);
            gtk_image_set_pixel_size(GTK_IMAGE(g->image), g_settings_get_int(settings, "icon-size"));
        }
}
static void quit_action(GSimpleAction *action, GVariant *value, gpointer data)
{
    (void)action; (void)value; Dock *d = data; g_application_quit(G_APPLICATION(d->app));
}
static void reveal_action(GSimpleAction *action, GVariant *value, gpointer data)
{
    (void)action; (void)value; show_dock(data); /* Do not steal menu focus. */
}
static void reload_action(GSimpleAction *action, GVariant *value, gpointer data)
{
    (void)action; (void)value; Dock *d = data;
    GError *error = NULL; GPtrArray *pins = slab_pins_load(d->pins_path, &error);
    if (!pins) { show_error(d, error->message); g_clear_error(&error); return; }
    g_ptr_array_unref(d->pins); d->pins = pins; d->config_ok = TRUE; queue_refresh(d);
}
static void activate(GtkApplication *app, gpointer data)
{
    Dock *d = data;
    if (d->window) { show_dock(d); gtk_window_present(d->window); return; }
    if (!GDK_IS_X11_DISPLAY(gdk_display_get_default())) {
        g_printerr("slab: testing backend requires X11; Wayland is not implemented.\n");
        g_application_quit(G_APPLICATION(app)); return;
    }
    d->display = XOpenDisplay(NULL); if (!d->display) { g_application_quit(G_APPLICATION(app)); return; }
    XSetErrorHandler(xerror); d->root = DefaultRootWindow(d->display);
    d->a_client_list = atom(d, "_NET_CLIENT_LIST"); d->a_active_window = atom(d, "_NET_ACTIVE_WINDOW");
    d->a_wm_name = atom(d, "_NET_WM_NAME"); d->a_wm_state = atom(d, "_NET_WM_STATE");
    d->a_wm_class = atom(d, "WM_CLASS"); d->a_gtk_application_id = atom(d, "_GTK_APPLICATION_ID");
    d->a_workarea = atom(d, "_NET_WORKAREA"); d->a_current_desktop = atom(d, "_NET_CURRENT_DESKTOP");
    d->a_client_stacking = atom(d, "_NET_CLIENT_LIST_STACKING");
    d->a_utf8_string = atom(d, "UTF8_STRING");
    d->pins_path = g_build_filename(g_get_user_config_dir(), "slab", "pins.txt", NULL);
    GError *error = NULL; d->pins = slab_pins_load(d->pins_path, &error); d->config_ok = d->pins != NULL;
    if (!d->pins) d->pins = g_ptr_array_new_with_free_func(g_free);
    d->groups = g_ptr_array_new_with_free_func(group_free); index_apps(d);
    slab_follow_theme();
    d->preferences = slab_preferences();
    d->autohide = d->preferences ? g_settings_get_boolean(d->preferences, "autohide") : TRUE;
    if (d->preferences) g_signal_connect(d->preferences, "changed", G_CALLBACK(preferences_changed), d);
    const GActionEntry actions[] = {{.name="quit", .activate=quit_action}, {.name="reload", .activate=reload_action}, {.name="reveal", .activate=reveal_action}};
    g_action_map_add_action_entries(G_ACTION_MAP(app), actions, G_N_ELEMENTS(actions), d);
    d->monitor = g_app_info_monitor_get(); d->monitor_handler = g_signal_connect(d->monitor, "changed", G_CALLBACK(apps_changed), d);
    d->window = GTK_WINDOW(gtk_application_window_new(app));
    g_object_ref(d->window); /* Keep cleanup safe after GtkApplication shutdown. */
    gtk_window_set_title(d->window, "slab"); gtk_window_set_decorated(d->window, FALSE); gtk_window_set_resizable(d->window, FALSE);
    gtk_widget_add_css_class(GTK_WIDGET(d->window), "slab");
    GtkCssProvider *css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css,
        "window.slab {background-color:@theme_bg_color; color:@theme_fg_color; border:none; box-shadow:none;}"
        "window.slab .slab-item {min-width:0; min-height:0; padding:3px; border-radius:3px;}"
        "window.slab .slab-count {font-size:8px; min-height:0; padding:0;}", -1);
    gtk_style_context_add_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    d->style = css;
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_widget_set_margin_start(box, 4); gtk_widget_set_margin_end(box, 4);
    gtk_widget_set_margin_top(box, 2); gtk_widget_set_margin_bottom(box, 2); gtk_window_set_child(d->window, box);
    GtkWidget *scroll = gtk_scrolled_window_new();
    d->scroll = GTK_SCROLLED_WINDOW(scroll);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_NEVER);
    gtk_scrolled_window_set_propagate_natural_width(GTK_SCROLLED_WINDOW(scroll), TRUE);
    gtk_scrolled_window_set_min_content_width(GTK_SCROLLED_WINDOW(scroll), 48);
    gtk_scrolled_window_set_max_content_width(GTK_SCROLLED_WINDOW(scroll), 560);
    d->tasks = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2));
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), GTK_WIDGET(d->tasks)); gtk_box_append(GTK_BOX(box), scroll);
    GtkDropTarget *target = gtk_drop_target_new(G_TYPE_INVALID, GDK_ACTION_COPY | GDK_ACTION_MOVE);
    gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(target), GTK_PHASE_CAPTURE);
    GType types[] = {G_TYPE_STRING, GDK_TYPE_FILE_LIST}; gtk_drop_target_set_gtypes(target, types, G_N_ELEMENTS(types));
    g_signal_connect(target, "drop", G_CALLBACK(drop), d); g_signal_connect(target, "enter", G_CALLBACK(drop_enter), d);
    g_signal_connect(target, "leave", G_CALLBACK(drop_leave), d);
    gtk_widget_add_controller(GTK_WIDGET(d->window), GTK_EVENT_CONTROLLER(target));
    GtkEventController *motion = gtk_event_controller_motion_new();
    g_signal_connect(motion, "enter", G_CALLBACK(pointer_enter), d); g_signal_connect(motion, "leave", G_CALLBACK(pointer_leave), d);
    gtk_widget_add_controller(GTK_WIDGET(d->window), motion);
    g_signal_connect(d->window, "notify::is-active", G_CALLBACK(active_changed), d);
    gtk_widget_realize(GTK_WIDGET(d->window)); d->xid = gdk_x11_surface_get_xid(gtk_native_get_surface(GTK_NATIVE(d->window)));
    Atom type = atom(d, "_NET_WM_WINDOW_TYPE_DOCK");
    XChangeProperty(d->display, d->xid, atom(d, "_NET_WM_WINDOW_TYPE"), XA_ATOM, 32, PropModeReplace, (unsigned char *)&type, 1);
    unsigned long desktop = 0xffffffff;
    XChangeProperty(d->display, d->xid, atom(d, "_NET_WM_DESKTOP"), XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&desktop, 1);
    XSetWindowAttributes attrs = {0}; attrs.override_redirect = True; attrs.event_mask = EnterWindowMask;
    d->edge = XCreateWindow(d->display, d->root, 0, 0, 1, 4, 0, 0, InputOnly, CopyFromParent, CWOverrideRedirect | CWEventMask, &attrs);
    XSelectInput(d->display, d->root, PropertyChangeMask); XSelectInput(d->display, d->xid, StructureNotifyMask); XFlush(d->display);
    d->source = watch_x(d);
    if (d->preferences) preferences_changed(d->preferences, NULL, d);
    gtk_widget_set_visible(GTK_WIDGET(d->window), TRUE); refresh(d); schedule_hide(d);
    if (error) { show_error(d, error->message); g_clear_error(&error); }
}
static void cleanup(Dock *d)
{
    d->closing = TRUE;
    if (d->preferences) g_signal_handlers_disconnect_by_data(d->preferences, d);
    g_clear_object(&d->preferences);
    if (d->monitor_handler) g_signal_handler_disconnect(d->monitor, d->monitor_handler);
    g_clear_object(&d->monitor);
    if (d->hide_timer) g_source_remove(d->hide_timer);
    if (d->source) g_source_remove(d->source);
    if (d->popover) gtk_popover_popdown(GTK_POPOVER(d->popover));
    if (d->refresh) g_source_remove(d->refresh);
    if (d->window) { gtk_window_destroy(d->window); g_clear_object(&d->window); }
    if (d->style) {
        gtk_style_context_remove_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(d->style));
        g_clear_object(&d->style);
    }
    if (d->display) { XDestroyWindow(d->display, d->edge); XCloseDisplay(d->display); }
    g_clear_pointer(&d->groups, g_ptr_array_unref); g_clear_pointer(&d->pins, g_ptr_array_unref);
    g_clear_pointer(&d->app_index, g_hash_table_unref); g_list_free_full(d->apps, g_object_unref); g_free(d->pins_path);
}
int main(int argc, char **argv)
{
    if (argc == 2 && g_str_equal(argv[1], "--reveal")) {
        g_autoptr(GDBusConnection) bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, NULL);
        if (bus) {
            g_autoptr(GVariant) reply = g_dbus_connection_call_sync(bus, "org.luxe.Slab", "/org/luxe/Slab",
                "org.gtk.Actions", "Activate", g_variant_new("(sava{sv})", "reveal", NULL, NULL),
                NULL, G_DBUS_CALL_FLAGS_NO_AUTO_START, 1000, NULL, NULL);
        }
        return 0;
    }
    luxe_gtk_init(); Dock dock = {0};
    dock.app = gtk_application_new("org.luxe.Slab", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(dock.app, "activate", G_CALLBACK(activate), &dock);
    int result = g_application_run(G_APPLICATION(dock.app), argc, argv);
    cleanup(&dock); g_object_unref(dock.app); return result;
}
