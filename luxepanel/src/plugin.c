/*
 * Copyright (C) 2006-2008 Hong Jen Yee (PCMan) <pcman.tw@gmail.com>
 * Copyright (C) 2026 Jerry Howell
 *               2006-2008 Jim Huang <jserv.tw@gmail.com>
 *               2008 Fred Chien <fred@lxde.org>
 *               2009-2010 Marty Jack <martyj19@comcast.net>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "private.h"

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gdk/gdk.h>
#include <gdk/x11/gdkx.h>
#include <X11/Xlib.h>

#include <string.h>
#include <stdlib.h>
#include <gio/gdesktopappinfo.h>

#include "misc.h"

#include <glib-object.h>
#include <glib/gi18n.h>
#include <libfm/fm.h>

//#define DEBUG
#include "dbg.h"
#include "gtk-compat.h"

static void plugin_class_unref(PluginClass * pc);

GQuark luxepanel_plugin_qinit;
GQuark luxepanel_plugin_qconf;
GQuark luxepanel_plugin_qdata;
GQuark luxepanel_plugin_qsize;
static GHashTable *_all_types = NULL;

/* Dynamic parameter for static (built-in) plugins must be FALSE so we will not try to unload them */
#define REGISTER_STATIC_PLUGIN_CLASS(pc) \
do {\
    extern PluginClass pc;\
    register_plugin_class(&pc, FALSE);\
} while (0)

static inline const LuxePanelPluginInit *_find_plugin(const char *name)
{
    return g_hash_table_lookup(_all_types, name);
}

static GtkWidget *_old_plugin_config(LuxePanel *panel, GtkWidget *instance)
{
    const LuxePanelPluginInit *init = PLUGIN_CLASS(instance);
    Plugin * plugin;

    g_return_val_if_fail(init != NULL && init->new_instance == NULL, NULL);
    plugin = luxepanel_plugin_get_data(instance);
    if (plugin == NULL || plugin->class == NULL)
        return NULL;
    if (plugin->class->config)
        plugin->class->config(plugin, GTK_WINDOW(panel));
    return NULL;
}

static void _old_plugin_reconfigure(LuxePanel *panel, GtkWidget *instance)
{
    const LuxePanelPluginInit *init = PLUGIN_CLASS(instance);
    Plugin * plugin;

    g_return_if_fail(init != NULL && init->new_instance == NULL);
    plugin = luxepanel_plugin_get_data(instance);
    if (plugin == NULL || plugin->class == NULL)
        return;
    if (plugin->class->panel_configuration_changed)
        plugin->class->panel_configuration_changed(plugin);
}

/* Register a PluginClass. */
static void register_plugin_class(PluginClass * pc, gboolean dynamic)
{
    LuxePanelPluginInit *init;
    if (_all_types == NULL || pc == NULL || pc->type == NULL || pc->name == NULL)
        return;
    init = g_new0(LuxePanelPluginInit, 1);
    init->_reserved1 = pc;
    init->name = pc->name;
    init->description = pc->description;
    if (pc->config)
        init->config = _old_plugin_config;
    if (pc->panel_configuration_changed)
        init->reconfigure = _old_plugin_reconfigure;
    init->one_per_system = pc->one_per_system;
    init->expand_available = pc->expand_available;
    init->expand_default = pc->expand_default;
    pc->dynamic = dynamic;
    g_hash_table_insert(_all_types, g_strdup(pc->type), init);
}

