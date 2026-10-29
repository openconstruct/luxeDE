/*
 * Copyright (C) 2006-2010 Hong Jen Yee (PCMan) <pcman.tw@gmail.com>
 * Copyright (C) 2026 Jerry Howell
 *               2006-2008 Jim Huang <jserv.tw@gmail.com>
 *               2008 Fred Chien <fred@lxde.org>
 *               2009 Ying-Chun Liu (PaulLiu) <grandpaul@gmail.com>
 *               2009-2010 Marty Jack <martyj19@comcast.net>
 *               2010 Jürgen Hötzel <juergen@archlinux.org>
 *               2010-2011 Julien Lavergne <julien.lavergne@gmail.com>
 *               2012-2013 Henry Gebhardt <hsggebhardt@gmail.com>
 *               2012 Michael Rawson <michaelrawson76@gmail.com>
 *               2014 Max Krummenacher <max.oss.09@gmail.com>
 *               2014 SHiNE CsyFeK <csyfek@users.sourceforge.net>
 *               2014 Andriy Grytsenko <andrej@rep.kiev.ua>
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
#include <config.h>
#endif

#include <stdlib.h>
#include <string.h>

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib.h>
#include <glib/gi18n.h>
#include "luxe-dialogs-gtk.h"

#include <menu-cache.h>
#include <libfm/fm.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

#include "misc.h"
#include "plugin.h"
#include "menu-policy.h"

#include "dbg.h"
#include "gtk-compat.h"

#include <gio/gdesktopappinfo.h>
#include "menu-section-state.h"

/* support for libmenu-cache 0.4.x */
#ifndef MENU_CACHE_CHECK_VERSION
# ifdef HAVE_MENU_CACHE_DIR_LIST_CHILDREN
#  define MENU_CACHE_CHECK_VERSION(_a,_b,_c) (_a == 0 && _b < 5) /* < 0.5.0 */
# else
#  define MENU_CACHE_CHECK_VERSION(_a,_b,_c) 0 /* not even 0.4.0 */
# endif
#endif

#define DEFAULT_MENU_ICON PACKAGE_DATA_DIR "/images/luxe-button.png"
#define LIGHT_MENU_ICON PACKAGE_DATA_DIR "/images/luxe-button-light.svg"
/*
 * SuxPanel version 0.1
 * Copyright (c) 2003 Leandro Pereira <leandro@linuxmag.com.br>
 */

/*
 * menu style code was taken from suxpanel
 */

/* FIXME: those are defined on panel main code */
void restart(void);
void gtk_run(void);
void logout(void);

/* GTK4 implementation: the popover menu with type-ahead search.
 * Structure: GtkSearchEntry + GtkListBox bound to a GtkFilterListModel
 * over a GListStore of MenuRow objects (configured entries + menu-cache
 * applications flattened with category headers).  Typing filters the
 * list down to matching applications; empty text shows the full tree.
 * Context menu (right-click on an item) offers "Add to desktop".
 * Drag-out of menu items (FmDndSrc) and item Properties dialog are
 * dropped: fm-dnd-src and file properties dialogs live in libfm-gtk. */

#define MENU_TYPE_ROW (menu_row_get_type())
G_DECLARE_FINAL_TYPE(MenuRow, menu_row, MENU, ROW, GObject)

struct _MenuRow
{
    GObject parent;
    gchar *label;              /* item text (may be NULL) */
    gchar *icon;               /* icon name or path for custom items */
    gchar *tooltip;
    gboolean is_sep;
    gboolean is_header;
    gboolean collapsed;        /* header rows: section folded away */
    gboolean system;           /* row came from menu-cache */
    void (*builtin)(void);     /* built-in command */
    gchar *action;             /* custom action: path or command line */
    FmFileInfo *fi;            /* owned; used for system applications */
    gchar *desktop_file;       /* local .desktop path for system apps */
    gchar *section_key;        /* stable key for this header's session state */
    MenuRow *section;          /* owning header row (children only) */
};

G_DEFINE_TYPE(MenuRow, menu_row, G_TYPE_OBJECT)

static void menu_row_finalize(GObject *object)
{
    MenuRow *row = MENU_ROW(object);

    g_free(row->label);
    g_free(row->icon);
    g_free(row->tooltip);
    g_free(row->action);
    g_free(row->desktop_file);
    g_free(row->section_key);
    if (row->fi)
        fm_file_info_unref(row->fi);
    if (row->section)
        g_object_unref(row->section);
    G_OBJECT_CLASS(menu_row_parent_class)->finalize(object);
}

static void menu_row_class_init(MenuRowClass *klass)
{
    G_OBJECT_CLASS(klass)->finalize = menu_row_finalize;
}

static void menu_row_init(MenuRow *row)
{
    (void)row;
}

static MenuRow *menu_row_new(void)
{
    return g_object_new(MENU_TYPE_ROW, NULL);
}

typedef struct
{
    GtkWidget *menu;      /* GtkPopover */
    guint focus_retry;
    guint focus_attempts;
    GtkWidget *box, *img;
    GtkWidget *search;    /* GtkSearchEntry */
    GtkWidget *list;      /* GtkListBox */
    GListStore *store;
    GtkFilterListModel *filter;
    GtkCustomFilter *flt;
    char *fname, *caption;
    int iconsize;
    gboolean has_system_menu;
    guint show_system_menu_idle;
    LuxePanel *panel;
    config_setting_t *settings;

    MenuCache* menu_cache;
    guint visibility_flags;
    gpointer reload_notify;
    LuxeMenuSectionStates *section_states;
} menup;

static GQuark row_qd = 0;
static GQuark section_arrow_qd = 0;

static void
menu_toggle_section(menup *m, MenuRow *row, GtkWidget *roww)
{
    GtkWidget *arrow = NULL;

    row->collapsed = !row->collapsed;
    if (row->section_key != NULL)
        luxe_menu_section_states_set(m->section_states, row->section_key,
                                     row->collapsed);
    if (roww != NULL && section_arrow_qd != 0)
        arrow = g_object_get_qdata(G_OBJECT(roww), section_arrow_qd);
    if (GTK_IS_LABEL(arrow))
        gtk_label_set_text(GTK_LABEL(arrow), row->collapsed ? "▸" : "▾");
    gtk_filter_changed(GTK_FILTER(m->flt), GTK_FILTER_CHANGE_DIFFERENT);
}

