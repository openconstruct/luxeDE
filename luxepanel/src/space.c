/*
 * Copyright (C) 2006-2008 Hong Jen Yee (PCMan) <pcman.tw@gmail.com>
 *               2006-2008 Jim Huang <jserv.tw@gmail.com>
 *               2009 Marty Jack <martyj19@comcast.net>
 *               2010 Julien Lavergne <julien.lavergne@gmail.com>
 *               2014-2016 Andriy Grytsenko <andrej@rep.kiev.ua>
 *
 * This file is a part of LXPanel project.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <stdlib.h>

#include <glib/gi18n.h>
#include <libfm/fm.h>

#include "space.h"

#include "private.h"

struct _PanelSpace
{
    GtkWidget parent;
    config_setting_t *settings;
    int size;
};

struct _PanelSpaceClass
{
    GtkWidgetClass parent_class;
};

/* GTK4: the drag-dest vtable API and libfm-gtk FmDndDest are gone.
 * A GtkDropTargetAsync handles the launcher payload (custom mime) and
 * plain text/uri-list file drops. */
static const char launcher_mime_type[] = "application/x-luxepanel-launcher";

static const char *drop_mime_types[] = {
    "application/x-luxepanel-launcher",
    "text/uri-list",
    NULL /* gdk_drop_read_async wants a NULL-terminated list */
};

typedef struct
{
    PanelSpace *sp;
    double x, y;
    GdkDrop *drop;
    gboolean got_uri_list;
} SpaceDropCtx;

static void space_drop_read_done(GObject *src, GAsyncResult *res, gpointer data);
static void space_drop_bytes_done(GObject *src, GAsyncResult *res, gpointer data);

static gboolean panel_space_make_launcher(GtkWidget *widget, gint x, gint y, const char *str);

static void space_drop_finish(SpaceDropCtx *ctx, gboolean success)
{
    if (ctx == NULL)
        return;
    gdk_drop_finish(ctx->drop, success ? GDK_ACTION_MOVE : GDK_ACTION_COPY);
    g_object_unref(ctx->drop);
    if (ctx->sp != NULL)
        g_object_unref(ctx->sp);
    g_free(ctx);
}

/* Called with the data stream for whichever mime type matched. */
static void space_drop_read_done(GObject *src, GAsyncResult *res, gpointer data)
{
    SpaceDropCtx *ctx = data;
    GInputStream *stream;
    GError *error = NULL;
    const char *got_mime = NULL;

    stream = gdk_drop_read_finish(GDK_DROP(src), res, &got_mime, &error);
    if (stream == NULL)
    {
        g_warning("PanelSpace: drop read failed: %s",
                  error != NULL ? error->message : "unknown error");
        g_clear_error(&error);
        space_drop_finish(ctx, FALSE);
        return;
    }
    ctx->got_uri_list = (got_mime != NULL &&
                         strcmp(got_mime, "text/uri-list") == 0);
    g_input_stream_read_bytes_async(stream, 8192, G_PRIORITY_DEFAULT, NULL,
                                    space_drop_bytes_done, ctx);
    g_object_unref(stream);
}

static void space_drop_bytes_done(GObject *src, GAsyncResult *res, gpointer data)
{
    SpaceDropCtx *ctx = data;
    GBytes *bytes;
    GError *error = NULL;
    gsize size;
    const char *raw;
    char *str = NULL;

    bytes = g_input_stream_read_bytes_finish(G_INPUT_STREAM(src), res, &error);
    if (bytes == NULL)
    {
        g_warning("PanelSpace: drop payload read failed: %s",
                  error != NULL ? error->message : "unknown error");
        g_clear_error(&error);
        space_drop_finish(ctx, FALSE);
        return;
    }
    raw = g_bytes_get_data(bytes, &size);
    /* Make sure the payload is NUL-terminated, and reject an embedded NUL
     * rather than silently launching only a prefix of the dropped value. */
    if (raw != NULL && size <= 8192 && memchr(raw, '\0', size) == NULL)
    {
        str = g_new0(char, size + 1);
        memcpy(str, raw, size);
    }
    g_bytes_unref(bytes);

    /* decide whether it is a launcher payload or a file URI list:
     * the mime type tells us which one matched.  Both are handled by
     * taking the first line, unescaping file:// URIs. */
    if (str != NULL && !ctx->got_uri_list && g_str_has_prefix(str, "menu://"))
        ; /* launcher payload, pass as-is */
    else if (str != NULL)
    {
        char *line = strtok(str, "\r\n");
        if (line == NULL)
        {
            g_free(str);
            str = NULL;
        }
        else if (g_str_has_prefix(line, "file://"))
        {
            char *unescaped = g_uri_unescape_string(line + 7, NULL);
            g_free(str);
            str = unescaped;
        }
        else if (strchr(line, ':') != NULL && !g_str_has_prefix(line, "menu://"))
        {
            /* some other URI, unsupported */
            g_free(str);
            str = NULL;
        }
        else
        {
            char *dup = g_strdup(line);
            g_free(str);
            str = dup;
        }
    }
    if (str == NULL)
    {
        space_drop_finish(ctx, FALSE);
        return;
    }
    /* try to make a launcher */
    gboolean ok = panel_space_make_launcher(GTK_WIDGET(ctx->sp),
                                            (gint)ctx->x, (gint)ctx->y, str);
    g_free(str);
    space_drop_finish(ctx, ok);
}