/* Load a dynamic plugin. */
static void plugin_load_dynamic(const char * type, const gchar * path)
{
    PluginClass * pc = NULL;
    const gchar *p;

    if (type == NULL || path == NULL || *type == '\0')
        return;
    for (p = type; *p != '\0'; p++)
        if (!g_ascii_isalnum(*p) && *p != '_' && *p != '-' && *p != '+')
            return;

    /* Load the external module. */
    GModule * m = g_module_open(path, G_MODULE_BIND_LAZY);
    if (m != NULL)
    {
        /* Formulate the name of the expected external variable of type PluginClass. */
        char class_name[128];
        g_snprintf(class_name, sizeof(class_name), "%s_plugin_class", type);

        /* Validate that the external variable is of type PluginClass. */
        gpointer tmpsym;
        if (( ! g_module_symbol(m, class_name, &tmpsym))	/* Ensure symbol is present */
        || ((pc = tmpsym) == NULL)
        || (pc->structure_size != sizeof(PluginClass))		/* Then check versioning information */
        || (pc->structure_version != PLUGINCLASS_VERSION)
        || (strcmp(type, pc->type) != 0))			/* Then and only then access other fields; check name */
        {
            g_module_close(m);
            g_warning("%s.so is not a luxepanel plugin", type);
            return;
        }

        /* Register the newly loaded and valid plugin. */
        pc->gmodule = m;
        register_plugin_class(pc, TRUE);
        pc->count = 1;
    }
}

/* Unreference a dynamic plugin. */
static void plugin_class_unref(PluginClass * pc)
{
    if (pc == NULL || pc->count <= 0)
        return;
    pc->count -= 1;

    /* If the reference count drops to zero, unload the plugin if it is dynamic and has declared itself unloadable. */
    if ((pc->count == 0)
    && (pc->dynamic)
    && ( ! pc->not_unloadable))
    {
        g_module_close(pc->gmodule);
    }
}

/* Loads all available old type plugins. Should be removed in future releases. */
static void plugin_get_available_classes(void)
{
#ifndef DISABLE_PLUGINS_LOADING
    GDir * dir = g_dir_open(PACKAGE_LIB_DIR "/luxepanel/plugins", 0, NULL);
    if (dir != NULL)
    {
        const char * file;
        while ((file = g_dir_read_name(dir)) != NULL)
        {
            if (g_str_has_suffix(file, ".so"))
            {
                char * type = g_strndup(file, strlen(file) - 3);
                if (_find_plugin(type) == NULL)
                {
                    /* If it has not been loaded, do it.  If successful, add it to the result. */
                    char * path = g_build_filename(PACKAGE_LIB_DIR "/luxepanel/plugins", file, NULL );
                    plugin_load_dynamic(type, path);
                    g_free(path);
                }
                g_free(type);
            }
        }
        g_dir_close(dir);
    }
#endif
}

/* GTK4 widgets are created hidden and gtk_widget_show_all() would also
   pop up attached popovers.  Show the whole subtree but skip popover
   widgets themselves (their contents still need to be visible). */
void luxepanel_plugin_show_contents(GtkWidget * w)
{
    GtkWidget * child;

    if (w == NULL)
        return;
    gtk_widget_show(w);
    for (child = gtk_widget_get_first_child(w); child != NULL;
         child = gtk_widget_get_next_sibling(child))
    {
        if (GTK_IS_POPOVER(child))
        {
            GtkWidget * pc = gtk_popover_get_child(GTK_POPOVER(child));
            if (pc != NULL)
                luxepanel_plugin_show_contents(pc);
        }
        else
            luxepanel_plugin_show_contents(child);
    }
}

/* Recursively set the background of all widgets on a panel background configuration change. */
void plugin_widget_set_background(GtkWidget * w, LuxePanel * panel)
{
    /* Backgrounds are painted by bg.c snapshot(); nothing per-window to
     * configure.  A redraw of the panel covers the whole tree. */
    if (w != NULL)
        gtk_widget_queue_draw(w);
}

static gboolean _popover_menu_unparent(gpointer data)
{
    GtkWidget *popover = data;
    if (gtk_widget_get_parent(popover))
        gtk_widget_unparent(popover);
    return G_SOURCE_REMOVE;
}

static void _popover_menu_closed(GtkPopover *popover, gpointer data)
{
    /* GtkPopoverMenu closes before dispatching the selected action.
     * Keep its ancestry intact until activation has reached the group. */
    g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, _popover_menu_unparent,
                    g_object_ref(popover), g_object_unref);
}