/* fm_launch_command_simple lives in libfm-gtk (dropped); minimal local
 * replacement on core libfm + GAppInfo. */
static void show_launch_error(const char* msg)
{
    luxe_show_error(NULL, msg, NULL);
}

static gboolean launch_command(const char* cmd)
{
    GAppInfo* appinfo;
    GAppLaunchContext* ctx;
    GError* error = NULL;
    gboolean ok;

    appinfo = fm_app_info_create_from_commandline(cmd, NULL, G_APP_INFO_CREATE_NONE, &error);
    if( ! appinfo )
    {
        show_launch_error(error ? error->message : _("Command not found"));
        g_clear_error(&error);
        return FALSE;
    }
    ctx = G_APP_LAUNCH_CONTEXT(gdk_display_get_app_launch_context(gdk_display_get_default()));
    ok = fm_app_info_launch(appinfo, NULL, ctx, &error);
    g_object_unref(ctx);
    g_object_unref(appinfo);
    if( ! ok )
    {
        show_launch_error(error ? error->message : _("Failed to launch command"));
        g_clear_error(&error);
    }
    return ok;
}

/* those were in configurator.c initially but it's safer to have those here */
typedef struct {
    char *name;
    char *disp_name;
    void (*cmd)(void);
} Command;

static Command commands[] = {
    { "run", N_("Run"), gtk_run },
    { "restart", N_("Restart"), restart },
    { "logout", N_("Logout"), logout },
    { NULL, NULL },
};

/* replaces fm_link_files() from libfm-gtk: copies the item .desktop file
 * onto the desktop directory */
static void add_to_desktop(FmFileInfo *fi)
{
    GFile *src = fm_path_to_gfile(fm_file_info_get_path(fi));
    GFile *dir = fm_path_to_gfile(fm_path_get_desktop());
    char *base = g_file_get_basename(src);
    GFile *dest = g_file_get_child(dir, base);
    GError *err = NULL;

    if (!g_file_copy(src, dest, G_FILE_COPY_NONE, NULL, NULL, NULL, &err) &&
        !g_error_matches(err, G_IO_ERROR, G_IO_ERROR_EXISTS))
        g_warning("menu: add to desktop: %s", err->message);
    g_clear_error(&err);
    g_object_unref(dest);
    g_free(base);
    g_object_unref(dir);
    g_object_unref(src);
}

static void on_add_to_desktop_clicked(GtkWidget *btn, FmFileInfo *fi)
{
    GtkWidget *pop = gtk_widget_get_ancestor(btn, GTK_TYPE_POPOVER);

    add_to_desktop(fi);
    if (pop)
        gtk_popover_popdown(GTK_POPOVER(pop));
}

/* creates FmFileInfo for the menu-cache item, it will be used in callbacks */
static FmFileInfo *make_fi(MenuCacheItem *item)
{
    char *mpath = menu_cache_dir_make_path(MENU_CACHE_DIR(item));
    FmPath *path = fm_path_new_relative(fm_path_get_apps_menu(), mpath + 13);
                                                    /* skip "/Applications" */
    FmFileInfo *fi = fm_file_info_new_from_menu_cache_item(path, item);

    g_free(mpath);
    fm_path_unref(path);
    return fi;
}

/* appends rows for the directory children into m->store,
 * returns number of visible entries */
static int load_rows(menup *m, MenuCacheDir *dir, MenuRow *section);

/* Keyboard availability depends on configuration, not eager menu loading. */
static gboolean has_system_block(config_setting_t *settings)
{
    config_setting_t *list = config_setting_add(settings, "", PANEL_CONF_TYPE_LIST);
    config_setting_t *child;
    for (guint i = 0; (child = config_setting_get_elem(list, i)) != NULL; i++) {
        const char *name = config_setting_get_name(child);
        if (!g_ascii_strcasecmp(name, "system"))
            return TRUE;
        if (!g_ascii_strcasecmp(name, "menu") && has_system_block(child))
            return TRUE;
    }
    return FALSE;
}

static int load_rows(menup *m, MenuCacheDir *dir, MenuRow *section)
{
    GSList *l;
    gint count = 0;
#if MENU_CACHE_CHECK_VERSION(0, 4, 0)
    GSList *children;
# if MENU_CACHE_CHECK_VERSION(0, 5, 0)
#  if !MENU_CACHE_CHECK_VERSION(1, 0, 0)
    char *kfpath;
    GKeyFile *kf;
#  endif
    if (!menu_cache_dir_is_visible(dir)) /* directory is hidden, ignore children */
        return 0;
#  if !MENU_CACHE_CHECK_VERSION(1, 0, 0)
    /* version 1.0.0 has NoDisplay checked internally */
    kfpath = menu_cache_item_get_file_path(MENU_CACHE_ITEM(dir));
    kf = g_key_file_new();
    /* for version 0.5.0 we enable hidden so should test NoDisplay flag */
    if (kfpath && g_key_file_load_from_file(kf, kfpath, 0, NULL) &&
        g_key_file_get_boolean(kf, "Desktop Entry", "NoDisplay", NULL))
        count = -1;
    g_free(kfpath);
    g_key_file_free(kf);
    if (count < 0) /* directory is hidden, ignore children */
        return 0;
#  endif /* < 1.0.0 */
# endif /* < 0.5.0 */
    children = menu_cache_dir_list_children(dir);
    for (l = children; l; l = l->next)
#else /* < 0.4.0 */
    for( l = menu_cache_dir_get_children(dir); l; l = l->next )
#endif
    {
        MenuCacheItem* item = MENU_CACHE_ITEM(l->data);

        gboolean is_visible = ((menu_cache_item_get_type(item) != MENU_CACHE_TYPE_APP) ||
                               (panel_menu_item_evaluate_visibility(item, m->visibility_flags)));

        if (is_visible)
        {
            if (menu_cache_item_get_type(item) == MENU_CACHE_TYPE_SEP)
            {
                MenuRow *row = menu_row_new();
                row->is_sep = TRUE;
                row->system = TRUE;
                if (section)
                    row->section = g_object_ref(section);
                g_list_store_append(m->store, row);
                g_object_unref(row);
                count++;
            }
            else
            {
                MenuRow *row = menu_row_new();
                row->system = TRUE;
                row->fi = make_fi(item);
                if (section)
                    row->section = g_object_ref(section);
                if (menu_cache_item_get_name(item))
                    row->label = g_strdup(menu_cache_item_get_name(item));
                if (menu_cache_item_get_type(item) == MENU_CACHE_TYPE_APP)
                {
                    char *kfpath = menu_cache_item_get_file_path(item);
                    const char *comment = menu_cache_item_get_comment(item);
                    if (kfpath != NULL)
                        row->desktop_file = kfpath; /* ownership moved */
                    if (comment != NULL)
                        row->tooltip = g_strdup(comment);
                }
                else
                {
                    row->is_header = TRUE;
                    if (menu_cache_item_get_type(item) == MENU_CACHE_TYPE_DIR)
                    {
                        char *path = menu_cache_dir_make_path(MENU_CACHE_DIR(item));

                        row->section_key = g_strdup_printf("system:%s",
                            path ? path : (row->label ? row->label : ""));
                        row->collapsed = luxe_menu_section_states_get(m->section_states,
                                                                        row->section_key);
                        g_free(path);
                    }
                }
                g_list_store_append(m->store, row);
                g_object_unref(row);
                count++;
                /* process subentries */
                if (menu_cache_item_get_type(item) == MENU_CACHE_TYPE_DIR)
                {
                    gint s_count = load_rows(m, MENU_CACHE_DIR(item), row);
                    if (!s_count)
                    {
                        /* don't keep empty submenus */
                        guint pos = g_list_model_get_n_items(G_LIST_MODEL(m->store)) - 1;
                        g_list_store_remove(m->store, pos);
                        count--;
                    }
                }
            }
        }
    }
#if MENU_CACHE_CHECK_VERSION(0, 4, 0)
    g_slist_foreach(children, (GFunc)menu_cache_item_unref, NULL);
    g_slist_free(children);
#endif
    return count;
}