static gboolean panel_space_target_drop(GtkDropTargetAsync *dest,
                                        double x, double y, GdkDrop *drop,
                                        gpointer user_data)
{
    PanelSpace *sp = PANEL_SPACE(user_data);
    SpaceDropCtx *ctx;
    GdkContentFormats *formats;
    GdkDragAction actions;

    (void)dest;
    if (sp == NULL || drop == NULL)
        return FALSE;
    formats = gdk_drop_get_formats(drop);
    if (formats == NULL)
        return FALSE;
    ctx = g_new0(SpaceDropCtx, 1);
    /* The async read can outlive the panel-space widget during a panel
     * rebuild or monitor removal. */
    ctx->sp = g_object_ref(sp);
    ctx->x = x;
    ctx->y = y;
    ctx->drop = g_object_ref(drop);

    if (gdk_content_formats_contain_mime_type(formats, launcher_mime_type))
        actions = GDK_ACTION_MOVE; /* see launchbar plugin */
    else
        actions = GDK_ACTION_COPY;
    gdk_drop_status(drop, actions, actions);
    gdk_drop_read_async(drop, drop_mime_types, G_PRIORITY_DEFAULT, NULL,
                        space_drop_read_done, ctx);
    return TRUE;
}

static GdkDragAction panel_space_target_enter(GtkDropTargetAsync *dest,
                                              double x, double y,
                                              gpointer user_data)
{
    (void)dest;
    (void)x;
    (void)y;
    (void)user_data;
    return GDK_ACTION_COPY | GDK_ACTION_MOVE;
}

/* find position of widget in its GtkBox parent by sibling traversal */
static GtkWidget *panel_space_parent_box(GtkWidget *widget, gint *idx)
{
    GtkWidget *box = gtk_widget_get_parent(widget);
    GtkWidget *iter;
    int i = 0;

    if (box == NULL)
    {
        if (idx != NULL)
            *idx = -1;
        return NULL;
    }
    for (iter = gtk_widget_get_first_child(box); iter != NULL;
         iter = gtk_widget_get_next_sibling(iter))
    {
        if (iter == widget)
        {
            if (idx != NULL)
                *idx = i;
            return box;
        }
        i++;
    }
    if (idx != NULL)
        *idx = -1;
    return box;
}