/* Shared helper: present a GMenu as a popover parented to the plugin. */
void luxepanel_show_popup_menu_at_widget(GMenu *menu, GtkWidget *parent)
{
    GtkWidget *popover;

    if (menu == NULL || parent == NULL || !GTK_IS_WIDGET(parent))
        return;
    popover = gtk_popover_menu_new_from_model(G_MENU_MODEL(menu));

    gtk_widget_set_parent(popover, parent);
    g_signal_connect(popover, "closed", G_CALLBACK(_popover_menu_closed), NULL);
    gtk_popover_popup(GTK_POPOVER(popover));
}

/* Handler for plugin activation button press (GTK4: from gesture).
 * External so can be used from a plugin. */
gboolean luxepanel_plugin_button_press_event(GtkWidget *plugin, GdkEvent *event, LuxePanel *panel)
{
    guint button;
    GdkModifierType state;
    if (plugin == NULL || event == NULL || panel == NULL)
        return FALSE;
    button = gdk_button_event_get_button(event);
    state = gdk_event_get_modifier_state(event);
    if (button == 3 && /* right button */
        (state & gtk_accelerator_get_default_mod_mask()) == 0) /* no key */
    {
        /* Plugins may reserve right-click for a primary action. */
        const gchar *action = g_object_get_data(G_OBJECT(plugin), "luxepanel-right-click-action");
        if (action && gtk_widget_activate_action(plugin, action, NULL))
            return TRUE;
        GMenu *menu = luxepanel_get_plugin_menu(panel, plugin, FALSE);
        luxepanel_show_popup_menu_at_widget(menu, plugin);
        return TRUE;
    }
    return FALSE;
}

/* for old plugins compatibility */
gboolean plugin_button_press_event(GtkWidget *widget, GdkEvent *event, Plugin *plugin)
{
    if (plugin == NULL || plugin->pwid == NULL)
        return FALSE;
    return luxepanel_plugin_button_press_event(plugin->pwid, event, PLUGIN_PANEL(plugin->pwid));
}

/* Helper for position-calculation callback for popup menus. */
void luxepanel_plugin_popup_set_position_helper(LuxePanel * p, GtkWidget * near, GtkWidget * popup, gint * px, gint * py)
{
    gint x, y;
    if (p == NULL || near == NULL || popup == NULL || px == NULL || py == NULL)
        return;
    GtkAllocation allocation;
    GtkAllocation popup_req;

    /* Get the allocation of the popup menu. */
    gtk_widget_realize(popup);
    gtk_widget_get_allocation(popup, &popup_req);
    {
        GtkNative *near_native;
        GdkSurface *near_surface;
        double nx = 0, ny = 0;

        popup_req.width = gtk_widget_get_width(popup);
        popup_req.height = gtk_widget_get_height(popup);

        /* Origin of the near widget in root coordinates via Xlib
         * (no gdk_surface_get_origin in GTK4); widget-relative
         * allocation offsets are added on top. */
        gtk_widget_realize(near);
        near_native = gtk_widget_get_native(near);
        near_surface = near_native ? gtk_native_get_surface(near_native) : NULL;
        if (near_surface != NULL)
        {
            Display *dpy = GDK_DISPLAY_XDISPLAY(gdk_surface_get_display(near_surface));
            Window root;
            gint ox = 0, oy = 0;

            if (XTranslateCoordinates(dpy, gdk_x11_surface_get_xid(near_surface),
                                      DefaultRootWindow(dpy), 0, 0, &ox, &oy, &root))
            {
                x = ox;
                y = oy;
            }
            else
            {
                x = 0;
                y = 0;
            }
        }
        else
        {
            x = 0;
            y = 0;
        }
        gtk_widget_get_allocation(near, &allocation);
        if (near_native && gtk_widget_translate_coordinates(near,
                GTK_WIDGET(near_native), 0, 0, &nx, &ny))
        {
            x += (gint)nx;
            y += (gint)ny;
        }
    }

    /* Dispatch on edge to lay out the popup menu with respect to the button.
     * Also set "push-in" to avoid any case where it might flow off screen. */
    switch (p->priv->edge)
    {
        case EDGE_TOP:          y += allocation.height;         break;
        case EDGE_BOTTOM:       y -= popup_req.height;                break;
        case EDGE_LEFT:         x += allocation.width;          break;
        case EDGE_RIGHT:        x -= popup_req.width;                 break;
    }

    /* Push onscreen. */
    /* Clamp only when a real X11 monitor/workarea is available. */
    {
        GdkDisplay *display = gdk_display_get_default();
        GdkSurface *popup_surface = GTK_IS_NATIVE(popup) ?
            gtk_native_get_surface(GTK_NATIVE(popup)) : NULL;
        GdkSurface *panel_surface = GTK_IS_NATIVE(p) ?
            gtk_native_get_surface(GTK_NATIVE(p)) : NULL;
        GdkMonitor *monitor = NULL;
        if (luxepanel_display_usable(display) && GDK_IS_X11_DISPLAY(display))
        {
            if (popup_surface != NULL)
                monitor = gdk_display_get_monitor_at_surface(display, popup_surface);
            if (monitor == NULL && panel_surface != NULL)
                monitor = gdk_display_get_monitor_at_surface(display, panel_surface);
        }
        if (monitor != NULL)
        {
            GdkRectangle workarea;
            gdk_x11_monitor_get_workarea(monitor, &workarea);
            if (workarea.width > popup_req.width)
                x = CLAMP(x, workarea.x,
                          workarea.x + workarea.width - popup_req.width);
            else
                x = workarea.x;
            if (workarea.height > popup_req.height)
                y = CLAMP(y, workarea.y,
                          workarea.y + workarea.height - popup_req.height);
            else
                y = workarea.y;
        }
    }

    *px = x;
    *py = y;
}