static void on_reload_menu(MenuCache* cache, gpointer user_data);

static void ensure_menu_cache(menup *m)
{
    if (m->menu_cache == NULL)
    {
        guint32 flags;
        m->menu_cache = panel_menu_cache_new(&flags);
        if (m->menu_cache == NULL)
        {
            g_warning("error loading applications menu");
            return;
        }
        m->visibility_flags = flags;
        m->reload_notify = menu_cache_add_reload_notify(m->menu_cache, on_reload_menu, m);
    }
    m->has_system_menu = TRUE;
}

/* parses a "menu" block from settings into rows */
static void parse_menu_block(menup *m, config_setting_t *s, MenuRow *section)
{
    config_setting_t *list, *c;
    const gchar *str;
    guint i;

    list = config_setting_add(s, "", PANEL_CONF_TYPE_LIST);
    for (i = 0; (c = config_setting_get_elem(list, i)) != NULL; i++)
    {
        str = config_setting_get_name(c);
        if (!g_ascii_strcasecmp(str, "item"))
        {
            const gchar *name, *fname, *action, *cmd;
            MenuRow *row = NULL;

            name = fname = action = NULL;
            config_setting_lookup_string(c, "name", &name);
            config_setting_lookup_string(c, "image", &fname);
            config_setting_lookup_string(c, "action", &action);
            if (config_setting_lookup_string(c, "command", &cmd))
            {
                Command *tmp;

                for (tmp = commands; tmp->name; tmp++)
                    if (!g_ascii_strcasecmp(cmd, tmp->name))
                        break;
                if (!tmp->name)
                    continue;
                row = menu_row_new();
                row->builtin = tmp->cmd;
                row->label = g_strdup(_(tmp->disp_name));
            }
            else if (action)
            {
                row = menu_row_new();
                row->action = g_strdup(action);
                row->label = g_strdup(name ? name : "");
            }
            else
                continue;
            if (fname)
                row->icon = expand_tilda(fname);
            if (section)
                row->section = g_object_ref(section);
            g_list_store_append(m->store, row);
            g_object_unref(row);
        }
        else if (!g_ascii_strcasecmp(str, "separator"))
        {
            MenuRow *row = menu_row_new();

            row->is_sep = TRUE;
            if (section)
                row->section = g_object_ref(section);
            g_list_store_append(m->store, row);
            g_object_unref(row);
        }
        else if (!g_ascii_strcasecmp(str, "system"))
        {
            MenuCacheDir *dir;

            ensure_menu_cache(m);
            if (m->menu_cache == NULL)
                continue;
            dir = menu_cache_dup_root_dir(m->menu_cache);
            if (dir)
            {
                load_rows(m, dir, section);
                menu_cache_item_unref(MENU_CACHE_ITEM(dir));
            }
        }
        else if (!g_ascii_strcasecmp(str, "menu"))
        {
            const gchar *name, *fname;
            MenuRow *row;

            name = fname = NULL;
            config_setting_lookup_string(c, "name", &name);
            config_setting_lookup_string(c, "image", &fname);
            /* a header for the submenu content */
            row = menu_row_new();
            row->is_header = TRUE;
            row->label = g_strdup(name ? name : "");
            row->section_key = g_strdup_printf("custom:%s", name ? name : "");
            row->collapsed = luxe_menu_section_states_get(m->section_states,
                                                            row->section_key);
            if (section)
                row->section = g_object_ref(section);
            if (fname)
                row->icon = expand_tilda(fname);
            g_list_store_append(m->store, row);
            g_object_unref(row);
            parse_menu_block(m, c, row);
        }
        else
            g_warning("menu: unknown block %s", str);
    }
}

static void on_reload_menu(MenuCache* cache, gpointer user_data)
{
    menup *m = user_data;
    guint i, n;

    (void)cache;
    if (m->menu != NULL && !gtk_widget_get_visible(m->menu))
        return; /* rows were freed on close; rebuilt on the next open */
    n = g_list_model_get_n_items(G_LIST_MODEL(m->store));

    /* remove previously loaded system rows */
    for (i = n; i > 0; i--)
    {
        MenuRow *row = g_list_model_get_item(G_LIST_MODEL(m->store), i - 1);
        if (row->system)
            g_list_store_remove(m->store, i - 1);
        g_object_unref(row);
    }
    if (m->menu_cache)
    {
        MenuCacheDir *dir = menu_cache_dup_root_dir(m->menu_cache);
        if (dir)
        {
            load_rows(m, dir, NULL);
            menu_cache_item_unref(MENU_CACHE_ITEM(dir));
        }
    }
}

