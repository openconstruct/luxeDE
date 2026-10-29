/*
 * LuxeNotify notification windows
 * Copyright (C) 2026 Jerry Howell
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "luxenotify.h"

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>
#include <gdk/x11/gdkx.h>
#include <glib-unix.h>
#include <string.h>

#define WINDOW_WIDTH 380
#define WINDOW_MARGIN 12
#define WINDOW_GAP 8
#define DEFAULT_TIMEOUT 6000
#define MAX_APP_TEXT_BYTES 512
#define MAX_SUMMARY_BYTES 2048
#define MAX_BODY_BYTES 8192
#define MAX_ACTION_TEXT_BYTES 512
#define MAX_ICON_LOADS 4
#define MAX_ICON_BYTES (4U * 1024U * 1024U)

/* P1 #8a: bound buttons from untrusted Notify actions. Actions arrive as
 * flat key/label pairs, so one Notify with N strings builds N/2 buttons;
 * cap pairs at 10 (spec clients rarely use more than 3-4) and ignore extras. */
#define MAX_ACTION_PAIRS 10
/* P1 #8b: sane upper bound for EWMH desktops. Any X client may set root
 * properties, so _NET_CURRENT_DESKTOP / _NET_NUMBER_OF_DESKTOPS are
 * untrusted and the index is clamped before any multiplication. */
#define MAX_DESKTOPS 64

/* Limit before markup parsing and Pango layout. Preserve complete UTF-8
 * characters and make truncation visible without retaining the full string. */
static gchar *bounded_text(const gchar *text, gsize limit)
{
    if (!text) return g_strdup("");
    gsize length = strnlen(text, limit + 1);
    gboolean truncated = length > limit;
    if (truncated) {
        length = limit;
        while (length > 0 && (((guchar)text[length] & 0xc0) == 0x80)) length--;
    }
    gchar *valid = g_utf8_make_valid(text, length);
    if (!truncated) return valid;
    gchar *result = g_strconcat(valid, "…", NULL);
    g_free(valid);
    return result;
}

/* GTK 4.18 deprecates direct Xlib access ahead of GTK 5. Luxe currently targets
 * X11/Openbox, where EWMH hints and exact work-area placement require the XID. */
G_GNUC_BEGIN_IGNORE_DEPRECATIONS
static Display *native_x11_display(GdkDisplay *display)
{
    return gdk_x11_display_get_xdisplay(display);
}

static Window native_x11_window(GdkSurface *surface)
{
    return gdk_x11_surface_get_xid(surface);
}
G_GNUC_END_IGNORE_DEPRECATIONS

typedef struct {
    LuxeNotification *notification;
    gchar *action_key;
} ActionData;

static void request_x11_states(Display *display,
                               Window xid,
                               Atom first,
                               Atom second)
{
    XEvent event = { 0 };
    event.xclient.type = ClientMessage;
    event.xclient.window = xid;
    event.xclient.message_type = XInternAtom(display, "_NET_WM_STATE", False);
    event.xclient.format = 32;
    event.xclient.data.l[0] = 1; /* _NET_WM_STATE_ADD */
    event.xclient.data.l[1] = (long)first;
    event.xclient.data.l[2] = (long)second;
    event.xclient.data.l[3] = 1; /* normal application source */
    XSendEvent(display,
               RootWindow(display, DefaultScreen(display)),
               False,
               SubstructureRedirectMask | SubstructureNotifyMask,
               &event);
}

static const gchar notification_css[] =
    ".luxe-notification {"
    "  background-color: @window_bg_color;"
    "  color: @window_fg_color;"
    "  border: 1px solid alpha(@borders, 0.9);"
    "  border-radius: 12px;"
    "  box-shadow: 0 4px 14px alpha(black, 0.38);"
    "  padding: 10px;"
    "}"
    ".luxe-notification.critical { border-left: 4px solid @error_color; }"
    ".luxe-notification.low { opacity: 0.92; }"
    ".luxe-notification .app-name {"
    "  color: alpha(@window_fg_color, 0.66);"
    "  font-size: 0.86em;"
    "}"
    ".luxe-notification .summary { font-weight: bold; }"
    ".luxe-notification .body { color: alpha(@window_fg_color, 0.82); }"
    ".luxe-notification .close-button { padding: 3px; min-width: 24px; min-height: 24px; }"
    ".luxe-notification progressbar trough { min-height: 5px; }"
    ".luxe-notification progressbar progress { min-height: 5px; }";

static void install_css_once(void)
{
    static gsize installed = 0;
    if (g_once_init_enter(&installed)) {
        GtkCssProvider *provider = gtk_css_provider_new();
        gtk_css_provider_load_from_string(provider, notification_css);
        gtk_style_context_add_provider_for_display(gdk_display_get_default(),
            GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        g_object_unref(provider);
        g_once_init_leave(&installed, 1);
    }
}

static void action_data_free(gpointer data, GClosure *closure)
{
    (void)closure;
    ActionData *action = data;
    g_free(action->action_key);
    g_free(action);
}

static void run_action(LuxeNotification *notification, const gchar *action_key)
{
    gboolean resident = notification->resident;
    LuxeNotifyServer *server = notification->server;
    luxe_notify_server_action(server, notification, action_key);
    if (!resident)
        luxe_notify_server_close(server, notification, 2, TRUE);
}

static void action_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    ActionData *action = user_data;
    run_action(action->notification, action->action_key);
}

static void close_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    LuxeNotification *notification = user_data;
    luxe_notify_server_close(notification->server, notification, 2, TRUE);
}

static void content_released(GtkGestureClick *gesture,
                             gint n_press,
                             gdouble x,
                             gdouble y,
                             gpointer user_data)
{
    (void)n_press;
    (void)x;
    (void)y;
    LuxeNotification *notification = user_data;
    if (gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture)) == 1 &&
        notification->has_default_action)
        run_action(notification, "default");
}

static gboolean window_close_requested(GtkWindow *window, gpointer user_data)
{
    (void)window;
    LuxeNotification *notification = user_data;
    luxe_notify_server_close(notification->server, notification, 2, TRUE);
    return TRUE;
}