/* for old plugins compatibility -- popup_req is ignored here */
void plugin_popup_set_position_helper(Plugin * p, GtkWidget * near, GtkWidget * popup, GtkRequisition * popup_req, gint * px, gint * py)
{
    (void)popup_req;
    if (p == NULL || p->panel == NULL)
        return;
    luxepanel_plugin_popup_set_position_helper(p->panel->topgwin, near, popup, px, py);
}

/* Adjust the position of a popup window to ensure that it is not hidden by the panel.
 * It is observed that some window managers do not honor the strut that is set on the panel. */
void luxepanel_plugin_adjust_popup_position(GtkWidget * popup, GtkWidget * parent)
{
    gint x = 0, y = 0;
    LuxePanel *panel;

    if (popup == NULL || parent == NULL)
        return;
    panel = PLUGIN_PANEL(parent);
    if (panel == NULL)
        return;

    /* Calculate desired position for the popup. */
    luxepanel_plugin_popup_set_position_helper(panel, parent,
                                             popup, &x, &y);
    /* Move the popup to position. */
    if (GTK_IS_NATIVE(popup))
    {
        GdkSurface *psf = gtk_native_get_surface(GTK_NATIVE(popup));
        if (psf != NULL && GDK_IS_X11_DISPLAY(gdk_surface_get_display(psf)))
            XMoveWindow(GDK_DISPLAY_XDISPLAY(gdk_surface_get_display(psf)),
                        gdk_x11_surface_get_xid(psf), x, y);
    }
}

/* for old plugins compatibility */
void plugin_adjust_popup_position(GtkWidget * popup, Plugin * plugin)
{
    if (plugin == NULL)
        return;
    luxepanel_plugin_adjust_popup_position(popup, plugin->pwid);
}

gboolean luxepanel_launch_path(LuxePanel *panel, FmPath *path)
{
    /* fm_launch_path_simple is libfm-gtk; launch via GIO directly.
     * .desktop files must be launched as applications (GDesktopAppInfo),
     * NOT with the inode/directory default handler (which would open
     * the file in a file manager instead of running the app). */
    GFile *gf;
    gchar *fn;
    if (path == NULL)
        return FALSE;
    gf = fm_path_to_gfile(path);
    if (gf == NULL)
        return FALSE;
    fn = g_file_get_path(gf);
    GDesktopAppInfo *dai;
    GAppInfo *app = NULL;
    GList files;
    gboolean ret = FALSE;
    GError *err = NULL;

    dai = (fn && g_str_has_suffix(fn, ".desktop")) ?
          g_desktop_app_info_new_from_filename(fn) : NULL;
    if (dai != NULL)
    {
        ret = g_app_info_launch(G_APP_INFO(dai), NULL, NULL, &err);
        g_object_unref(dai);
    }
    else
    {
        app = g_app_info_get_default_for_type("inode/directory", TRUE);
        if (app == NULL)
            g_warning("luxepanel: no handler for path launch");
        else
        {
            files.data = gf;
            files.next = NULL;
            ret = g_app_info_launch(app, &files, NULL, &err);
            g_object_unref(app);
        }
    }
    if (err != NULL)
    {
        g_warning("luxepanel: launch failed: %s", err->message);
        g_error_free(err);
    }
    g_free(fn);
    g_object_unref(gf);
    return ret;
}