/* the filter: with empty search text everything is visible, otherwise
 * only items (custom or applications) matching the text caseless */
static gboolean menu_row_filter_func(gpointer item, gpointer user_data)
{
    menup *m = user_data;
    MenuRow *row = item;
    const char *text = gtk_editable_get_text(GTK_EDITABLE(m->search));
    char *folded, *folded_text;
    gboolean match = TRUE;

    if (text == NULL || text[0] == '\0')
    {
        /* idle view: hide children of folded sections */
        if (row->section != NULL && row->section->collapsed)
            return FALSE;
        return TRUE;
    }
    if (row->is_header || row->is_sep)
        return FALSE;
    if (row->label == NULL)
        return FALSE;
    folded_text = g_utf8_casefold(text, -1);
    folded = g_utf8_casefold(row->label, -1);
    match = (strstr(folded, folded_text) != NULL);
    g_free(folded);
    g_free(folded_text);
    return match;
}

static void on_search_changed(GtkSearchEntry *entry, menup *m)
{
    (void)entry;
    gtk_filter_changed(GTK_FILTER(m->flt), GTK_FILTER_CHANGE_DIFFERENT);
}

static void on_search_stopped(GtkSearchEntry *entry, menup *m)
{
    (void)entry;
    gtk_popover_popdown(GTK_POPOVER(m->menu));
}

static void row_activate(menup *m, MenuRow *row)
{
    gtk_popover_popdown(GTK_POPOVER(m->menu));
    if (row->builtin)
        row->builtin();
    else if (row->action)
        launch_command(row->action);
    else if (row->desktop_file)
    {
        /* system app: run the launcher itself, not the default handler
         * for the .desktop file (which would just open a file manager) */
        GDesktopAppInfo *dai = g_desktop_app_info_new_from_filename(row->desktop_file);
        GAppLaunchContext *ctx =
            G_APP_LAUNCH_CONTEXT(gdk_display_get_app_launch_context(
                                     gtk_widget_get_display(m->img)));
        GError *err = NULL;

        if (dai != NULL)
        {
            if (!g_app_info_launch(G_APP_INFO(dai), NULL, ctx, &err) && err)
            {
                show_launch_error(err->message);
                g_error_free(err);
            }
            g_object_unref(dai);
        }
        else
            show_launch_error(_("Failed to load launcher"));
        g_object_unref(ctx);
    }
    else if (row->fi)
        luxepanel_launch_path(m->panel, fm_file_info_get_path(row->fi));
}

/* activate the first visible row (Enter pressed in the search box) */
static void activate_first_row(GtkSearchEntry *entry, menup *m)
{
    guint n, i;

    (void)entry;
    n = g_list_model_get_n_items(G_LIST_MODEL(m->filter));
    for (i = 0; i < n; i++)
    {
        gpointer item = g_list_model_get_item(G_LIST_MODEL(m->filter), i);
        MenuRow *row = (MenuRow*)item;
        /* skip headers/separators: they have no launch action (the
         * fi fallback would just open a file manager on them) */
        if (row && (row->builtin || row->action || row->desktop_file))
        {
            row_activate(m, row);
            g_object_unref(item);
            return;
        }
        if (item)
            g_object_unref(item);
    }
}

static void on_row_released(GtkGestureClick *gesture, int n_press,
                            double x, double y, menup *m)
{
    GtkWidget *roww = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
    MenuRow *row = g_object_get_qdata(G_OBJECT(roww), row_qd);
    guint btn = gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture));

    (void)n_press;
    if (row == NULL)
        return;
    if (btn == 1)
        row_activate(m, row);
    else if (btn == 3 && row->fi)
    {
        GtkWidget *pop, *box, *btnw;
        GdkRectangle rect = { x, y, 1, 1 };

        gtk_popover_popdown(GTK_POPOVER(m->menu));
        pop = gtk_popover_new();
        gtk_widget_set_parent(pop, roww);
        gtk_popover_set_pointing_to(GTK_POPOVER(pop), &rect);
        box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        btnw = gtk_button_new_with_label(_("Add to desktop"));
        gtk_widget_add_css_class(btnw, "flat");
        g_signal_connect(btnw, "clicked", G_CALLBACK(on_add_to_desktop_clicked),
                         row->fi);
        gtk_box_append(GTK_BOX(box), btnw);
        gtk_widget_show(btnw);
        gtk_popover_set_child(GTK_POPOVER(pop), box);
        g_signal_connect_swapped(pop, "closed",
                                 G_CALLBACK(gtk_widget_unparent), pop);
        gtk_popover_popup(GTK_POPOVER(pop));
    }
}

/* toggle a section header between collapsed and expanded */
static void on_header_released(GtkGestureClick *gesture, int n_press,
                               double x, double y, menup *m)
{
    GtkWidget *roww = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
    MenuRow *row = g_object_get_qdata(G_OBJECT(roww), row_qd);

    (void)n_press;
    (void)x;
    (void)y;
    if (row == NULL || !row->is_header)
        return;
    if (gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture)) != 1)
        return;
    menu_toggle_section(m, row, roww);
}

/* Header rows are deliberately not GtkListBox-activatable: otherwise a
 * mouse release can reach both this header gesture and row-activated(),
 * toggling a section twice.  Keep keyboard operation explicit instead. */
static gboolean
on_header_key_pressed(GtkEventControllerKey *controller, guint keyval,
                      guint keycode, GdkModifierType state, menup *m)
{
    GtkWidget *roww = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(controller));
    MenuRow *row = g_object_get_qdata(G_OBJECT(roww), row_qd);

    (void)keycode;
    (void)state;
    if (row == NULL || !row->is_header)
        return FALSE;
    if (keyval != GDK_KEY_Return && keyval != GDK_KEY_KP_Enter &&
        keyval != GDK_KEY_space)
        return FALSE;
    menu_toggle_section(m, row, roww);
    return TRUE;
}

/* Headers are not activatable, so GTK's automatic row prelight is not enough.
 * Widget-owned motion controllers give all actionable rows the same visual
 * feedback without selecting them or moving keyboard focus. */
static void menu_row_enter(GtkEventControllerMotion *controller, double x, double y, gpointer data)
{
    (void)x; (void)y; (void)data;
    gtk_widget_add_css_class(gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(controller)), "menu-hover");
}