static gboolean notification_expired(gpointer user_data)
{
    LuxeNotification *notification = user_data;
    notification->timeout_id = 0;
    luxe_notify_server_close(notification->server, notification, 1, TRUE);
    return G_SOURCE_REMOVE;
}

static GVariant *find_image_data(GVariant *hints)
{
    static const gchar *keys[] = { "image-data", "image_data", "icon_data", NULL };
    for (guint i = 0; keys[i] != NULL; i++) {
        GVariant *value = g_variant_lookup_value(hints, keys[i],
            G_VARIANT_TYPE("(iiibiiay)"));
        if (value != NULL)
            return value;
    }
    return NULL;
}

static void free_pixbuf_pixels(guchar *pixels, gpointer user_data)
{
    (void)pixels;
    g_variant_unref(user_data);
}

static GdkPaintable *paintable_from_image_data(GVariant *hints)
{
    GVariant *image = find_image_data(hints);
    if (image == NULL)
        return NULL;

    gint width = 0;
    gint height = 0;
    gint rowstride = 0;
    gboolean alpha = FALSE;
    gint bits = 0;
    gint channels = 0;
    GVariant *bytes = NULL;
    g_variant_get(image, "(iiibii@ay)", &width, &height, &rowstride,
                  &alpha, &bits, &channels, &bytes);

    gsize length = 0;
    const guint8 *source = g_variant_get_fixed_array(bytes, &length, sizeof(guint8));
    gsize required = 0;
    GdkPaintable *paintable = NULL;
    if (width > 0 && height > 0 && width <= 2048 && height <= 2048 &&
        rowstride > 0 && bits == 8 && channels == (alpha ? 4 : 3) &&
        rowstride >= width * channels &&
        (gsize)height <= G_MAXSIZE / (gsize)rowstride) {
        required = (gsize)(height - 1) * (gsize)rowstride + (gsize)width * channels;
    }
    if (required > 0 && required <= MAX_ICON_BYTES && length >= required) {
        /* Keep the immutable variant backing alive while the temporary
         * pixbuf is scaled; avoid copying an entire large raw image first. */
        GVariant *backing = g_variant_ref(bytes);
        GdkPixbuf *pixbuf = gdk_pixbuf_new_from_data((guchar *)source,
                                                     GDK_COLORSPACE_RGB,
                                                     alpha,
                                                     bits,
                                                     width,
                                                     height,
                                                     rowstride,
                                                     free_pixbuf_pixels,
                                                     backing);
        if (pixbuf != NULL) {
            GdkPixbuf *display_pixbuf = pixbuf;
            if (width > 40 || height > 40) {
                double scale = MIN(40.0 / width, 40.0 / height);
                gint target_width = MAX(1, (gint)(width * scale + 0.5));
                gint target_height = MAX(1, (gint)(height * scale + 0.5));
                display_pixbuf = gdk_pixbuf_scale_simple(pixbuf, target_width, target_height,
                                                          GDK_INTERP_BILINEAR);
            }
            if (display_pixbuf != NULL)
                paintable = GDK_PAINTABLE(gdk_texture_new_for_pixbuf(display_pixbuf));
            if (display_pixbuf != pixbuf)
                g_clear_object(&display_pixbuf);
            g_object_unref(pixbuf);
        } else {
            g_variant_unref(backing);
        }
    }
    g_variant_unref(bytes);
    g_variant_unref(image);
    return paintable;
}

typedef struct {
    GtkPicture *picture;
    GInputStream *stream;
    GCancellable *cancel;
    GByteArray *bytes;
} IconLoad;
/* All starts and completions run on the GTK main context. */
static guint active_icon_loads;

static void icon_load_free(IconLoad *load)
{
    g_clear_object(&load->stream);
    g_clear_object(&load->picture);
    g_clear_object(&load->cancel);
    g_clear_pointer(&load->bytes, g_byte_array_unref);
    g_assert(active_icon_loads > 0);
    active_icon_loads--;
    g_free(load);
}

static void icon_pixbuf_loaded(GObject *source, GAsyncResult *result, gpointer user_data)
{
    (void)source;
    IconLoad *load = user_data;
    GError *error = NULL;
    GdkPixbuf *pixbuf = gdk_pixbuf_new_from_stream_finish(result, &error);
    if (pixbuf != NULL && !g_cancellable_is_cancelled(load->cancel)) {
        GdkTexture *texture = gdk_texture_new_for_pixbuf(pixbuf);
        gtk_picture_set_paintable(load->picture, GDK_PAINTABLE(texture));
        g_object_unref(texture);
    }
    g_clear_object(&pixbuf);
    g_clear_error(&error);
    icon_load_free(load);
}

static void icon_read_next(IconLoad *load);

static void icon_chunk_read(GObject *source, GAsyncResult *result, gpointer user_data)
{
    IconLoad *load = user_data;
    g_autoptr(GError) error = NULL;
    g_autoptr(GBytes) chunk = g_input_stream_read_bytes_finish(G_INPUT_STREAM(source), result, &error);
    if (!chunk || g_cancellable_is_cancelled(load->cancel)) {
        icon_load_free(load);
        return;
    }
    gsize size = 0;
    const guint8 *data = g_bytes_get_data(chunk, &size);
    if (size > MAX_ICON_BYTES - load->bytes->len) {
        icon_load_free(load);
        return;
    }
    if (size) {
        g_byte_array_append(load->bytes, data, size);
        icon_read_next(load);
        return;
    }
    g_autoptr(GBytes) encoded = g_byte_array_free_to_bytes(g_steal_pointer(&load->bytes));
    g_clear_object(&load->stream);
    load->stream = g_memory_input_stream_new_from_bytes(encoded);
    gdk_pixbuf_new_from_stream_at_scale_async(load->stream, 40, 40, TRUE, load->cancel,
                                               icon_pixbuf_loaded, load);
}