void luxepanel_plugin_show_config_dialog(GtkWidget* plugin)
{
    const LuxePanelPluginInit *init;
    LuxePanel *panel;
    GtkWidget *dlg;

    if (plugin == NULL)
        return;
    init = PLUGIN_CLASS(plugin);
    panel = PLUGIN_PANEL(plugin);
    if (panel == NULL)
        return;
    dlg = panel->priv->plugin_pref_dialog;

    if (dlg && g_object_get_data(G_OBJECT(dlg), "generic-config-plugin") == plugin)
    {
        gtk_window_present(GTK_WINDOW(dlg));
        return;
    }
    if (init == NULL || init->config == NULL)
        return;
    dlg = init->config(panel, plugin);
    if (dlg)
        _panel_show_config_dialog(panel, plugin, dlg);
}

#if GLIB_CHECK_VERSION(2, 32, 0)
static GRecMutex _mutex;
#else
static GStaticRecMutex _mutex = G_STATIC_REC_MUTEX_INIT;
#endif

#ifndef DISABLE_PLUGINS_LOADING
FM_MODULE_DEFINE_TYPE(luxepanel_gtk, LuxePanelPluginInit, 1)

static gboolean fm_module_callback_luxepanel_gtk(const char *name, gpointer init, int ver)
{
    /* ignore ver for now, only 1 exists */
    return luxepanel_register_plugin_type(name, init);
}
#endif

static gboolean old_plugins_loaded = FALSE;

void luxepanel_prepare_modules(void)
{
    if (_all_types != NULL)
        return;
    _all_types = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    luxepanel_plugin_qdata = g_quark_from_static_string("LuxePanel::plugin-data");
    luxepanel_plugin_qinit = g_quark_from_static_string("LuxePanel::plugin-init");
    luxepanel_plugin_qconf = g_quark_from_static_string("LuxePanel::plugin-conf");
    luxepanel_plugin_qsize = g_quark_from_static_string("LuxePanel::plugin-size");
#ifndef DISABLE_PLUGINS_LOADING
    fm_modules_add_directory(PACKAGE_LIB_DIR "/luxepanel/plugins");
    fm_module_register_luxepanel_gtk();
#endif
}

void luxepanel_unload_modules(void)
{
    GHashTableIter iter;
    if (_all_types == NULL)
        return;
    gpointer key, val;

    g_hash_table_iter_init(&iter, _all_types);
    while(g_hash_table_iter_next(&iter, &key, &val))
    {
        register const LuxePanelPluginInit *init = val;
        if (init->new_instance == NULL) /* old type of plugin */
        {
            plugin_class_unref(init->_reserved1);
            g_free(val);
        }
    }
    g_hash_table_destroy(_all_types);
    _all_types = NULL;
#ifndef DISABLE_PLUGINS_LOADING
    fm_module_unregister_type("luxepanel_gtk");
#endif
    old_plugins_loaded = FALSE;
}