static void menu_row_leave(GtkEventControllerMotion *controller, gpointer data)
{
    (void)data;
    gtk_widget_remove_css_class(gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(controller)), "menu-hover");
}

/* GtkListBoxCreateWidgetFunc */
static GtkWidget *menu_row_create(gpointer item, gpointer user_data)
{
    menup *m = user_data;
    MenuRow *row = item;
    GtkWidget *roww = gtk_list_box_row_new();
    GtkWidget *content;

    if (row_qd == 0)
        row_qd = g_quark_from_static_string("MenuRowObj");
    g_object_set_qdata_full(G_OBJECT(roww), row_qd, g_object_ref(row),
                            (GDestroyNotify)g_object_unref);
    /* Separators are not keyboard-focusable; everything else is, so
     * Up/Down move through the list (SELECTION_SINGLE on the list). */
    gtk_list_box_row_set_selectable(GTK_LIST_BOX_ROW(roww), !row->is_sep);
    gtk_widget_set_focusable(roww, !row->is_sep);
    gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(roww),
                                     !row->is_sep && !row->is_header);
    if (row->is_sep)
    {
        content = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
        gtk_widget_set_margin_start(content, 6);
        gtk_widget_set_margin_end(content, 6);
        gtk_widget_set_margin_top(content, 3);
        gtk_widget_set_margin_bottom(content, 3);
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(roww), content);
        return roww;
    }
    gtk_widget_add_css_class(roww, "luxe-menu-item");
    GtkEventController *motion = gtk_event_controller_motion_new();
    g_signal_connect(motion, "enter", G_CALLBACK(menu_row_enter), NULL);
    g_signal_connect(motion, "leave", G_CALLBACK(menu_row_leave), NULL);
    gtk_widget_add_controller(roww, motion);
    content = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_margin_start(content, 6);
    gtk_widget_set_margin_end(content, 6);
    gtk_widget_set_margin_top(content, 4);
    gtk_widget_set_margin_bottom(content, 4);
    if (row->icon)
    {
        gtk_box_append(GTK_BOX(content),
                       luxepanel_image_new_for_icon(m->panel, row->icon,
                                                  m->iconsize, NULL));
    }
    else if (row->fi)
    {
        FmIcon *fmi = fm_file_info_get_icon(row->fi);
        if (fmi)
            gtk_box_append(GTK_BOX(content),
                           luxepanel_image_new_for_fm_icon(m->panel, fmi,
                                                         m->iconsize, NULL));
    }
    else if (!row->is_header)
    {
        gtk_box_append(GTK_BOX(content),
                       luxepanel_image_new_for_icon(m->panel,
                                                  "application-x-executable",
                                                  m->iconsize, NULL));
    }
    if (row->label)
    {
        GtkWidget *lbl = gtk_label_new(row->label);

        gtk_label_set_xalign(GTK_LABEL(lbl), 0.0);
        gtk_label_set_ellipsize(GTK_LABEL(lbl), PANGO_ELLIPSIZE_END);
        gtk_widget_set_hexpand(lbl, TRUE);
        if (row->is_header)
        {
            GtkWidget *arrow = gtk_label_new(row->collapsed ? "▸" : "▾");

            gtk_widget_add_css_class(lbl, "heading");
            gtk_widget_set_halign(arrow, GTK_ALIGN_END);
            gtk_widget_set_hexpand(arrow, TRUE);
            gtk_box_append(GTK_BOX(content), lbl);
            gtk_box_append(GTK_BOX(content), arrow);
            if (section_arrow_qd == 0)
                section_arrow_qd = g_quark_from_static_string("MenuSectionArrow");
            g_object_set_qdata(G_OBJECT(roww), section_arrow_qd, arrow);
        }
        else
            gtk_box_append(GTK_BOX(content), lbl);
    }
    if (row->tooltip)
        gtk_widget_set_tooltip_text(roww, row->tooltip);
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(roww), content);
    if (row->is_header)
    {
        GtkGestureClick *click = GTK_GESTURE_CLICK(gtk_gesture_click_new());
        GtkEventController *key = gtk_event_controller_key_new();

        gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), 1);
        g_signal_connect(click, "released", G_CALLBACK(on_header_released), m);
        gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(click),
                                                   GTK_PHASE_CAPTURE);
        gtk_widget_add_controller(roww, GTK_EVENT_CONTROLLER(click));
        g_signal_connect(key, "key-pressed", G_CALLBACK(on_header_key_pressed), m);
        gtk_widget_add_controller(roww, key);
    }
    else if (row->section != NULL || row->fi != NULL || row->action != NULL)
    {
        GtkGestureClick *click = GTK_GESTURE_CLICK(gtk_gesture_click_new());

        gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), 0);
        g_signal_connect(click, "released", G_CALLBACK(on_row_released), m);
        gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(click),
                                                   GTK_PHASE_CAPTURE);
        gtk_widget_add_controller(roww, GTK_EVENT_CONTROLLER(click));
    }
    return roww;
}

static void on_row_activated(GtkListBox *list, GtkListBoxRow *r, menup *m)
{
    MenuRow *row;

    (void)list;
    if (r == NULL)
        return;
    row = g_object_get_qdata(G_OBJECT(r), row_qd);
    if (row == NULL || row->is_sep)
        return;
    row_activate(m, row);
}

/* Down in the search entry moves focus into the result list */
static gboolean on_search_key_pressed(GtkEventControllerKey *ctl, guint keyval,
                                      guint keycode, GdkModifierType state,
                                      menup *m)
{
    (void)ctl; (void)keycode; (void)state;
    if (keyval == GDK_KEY_Down || keyval == GDK_KEY_KP_Down)
    {
        GtkListBoxRow *sel =
            gtk_list_box_get_selected_row(GTK_LIST_BOX(m->list));
        if (sel == NULL)
        {
            int i;
            for (i = 0; (sel = gtk_list_box_get_row_at_index(GTK_LIST_BOX(m->list), i)); i++)
                if (gtk_list_box_row_get_selectable(sel))
                    break;
        }
        if (sel != NULL)
        {
            gtk_list_box_select_row(GTK_LIST_BOX(m->list), sel);
            gtk_widget_grab_focus(GTK_WIDGET(sel));
        }
        return TRUE;
    }
    return FALSE;
}