static void icon_read_next(IconLoad *load)
{
    g_input_stream_read_bytes_async(load->stream, 65536, G_PRIORITY_DEFAULT, load->cancel,
                                    icon_chunk_read, load);
}

static void icon_info_ready(GObject *source, GAsyncResult *result, gpointer user_data)
{
    IconLoad *load = user_data;
    g_autoptr(GError) error = NULL;
    g_autoptr(GFileInfo) info = g_file_input_stream_query_info_finish(G_FILE_INPUT_STREAM(source), result, &error);
    if (!info || g_cancellable_is_cancelled(load->cancel) ||
        g_file_info_get_file_type(info) != G_FILE_TYPE_REGULAR ||
        g_file_info_get_size(info) > MAX_ICON_BYTES) {
        icon_load_free(load);
        return;
    }
    load->bytes = g_byte_array_new();
    icon_read_next(load);
}

static void icon_stream_opened(GObject *source, GAsyncResult *result, gpointer user_data)
{
    IconLoad *load = user_data;
    g_autoptr(GError) error = NULL;
    load->stream = G_INPUT_STREAM(g_file_read_finish(G_FILE(source), result, &error));
    if (!load->stream || g_cancellable_is_cancelled(load->cancel)) {
        icon_load_free(load);
        return;
    }
    g_file_input_stream_query_info_async(G_FILE_INPUT_STREAM(load->stream),
        G_FILE_ATTRIBUTE_STANDARD_TYPE "," G_FILE_ATTRIBUTE_STANDARD_SIZE,
        G_PRIORITY_DEFAULT, load->cancel, icon_info_ready, load);
}

static gboolean start_icon_load(GtkPicture *picture, const gchar *location, GCancellable *cancel)
{
    GFile *file = NULL;
    if (active_icon_loads >= MAX_ICON_LOADS || g_cancellable_is_cancelled(cancel))
        return FALSE;
    if (location != NULL && g_str_has_prefix(location, "file://"))
        file = g_file_new_for_uri(location);
    else if (location != NULL && g_path_is_absolute(location))
        file = g_file_new_for_path(location);
    if (file == NULL)
        return FALSE;
    IconLoad *load = g_new0(IconLoad, 1);
    load->picture = g_object_ref(picture);
    load->cancel = g_object_ref(cancel);
    active_icon_loads++;
    g_file_read_async(file, G_PRIORITY_DEFAULT, cancel, icon_stream_opened, load);
    g_object_unref(file);
    return TRUE;
}

static GtkWidget *create_icon(LuxeNotification *notification, const gchar *app_icon, GVariant *hints)
{
    GdkPaintable *paintable = paintable_from_image_data(hints);
    const gchar *image_path = NULL;
    if (paintable == NULL) {
        if (!g_variant_lookup(hints, "image-path", "&s", &image_path))
            g_variant_lookup(hints, "image_path", "&s", &image_path);
    }
    if (paintable == NULL) {
        GtkWidget *icon = gtk_picture_new();
        gtk_picture_set_can_shrink(GTK_PICTURE(icon), TRUE);
        gtk_picture_set_content_fit(GTK_PICTURE(icon), GTK_CONTENT_FIT_CONTAIN);
        if (start_icon_load(GTK_PICTURE(icon), image_path, notification->icon_cancel) ||
            start_icon_load(GTK_PICTURE(icon), app_icon, notification->icon_cancel)) {
            gtk_widget_set_size_request(icon, 40, 40);
            gtk_widget_set_valign(icon, GTK_ALIGN_START);
            return icon;
        }
        g_object_ref_sink(icon);
        g_object_unref(icon);
    }

    GtkWidget *icon;
    if (paintable != NULL) {
        icon = gtk_picture_new_for_paintable(paintable);
        gtk_picture_set_can_shrink(GTK_PICTURE(icon), TRUE);
        gtk_picture_set_content_fit(GTK_PICTURE(icon), GTK_CONTENT_FIT_CONTAIN);
        g_object_unref(paintable);
    } else {
        const gchar *icon_name = app_icon != NULL && *app_icon != '\0' ?
                                 app_icon : "dialog-information-symbolic";
        icon = gtk_image_new_from_icon_name(icon_name);
        gtk_image_set_pixel_size(GTK_IMAGE(icon), 40);
    }
    gtk_widget_set_size_request(icon, 40, 40);
    gtk_widget_set_valign(icon, GTK_ALIGN_START);
    return icon;
}

/* Body markup is rendered with Pango, where <a href> can hand URLs to
 * external handlers. Only a small tag whitelist is honoured (b/i/u/s/sub/
 * sup/tt/a); for <a> only http(s)/mailto href schemes are kept. Anything
 * else is escaped so it renders as literal text instead of markup. */
static gboolean markup_tag_allowed(const gchar *name)
{
    static const gchar *allowed[] = {
        "b", "i", "u", "s", "sub", "sup", "tt", NULL
    };
    for (guint i = 0; allowed[i] != NULL; i++) {
        if (g_str_equal(name, allowed[i]))
            return TRUE;
    }
    return FALSE;
}

static gboolean markup_href_allowed(const gchar *href)
{
    return g_ascii_strncasecmp(href, "http://", 7) == 0 ||
           g_ascii_strncasecmp(href, "https://", 8) == 0 ||
           g_ascii_strncasecmp(href, "mailto:", 7) == 0;
}

static gboolean markup_entity_at(const gchar *text, gsize *length_out)
{
    static const gchar *named[] = {
        "&amp;", "&lt;", "&gt;", "&quot;", "&apos;", NULL
    };
    for (guint i = 0; named[i] != NULL; i++) {
        gsize length = strlen(named[i]);
        if (strncmp(text, named[i], length) == 0) {
            *length_out = length;
            return TRUE;
        }
    }
    if (text[0] == '&' && text[1] == '#') {
        const gchar *digits = text + 2;
        gboolean hex = FALSE;
        if (*digits == 'x' || *digits == 'X') {
            hex = TRUE;
            digits++;
        }
        const gchar *end = digits;
        while ((hex && g_ascii_isxdigit((guchar)*end)) ||
               (!hex && g_ascii_isdigit((guchar)*end)))
            end++;
        if (end > digits && *end == ';') {
            *length_out = (gsize)(end - text + 1);
            return TRUE;
        }
    }
    return FALSE;
}