gboolean luxepanel_register_plugin_type(const char *name, const LuxePanelPluginInit *init)
{
    const LuxePanelPluginInit *data;

    /* validate it */
    if (_all_types == NULL || init == NULL || init->new_instance == NULL ||
        name == NULL || name[0] == '\0')
        return FALSE;
#if GLIB_CHECK_VERSION(2, 32, 0)
    g_rec_mutex_lock(&_mutex);
#else
    g_static_rec_mutex_lock(&_mutex);
#endif
    /* test if it's registered already */
    data = _find_plugin(name);
    if (data == NULL)
    {
        if (init->init)
            init->init();
        g_hash_table_insert(_all_types, g_strdup(name), (gpointer)init);
    }
#if GLIB_CHECK_VERSION(2, 32, 0)
    g_rec_mutex_unlock(&_mutex);
#else
    g_static_rec_mutex_unlock(&_mutex);
#endif
    return (data == NULL);
}

static void _old_plugin_save_hook(const config_setting_t * setting, FILE * f, gpointer user_data)
{
    Plugin *pl = user_data;
    PluginClass *pc = pl->class;
    if (pc->save)
        pc->save(pl, f);
}

/* This is called right before Plugin instance is destroyed */
static void _old_plugin_destroy(gpointer data)
{
    Plugin *pl = data;
    if (pl == NULL)
        return;

    plugin_class_unref(pl->class);

    /* Free the Plugin structure. */
    g_free(pl);
}

static void _on_old_widget_destroy(GtkWidget *widget, Plugin *pl)
{
    if (widget == NULL || pl == NULL || pl->class == NULL)
        return;
    /* Never let run it again. */
    g_signal_handlers_disconnect_by_func(widget, _on_old_widget_destroy, pl);
    /* Run the destructor before destroying the top level widget.
     * This prevents problems with the plugin destroying child widgets. */
    pl->class->destructor(pl);
}

/* Gesture handler replacing the legacy "button-press-event" signal
 * wiring on plugin widgets.  Runs luxepanel's own handler first; if it
 * claims the event (context menu), the plugin handler is skipped --
 * matching the legacy signal-return-value semantics. */
static void luxepanel_plugin_click_gesture(GtkGestureClick *gesture, gint n,
                                         gdouble x, gdouble y, LuxePanel *p)
{
    GtkWidget *plugin = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
    GdkEvent *event = gtk_event_controller_get_current_event(GTK_EVENT_CONTROLLER(gesture));
    const LuxePanelPluginInit *init = PLUGIN_CLASS(plugin);

    if (luxepanel_plugin_button_press_event(plugin, event, p))
    {
        gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
        return;
    }
    if (init != NULL && init->button_press_event)
        init->button_press_event(plugin, event, p);
}

/* Install the click gesture on a plugin instance widget. */
void luxepanel_plugin_attach_click(GtkWidget *widget, LuxePanel *panel)
{
    GtkGesture *gesture;
    if (widget == NULL || panel == NULL)
        return;
    gesture = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gesture), 0);

    g_signal_connect(gesture, "pressed", G_CALLBACK(luxepanel_plugin_click_gesture), panel);
    gtk_widget_add_controller(widget, GTK_EVENT_CONTROLLER(gesture));
}