/* Up on the first row moves focus back to the search entry.
 * Capture phase: the listbox's own Up handling must not run first
 * (it would move row 1 -> row 0 and then look like a row-0 Up). */
static gboolean on_list_key_pressed(GtkEventControllerKey *ctl, guint keyval,
                                    guint keycode, GdkModifierType state,
                                    menup *m)
{
    (void)ctl; (void)keycode; (void)state;
    if (keyval == GDK_KEY_Up || keyval == GDK_KEY_KP_Up)
    {
        GtkListBoxRow *sel =
            gtk_list_box_get_selected_row(GTK_LIST_BOX(m->list));
        if (sel != NULL && gtk_list_box_row_get_index(sel) == 0)
        {
            gtk_widget_grab_focus(m->search);
            return TRUE;
        }
    }
    return FALSE;
}

/* builds the popover with a search entry and the list of items */
static void make_menu(menup *m);

static gboolean menu_has_keyboard(menup *m)
{
    GdkDisplay *display = gtk_widget_get_display(m->menu);
    GdkDevice *keyboard = gdk_seat_get_keyboard(gdk_display_get_default_seat(display));
    return gtk_widget_get_mapped(m->menu) && keyboard != NULL &&
        gdk_display_device_is_grabbed(display, keyboard);
}

static void focus_menu(GtkWidget *popover, menup *m)
{
    if (gtk_widget_get_mapped(popover))
        gtk_widget_grab_focus(m->search);
    if (m->focus_retry && menu_has_keyboard(m)) {
        g_source_remove(m->focus_retry);
        m->focus_retry = 0;
    }
}

static gboolean retry_menu_focus(gpointer data)
{
    menup *m = data;
    if (menu_has_keyboard(m)) {
        m->focus_retry = 0;
        focus_menu(m->menu, m);
        return G_SOURCE_REMOVE;
    }
    if (++m->focus_attempts >= 20) {
        m->focus_retry = 0;
        gtk_popover_popdown(GTK_POPOVER(m->menu));
        g_list_store_remove_all(m->store);
        if(m->img) gtk_widget_remove_css_class(m->img, "menu-open");
        return G_SOURCE_REMOVE;
    }
    /* A failed GDK seat grab can leave GtkPopover visible without input.
     * popup() then does nothing because visibility is already TRUE. Toggle
     * visibility to retry the normal GTK grab; never steal a foreign grab. */
    gtk_popover_popdown(GTK_POPOVER(m->menu));
    gtk_popover_popup(GTK_POPOVER(m->menu));
    return m->focus_retry ? G_SOURCE_CONTINUE : G_SOURCE_REMOVE;
}

/* Rows (MenuRow objects plus their list widgets) live only while the menu
 * is open: freeing them on popover close keeps a long-lived panel from
 * retaining menu-cache data forever.  They are rebuilt on the next open. */
static void on_menu_closed(GtkPopover *pop, menup *m)
{
    (void)pop;
    /* GTK also closes the popup after a failed seat grab. Keep the rows
     * until a pending retry succeeds or expires. */
    if (m->focus_retry)
        return;
    if(m->img) gtk_widget_remove_css_class(m->img, "menu-open");
    if (m->store != NULL)
        g_list_store_remove_all(m->store);
}

/* Drop all rows and re-parse them from config + menu-cache. */
static void menu_rebuild_rows(menup *m)
{
    g_list_store_remove_all(m->store);
    parse_menu_block(m, m->settings, NULL);
}

/* builds the popover with a search entry and the list of items */
static void make_menu(menup *m)
{
    GtkWidget *scroll, *box;
    GtkEventController *ctl;

    m->search = gtk_search_entry_new();
    g_signal_connect(m->search, "changed", G_CALLBACK(on_search_changed), m);
    g_signal_connect(m->search, "stop-search", G_CALLBACK(on_search_stopped), m);
    g_signal_connect(m->search, "activate", G_CALLBACK(activate_first_row), m);
    ctl = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(ctl, GTK_PHASE_CAPTURE);
    g_signal_connect(ctl, "key-pressed", G_CALLBACK(on_search_key_pressed), m);
    gtk_widget_add_controller(m->search, ctl);
    m->store = g_list_store_new(MENU_TYPE_ROW);
    m->flt = GTK_CUSTOM_FILTER(gtk_custom_filter_new(menu_row_filter_func, m, NULL));
    /* The filter model takes ownership; keep our own references for cleanup. */
    m->filter = gtk_filter_list_model_new(G_LIST_MODEL(g_object_ref(m->store)),
                                          GTK_FILTER(g_object_ref(m->flt)));
    m->list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(m->list), GTK_SELECTION_SINGLE);
    ctl = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(ctl, GTK_PHASE_CAPTURE);
    g_signal_connect(ctl, "key-pressed", G_CALLBACK(on_list_key_pressed), m);
    gtk_widget_add_controller(m->list, ctl);
    gtk_list_box_bind_model(GTK_LIST_BOX(m->list), G_LIST_MODEL(m->filter),
                            menu_row_create, m, NULL);
    g_signal_connect(m->list, "row-activated", G_CALLBACK(on_row_activated), m);
    scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scroll), 400);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), m->list);
    box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_margin_top(m->search, 4);
    gtk_widget_set_margin_start(m->search, 4);
    gtk_widget_set_margin_end(m->search, 4);
    gtk_box_append(GTK_BOX(box), m->search);
    gtk_box_append(GTK_BOX(box), scroll);
    gtk_widget_set_size_request(box, 340, -1);
    m->menu = gtk_popover_new();
    gtk_widget_add_css_class(m->menu, "luxe-application-menu");
    /* GtkBox may dispose its children before our data destructor runs. */
    g_object_ref_sink(m->menu);
    gtk_popover_set_child(GTK_POPOVER(m->menu), box);
    g_signal_connect(m->menu, "closed", G_CALLBACK(on_menu_closed), m);
    g_signal_connect(m->menu, "map", G_CALLBACK(focus_menu), m);
    gtk_widget_set_parent(m->menu, m->box);
}