static gboolean anchor_tag_valid(const gchar *inner)
{
    const gchar *tag = inner;
    gboolean closing = FALSE;
    if (*tag == '/') {
        closing = TRUE;
        tag++;
    }
    if (tag[0] != 'a' ||
        (tag[1] != '\0' && !g_ascii_isspace((guchar)tag[1])))
        return FALSE;
    tag++;
    if (closing)
        return *tag == '\0';
    while (g_ascii_isspace((guchar)*tag))
        tag++;
    if (*tag == '\0')
        return TRUE;
    if (strncmp(tag, "href", 4) != 0)
        return FALSE;
    tag += 4;
    while (g_ascii_isspace((guchar)*tag))
        tag++;
    if (*tag != '=')
        return FALSE;
    tag++;
    while (g_ascii_isspace((guchar)*tag))
        tag++;
    gchar quote = *tag;
    if (quote != '"' && quote != '\'')
        return FALSE;
    tag++;
    const gchar *value_end = strchr(tag, quote);
    if (value_end == NULL || *(value_end + 1) != '\0')
        return FALSE;
    gsize href_length = (gsize)(value_end - tag);
    if (href_length == 0 || href_length > 2048)
        return FALSE;
    gchar *href = g_strndup(tag, href_length);
    gboolean ok = markup_href_allowed(href);
    if (ok && (strchr(href, '<') != NULL || strchr(href, '>') != NULL ||
               strchr(href, '"') != NULL || strchr(href, '\'') != NULL ||
               strchr(href, ' ') != NULL || strchr(href, '\t') != NULL ||
               strchr(href, '\n') != NULL || strchr(href, '\r') != NULL))
        ok = FALSE;
    g_free(href);
    return ok;
}

static gboolean simple_tag_valid(const gchar *inner)
{
    const gchar *tag = inner;
    if (*tag == '/')
        tag++;
    gsize length = 0;
    while (tag[length] != '\0' && g_ascii_islower((guchar)tag[length]))
        length++;
    /* Whitelisted inline tags are at most 3 chars (sub/sup); anything longer
     * or carrying attributes is escaped instead of rendered. */
    if (length == 0 || length > 3 || tag[length] != '\0')
        return FALSE;
    gchar name[4];
    memcpy(name, tag, length);
    name[length] = '\0';
    return markup_tag_allowed(name);
}

static gchar *sanitize_body_markup(const gchar *body)
{
    GString *output = g_string_new(NULL);
    const gchar *p = body;
    while (*p != '\0') {
        if (*p == '&') {
            gsize length = 0;
            if (markup_entity_at(p, &length)) {
                g_string_append_len(output, p, (gssize)length);
                p += length;
            } else {
                g_string_append(output, "&amp;");
                p++;
            }
        } else if (*p == '<') {
            const gchar *end = strchr(p, '>');
            if (end == NULL) {
                g_string_append(output, "&lt;");
                p++;
            } else {
                gchar *raw = g_strndup(p, (gsize)(end - p + 1));
                gchar *inner = g_strndup(p + 1, (gsize)(end - p - 1));
                g_strstrip(inner);
                gboolean keep = FALSE;
                if (*inner != '\0') {
                    const gchar *name = inner;
                    if (*name == '/')
                        name++;
                    if (name[0] == 'a' &&
                        (name[1] == '\0' || g_ascii_isspace((guchar)name[1])))
                        keep = anchor_tag_valid(inner);
                    else
                        keep = simple_tag_valid(inner);
                }
                if (keep) {
                    g_string_append(output, raw);
                } else {
                    gchar *escaped = g_markup_escape_text(raw, -1);
                    g_string_append(output, escaped);
                    g_free(escaped);
                }
                g_free(raw);
                g_free(inner);
                p = end + 1;
            }
        } else {
            g_string_append_c(output, *p);
            p++;
        }
    }
    return g_string_free(output, FALSE);
}

static gint hint_integer(GVariant *hints, const gchar *key, gint fallback)
{
    GVariant *value = g_variant_lookup_value(hints, key, NULL);
    if (value == NULL)
        return fallback;
    gint result = fallback;
    if (g_variant_is_of_type(value, G_VARIANT_TYPE_INT32))
        result = g_variant_get_int32(value);
    else if (g_variant_is_of_type(value, G_VARIANT_TYPE_UINT32))
        result = (gint)g_variant_get_uint32(value);
    else if (g_variant_is_of_type(value, G_VARIANT_TYPE_BYTE))
        result = g_variant_get_byte(value);
    g_variant_unref(value);
    return result;
}

static gboolean hint_boolean(GVariant *hints, const gchar *key)
{
    gboolean value = FALSE;
    g_variant_lookup(hints, key, "b", &value);
    return value;
}

static gboolean body_markup_valid(const gchar *markup)
{
    /* GtkLabel supports anchors in addition to Pango's inline tags. The
     * sanitizer already restricts tags/attributes; validate their nesting
     * and entities without rejecting GTK's <a href> extension. */
    const GMarkupParser parser = { 0 };
    GMarkupParseContext *context = g_markup_parse_context_new(&parser, 0, NULL, NULL);
    gchar *wrapped = g_strconcat("<markup>", markup, "</markup>", NULL);
    gboolean valid = g_markup_parse_context_parse(context, wrapped, -1, NULL) &&
                     g_markup_parse_context_end_parse(context, NULL);
    g_markup_parse_context_free(context);
    g_free(wrapped);
    return valid;
}