GtkWidget *luxepanel_add_plugin(LuxePanel *p, const char *name, config_setting_t *cfg, gint at)
{
    const LuxePanelPluginInit *init;
    GtkWidget *widget;
    config_setting_t *s, *pconf;
    gint expand, padding = 0, border = 0, i;

    if (p == NULL || p->priv == NULL || p->priv->box == NULL ||
        name == NULL || cfg == NULL)
        return NULL;
    CHECK_MODULES();
    if (!old_plugins_loaded)
        plugin_get_available_classes();
    old_plugins_loaded = TRUE;
    init = _find_plugin(name);
    if (init == NULL)
        return NULL;
    /* prepare widget settings */
    if (!init->expand_available)
        expand = 0;
    else if ((s = config_setting_get_member(cfg, "expand")))
        expand = config_setting_get_int(s);
    else
        expand = init->expand_default;
    s = config_setting_get_member(cfg, "padding");
    if (s)
        padding = config_setting_get_int(s);
    s = config_setting_get_member(cfg, "border");
    /* FIXME: this is useless setting, it should be 0 or panel becomes weird */
    if (s)
        border = config_setting_get_int(s);
    expand = expand != 0;
    padding = CLAMP(padding, 0, 10000);
    border = CLAMP(border, 0, 10000);
    /* prepare config and create it if need */
    s = config_setting_add(cfg, "", PANEL_CONF_TYPE_LIST);
    if (s == NULL)
        return NULL;
    for (i = 0; (pconf = config_setting_get_elem(s, i)); i++)
        if (g_strcmp0(config_setting_get_name(pconf), "Config") == 0)
            break;
    if (!pconf)
        pconf = config_setting_add(s, "Config", PANEL_CONF_TYPE_GROUP);
    if (pconf == NULL)
        return NULL;
    /* If this plugin can only be instantiated once, count the instantiation.
     * This causes the configuration system to avoid displaying the plugin as one that can be added. */
    if (init->new_instance) /* new style of plugin */
    {
        widget = init->new_instance(p, pconf);
        if (widget == NULL)
            return widget;
        /* always connect luxepanel_plugin_button_press_event() */
        luxepanel_plugin_attach_click(widget, p);
    }
    else
    {
        Plugin *pl = g_new0(Plugin, 1);
        PluginClass *pc = init->_reserved1;
        char *conf = config_setting_to_string(pconf), *fp;

        if (conf == NULL || strlen(conf) < 9)
        {
            g_free(conf);
            g_free(pl);
            return NULL;
        }
        pl->class = pc;
        pl->panel = p->priv;
        widget = NULL;
        fp = &conf[9]; /* skip "Config {\n" */
        /* g_debug("created conf: %s",conf); */
    /* Call the constructor.
     * It is responsible for parsing the parameters, and setting "pwid" to the top level widget. */
        if (pc->constructor(pl, &fp))
            widget = pl->pwid;
        g_free(conf);

        if (widget == NULL) /* failed */
        {
            g_free(pl);
            return widget;
        }

        pc->count += 1;
        g_signal_connect(widget, "destroy", G_CALLBACK(_on_old_widget_destroy), pl);
        config_setting_set_save_hook(pconf, _old_plugin_save_hook, pl);
        luxepanel_plugin_set_data(widget, pl, _old_plugin_destroy);
    }
    gtk_widget_set_name(widget, name);
    gtk_box_append(GTK_BOX(p->priv->box), widget);
    /* GTK4 widgets are created hidden and show_all() is gone; make the
       plugin subtree visible.  Attached popovers must NOT be shown (that
       would pop them up), but their contents do need to be visible. */
    luxepanel_plugin_show_contents(widget);
    if (border)
        gtk_widget_set_margin_start(widget, border);
    if (expand)
    {
        if (panel_get_orientation(p) == GTK_ORIENTATION_HORIZONTAL)
            gtk_widget_set_hexpand(widget, TRUE);
        else
            gtk_widget_set_vexpand(widget, TRUE);
    }
    if (padding)
    {
        if (panel_get_orientation(p) == GTK_ORIENTATION_HORIZONTAL)
        {
            gtk_widget_set_margin_start(widget, padding / 2);
            gtk_widget_set_margin_end(widget, padding / 2);
        }
        else
        {
            gtk_widget_set_margin_top(widget, padding / 2);
            gtk_widget_set_margin_bottom(widget, padding / 2);
        }
    }
    if (at >= 0)
    {
        /* position within the box: reorder after the (at-1)-th child */
        GtkWidget *iter, *prev = NULL;
        gint i;
        for (i = 0, iter = gtk_widget_get_first_child(p->priv->box);
             iter != NULL && i < at;
             iter = gtk_widget_get_next_sibling(iter), i++)
            prev = iter;
        if (prev != NULL && prev != widget)
            gtk_box_reorder_child_after(GTK_BOX(p->priv->box), widget, prev);
    }
    gtk_widget_show(widget);
    g_object_set_qdata(G_OBJECT(widget), luxepanel_plugin_qconf, cfg);
    g_object_set_qdata(G_OBJECT(widget), luxepanel_plugin_qinit, (gpointer)init);
    return widget;
}

/* transfer none - note that not all fields are valid there */
GHashTable *luxepanel_get_all_types(void)
{
    return _all_types;
}