static void menu_destructor(gpointer user_data)
{
    menup *m = (menup *)user_data;

    if (m->focus_retry)
        g_source_remove(m->focus_retry);
    m->focus_retry = 0;
    if (m->show_system_menu_idle)
        g_source_remove(m->show_system_menu_idle);
    if (m->store != NULL)
        g_list_store_remove_all(m->store); /* free rows eagerly */
    if (m->menu != NULL && gtk_widget_get_parent(m->menu) != NULL)
        gtk_widget_unparent(m->menu);
    g_clear_object(&m->menu);
    if (m->filter)
        g_object_unref(m->filter);
    if (m->flt)
        g_object_unref(m->flt);
    if (m->store)
        g_object_unref(m->store);
    luxe_menu_section_states_free(m->section_states);

    if( m->menu_cache )
    {
        menu_cache_remove_reload_notify(m->menu_cache, m->reload_notify);
        menu_cache_unref( m->menu_cache );
    }

    g_free(m->fname);
    g_free(m->caption);
    g_free(m);
}

static void show_menu( GtkWidget* widget, menup* m )
{
    (void)widget;
    if(m->img) gtk_widget_add_css_class(m->img, "menu-open");
    menu_rebuild_rows(m); /* rows were freed on close; rebuild for this open */
    gtk_editable_set_text(GTK_EDITABLE(m->search), "");
    gtk_popover_popup(GTK_POPOVER(m->menu));
    focus_menu(m->menu, m); /* Also recover focus for an already-open popup. */
    if (!menu_has_keyboard(m) && m->focus_retry == 0) {
        m->focus_attempts = 0;
        m->focus_retry = g_timeout_add(100, retry_menu_focus, m);
    }
}

static gboolean
menu_button_press_event(GtkWidget * widget, GdkEvent * event, LuxePanel * panel)
{
    ENTER;

    (void)panel;
    if (gdk_event_get_event_type(event) == GDK_BUTTON_PRESS &&
        gdk_button_event_get_button(event) == 1)
    {
        show_menu( widget, luxepanel_plugin_get_data(widget) );
        RET(TRUE);
    }
    RET(FALSE);
}

static void reveal_slab_ready(GObject *object, GAsyncResult *result, gpointer data)
{
    (void)object; (void)data;
    GDBusConnection *bus = g_bus_get_finish(result, NULL);
    if (!bus) return;
    /* Optional running dock only: never D-Bus activate/start slab. */
    g_dbus_connection_call(bus, "org.luxe.Slab", "/org/luxe/Slab", "org.gtk.Actions", "Activate",
        g_variant_new("(sava{sv})", "reveal", NULL, NULL), NULL,
        G_DBUS_CALL_FLAGS_NO_AUTO_START, 1000, NULL, NULL, NULL);
    g_object_unref(bus);
}
static gboolean show_system_menu_idle(gpointer user_data)
{
    menup* m = (menup*)user_data;
    if (g_source_is_destroyed(g_main_current_source()))
        return FALSE;
    show_menu( m->box, m );
    g_bus_get(G_BUS_TYPE_SESSION, NULL, reveal_slab_ready, NULL);
    m->show_system_menu_idle = 0;
    return FALSE;
}

static void show_system_menu(GtkWidget *p)
{
    menup *m = luxepanel_plugin_get_data(p);

    if (m->has_system_menu && m->show_system_menu_idle == 0)
        /* FIXME: I've no idea why this doesn't work without timeout
                              under some WMs, like icewm. */
        m->show_system_menu_idle = g_timeout_add(200, show_system_menu_idle, m);
}

static void wordmark_enter(GtkEventControllerMotion *controller, double x, double y, gpointer data)
{
    (void)controller; (void)x; (void)y;
    menup *m = data;
    if(m->img) gtk_widget_add_css_class(m->img, "menu-hover");
}

static void wordmark_leave(GtkEventControllerMotion *controller, gpointer data)
{
    (void)controller;
    menup *m = data;
    if(m->img) gtk_widget_remove_css_class(m->img, "menu-hover");
}

static void
make_button(menup *m, const gchar *fname, const gchar *name, GdkRGBA *tint)
{
    char *title = NULL;
    if (name && g_str_has_suffix(name, ".directory")) {
        GKeyFile *kf = g_key_file_new();
        char *path = g_build_filename("desktop-directories", name, NULL);
        if (g_key_file_load_from_data_dirs(kf, path, NULL, 0, NULL))
            title = g_key_file_get_locale_string(kf, "Desktop Entry", "Name", NULL, NULL);
        g_free(path); g_key_file_free(kf);
    }
    const char *button_style = "auto";
    config_setting_lookup_string(m->settings, "button-style", &button_style);
    gboolean wordmark = g_strcmp0(button_style, "wordmark") == 0 ||
        (g_strcmp0(button_style, "image") != 0 &&
         (g_strcmp0(fname, DEFAULT_MENU_ICON) == 0 || g_strcmp0(fname, LIGHT_MENU_ICON) == 0));
    /* Custom images remain unchanged. Explicit image mode retains the old
     * paired stock assets; auto uses native text instead of a logo bitmap. */
    if (!wordmark && g_strcmp0(fname, DEFAULT_MENU_ICON) == 0)
    {
        gboolean dark = FALSE;
        g_autofree char *theme = NULL;
        g_object_get(gtk_settings_get_default(),
                     "gtk-application-prefer-dark-theme", &dark,
                     "gtk-theme-name", &theme, NULL);
        /* Fixed-mode global palettes leave prefer-dark unset for libadwaita. */
        if(theme && g_str_has_prefix(theme, "Luxe-"))
            dark = g_strcmp0(theme, "Luxe-light") != 0;
        if (!dark)
            fname = LIGHT_MENU_ICON;
    }

    if (m->img) {
        gtk_box_remove(GTK_BOX(m->box), m->img);
        m->img = NULL;
    }
    /* A menu logo can be a wide image, not a square themed icon. Scale
     * against panel thickness and let its natural aspect set button width. */
    GdkPixbuf *pixbuf = NULL;
    int size = MAX(1, panel_get_icon_size(m->panel));
    gboolean horizontal = panel_get_orientation(m->panel) == GTK_ORIENTATION_HORIZONTAL;
    if (!wordmark && fname && g_path_is_absolute(fname))
        pixbuf = gdk_pixbuf_new_from_file_at_scale(fname, horizontal ? -1 : size,
                                                  horizontal ? size : -1, TRUE, NULL);
    if(wordmark) {
        m->img = gtk_button_new_with_label("luxe");
        /* Pointer routing stays with the existing plugin/panel controllers.
         * AT activation can still invoke the native button's clicked action. */
        gtk_widget_set_can_target(m->img, FALSE);
        gtk_widget_set_focusable(m->img, FALSE);
        gtk_widget_add_css_class(m->img, "luxe-menu-wordmark");
        if(!horizontal) gtk_widget_add_css_class(m->img, "vertical");
        gtk_widget_set_tooltip_text(m->img, _("Application Menu"));
        gtk_accessible_update_property(GTK_ACCESSIBLE(m->img),
            GTK_ACCESSIBLE_PROPERTY_LABEL, _("Application Menu"), -1);
        g_signal_connect(m->img, "clicked", G_CALLBACK(show_menu), m);
        if(gtk_widget_get_visible(m->menu)) gtk_widget_add_css_class(m->img, "menu-open");
    } else if (pixbuf) {
        m->img = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
        GtkWidget *picture = gtk_picture_new_for_pixbuf(pixbuf);
        gtk_picture_set_can_shrink(GTK_PICTURE(picture), FALSE);
        gtk_widget_set_size_request(picture, gdk_pixbuf_get_width(pixbuf), gdk_pixbuf_get_height(pixbuf));
        gtk_widget_set_halign(picture, GTK_ALIGN_CENTER);
        gtk_widget_set_valign(picture, GTK_ALIGN_CENTER);
        gtk_box_append(GTK_BOX(m->img), picture);
        if (title || name) gtk_box_append(GTK_BOX(m->img), gtk_label_new(title ? title : name));
        gtk_widget_set_tooltip_text(m->img, _("Application Menu"));
        g_object_unref(pixbuf);
    } else
        m->img = luxepanel_button_new_for_icon(m->panel, fname, tint, title ? title : name);
    gtk_box_append(GTK_BOX(m->box), m->img);
    g_free(title);
}