static GtkWidget *create_content(LuxeNotification *notification,
                                 const gchar *app_icon,
                                 const gchar *summary,
                                 const gchar *body,
                                 GVariant *actions,
                                 GVariant *hints)
{
    GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_add_css_class(card, "luxe-notification");
    gint urgency = hint_integer(hints, "urgency", 1);
    if (urgency >= 2)
        gtk_widget_add_css_class(card, "critical");
    else if (urgency == 0)
        gtk_widget_add_css_class(card, "low");

    GtkWidget *top = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_append(GTK_BOX(card), top);
    gtk_box_append(GTK_BOX(top), create_icon(notification, app_icon, hints));

    GtkWidget *text = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_hexpand(text, TRUE);
    gtk_widget_set_valign(text, GTK_ALIGN_CENTER);
    GtkEventController *click = GTK_EVENT_CONTROLLER(gtk_gesture_click_new());
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), 1);
    g_signal_connect(click, "released", G_CALLBACK(content_released), notification);
    gtk_widget_add_controller(text, click);
    gtk_box_append(GTK_BOX(top), text);

    if (notification->app_name != NULL && *notification->app_name != '\0') {
        GtkWidget *app = gtk_label_new(notification->app_name);
        gtk_label_set_xalign(GTK_LABEL(app), 0.0f);
        gtk_label_set_ellipsize(GTK_LABEL(app), PANGO_ELLIPSIZE_END);
        gtk_widget_add_css_class(app, "app-name");
        gtk_box_append(GTK_BOX(text), app);
    }

    GtkWidget *summary_label = gtk_label_new(summary);
    gtk_label_set_xalign(GTK_LABEL(summary_label), 0.0f);
    gtk_label_set_wrap(GTK_LABEL(summary_label), TRUE);
    gtk_label_set_wrap_mode(GTK_LABEL(summary_label), PANGO_WRAP_WORD_CHAR);
    gtk_label_set_lines(GTK_LABEL(summary_label), 2);
    gtk_label_set_ellipsize(GTK_LABEL(summary_label), PANGO_ELLIPSIZE_END);
    gtk_widget_add_css_class(summary_label, "summary");
    gtk_box_append(GTK_BOX(text), summary_label);

    if (body != NULL && *body != '\0') {
        GtkWidget *body_label = gtk_label_new(NULL);
        gchar *sanitized = sanitize_body_markup(body);
        if (body_markup_valid(sanitized))
            gtk_label_set_markup(GTK_LABEL(body_label), sanitized);
        else
            gtk_label_set_text(GTK_LABEL(body_label), body);
        g_free(sanitized);
        gtk_label_set_xalign(GTK_LABEL(body_label), 0.0f);
        gtk_label_set_wrap(GTK_LABEL(body_label), TRUE);
        gtk_label_set_wrap_mode(GTK_LABEL(body_label), PANGO_WRAP_WORD_CHAR);
        gtk_label_set_lines(GTK_LABEL(body_label), 4);
        gtk_label_set_ellipsize(GTK_LABEL(body_label), PANGO_ELLIPSIZE_END);
        gtk_widget_add_css_class(body_label, "body");
        gtk_box_append(GTK_BOX(text), body_label);
    }

    GtkWidget *close = gtk_button_new_from_icon_name("window-close-symbolic");
    gtk_widget_add_css_class(close, "flat");
    gtk_widget_add_css_class(close, "circular");
    gtk_widget_add_css_class(close, "close-button");
    gtk_widget_set_valign(close, GTK_ALIGN_START);
    gtk_widget_set_tooltip_text(close, "Dismiss notification");
    g_signal_connect(close, "clicked", G_CALLBACK(close_clicked), notification);
    gtk_box_append(GTK_BOX(top), close);

    gint progress = hint_integer(hints, "value", -1);
    if (progress >= 0) {
        GtkWidget *bar = gtk_progress_bar_new();
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(bar),
                                      CLAMP(progress, 0, 100) / 100.0);
        gtk_widget_set_tooltip_text(bar, "Progress");
        gtk_box_append(GTK_BOX(card), bar);
    }

    gsize action_count = g_variant_n_children(actions);
    if (action_count >= 2) {
        /* Cap pairs before iterating so a hostile 100k-string array only
         * builds MAX_ACTION_PAIRS buttons. */
        gsize pairs = action_count / 2;
        if (pairs > MAX_ACTION_PAIRS)
            pairs = MAX_ACTION_PAIRS;
        gsize capped = pairs * 2;
        GtkWidget *buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        gtk_widget_set_halign(buttons, GTK_ALIGN_END);
        gboolean have_visible_action = FALSE;
        for (gsize i = 0; i + 1 < capped; i += 2) {
            const gchar *key = NULL;
            const gchar *label = NULL;
            g_variant_get_child(actions, i, "&s", &key);
            g_variant_get_child(actions, i + 1, "&s", &label);
            if (strnlen(key, MAX_ACTION_TEXT_BYTES + 1) > MAX_ACTION_TEXT_BYTES)
                continue;
            if (g_str_equal(key, "default")) {
                notification->has_default_action = TRUE;
                continue;
            }
            g_autofree gchar *safe_label = bounded_text(label, MAX_ACTION_TEXT_BYTES);
            GtkWidget *button = gtk_button_new_with_label(safe_label);
            ActionData *data = g_new0(ActionData, 1);
            data->notification = notification;
            data->action_key = g_strdup(key);
            g_signal_connect_data(button, "clicked", G_CALLBACK(action_clicked), data,
                                  action_data_free, 0);
            gtk_box_append(GTK_BOX(buttons), button);
            have_visible_action = TRUE;
        }
        if (have_visible_action)
            gtk_box_append(GTK_BOX(card), buttons);
        else {
            g_object_ref_sink(buttons);
            g_object_unref(buttons);
        }
    }

    return card;
}