static gboolean panel_space_make_launcher(GtkWidget *widget, gint x, gint y, const char *str)
{
    PanelSpace *sp = PANEL_SPACE(widget);
    GtkWidget *panel_box, *launchbar, *sp2;
    LuxePanel *panel;
    config_setting_t *cfg;
    LuxePanelPluginInit *init;
    char *cmd;
    GtkAllocation alloc;
    int idx = -1, size, size2 = 0;
    int icon_size;
    gboolean expand = FALSE;

    if (widget == NULL || str == NULL || sp == NULL)
        return FALSE;

    /* find position of this widget */
    panel_box = panel_space_parent_box(widget, &idx);
    /* the "expand" flag lives in our own config settings */
    {
        int expand_int = 0;

        if (sp->settings != NULL)
            config_setting_lookup_int(sp->settings, "expand", &expand_int);
        expand = (expand_int != 0);
    }
    if (idx < 1 || panel_box == NULL)
        return FALSE;
    panel = PLUGIN_PANEL(widget);
    if (panel == NULL)
        return FALSE;
    /* prepare config for new widget and create launcher before PS */
    cfg = config_group_add_subgroup(config_root_setting(panel->priv->config),
                                    "Plugin");
    if (cfg == NULL)
        return FALSE;
    config_group_set_string(cfg, "type", "launchbar");
    if (!config_setting_move_elem(cfg, config_setting_get_parent(cfg), idx + 1))
    {
        config_setting_destroy(cfg);
        return FALSE;
    }
    launchbar = luxepanel_add_plugin(panel, "launchbar", cfg, idx);
    if (launchbar == NULL) /* failed to create */
    {
        config_setting_destroy(cfg);
        return FALSE;
    }
    init = PLUGIN_CLASS(launchbar);
    if (init == NULL || !init->control) /* cannot create a launcher */
    {
        luxepanel_remove_plugin(panel, launchbar);
        return FALSE;
    }
    if (strncmp(str, "menu://applications/", 20) == 0)
        cmd = g_strdup_printf("add %s", strrchr(str, '/') + 1);
    else
        cmd = g_strdup_printf("add %s", str);
    if (!init->control(launchbar, cmd)) /* failed to create a launcher */
    {
        g_free(cmd);
        luxepanel_remove_plugin(panel, launchbar);
        return FALSE;
    }
    /* success */
    g_free(cmd);
    /* now to find where to insert the launcher */
    icon_size = panel_get_icon_size(panel);
    if (!expand && sp->size <= icon_size/2 + 4) //just drop this PS
    {
        luxepanel_remove_plugin(panel, widget);
        return TRUE;
    }
    gtk_widget_get_allocation(widget, &alloc);
    if (panel_get_orientation(panel) == GTK_ORIENTATION_HORIZONTAL)
    {
        size = alloc.width;
    }
    else
    {
        size = alloc.height;
        x = y; /* use x below as a position value */
    }
    /* g_debug("making launcher at %d on PanelSpace of size %d", x, size); */
    if (x <= icon_size/2 + 4) //leave launchbar at idx (before PS), size -= icon_size+3
    {
        luxepanel_config_save(panel);
    }
    else if (x >= size - icon_size/2 - 4) //move launchbar to idx+1 (after PS), size -= icon_size+3
    {
        gtk_box_reorder_child_after(GTK_BOX(panel_box), launchbar,
                                    gtk_widget_get_next_sibling(widget));
        config_setting_move_elem(cfg, config_setting_get_parent(cfg), idx + 2);
        luxepanel_config_save(panel);
    }
    else if (expand && x < size/2) //create another PS at idx of size pos-icon_size/2-2, shifting launchbar
    {
        cfg = config_group_add_subgroup(config_root_setting(panel->priv->config),
                                        "Plugin");
        if (cfg == NULL)
        {
            luxepanel_remove_plugin(panel, launchbar);
            return FALSE;
        }
        config_group_set_string(cfg, "type", "space");
        sp2 = luxepanel_add_plugin(panel, "space", cfg, idx);
        size2 = x - icon_size/2 - 2;
        /* g_debug("adding new PanelSpace of size %d before Launcher", size2); */
        if (!config_setting_move_elem(cfg, config_setting_get_parent(cfg), idx + 1))
        {
            if (sp2 != NULL)
                luxepanel_remove_plugin(panel, sp2);
            else
                config_setting_destroy(cfg);
            luxepanel_remove_plugin(panel, launchbar);
            return FALSE;
        }
        if (sp2 == NULL)
            config_setting_destroy(cfg);
        else
            _panel_space_resize(sp2, size2);
    }
    else //move launchbar to idx+1, then create another PS at idx+2 of size size-pos-icon_size/2-2
    {
        gtk_box_reorder_child_after(GTK_BOX(panel_box), launchbar,
                                    gtk_widget_get_next_sibling(widget));
        config_setting_move_elem(cfg, config_setting_get_parent(cfg), idx + 2);
        cfg = config_group_add_subgroup(config_root_setting(panel->priv->config),
                                        "Plugin");
        if (cfg == NULL)
        {
            luxepanel_remove_plugin(panel, launchbar);
            return FALSE;
        }
        config_group_set_string(cfg, "type", "space");
        sp2 = luxepanel_add_plugin(panel, "space", cfg, idx + 2);
        size2 = size - x - icon_size/2 - 2;
        /* g_debug("adding new PanelSpace of size %d after Launcher", size2); */
        if (!config_setting_move_elem(cfg, config_setting_get_parent(cfg), idx + 3))
        {
            if (sp2 != NULL)
                luxepanel_remove_plugin(panel, sp2);
            else
                config_setting_destroy(cfg);
            luxepanel_remove_plugin(panel, launchbar);
            return FALSE;
        }
        if (sp2 == NULL)
            config_setting_destroy(cfg);
        else
            _panel_space_resize(sp2, size2);
    }
    if (!expand) //resize to sp->size - icon_size - 3 - size2, then queue resize
    {
        /* g_debug("resizing this space to %d", sp->size - icon_size - 3 - size2); */
        _panel_space_resize(widget, sp->size - icon_size - 3 - size2);
    }
    return TRUE;
}