static void menu_reconfigure(LuxePanel *panel, GtkWidget *widget)
{
    menup *m = luxepanel_plugin_get_data(widget);
    GdkRGBA tint = { 0.0, 0.0, 36.0 / 255.0, 96.0 / 255.0 };
    const char *color;
    (void)panel;
    if (config_setting_lookup_string(m->settings, "tintcolor", &color))
        gdk_rgba_parse(&tint, color);
    make_button(m, m->fname, m->caption, &tint);
}

static gboolean apply_config(gpointer user_data)
{
    GtkWidget *p = user_data;
    menup* m = luxepanel_plugin_get_data(p);

    menu_reconfigure(m->panel, p);
    config_group_set_string(m->settings, "image", m->fname);
    /* config_group_set_int(m->settings, "panelSize", m->match_panel); */
    config_group_set_string(m->settings, "name", m->caption);
    return FALSE;
}

static GtkWidget *menu_config(LuxePanel *panel, GtkWidget *p)
{
    menup* menu = luxepanel_plugin_get_data(p);
    return luxepanel_generic_config_dlg(_("Menu"), panel, apply_config, p,
                                      _("Icon"), &menu->fname, CONF_TYPE_FILE_ENTRY,
                                      /* _("Use panel size as icon size"), &menu->match_panel, CONF_TYPE_INT, */
                                      /* _("Caption"), &menu->caption, CONF_TYPE_STR, */
                                      NULL);
}

static GtkWidget *
menu_constructor(LuxePanel *panel, config_setting_t *settings)
{
    menup *m;
    config_setting_t *s;

    m = g_new0(menup, 1);
    g_return_val_if_fail(m != NULL, 0);

    /* menu list icons follow the panel icon size (which follows the panel
     * height) instead of the fixed 16px menu size, so a taller panel
     * gives bigger, more readable icons */
    m->iconsize = panel_get_icon_size(panel);

    m->box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    GtkEventController *motion = gtk_event_controller_motion_new();
    g_signal_connect(motion, "enter", G_CALLBACK(wordmark_enter), m);
    g_signal_connect(motion, "leave", G_CALLBACK(wordmark_leave), m);
    gtk_widget_add_controller(m->box, motion);
    luxepanel_plugin_set_data(m->box, m, menu_destructor);
    m->section_states = luxe_menu_section_states_new();

    /* Save construction pointers */
    m->panel = panel;
    m->settings = settings;

    /* Check if configuration exists */
    settings = config_setting_add(settings, "", PANEL_CONF_TYPE_LIST);
    if (config_setting_get_elem(settings, 0) == NULL)
    {
        /* create default menu */
        config_setting_add(settings, "system", PANEL_CONF_TYPE_GROUP);
        config_setting_add(settings, "separator", PANEL_CONF_TYPE_GROUP);
        s = config_setting_add(settings, "item", PANEL_CONF_TYPE_GROUP);
            config_group_set_string(s, "command", "run");
        config_setting_add(settings, "separator", PANEL_CONF_TYPE_GROUP);
        s = config_setting_add(settings, "item", PANEL_CONF_TYPE_GROUP);
            config_group_set_string(s, "command", "logout");
            config_group_set_string(s, "image", "gnome-logout");
        config_group_set_string(m->settings, "image", DEFAULT_MENU_ICON);
    }

    /* Rows are built at first open and freed on close. Do not retain the
     * complete menu from login until the first open/close cycle. */
    make_menu(m);
    m->has_system_menu = has_system_block(m->settings);
    {
        const gchar *name = NULL, *fname = NULL, *str;
        GdkRGBA color = { 0.0, 0.0, 36.0 / 255.0, 96.0 / 255.0 };

        config_setting_lookup_string(m->settings, "name", &name);
        config_setting_lookup_string(m->settings, "image", &fname);
        if (config_setting_lookup_string(m->settings, "tintcolor", &str))
            gdk_rgba_parse(&color, str);
        g_free(m->fname);
        m->fname = fname ? expand_tilda(fname) : g_strdup(DEFAULT_MENU_ICON);
        g_free(m->caption);
        m->caption = g_strdup(name);
        make_button(m, m->fname, name, &color);
    }

    /* FIXME: allow bind a global key to toggle menu using libkeybinder */
    return m->box;
}

LuxePanelPluginInit luxepanel_static_plugin_menu = {
    .name = N_("Menu"),
    .description = N_("Application Menu"),

    .new_instance = menu_constructor,
    .config = menu_config,
    .reconfigure = menu_reconfigure,
    .button_press_event = menu_button_press_event,
    .show_system_menu = show_system_menu
};

/* vim: set sw=4 et sts=4 : */