static void set_x11_notification_hints(GtkWindow *window)
{
    GdkSurface *surface = gtk_native_get_surface(GTK_NATIVE(window));
    if (surface == NULL || !GDK_IS_X11_SURFACE(surface))
        return;

    Display *display = native_x11_display(gdk_surface_get_display(surface));
    Window xid = native_x11_window(surface);
    Atom state = XInternAtom(display, "_NET_WM_STATE", False);
    Atom states[] = {
        XInternAtom(display, "_NET_WM_STATE_ABOVE", False),
        XInternAtom(display, "_NET_WM_STATE_SKIP_TASKBAR", False),
        XInternAtom(display, "_NET_WM_STATE_SKIP_PAGER", False),
        XInternAtom(display, "_NET_WM_STATE_STICKY", False)
    };
    XChangeProperty(display, xid, state, XA_ATOM, 32, PropModeReplace,
                    (unsigned char *)states, G_N_ELEMENTS(states));

    Atom window_type = XInternAtom(display, "_NET_WM_WINDOW_TYPE", False);
    Atom notification_type = XInternAtom(display,
                                         "_NET_WM_WINDOW_TYPE_NOTIFICATION",
                                         False);
    XChangeProperty(display, xid, window_type, XA_ATOM, 32, PropModeReplace,
                    (unsigned char *)&notification_type, 1);

    Atom desktop = XInternAtom(display, "_NET_WM_DESKTOP", False);
    unsigned long all_desktops = 0xffffffffUL;
    XChangeProperty(display, xid, desktop, XA_CARDINAL, 32, PropModeReplace,
                    (unsigned char *)&all_desktops, 1);

    XWMHints wm_hints = { 0 };
    wm_hints.flags = InputHint;
    wm_hints.input = False;
    XSetWMHints(display, xid, &wm_hints);

    if (gtk_widget_get_mapped(GTK_WIDGET(window))) {
        request_x11_states(display, xid, states[1], states[2]);
        request_x11_states(display, xid, states[0], states[3]);
        XRaiseWindow(display, xid);
    }
    XFlush(display);
}

static void primary_monitor_geometry(Display *display,
                                     gint *x,
                                     gint *y,
                                     gint *width,
                                     gint *height)
{
    int screen = DefaultScreen(display);
    Window root = RootWindow(display, screen);
    *x = 0;
    *y = 0;
    *width = DisplayWidth(display, screen);
    *height = DisplayHeight(display, screen);

    RROutput primary = XRRGetOutputPrimary(display, root);
    if (primary == None)
        return;
    XRRScreenResources *resources = XRRGetScreenResourcesCurrent(display, root);
    if (resources == NULL)
        return;
    XRROutputInfo *output = XRRGetOutputInfo(display, resources, primary);
    if (output != NULL && output->connection == RR_Connected && output->crtc != None) {
        XRRCrtcInfo *crtc = XRRGetCrtcInfo(display, resources, output->crtc);
        if (crtc != NULL) {
            *x = crtc->x;
            *y = crtc->y;
            *width = (gint)crtc->width;
            *height = (gint)crtc->height;
            XRRFreeCrtcInfo(crtc);
        }
    }
    if (output != NULL)
        XRRFreeOutputInfo(output);
    XRRFreeScreenResources(resources);
}

static void apply_desktop_workarea(Display *display,
                                   gint *x,
                                   gint *y,
                                   gint *width,
                                   gint *height)
{
    Window root = RootWindow(display, DefaultScreen(display));
    Atom cardinal = XA_CARDINAL;
    Atom actual_type = None;
    int actual_format = 0;
    unsigned long count = 0;
    unsigned long remaining = 0;
    unsigned char *desktop_data = NULL;
    unsigned long desktop = 0;

    Atom current_desktop = XInternAtom(display, "_NET_CURRENT_DESKTOP", False);
    if (XGetWindowProperty(display, root, current_desktop, 0, 1, False,
                           cardinal, &actual_type, &actual_format, &count,
                           &remaining, &desktop_data) == Success &&
        actual_type == cardinal && actual_format == 32 && count == 1) {
        desktop = *(unsigned long *)desktop_data;
    }
    if (desktop_data != NULL)
        XFree(desktop_data);

    /* Any X client may set root properties: treat both values as untrusted.
     * Clamp the desktop index to the known desktop count (defaulting to 1)
     * BEFORE any multiplication, and bound the count itself, so a hostile
     * _NET_CURRENT_DESKTOP cannot wrap (desktop + 1) * 4 in unsigned long
     * arithmetic or drive an out-of-bounds values + desktop * 4 read. */
    unsigned long n_desktops = 1;
    unsigned char *n_desktops_data = NULL;
    Atom number_of_desktops = XInternAtom(display, "_NET_NUMBER_OF_DESKTOPS", False);
    if (XGetWindowProperty(display, root, number_of_desktops, 0, 1, False,
                           cardinal, &actual_type, &actual_format, &count,
                           &remaining, &n_desktops_data) == Success &&
        actual_type == cardinal && actual_format == 32 && count == 1) {
        n_desktops = *(unsigned long *)n_desktops_data;
    }
    if (n_desktops_data != NULL)
        XFree(n_desktops_data);
    if (n_desktops < 1)
        n_desktops = 1;
    if (n_desktops > MAX_DESKTOPS)
        n_desktops = MAX_DESKTOPS;
    if (desktop >= n_desktops)
        desktop = 0;

    unsigned char *workarea_data = NULL;
    Atom workarea = XInternAtom(display, "_NET_WORKAREA", False);
    if (XGetWindowProperty(display, root, workarea, 0, 4096, False,
                           cardinal, &actual_type, &actual_format, &count,
                           &remaining, &workarea_data) != Success ||
        actual_type != cardinal || actual_format != 32 ||
        desktop >= count / 4) {
        if (workarea_data != NULL)
            XFree(workarea_data);
        return;
    }

    unsigned long *values = (unsigned long *)workarea_data + desktop * 4;
    gint64 work_x = (gint32)values[0];
    gint64 work_y = (gint32)values[1];
    gint64 work_right = work_x + (guint32)values[2];
    gint64 work_bottom = work_y + (guint32)values[3];
    gint64 monitor_right = (gint64)*x + *width;
    gint64 monitor_bottom = (gint64)*y + *height;
    gint64 clipped_x = MAX(*x, work_x);
    gint64 clipped_y = MAX(*y, work_y);
    gint64 clipped_right = MIN(monitor_right, work_right);
    gint64 clipped_bottom = MIN(monitor_bottom, work_bottom);
    if (clipped_right > clipped_x && clipped_bottom > clipped_y) {
        *x = clipped_x;
        *y = clipped_y;
        *width = clipped_right - clipped_x;
        *height = clipped_bottom - clipped_y;
    }
    XFree(workarea_data);
}