static void panel_space_measure(GtkWidget *widget, GtkOrientation orientation,
                                int for_size, int *minimum, int *natural,
                                int *minimum_baseline, int *natural_baseline)
{
    PanelSpace *p = PANEL_SPACE(widget);

    (void)orientation;
    (void)for_size;
    if (minimum_baseline)
        *minimum_baseline = -1;
    if (natural_baseline)
        *natural_baseline = -1;
    if (minimum)
        *minimum = 2;
    if (natural)
        *natural = p->size;
}

G_DEFINE_TYPE(PanelSpace, panel_space, GTK_TYPE_WIDGET)

static void panel_space_dispose(GObject *object)
{

    G_OBJECT_CLASS(panel_space_parent_class)->dispose(object);
}

static void panel_space_class_init(PanelSpaceClass *klass)
{
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);
    GObjectClass *object_class = G_OBJECT_CLASS(klass);

    widget_class->measure = panel_space_measure;

    object_class->dispose = panel_space_dispose;

}

static void panel_space_init(PanelSpace *self)
{
    {
        /* GTK4 drop handling: launcher payload + uri-lists */
        GdkContentFormats *formats;
        GtkDropTargetAsync *dest;

        formats = gdk_content_formats_new(drop_mime_types, 2);
        dest = gtk_drop_target_async_new(formats,
                                         GDK_ACTION_COPY | GDK_ACTION_MOVE);
        g_signal_connect(dest, "drop", G_CALLBACK(panel_space_target_drop), self);
        g_signal_connect(dest, "drag-enter", G_CALLBACK(panel_space_target_enter), self);
        gtk_widget_add_controller(GTK_WIDGET(self), GTK_EVENT_CONTROLLER(dest));
    }
}

/* Plugin constructor. */
GtkWidget *_panel_space_new(LuxePanel *panel, config_setting_t *settings)
{
    /* Allocate plugin context and set into Plugin private data pointer. */
    PanelSpace * p = g_object_new(PANEL_TYPE_SPACE, NULL);

    if (settings != NULL)
        config_setting_lookup_int(settings, "Size", &p->size);
    p->size = CLAMP(p->size, 2, 1000000);

    /* Save construction pointers */
    p->settings = settings;

    /* The constructor has already applied a safe default for zero. */

    return GTK_WIDGET(p);
}

/* Callback when the configuration dialog has recorded a configuration change. */
static gboolean space_apply_configuration(gpointer user_data)
{
    PanelSpace *p = user_data;

    if (p == NULL || p->settings == NULL)
        return G_SOURCE_REMOVE;
    /* Apply settings. */
    gtk_widget_queue_resize(GTK_WIDGET(p));
    /* Save config values */
    config_group_set_int(p->settings, "Size", p->size);
    return FALSE;
}

void _panel_space_resize(GtkWidget *spacer, gint size)
{
    PanelSpace * p = PANEL_SPACE(spacer);
    LuxePanel *panel;

    if (p == NULL)
        return;
    p->size = CLAMP(size, 0, 1000000);
    space_apply_configuration(p);
    panel = PLUGIN_PANEL(spacer);
    if (panel != NULL)
        luxepanel_config_save(panel);
}

gint _panel_space_get_size(GtkWidget *spacer)
{
    PanelSpace *p = PANEL_SPACE(spacer);
    return p != NULL ? p->size : 0;
}

/* Callback when the configuration dialog is to be shown. */
static GtkWidget *space_configure(LuxePanel *panel, GtkWidget *instance)
{
    PanelSpace * p = PANEL_SPACE(instance);
    GtkWidget * dlg;

    if (p == NULL)
        return NULL;
    dlg = luxepanel_generic_config_dlg(_("Spacer"), panel,
                                     space_apply_configuration, instance,
                                     _("Size"), &p->size, CONF_TYPE_INT, NULL);
    gtk_widget_set_size_request(dlg, 200, -1);	/* Improve geometry */
    return dlg;
}

/* Plugin descriptor. */
LuxePanelPluginInit _luxepanel_static_plugin_space = {
    .name = N_("Spacer"),
    .description = N_("Allocate space"),

    /* Stretch is available but not default for this plugin. */
    .expand_available = TRUE,

    .new_instance = _panel_space_new,
    .config = space_configure,
};