/* Cached primary-monitor workarea.
 *
 * RandR lookups (XRRGetScreenResourcesCurrent and friends) plus the
 * _NET_WORKAREA property reads used to run on every reposition. The result
 * is now cached and only recomputed after monitor/layout changes, observed
 * through the GTK4 monitors list model (GListModel::items-changed plus
 * per-monitor notify) and, where the running backend still provides them,
 * the legacy "monitors-changed"/"layout-changed" display signals. */
static void workarea_invalidate(LuxeNotifyServer *server)
{
    server->workarea_valid = FALSE;
    if (server->notifications != NULL)
        luxe_notification_reposition_all(server);
}

static void workarea_display_changed(GdkDisplay *display, gpointer user_data)
{
    (void)display;
    workarea_invalidate(user_data);
}

static void workarea_monitor_notified(GObject *monitor,
                                      GParamSpec *pspec,
                                      gpointer user_data)
{
    (void)monitor;
    (void)pspec;
    workarea_invalidate(user_data);
}

static void workarea_watch_monitors(LuxeNotifyServer *server)
{
    GHashTableIter iter;
    gpointer monitor;
    g_hash_table_iter_init(&iter, server->watched_monitors);
    while (g_hash_table_iter_next(&iter, &monitor, NULL))
        g_signal_handlers_disconnect_by_data(monitor, server);
    g_hash_table_remove_all(server->watched_monitors);
    for (guint i = 0; i < g_list_model_get_n_items(server->workarea_monitors); i++) {
        monitor = g_list_model_get_item(server->workarea_monitors, i);
        if (!monitor) continue;
        g_signal_connect(monitor, "notify",
                         G_CALLBACK(workarea_monitor_notified), server);
        g_hash_table_add(server->watched_monitors, monitor);
    }
}

static void workarea_monitors_changed(GListModel *model,
                                      guint position,
                                      guint removed,
                                      guint added,
                                      gpointer user_data)
{
    (void)model; (void)position; (void)removed; (void)added;
    LuxeNotifyServer *server = user_data;
    workarea_invalidate(server);
    /* New monitors need geometry watches so later moves without further
     * add/remove events still invalidate the cache. */
    workarea_watch_monitors(server);
}

static gboolean workarea_x11_events(gint fd, GIOCondition condition, gpointer data)
{
    (void)fd;
    LuxeNotifyServer *server = data;
    Display *display = server->workarea_x11;
    if (condition & (G_IO_ERR | G_IO_HUP | G_IO_NVAL)) {
        server->workarea_x11_source = 0;
        return G_SOURCE_REMOVE;
    }
    while (XPending(display) > 0) {
        XEvent event;
        XNextEvent(display, &event);
        if (event.type == PropertyNotify &&
            (event.xproperty.atom == server->workarea_atom || event.xproperty.atom == server->workspace_atom))
            workarea_invalidate(server);
    }
    return G_SOURCE_CONTINUE;
}

void luxe_notification_stop_monitoring(LuxeNotifyServer *server)
{
    if (server->workarea_x11_source) {
        g_source_remove(server->workarea_x11_source);
        server->workarea_x11_source = 0;
    }
    if (server->workarea_x11) {
        XCloseDisplay(server->workarea_x11);
        server->workarea_x11 = NULL;
    }
    if (server->workarea_display)
        g_signal_handlers_disconnect_by_data(server->workarea_display, server);
    if (server->workarea_monitors)
        g_signal_handlers_disconnect_by_data(server->workarea_monitors, server);
    if (server->watched_monitors) {
        GHashTableIter iter;
        gpointer monitor;
        g_hash_table_iter_init(&iter, server->watched_monitors);
        while (g_hash_table_iter_next(&iter, &monitor, NULL))
            g_signal_handlers_disconnect_by_data(monitor, server);
        g_clear_pointer(&server->watched_monitors, g_hash_table_unref);
    }
    g_clear_object(&server->workarea_monitors);
    g_clear_object(&server->workarea_display);
    server->workarea_valid = FALSE;
}

static void workarea_ensure_subscriptions(LuxeNotifyServer *server, GdkDisplay *display)
{
    static const gchar *legacy_names[] = {
        "monitors-changed", "layout-changed", NULL
    };

    if (display == NULL || display == server->workarea_display)
        return;
    luxe_notification_stop_monitoring(server);
    server->workarea_display = g_object_ref(display);
    server->workarea_valid = FALSE;
    if (GDK_IS_X11_DISPLAY(display)) {
        Display *native = native_x11_display(display);
        Display *watch = XOpenDisplay(DisplayString(native));
        if (watch) {
            server->workarea_x11 = watch;
            server->workarea_atom = XInternAtom(watch, "_NET_WORKAREA", False);
            server->workspace_atom = XInternAtom(watch, "_NET_CURRENT_DESKTOP", False);
            XSelectInput(watch, DefaultRootWindow(watch), PropertyChangeMask);
            XSync(watch, False);
            server->workarea_x11_source = g_unix_fd_add(ConnectionNumber(watch),
                G_IO_IN | G_IO_ERR | G_IO_HUP, workarea_x11_events, server);
        }
    }

    for (guint i = 0; legacy_names[i] != NULL; i++) {
        if (g_signal_lookup(legacy_names[i], GDK_TYPE_DISPLAY) != 0) {
            g_signal_connect(display, legacy_names[i],
                             G_CALLBACK(workarea_display_changed), server);
        }
    }

    GListModel *monitors = gdk_display_get_monitors(display);
    if (monitors == NULL)
        return;
    server->workarea_monitors = g_object_ref(monitors);
    server->watched_monitors = g_hash_table_new_full(g_direct_hash, g_direct_equal, g_object_unref, NULL);
    g_signal_connect(monitors, "items-changed",
                     G_CALLBACK(workarea_monitors_changed), server);
    workarea_watch_monitors(server);
}

static gboolean reposition_idle(gpointer user_data)
{
    LuxeNotifyServer *server = user_data;
    server->reposition_id = 0;
    GdkDisplay *gdk_display = gdk_display_get_default();
    if (!GDK_IS_X11_DISPLAY(gdk_display))
        return G_SOURCE_REMOVE;

    Display *display = native_x11_display(gdk_display);
    workarea_ensure_subscriptions(server, gdk_display);
    if (!server->workarea_valid) {
        primary_monitor_geometry(display,
                                 &server->workarea_x,
                                 &server->workarea_y,
                                 &server->workarea_width,
                                 &server->workarea_height);
        server->workarea_valid = TRUE;
    }
    gint monitor_x = server->workarea_x;
    gint monitor_y = server->workarea_y;
    gint monitor_width = server->workarea_width;
    gint monitor_height = server->workarea_height;
    apply_desktop_workarea(display, &monitor_x, &monitor_y,
                           &monitor_width, &monitor_height);
    gint y = monitor_y + WINDOW_MARGIN;
    gint maximum_y = monitor_y + monitor_height - WINDOW_MARGIN;
    gint right = monitor_x + monitor_width - WINDOW_MARGIN;
    gint column_width = 0;
    gboolean first = TRUE;

    /* Newer cards take precedence when the usable area fills. Close older
     * cards through the normal protocol path instead of placing them in an
     * inaccessible column beyond the left edge. */
    for (GList *item = g_list_last(server->notifications); item != NULL; ) {
        GList *previous = item->prev;
        LuxeNotification *notification = item->data;
        if (notification->window == NULL) {
            item = previous;
            continue;
        }
        GdkSurface *surface = gtk_native_get_surface(GTK_NATIVE(notification->window));
        if (surface == NULL || !GDK_IS_X11_SURFACE(surface)) {
            item = previous;
            continue;
        }
        /* GTK may update EWMH properties while mapping. Reapply the notification
         * type and task/pager exclusions once the surface is visible. */
        set_x11_notification_hints(notification->window);
        Window xid = native_x11_window(surface);
        XWindowAttributes attributes = { 0 };
        XGetWindowAttributes(display, xid, &attributes);
        gint width = attributes.width;
        gint height = attributes.height;
        if (width <= 1)
            width = WINDOW_WIDTH * gdk_surface_get_scale_factor(surface);
        if (height <= 1) {
            gint minimum = 0;
            gtk_widget_measure(GTK_WIDGET(notification->window),
                               GTK_ORIENTATION_VERTICAL,
                               WINDOW_WIDTH,
                               &minimum,
                               &height,
                               NULL,
                               NULL);
            height *= gdk_surface_get_scale_factor(surface);
        }
        if (y > monitor_y + WINDOW_MARGIN && y + height > maximum_y) {
            right -= column_width + WINDOW_GAP;
            column_width = 0;
            y = monitor_y + WINDOW_MARGIN;
        }
        gint x = right - width;
        if (x < monitor_x + WINDOW_MARGIN && !first) {
            luxe_notify_server_close(server, notification, 4, TRUE);
            item = previous;
            continue;
        }
        /* A single card can be wider than an unusually small workarea; keep
         * its leading edge reachable. Height constraints are handled separately. */
        x = MAX(x, monitor_x + WINDOW_MARGIN);
        XMoveWindow(display, xid, x, y);
        column_width = MAX(column_width, width);
        first = FALSE;
        y += height + WINDOW_GAP;
        item = previous;
    }
    XFlush(display);
    return G_SOURCE_REMOVE;
}

void luxe_notification_reposition_all(LuxeNotifyServer *server)
{
    if (server->reposition_id == 0)
        server->reposition_id = g_idle_add(reposition_idle, server);
}

LuxeNotification *luxe_notification_window_new(LuxeNotifyServer *server,
                                                guint id,
                                                const gchar *app_name,
                                                const gchar *app_icon,
                                                const gchar *summary,
                                                const gchar *body,
                                                GVariant *actions,
                                                GVariant *hints,
                                                gint expire_timeout)
{
    install_css_once();
    LuxeNotification *notification = g_new0(LuxeNotification, 1);
    notification->server = server;
    notification->id = id;
    notification->icon_cancel = g_cancellable_new();
    notification->app_name = bounded_text(app_name, MAX_APP_TEXT_BYTES);
    notification->resident = hint_boolean(hints, "resident");

    g_autofree gchar *safe_summary = bounded_text(summary, MAX_SUMMARY_BYTES);
    g_autofree gchar *safe_body = bounded_text(body, MAX_BODY_BYTES);
    GtkWidget *window = gtk_window_new();
    notification->window = GTK_WINDOW(window);
    gtk_window_set_title(notification->window,
                         *safe_summary != '\0' ? safe_summary : "Notification");
    gtk_window_set_decorated(notification->window, FALSE);
    gtk_window_set_resizable(notification->window, FALSE);
    gtk_window_set_default_size(notification->window, WINDOW_WIDTH, -1);
    gtk_widget_set_size_request(window, WINDOW_WIDTH, -1);
    gtk_window_set_child(notification->window,
                         create_content(notification, app_icon, safe_summary, safe_body,
                                        actions, hints));
    g_signal_connect(window, "close-request",
                     G_CALLBACK(window_close_requested), notification);

    gtk_widget_realize(window);
    set_x11_notification_hints(notification->window);
    gtk_widget_set_visible(window, TRUE);

    gint timeout = expire_timeout;
    if (timeout < 0)
        timeout = hint_integer(hints, "urgency", 1) >= 2 ? 0 : DEFAULT_TIMEOUT;
    if (timeout > 0)
        notification->timeout_id = g_timeout_add((guint)timeout,
                                                  notification_expired,
                                                  notification);
    return notification;
}
