/*
 * Copyright (C) 2016 Andriy Grytsenko <andrej@rep.kiev.ua>
 * Copyright (C) 2026 Jerry Howell
 *
 * This file is a part of LXHotkey project.
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
# include "config.h"
#endif

#define WANT_OPTIONS_EQUAL

#include "luxehotkey.h"
#include "edit.h"
#include "luxe-dialogs-gtk.h"

#include <stdlib.h>
#include <glib/gi18n-lib.h>
#include <gtk/gtk.h>
#include "luxe-gtk-ui.h"
#include <gdk/gdkkeysyms.h>

/* defined below, needed by the two-phase commit in on_save_btn() */
static GList *copy_options(GList *orig);
static void option_free(LuxeHotkeyAttr *attr);

/* TRUE if either new accelerator reuses a key currently bound to the entry
   being edited. Such keys cannot be staged before the old binding is gone. */
static gboolean accel_reuses_old_key(const char *new1, const char *new2,
                                     const char *old1, const char *old2)
{
    if (new1 && new1[0] &&
        (g_strcmp0(new1, old1) == 0 || g_strcmp0(new1, old2) == 0))
        return TRUE;
    if (new2 && new2[0] &&
        (g_strcmp0(new2, old1) == 0 || g_strcmp0(new2, old2) == 0))
        return TRUE;
    return FALSE;
}

typedef gboolean (*ApplyBinding)(PluginData *, gpointer, gboolean, GError **);
typedef gpointer (*CopyBinding)(gconstpointer);
typedef void (*FreeBinding)(gpointer);

static gboolean apply_global_binding(PluginData *data, gpointer record,
                                     gboolean remove, GError **error)
{
    LuxeHotkeyGlobal request = *(LuxeHotkeyGlobal *)record;
    if (remove) request.accel1 = request.accel2 = NULL;
    return data->cb->set_wm_key(*data->config, &request, error);
}

static gboolean apply_app_binding(PluginData *data, gpointer record,
                                  gboolean remove, GError **error)
{
    LuxeHotkeyApp request = *(LuxeHotkeyApp *)record;
    if (remove) request.accel1 = request.accel2 = NULL;
    return data->cb->set_app_key(*data->config, &request, error);
}

static gpointer copy_global_binding(gconstpointer record)
{
    const LuxeHotkeyGlobal *source = record;
    LuxeHotkeyGlobal *copy = g_new0(LuxeHotkeyGlobal, 1);
    *copy = *source;
    copy->actions = copy_options(source->actions);
    copy->accel1 = g_strdup(source->accel1);
    copy->accel2 = g_strdup(source->accel2);
    copy->data1 = copy->data2 = NULL;
    return copy;
}

static gpointer copy_app_binding(gconstpointer record)
{
    const LuxeHotkeyApp *source = record;
    LuxeHotkeyApp *copy = g_new0(LuxeHotkeyApp, 1);
    *copy = *source;
    copy->options = copy_options(source->options);
    copy->exec = g_strdup(source->exec);
    copy->accel1 = g_strdup(source->accel1);
    copy->accel2 = g_strdup(source->accel2);
    copy->data1 = copy->data2 = NULL;
    return copy;
}

static void free_global_binding(gpointer record)
{
    LuxeHotkeyGlobal *copy = record;
    g_list_free_full(copy->actions, (GDestroyNotify)option_free);
    g_free(copy->accel1);
    g_free(copy->accel2);
    g_free(copy);
}

static void free_app_binding(gpointer record)
{
    LuxeHotkeyApp *copy = record;
    g_list_free_full(copy->options, (GDestroyNotify)option_free);
    g_free(copy->exec);
    g_free(copy->accel1);
    g_free(copy->accel2);
    g_free(copy);
}

/* Preserve the old binding across a failed replacement. New keys are staged
 * first when possible; reused keys require a snapshot and rollback. */
static gboolean replace_binding(PluginData *data, gpointer old_record,
                                gpointer new_record, const char *old1,
                                const char *old2, const char *new1,
                                const char *new2, ApplyBinding apply,
                                CopyBinding copy, FreeBinding release,
                                GError **error)
{
    if (!old_record)
        return apply(data, new_record, FALSE, error);
    if (!accel_reuses_old_key(new1, new2, old1, old2)) {
        if (!apply(data, new_record, FALSE, error))
            return FALSE;
        return apply(data, old_record, TRUE, error);
    }

    gpointer saved = copy(old_record);
    if (!apply(data, old_record, TRUE, error)) {
        release(saved);
        return FALSE;
    }
    if (apply(data, new_record, FALSE, error)) {
        release(saved);
        return TRUE;
    }

    GError *restore_error = NULL;
    if (!apply(data, saved, FALSE, &restore_error)) {
        if (error && *error)
            g_prefix_error(error, _("(old binding could not be restored: %s) "),
                           restore_error ? restore_error->message : _("unknown error"));
        else if (error)
            *error = restore_error;
        else
            g_clear_error(&restore_error);
    }
    g_clear_error(&restore_error);
    release(saved);
    return FALSE;
}

enum {
    EDIT_MODE_NONE,
    EDIT_MODE_ADD, /* add action */
    EDIT_MODE_EDIT, /* change selected */
    EDIT_MODE_OPTION /* add suboption */
};

static const LuxeHotkeyAttr *find_template_for_option(GtkTreeModel *model,
                                                    GtkTreeIter *iter,
                                                    const GList *t_list)
{
    const LuxeHotkeyAttr *opt, *tmpl;

    gtk_tree_model_get(model, iter, 2, &opt, -1);
    while (t_list)
    {
        tmpl = t_list->data;
        if (g_strcmp0(tmpl->name, opt->name) == 0)
            return tmpl;
        t_list = t_list->next;
    }
    return NULL;
}

static const GList *get_options_from_template(const LuxeHotkeyAttr *tmpl, PluginData *data)
{
    if (tmpl == NULL)
        return NULL;
    else if (tmpl->has_actions)
        return data->edit_template;
    else
        return tmpl->subopts;
}

static const GList *get_parent_template_list(GtkTreeModel *model, GtkTreeIter *iter,
                                             PluginData *data)
{
    const GList *tmpl_list;
    GtkTreeIter parent_iter;
    const LuxeHotkeyAttr *parent;

    if (!gtk_tree_model_iter_parent(model, &parent_iter, iter))
        return data->edit_template;
    /* get list for parent of parent first - recursion is here */
    tmpl_list = get_parent_template_list(model, &parent_iter, data);
    /* now find parent in that list */
    parent = find_template_for_option(model, &parent_iter, tmpl_list);
    return get_options_from_template(parent, data);
}

static void fill_edit_frame(PluginData *data, const LuxeHotkeyAttr *opt,
                            const GList *subopts, const GList *exempt)
{
    GtkListStore *names_store;
    const LuxeHotkeyAttr *sub;
    const GList *l;
    int i = 0;

    names_store = GTK_LIST_STORE(gtk_list_store_new(3, G_TYPE_STRING, /* description */
                                                       G_TYPE_STRING, /* name */
                                                       G_TYPE_POINTER)); /* template */
    while (subopts)
    {
        sub = subopts->data;
        /* ignore existing opts */
        for (l = exempt; l; l = l->next)
            if (strcmp(sub->name, ((LuxeHotkeyAttr *)l->data)->name) == 0)
                break;
        if (l == NULL)
            gtk_list_store_insert_with_values(names_store, NULL, i++, 0, _(sub->name),
                                                                      1, sub->name,
                                                                      2, sub, -1);
        subopts = subopts->next;
    }
    gtk_combo_box_set_model(data->edit_actions, GTK_TREE_MODEL(names_store));
    g_object_unref(names_store);
    gtk_combo_box_set_active(data->edit_actions, 0);
    /* values box will be set by changing active callback */
    gtk_widget_set_visible(GTK_WIDGET(data->edit_actions), opt == NULL); /* visible on add */
    gtk_widget_set_visible(data->edit_option_name, opt != NULL); /* visible on edit */
    if (opt)
        gtk_label_set_text(GTK_LABEL(data->edit_option_name), _(opt->name));
}

static void update_edit_toolbar(PluginData *data)
{
    const LuxeHotkeyAttr *opt, *tmpl;
    const GList *tmpl_list;
    GtkTreeModel *model;
    GtkTreeIter iter;
    gboolean is_action;

    /* Bail on mid-dispose calls: dialog teardown emits selection
     * "changed" after sibling toolbar buttons may be finalized. */
    if (!GTK_IS_WIDGET(data->rm_option_button) ||
        !GTK_IS_WIDGET(data->edit_option_button) ||
        !GTK_IS_WIDGET(data->add_suboption_button) ||
        !GTK_IS_WIDGET(data->edit_tree))
        return;
    /* update AddOption button -- exec only */
    if (gtk_widget_get_visible(data->add_option_button))
    {
        /* make not clickable if no more options to add */
        gtk_widget_set_sensitive(data->add_option_button,
                g_list_length((GList *)data->edit_template) != g_list_length(data->edit_options_copy));
    }
    /* update Remove button */
    if (!gtk_tree_selection_get_selected(gtk_tree_view_get_selection(data->edit_tree),
                                         &model, &iter))
    {
        gtk_widget_set_sensitive(data->rm_option_button, FALSE);
        gtk_widget_set_sensitive(data->edit_option_button, FALSE);
        gtk_widget_set_sensitive(data->add_suboption_button, FALSE);
        return;
    }
    gtk_widget_set_sensitive(data->rm_option_button, TRUE);
    gtk_tree_model_get(model, &iter, 2, &opt, -1);
    tmpl_list = get_parent_template_list(model, &iter, data);
    is_action = (data->current_page == data->acts && tmpl_list == data->edit_template);
    while (tmpl_list)
    {
        tmpl = tmpl_list->data;
        if (g_strcmp0(tmpl->name, opt->name) == 0)
            break;
        tmpl_list = tmpl_list->next;
    }
    if (G_UNLIKELY(tmpl_list == NULL))
    {
        /* option isn't supported, probably deprecated one */
        gtk_widget_set_sensitive(data->edit_option_button, FALSE);
        gtk_widget_set_sensitive(data->add_suboption_button, FALSE);
        return;
    }
    gtk_widget_set_sensitive(data->edit_option_button,
                             !is_action && (tmpl->subopts == NULL || tmpl->has_value));
    gtk_widget_set_sensitive(data->add_suboption_button,
                             (tmpl->has_actions ||
                              g_list_length(tmpl->subopts) != g_list_length(opt->subopts)));
}

/* main-window Save action lives in gtk.c; enable it from the edit
 * dialog after a successful apply. The widget guard is load-bearing:
 * the main window may already be mid-dispose when edit paths run. */
static void set_action_enabled_simple(PluginData *data, gboolean enabled)
{
    if(data->save_action)
        g_simple_action_set_enabled(data->save_action, enabled);
    if(data->save_button && GTK_IS_WIDGET(data->save_button))
        gtk_widget_set_sensitive(data->save_button, enabled);
}

static void on_cancel_btn(GtkButton *button, PluginData *data)
{
    (void)button;
    /* Discard the edit dialog without saving (options copy is freed
     * by the caller paths via _edit_cleanup on next edit). */
    _edit_cleanup(data);
}

static void on_save_btn(GtkButton *button, PluginData *data)
{
    GError *error = NULL;
    LuxeHotkeyGlobal *act = NULL;
    LuxeHotkeyApp *app = NULL;
    GtkTreeModel *model;
    GtkTreeIter iter;
    LuxeHotkeyGlobal new_act;
    LuxeHotkeyApp new_app;
    gboolean ok = FALSE;
    (void)button;

    if (data->current_page == data->acts)
    {
        /* it's global */
        new_act.accel1 = g_object_get_data(G_OBJECT(data->edit_key1), "accelerator_name");
        new_act.accel2 = g_object_get_data(G_OBJECT(data->edit_key2), "accelerator_name");
        if (new_act.accel1 == NULL || new_act.accel1[0] == 0)
        {
            new_act.accel1 = new_act.accel2;
            new_act.accel2 = NULL;
        }
        new_act.actions = data->edit_options_copy;
        if (gtk_tree_selection_get_selected(gtk_tree_view_get_selection(data->acts),
                                            &model, &iter))
            gtk_tree_model_get(model, &iter, 4, &act, -1);
        if (act)
        {
            if (!options_equal(act->actions, new_act.actions))
            {
                if (new_act.accel1 == NULL || new_act.accel1[0] == '\0')
                {
                    g_set_error_literal(&error,
                                        g_quark_from_static_string("luxehotkey-edit"),
                                        0, _("At least one hotkey must be specified."));
                    goto _exit;
                }
                ok = replace_binding(data, act, &new_act,
                    act->accel1, act->accel2, new_act.accel1, new_act.accel2,
                    apply_global_binding, copy_global_binding,
                    free_global_binding, &error);
                goto _exit;
            }
            else if (g_strcmp0(act->accel1, new_act.accel1) == 0 &&
                     g_strcmp0(act->accel2, new_act.accel2) == 0)
                /* nothing was changed */
                goto _exit;
        }
        /* else it was added */
        ok = data->cb->set_wm_key(*data->config, &new_act, &error);
    }
    else
    {
        /* it's application */
        new_app.accel1 = g_object_get_data(G_OBJECT(data->edit_key1), "accelerator_name");
        new_app.accel2 = g_object_get_data(G_OBJECT(data->edit_key2), "accelerator_name");
        if (new_app.accel1 == NULL || new_app.accel1[0] == 0)
        {
            new_app.accel1 = new_app.accel2;
            new_app.accel2 = NULL;
        }
        new_app.exec = (char *)gtk_editable_get_text(GTK_EDITABLE(data->edit_exec));
        new_app.options = data->edit_options_copy;
        if (gtk_tree_selection_get_selected(gtk_tree_view_get_selection(data->apps),
                                            &model, &iter))
            gtk_tree_model_get(model, &iter, 3, &app, -1);
        if (app)
        {
            /* app edited */
            if (g_strcmp0(app->exec, new_app.exec) != 0 ||
                !options_equal(app->options, new_app.options))
            {
                if (new_app.accel1 == NULL || new_app.accel1[0] == '\0')
                {
                    g_set_error_literal(&error,
                                        g_quark_from_static_string("luxehotkey-edit"),
                                        0, _("At least one hotkey must be specified."));
                    goto _exit;
                }
                ok = replace_binding(data, app, &new_app,
                    app->accel1, app->accel2, new_app.accel1, new_app.accel2,
                    apply_app_binding, copy_app_binding, free_app_binding,
                    &error);
                goto _exit;
            }
            else if (g_strcmp0(app->accel1, new_app.accel1) == 0 &&
                     g_strcmp0(app->accel2, new_app.accel2) == 0)
                /* nothing was changed */
                goto _exit;
        }
        /* else it was added */
        ok = data->cb->set_app_key(*data->config, &new_app, &error);
    }

_exit:
    if (error)
    {
        _show_error(_("Apply error: "), error);
        g_error_free(error);
    }
    /* update main window */
    if (ok)
        set_action_enabled_simple(data, TRUE);
    _edit_cleanup(data);
    /* Rollback recreates entries too: refresh stale model pointers on errors. */
    _main_refresh(data);
}

static void on_add_action_btn(GtkButton *button, PluginData *data)
{
    (void)button;
    /* fill frame with empty data, set choices from data->edit_template, hide value */
    data->edit_mode = EDIT_MODE_ADD;
    gtk_frame_set_label(GTK_FRAME(data->edit_frame), _("Add action"));
    fill_edit_frame(data, NULL, data->edit_template, NULL);
    gtk_widget_set_visible(GTK_WIDGET(data->edit_values), FALSE);
    gtk_widget_set_visible(GTK_WIDGET(data->edit_value), FALSE);
    gtk_widget_set_visible(data->edit_frame, TRUE);
    gtk_widget_grab_focus(data->edit_frame);
}

static void on_add_option_btn(GtkButton *button, PluginData *data)
{
    (void)button;
    /* fill frame with empty data, set choices from data->edit_template */
    data->edit_mode = EDIT_MODE_ADD;
    gtk_frame_set_label(GTK_FRAME(data->edit_frame), _("Add option"));
    fill_edit_frame(data, NULL, data->edit_template, data->edit_options_copy);
    gtk_widget_set_visible(data->edit_frame, TRUE);
    gtk_widget_grab_focus(data->edit_frame);
}

#define free_options(acts) g_list_free_full(acts, (GDestroyNotify)option_free)
static void option_free(LuxeHotkeyAttr *attr)
{
    g_free(attr->name);
    g_list_free_full(attr->values, g_free);
    free_options(attr->subopts);
    g_free(attr->desc);
    g_slice_free(LuxeHotkeyAttr, attr);
}

static void on_remove_btn(GtkButton *button, PluginData *data)
{
    LuxeHotkeyAttr *opt, *parent;
    GtkTreeModel *model;
    GtkTreeIter iter, parent_iter;
    (void)button;

    if (!gtk_tree_selection_get_selected(gtk_tree_view_get_selection(data->edit_tree),
                                         &model, &iter))
        /* no item selected */
        return;
    /* find and remove option from data->edit_options_copy */
    gtk_tree_model_get(model, &iter, 2, &opt, -1);
    if (gtk_tree_model_iter_parent(model, &parent_iter, &iter))
    {
        gtk_tree_model_get(model, &parent_iter, 2, &parent, -1);
        parent->subopts = g_list_remove(parent->subopts, opt);
    }
    else
        data->edit_options_copy = g_list_remove(data->edit_options_copy, opt);
    option_free(opt);
    /* remove selected row from model */
    gtk_tree_store_remove(GTK_TREE_STORE(model), &iter);
    gtk_widget_set_sensitive(data->edit_apply_button, TRUE);
}

static void start_edit(GtkTreeModel *model, GtkTreeIter *iter, PluginData *data)
{
    const LuxeHotkeyAttr *opt;
    const GList *tmpl_list;
    GList single = { .prev = NULL, .next = NULL };

    /* name - only current from selection */
    gtk_tree_model_get(model, iter, 2, &opt, -1);
    /* values - from template */
    tmpl_list = get_parent_template_list(model, iter, data);
    if (data->current_page == data->acts &&
        tmpl_list == data->edit_template) /* it's action */
        return;
    single.data = (gpointer)find_template_for_option(model, iter, tmpl_list);
    if (single.data == NULL)
    {
        g_warning("no template found for option '%s'", opt->name);
        return;
    }
    /* fill frame from selection */
    data->edit_mode = EDIT_MODE_EDIT;
    gtk_frame_set_label(GTK_FRAME(data->edit_frame), _("Change option"));
    fill_edit_frame(data, opt, &single, NULL);
    gtk_widget_set_visible(data->edit_frame, TRUE);
    gtk_widget_grab_focus(data->edit_frame);
}

static void on_edit_btn(GtkButton *button, PluginData *data)
{
    GtkTreeModel *model;
    GtkTreeIter iter;
    (void)button;

    if (!gtk_tree_selection_get_selected(gtk_tree_view_get_selection(data->edit_tree),
                                         &model, &iter))
        /* no item selected */
        return;
    start_edit(model, &iter, data);
}

static void on_add_suboption_btn(GtkButton *button, PluginData *data)
{
    const LuxeHotkeyAttr *opt, *tmpl;
    const GList *tmpl_list;
    GtkTreeModel *model;
    GtkTreeIter iter;
    (void)button;

    if (!gtk_tree_selection_get_selected(gtk_tree_view_get_selection(data->edit_tree),
                                         &model, &iter))
        /* no item selected */
        return;

    tmpl_list = get_parent_template_list(model, &iter, data);
    tmpl = find_template_for_option(model, &iter, tmpl_list);
    if (tmpl == NULL)
        /* no options found */
        return;
    tmpl_list = get_options_from_template(tmpl, data);
    gtk_tree_model_get(model, &iter, 2, &opt, -1);
    data->edit_mode = EDIT_MODE_OPTION;
    /* fill frame with empty data and set name choices from selection's subopts */
    if (tmpl->has_actions)
        gtk_frame_set_label(GTK_FRAME(data->edit_frame), _("Add action"));
    else
        gtk_frame_set_label(GTK_FRAME(data->edit_frame), _("Add option"));
    fill_edit_frame(data, NULL, tmpl_list, opt->subopts);
    gtk_widget_set_visible(data->edit_frame, TRUE);
    gtk_widget_grab_focus(data->edit_frame);
}

/* Key capture button (taken from LuxePanel key Button, adapted to GTK4):
 * the button shows the current hotkey; while focused, typed keys become
 * the new hotkey. GtkEventControllerKey replaces the GTK2 focus/key
 * events + gdk_keyboard_grab: a focused widget receives its keys
 * without any grab. */
static gboolean is_modifier_keyval(guint keyval)
{
    switch (keyval)
    {
    case GDK_KEY_Shift_L: case GDK_KEY_Shift_R:
    case GDK_KEY_Control_L: case GDK_KEY_Control_R:
    case GDK_KEY_Alt_L: case GDK_KEY_Alt_R:
    case GDK_KEY_Meta_L: case GDK_KEY_Meta_R:
    case GDK_KEY_Super_L: case GDK_KEY_Super_R:
    case GDK_KEY_Hyper_L: case GDK_KEY_Hyper_R:
    case GDK_KEY_ISO_Level3_Shift:
        return TRUE;
    default:
        return FALSE;
    }
}

static GdkModifierType modifier_mask(guint keyval)
{
    switch (keyval) {
    case GDK_KEY_Shift_L: case GDK_KEY_Shift_R: return GDK_SHIFT_MASK;
    case GDK_KEY_Control_L: case GDK_KEY_Control_R: return GDK_CONTROL_MASK;
    case GDK_KEY_Alt_L: case GDK_KEY_Alt_R: return GDK_ALT_MASK;
    case GDK_KEY_Meta_L: case GDK_KEY_Meta_R: return GDK_META_MASK;
    case GDK_KEY_Super_L: case GDK_KEY_Super_R: return GDK_SUPER_MASK;
    case GDK_KEY_Hyper_L: case GDK_KEY_Hyper_R: return GDK_HYPER_MASK;
    default: return 0;
    }
}

static void update_apply_from_keys(PluginData *data)
{
    const char *label;

    if ((((label = g_object_get_data(G_OBJECT(data->edit_key1), "accelerator_name")) == NULL) || label[0] == 0) &&
        (((label = g_object_get_data(G_OBJECT(data->edit_key2), "accelerator_name")) == NULL) || label[0] == 0))
        gtk_widget_set_sensitive(data->edit_apply_button, FALSE);
    else
        gtk_widget_set_sensitive(data->edit_apply_button, TRUE);
}

static gboolean on_key_pressed(GtkEventControllerKey *ctrl, guint keyval,
                               guint keycode, GdkModifierType state,
                               PluginData *data)
{
    GtkButton *test = GTK_BUTTON(gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(ctrl)));
    char *text;
    (void)keycode;

    /* A modifier tap is a key too. Commit on release so chords still work.
     * Some keyboards include the pressed key's own modifier in the event. */
    guint pending = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(test), "pending-key"));
    g_object_set_data(G_OBJECT(test), "pending-key", NULL);
    /* NOTE: the old Win-key workaround via GDK_MOD4_MASK is obsolete;
     * GDK_SUPER_MASK covers it portably in GTK4. */
    state &= gtk_accelerator_get_default_mod_mask();
    /* if mod key event then update test label and go */
    if (is_modifier_keyval(keyval))
    {
        if ((pending == 0 || pending == keyval) && (state & ~modifier_mask(keyval)) == 0)
            g_object_set_data(G_OBJECT(test), "pending-key", GUINT_TO_POINTER(keyval));
        if (state != 0)
        {
            text = gtk_accelerator_get_label(0, state);
            gtk_button_set_label(test, text);
            g_free(text);
        }
        /* if no modifiers currently then show original state */
        else
            gtk_button_set_label(test, g_object_get_data(G_OBJECT(test), "original_label"));
        return TRUE;
    }
    if (keyval == 0 || gdk_keyval_name(keyval) == NULL)
        return TRUE;
    /* update the label now */
    text = gtk_accelerator_get_label(keyval, state);
    gtk_button_set_label(test, text);
    /* save new value now */
    g_object_set_data_full(G_OBJECT(test), "original_label", text, g_free);
    text = gtk_accelerator_name(keyval, state);
    g_object_set_data_full(G_OBJECT(test), "accelerator_name", text, g_free);
    gtk_widget_set_sensitive(data->edit_apply_button, TRUE);
    /* change focus onto exec line or actions tree now */
    if (data->edit_exec)
        gtk_widget_grab_focus(GTK_WIDGET(data->edit_exec));
    else
        gtk_widget_grab_focus(GTK_WIDGET(data->edit_tree));
    return TRUE;
}

static void on_key_released(GtkEventControllerKey *ctrl, guint keyval,
                            guint keycode, GdkModifierType state,
                            PluginData *data)
{
    GtkButton *test = GTK_BUTTON(gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(ctrl)));
    guint pending = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(test), "pending-key"));
    (void)keycode;
    g_object_set_data(G_OBJECT(test), "pending-key", NULL);
    if (pending != 0 && pending == keyval)
    {
        char *label = gtk_accelerator_get_label(keyval, 0);
        gtk_button_set_label(test, label);
        g_object_set_data_full(G_OBJECT(test), "original_label", label, g_free);
        g_object_set_data_full(G_OBJECT(test), "accelerator_name",
                              gtk_accelerator_name(keyval, 0), g_free);
        update_apply_from_keys(data);
        if (data->edit_exec)
            gtk_widget_grab_focus(GTK_WIDGET(data->edit_exec));
        else
            gtk_widget_grab_focus(GTK_WIDGET(data->edit_tree));
        return;
    }
    /* refresh the label when a modifier is released */
    if (is_modifier_keyval(keyval))
    {
        char *text;
        state &= gtk_accelerator_get_default_mod_mask();
        if (state != 0)
        {
            text = gtk_accelerator_get_label(0, state);
            gtk_button_set_label(test, text);
            g_free(text);
        }
        else
            gtk_button_set_label(test, g_object_get_data(G_OBJECT(test), "original_label"));
    }
}

static void key_capture_focus_left(GtkEventControllerFocus *controller, gpointer data)
{
    GtkWidget *widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(controller));
    g_object_set_data(G_OBJECT(widget), "pending-key", NULL);
}

static GtkWidget *key_button_new(PluginData *data, const char *hotkey)
{
    GtkWidget *w;
    char *label;
    guint keyval = 0;
    GdkModifierType state = 0;
    GtkEventController *key_ctrl;

    if (hotkey)
        gtk_accelerator_parse(hotkey, &keyval, &state);
    label = gtk_accelerator_get_label(keyval, state);
    w = gtk_button_new_with_label(label);
    g_object_set_data_full(G_OBJECT(w), "accelerator_name", g_strdup(hotkey), g_free);
    g_object_set_data_full(G_OBJECT(w), "original_label", label, g_free);
    key_ctrl = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(key_ctrl, GTK_PHASE_CAPTURE);
    g_signal_connect(key_ctrl, "key-pressed", G_CALLBACK(on_key_pressed), data);
    g_signal_connect(key_ctrl, "key-released", G_CALLBACK(on_key_released), data);
    gtk_widget_add_controller(w, key_ctrl);
    key_ctrl = gtk_event_controller_focus_new();
    g_signal_connect(key_ctrl, "leave", G_CALLBACK(key_capture_focus_left), NULL);
    gtk_widget_add_controller(w, key_ctrl);
    return w;
}

static void clear_captured_key(GtkButton *button, PluginData *data)
{
    GtkButton *key = g_object_get_data(G_OBJECT(button), "key-button");
    g_object_set_data(G_OBJECT(key), "pending-key", NULL);
    g_object_set_data(G_OBJECT(key), "accelerator_name", NULL);
    g_object_set_data(G_OBJECT(key), "original_label", NULL);
    gtk_button_set_label(key, "");
    update_apply_from_keys(data);
}

static GtkWidget *key_frame_new(PluginData *data, const char *title,
                              const char *hotkey, GtkWidget **key)
{
    GtkWidget *frame = gtk_frame_new(title);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *clear = gtk_button_new_with_label(_("Clear"));
    *key = key_button_new(data, hotkey);
    gtk_widget_set_hexpand(*key, TRUE);
    gtk_widget_set_tooltip_text(*key, _("Focus here and press any key or combination. Use Clear to remove it."));
    g_object_set_data(G_OBJECT(clear), "key-button", *key);
    g_signal_connect(clear, "clicked", G_CALLBACK(clear_captured_key), data);
    gtk_box_append(GTK_BOX(box), *key);
    gtk_box_append(GTK_BOX(box), clear);
    gtk_frame_set_child(GTK_FRAME(frame), box);
    return frame;
}

/* used by edit_action() to be able to edit */
static GList *copy_options(GList *orig)
{
    GList *copy = NULL;

    /* copy contents recursively */
    while (orig)
    {
        LuxeHotkeyAttr *attr = g_slice_new(LuxeHotkeyAttr);
        LuxeHotkeyAttr *attr_orig = orig->data;

        attr->name = g_strdup(attr_orig->name);
        attr->values = g_list_copy_deep(attr_orig->values, (GCopyFunc)g_strdup, NULL);
        attr->subopts = copy_options(attr_orig->subopts);
        attr->desc = g_strdup(attr_orig->desc);
        attr->has_actions = attr_orig->has_actions;
        attr->has_value = attr_orig->has_value;
        copy = g_list_prepend(copy, attr);
        orig = orig->next;
    }
    return g_list_reverse(copy);
}

static void add_options_to_tree(GtkTreeStore *store, GtkTreeIter *parent_iter,
                                GList *list)
{
    LuxeHotkeyAttr *opt;
    GtkTreeIter iter;
    const char *val;

    while (list)
    {
        opt = list->data;
        val = opt->values ? opt->values->data : NULL;
        gtk_tree_store_insert_with_values(store, &iter, parent_iter, -1,
                                          0, opt->name,
                                          1, val,
                                          2, opt,
                                          3, _(opt->name),
                                          4, (val && val[0]) ? _(val) : NULL, -1);
        if (opt->subopts)
            add_options_to_tree(store, &iter, opt->subopts);
        list = list->next;
    }
}

static void update_options_tree(PluginData *data)
{
    GtkTreeStore *store = gtk_tree_store_new(5, G_TYPE_STRING, /* option name */
                                                G_TYPE_STRING, /* option value */
                                                G_TYPE_POINTER, /* LuxeHotkeyAttr */
                                                G_TYPE_STRING, /* shown name */
                                                G_TYPE_STRING); /* shown value */

    add_options_to_tree(store, NULL, data->edit_options_copy);
    gtk_tree_view_set_model(data->edit_tree, GTK_TREE_MODEL(store));
    gtk_tree_view_expand_all(data->edit_tree);
    g_object_unref(store);
}

#define edit_is_active(data) (data->edit_mode != EDIT_MODE_NONE)

static void cancel_edit(PluginData *data)
{
    data->edit_mode = EDIT_MODE_NONE;
    gtk_widget_set_visible(data->edit_frame, FALSE);
}

static void on_exec_changed(GtkEditable *exec, PluginData *data)
{
    update_apply_from_keys(data);
    (void)exec;
}

static void on_row_activated(GtkTreeView *view, GtkTreePath *path,
                             GtkTreeViewColumn *column, PluginData *data)
{
    GtkTreeModel *model;
    GtkTreeIter iter;
    (void)column;

    model = gtk_tree_view_get_model(view);
    if (!gtk_tree_model_get_iter(model, &iter, path))
        /* invalid path */
        return;
    start_edit(model, &iter, data);
}

static void on_selection_changed(GtkTreeSelection *selection, PluginData *data)
{
    if (edit_is_active(data))
        //FIXME: ask confirmation and revert, is that possible?
        cancel_edit(data);
    //update toolbar buttons visibility
    update_edit_toolbar(data);
    (void)selection;
}

/* Runs on WM-driven dialog close (Alt+F4/X button): detach the model
 * and drop the selection handler so disposal stays warning-free.
 * Returns FALSE to let the default handler destroy the window. */
static gboolean on_edit_close(GtkWindow *win, PluginData *data)
{
    /* Hide first: unmapping up-front avoids a gtk_css_node assertion
     * inside GTK when mapped siblings are torn down. */
    gtk_widget_set_visible(GTK_WIDGET(win), FALSE);
    if (data->edit_tree)
    {
        g_signal_handlers_disconnect_by_func(
            gtk_tree_view_get_selection(data->edit_tree),
            (gpointer)on_selection_changed, data);
        gtk_tree_view_set_model(data->edit_tree, NULL);
    }
    return FALSE;
}

static void on_option_changed(GtkComboBox *box, PluginData *data)
{
    const LuxeHotkeyAttr *opt, *tmpl;
    const GList *values;
    GtkTreeModel *model;
    GtkListStore *values_store;
    GtkTreeIter iter;
    int i, sel;
    gboolean is_action = FALSE;

    /* g_debug("on_option_changed"); */
    opt = NULL;
    if (data->edit_mode == EDIT_MODE_ADD)
        is_action = (data->current_page == data->acts);
    else if (gtk_tree_selection_get_selected(gtk_tree_view_get_selection(data->edit_tree),
                                             &model, &iter))
    {
        if (data->edit_mode == EDIT_MODE_EDIT)
        {
            gtk_tree_model_get(model, &iter, 2, &opt, -1);
            if (data->current_page == data->acts)
                is_action = (get_parent_template_list(model, &iter, data) == data->edit_template);
        }
        else /* EDIT_MODE_OPTION */
        {
            tmpl = find_template_for_option(model, &iter,
                                            get_parent_template_list(model, &iter, data));
            if (tmpl)
                is_action = tmpl->has_actions;
        }
    }
    if (!gtk_combo_box_get_active_iter(box, &iter))
        /* no item selected */
        return;
    gtk_tree_model_get(gtk_combo_box_get_model(box), &iter, 2, &tmpl, -1);
    if (tmpl->has_actions || is_action ||
        (tmpl->subopts != NULL && !tmpl->has_value))
    {
        /* either it's action, or option has suboptions instead of values */
        gtk_widget_set_visible(data->edit_value_label, FALSE);
        gtk_widget_set_visible(GTK_WIDGET(data->edit_value), FALSE);
        gtk_widget_set_visible(GTK_WIDGET(data->edit_values), FALSE);
        gtk_widget_set_visible(data->edit_value_num, FALSE);
        gtk_widget_set_visible(data->edit_value_num_label, FALSE);
    }
    else if ((values = tmpl->values) != NULL)
    {
        values_store = GTK_LIST_STORE(gtk_list_store_new(2, G_TYPE_STRING, /* description */
                                                            G_TYPE_STRING)); /* value */
        for (sel = 0, i = 0; values; values = values->next, i++)
        {
            gtk_list_store_insert_with_values(values_store, NULL, i, 0, _(values->data),
                                                                     1, values->data, -1);
            if (opt && opt->values)
            {
                if (((char *)values->data)[0] == '#')
                {
                    size_t len = strspn(opt->values->data, "0123456789");
                    /* test if value is an integer number */
                    if (len == strlen(opt->values->data))
                        sel = i;
                }
                else if (((char *)values->data)[0] == '%')
                {
                    const char *str = opt->values->data;
                    size_t len = strspn(str, "0123456789");
                    /* test if value is either a fraction or a percent value */
                    if (len > 0 && (str[len] == '%' || str[len] == '/'))
                        sel = i;
                }
                else if (g_strcmp0(opt->values->data, values->data) == 0)
                    sel = i;
            }
        }
        gtk_combo_box_set_model(data->edit_values, GTK_TREE_MODEL(values_store));
        g_object_unref(values_store);
        gtk_combo_box_set_active(data->edit_values, sel);
        gtk_widget_set_visible(data->edit_value_label, TRUE);
        gtk_widget_set_visible(GTK_WIDGET(data->edit_values), TRUE);
        gtk_widget_set_visible(GTK_WIDGET(data->edit_value), FALSE);
    }
    else
    {
        gtk_widget_set_visible(data->edit_value_label, TRUE);
        gtk_widget_set_visible(data->edit_value_num, FALSE);
        gtk_widget_set_visible(data->edit_value_num_label, FALSE);
        gtk_widget_set_visible(GTK_WIDGET(data->edit_values), FALSE);
        gtk_widget_set_visible(GTK_WIDGET(data->edit_value), TRUE);
        if (opt && opt->values)
            gtk_editable_set_text(GTK_EDITABLE(data->edit_value), opt->values->data);
        else
            gtk_editable_set_text(GTK_EDITABLE(data->edit_value), "");
    }
}

static void on_value_changed(GtkComboBox *box, PluginData *data)
{
    LuxeHotkeyAttr *opt;
    const char *value;
    char *selected_value = NULL;
    GtkTreeModel *model;
    GtkTreeIter iter;
    gdouble num = 0;
    long div;

    model = gtk_combo_box_get_model(box);
    if (!gtk_combo_box_get_active_iter(box, &iter))
        /* no value chosen */
        goto _general;
    gtk_tree_model_get(model, &iter, 1, &selected_value, -1);
    value = selected_value;
    if (!value)
        /* illegal really */
        goto _general;
    if (value[0] == '#')
    {
        /* if value is # then show data->edit_value_num */
        gtk_spin_button_set_range(GTK_SPIN_BUTTON(data->edit_value_num),
                                  -1000.0, +1000.0);
        if (data->edit_mode == EDIT_MODE_EDIT &&
            gtk_tree_selection_get_selected(gtk_tree_view_get_selection(data->edit_tree),
                                            &model, &iter))
        {
            gtk_tree_model_get(model, &iter, 2, &opt, -1);
            if (opt && opt->values)
                num = strtol(opt->values->data, NULL, 10);
        }
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(data->edit_value_num), num);
        gtk_widget_set_visible(data->edit_value_num, TRUE);
        gtk_widget_set_visible(data->edit_value_num_label, FALSE);
    }
    else if (value[0] == '%')
    {
        /* if value is % then show data->edit_value_num with label "%" */
        gtk_spin_button_set_range(GTK_SPIN_BUTTON(data->edit_value_num),
                                  0.0, +100.0);
        if (data->edit_mode == EDIT_MODE_EDIT &&
            gtk_tree_selection_get_selected(gtk_tree_view_get_selection(data->edit_tree),
                                            &model, &iter))
        {
            gtk_tree_model_get(model, &iter, 2, &opt, -1);
            if (opt && opt->values)
            {
                value = opt->values->data;
                num = strtol(value, (char **)&value, 10);
                if (*value == '/')
                {
                    /* convert fraction into percent */
                    div = strtol(value + 1, NULL, 10);
                    div = MAX(div, 1);
                    num *= 100.0 / div;
                }
            }
        }
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(data->edit_value_num), num);
        gtk_widget_set_visible(data->edit_value_num, TRUE);
        gtk_label_set_text(GTK_LABEL(data->edit_value_num_label), "%");
        gtk_widget_set_visible(data->edit_value_num_label, TRUE);
    }
    else
    {
_general:
        /* else hide both data->edit_value_num and label */
        gtk_widget_set_visible(data->edit_value_num, FALSE);
        gtk_widget_set_visible(data->edit_value_num_label, FALSE);
    }
    g_free(selected_value);
}

static void apply_options(PluginData *data, LuxeHotkeyAttr *opt)
{
    GtkTreeModel *model;
    GtkTreeIter iter;
    char *name, *_value = NULL;
    const char *value = NULL;
    gboolean changed = FALSE;

    /* process name */
    if (gtk_widget_get_visible(GTK_WIDGET(data->edit_actions)) &&
        gtk_combo_box_get_active_iter(data->edit_actions, &iter))
    {
        model = gtk_combo_box_get_model(data->edit_actions);
        const LuxeHotkeyAttr *tmpl = NULL;
        gtk_tree_model_get(model, &iter, 2, &tmpl, -1);
        if (tmpl) {
            opt->has_actions = tmpl->has_actions;
            opt->has_value = tmpl->has_value;
        }
        gtk_tree_model_get(model, &iter, 1, &name, -1);
        if (g_strcmp0(opt->name, name) == 0)
            g_free(name);
        else
        {
            g_free(opt->name);
            opt->name = name;
            changed = TRUE;
        }
    }
    /* process value */
    if (gtk_widget_get_visible(data->edit_value_num))
    {
        gdouble num_val = gtk_spin_button_get_value(GTK_SPIN_BUTTON(data->edit_value_num));
        if (gtk_widget_get_visible(data->edit_value_num_label))
            value = _value = g_strdup_printf("%d%s", (int)num_val,
                                             gtk_label_get_text(GTK_LABEL(data->edit_value_num_label)));
        else
            value = _value = g_strdup_printf("%d", (int)num_val);
    }
    else if (gtk_widget_get_visible(GTK_WIDGET(data->edit_values)) &&
             gtk_combo_box_get_active_iter(data->edit_values, &iter))
    {
        model = gtk_combo_box_get_model(data->edit_values);
        gtk_tree_model_get(model, &iter, 1, &_value, -1);
        value = _value;
    }
    else if (gtk_widget_get_visible(GTK_WIDGET(data->edit_value)))
    {
        value = gtk_editable_get_text(GTK_EDITABLE(data->edit_value));
    }
    if (opt->values && g_strcmp0(opt->values->data, value) == 0)
        g_free(_value);
    else
    {
        if (_value == NULL)
            value = g_strdup(value);
        if (opt->values == NULL)
            opt->values = g_list_prepend(NULL, (gpointer)value);
        else
        {
            g_free(opt->values->data);
            opt->values->data = (gpointer)value;
        }
        changed = TRUE;
    }
    /* process changed state */
    if (((value = gtk_button_get_label(GTK_BUTTON(data->edit_key1))) == NULL || value[0] == 0) &&
        ((value = gtk_button_get_label(GTK_BUTTON(data->edit_key2))) == NULL || value[0] == 0))
        gtk_widget_set_sensitive(data->edit_apply_button, FALSE);
    else if (changed)
        gtk_widget_set_sensitive(data->edit_apply_button, TRUE);
}

static void on_apply_button(GtkButton *btn, PluginData *data)
{
    LuxeHotkeyAttr *opt;
    GtkTreeModel *model;
    GtkTreeIter iter;
    const char *val;
    (void)btn;

    switch (data->edit_mode)
    {
    case EDIT_MODE_ADD:
        opt = g_slice_new0(LuxeHotkeyAttr);
        apply_options(data, opt);
        /* insert new option/action */
        data->edit_options_copy = g_list_append(data->edit_options_copy, opt);
        model = gtk_tree_view_get_model(data->edit_tree);
        /* update the tree */
        val = opt->values ? opt->values->data : NULL;
        gtk_tree_store_insert_with_values(GTK_TREE_STORE(model), NULL, NULL, -1,
                                          0, opt->name,
                                          1, val,
                                          2, opt,
                                          3, _(opt->name),
                                          4, (val && val[0]) ? _(val) : NULL, -1);
        /* update toolbar */
        update_edit_toolbar(data);
        break;
    case EDIT_MODE_EDIT:
        if (gtk_tree_selection_get_selected(gtk_tree_view_get_selection(data->edit_tree),
                                            &model, &iter))
        {
            gtk_tree_model_get(model, &iter, 2, &opt, -1);
            apply_options(data, opt);
            val = opt->values ? opt->values->data : NULL;
            gtk_tree_store_set(GTK_TREE_STORE(model), &iter,
                               1, val,
                               4, (val && val[0]) ? _(val) : NULL, -1);
            update_edit_toolbar(data);
        }
        break;
    case EDIT_MODE_OPTION:
        if (gtk_tree_selection_get_selected(gtk_tree_view_get_selection(data->edit_tree),
                                            &model, &iter))
        {
            LuxeHotkeyAttr *parent;
            gtk_tree_model_get(model, &iter, 2, &parent, -1);
            opt = g_slice_new0(LuxeHotkeyAttr);
            apply_options(data, opt);
            parent->subopts = g_list_append(parent->subopts, opt);
            model = gtk_tree_view_get_model(data->edit_tree);
            val = opt->values ? opt->values->data : NULL;
            gtk_tree_store_insert_with_values(GTK_TREE_STORE(model), NULL, &iter, -1,
                                              0, opt->name,
                                              1, val,
                                              2, opt,
                                              3, _(opt->name),
                                              4, (val && val[0]) ? _(val) : NULL, -1);
            gtk_tree_view_expand_all(data->edit_tree);
            update_edit_toolbar(data);
        }
        break;
    case EDIT_MODE_NONE:
    default:
        break;
    }
    cancel_edit(data);
}

static void on_cancel_button(GtkButton *btn, PluginData *data)
{
    (void)btn;
    cancel_edit(data);
}

/* free all allocated data */
void _edit_cleanup(PluginData *data)
{
    if (data->edit_window)
    {
        cancel_edit(data);
        /* Disconnect first: dialog disposal emits selection "changed"
         * after sibling toolbar buttons may be finalized already. */
        if (data->edit_tree)
        {
            g_signal_handlers_disconnect_by_func(
                gtk_tree_view_get_selection(data->edit_tree),
                (gpointer)on_selection_changed, data);
            /* Detach the model first: destroying a TreeView with an
             * attached model+selection trips a gtk_css_node assertion
             * inside GTK during disposal. */
            gtk_tree_view_set_model(data->edit_tree, NULL);
        }
        g_object_remove_weak_pointer(G_OBJECT(data->edit_window), (gpointer)&data->edit_window);
        gtk_window_destroy(data->edit_window);
        data->edit_window = NULL;
    }
    if (data->edit_options_copy)
    {
        free_options(data->edit_options_copy);
        data->edit_options_copy = NULL;
    }
}

/* small helper: labeled toolbar button wired to a clicked handler */
static GtkWidget *edit_tool_button(const char *icon_name, const char *tooltip,
                                   GCallback callback, PluginData *data)
{
    GtkWidget *btn = gtk_button_new_from_icon_name(icon_name);
    gtk_widget_set_tooltip_text(btn, tooltip);
    g_signal_connect(btn, "clicked", callback, data);
    return btn;
}

void _edit_action(PluginData *data, GError **error)
{
    LuxeHotkeyGlobal *act = NULL;
    LuxeHotkeyApp *app = NULL;
    const char *accel1 = NULL, *accel2 = NULL;
    GtkBox *vbox, *xbox;
    GtkWidget *widget;
    GtkCellRenderer *column;
    GtkTreeModel *model;
    GtkTreeIter iter;
    gboolean is_action = FALSE;
    (void)error;

    if (data->edit_window)
    {
        /* OOPS, another edit is still opened */
        return;
    }
    /* do cleanup */
    _edit_cleanup(data);
    /* get a template */
    if (data->current_page == data->acts)
    {
        if (data->cb->get_wm_actions == NULL) /* not available for edit */
            return;
        if (data->cb->set_wm_key == NULL) /* not available for save */
            return;
        data->edit_template = data->cb->get_wm_actions(*data->config, NULL);
        //FIXME: test for error
        is_action = TRUE;
        if (gtk_tree_selection_get_selected(gtk_tree_view_get_selection(data->acts),
                                            &model, &iter))
            gtk_tree_model_get(model, &iter, 4, &act, -1);
        if (act)
        {
            /* if there is a selection then copy its options */
            data->edit_options_copy = copy_options(act->actions);
            accel1 = act->accel1;
            accel2 = act->accel2;
        }
    }
    else
    {
        if (data->cb->get_app_options == NULL) /* not available for edit */
            return;
        if (data->cb->set_app_key == NULL) /* not available for save */
            return;
        data->edit_template = data->cb->get_app_options(*data->config, NULL);
        //FIXME: test for error
        if (gtk_tree_selection_get_selected(gtk_tree_view_get_selection(data->apps),
                                            &model, &iter))
            gtk_tree_model_get(model, &iter, 3, &app, -1);
        if (app)
        {
            /* if there is a selection then copy its options */
            data->edit_options_copy = copy_options(app->options);
            accel1 = app->accel1;
            accel2 = app->accel2;
        }
    }

    /* create a window with a vertical box inside */
    data->edit_window = GTK_WINDOW(gtk_window_new());
    gtk_window_set_default_size(data->edit_window, 480, 400);
    gtk_window_set_title(data->edit_window, _("Edit shortcut"));
    gtk_window_set_transient_for(data->edit_window,
                                 GTK_WINDOW(gtk_widget_get_root(GTK_WIDGET(data->notebook))));
    g_object_add_weak_pointer(G_OBJECT(data->edit_window), (gpointer)&data->edit_window);
    vbox = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 14));
    gtk_widget_set_margin_top(GTK_WIDGET(vbox), 10);
    gtk_widget_set_margin_bottom(GTK_WIDGET(vbox), 10);
    gtk_widget_set_margin_start(GTK_WIDGET(vbox), 10);
    gtk_widget_set_margin_end(GTK_WIDGET(vbox), 10);

    /* add the toolbar (plain buttons; GtkToolbar/UIManager are gone) */
    widget = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_append(GTK_BOX(widget),
                   edit_tool_button("window-close", _("Discard changes"),
                                    G_CALLBACK(on_cancel_btn), data));
    data->edit_apply_button =
        edit_tool_button("document-save", _("Accept changes"),
                         G_CALLBACK(on_save_btn), data);
    gtk_widget_add_css_class(data->edit_apply_button, "suggested-action");
    gtk_box_append(GTK_BOX(widget), data->edit_apply_button);
    {
        GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
        gtk_box_append(GTK_BOX(widget), sep);
    }
    data->add_action_button =
        edit_tool_button("list-add", _("Add an action"),
                         G_CALLBACK(on_add_action_btn), data);
    gtk_box_append(GTK_BOX(widget), data->add_action_button);
    data->add_option_button =
        edit_tool_button("list-add", _("Add an option to this command"),
                         G_CALLBACK(on_add_option_btn), data);
    gtk_box_append(GTK_BOX(widget), data->add_option_button);
    data->rm_option_button =
        edit_tool_button("list-remove", _("Remove selection"),
                         G_CALLBACK(on_remove_btn), data);
    gtk_widget_add_css_class(data->rm_option_button, "destructive-action");
    gtk_box_append(GTK_BOX(widget), data->rm_option_button);
    data->edit_option_button =
        edit_tool_button("document-edit", _("Change selected option"),
                         G_CALLBACK(on_edit_btn), data);
    gtk_box_append(GTK_BOX(widget), data->edit_option_button);
    data->add_suboption_button =
        edit_tool_button("list-add", _("Add an option to selection"),
                         G_CALLBACK(on_add_suboption_btn), data);
    gtk_box_append(GTK_BOX(widget), data->add_suboption_button);
    //TODO: 'Change' -- also 2click and Enter
    gtk_widget_set_sensitive(data->edit_apply_button, FALSE);
    gtk_box_append(vbox, widget);

    /* add frames for accel1 and accel2 */
    xbox = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0));
    gtk_box_set_homogeneous(xbox, TRUE);
    widget = key_frame_new(data, _("Hotkey 1"), accel1, &data->edit_key1);
    gtk_box_append(xbox, widget);
    widget = key_frame_new(data, _("Hotkey 2"), accel2, &data->edit_key2);
    gtk_box_append(xbox, widget);
    gtk_box_append(vbox, GTK_WIDGET(xbox));

    /* add frame with all options */
    widget = gtk_frame_new(NULL);
    xbox = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 0));
    if (is_action)
    {
        GtkWidget *label = gtk_label_new(_("Actions:"));
        gtk_label_set_xalign(GTK_LABEL(label), 0.0);
        gtk_box_append(xbox, label);
        data->edit_exec = NULL;
    }
    else
    {
        /* for application add an entry for exec line */
        GtkWidget *label = gtk_label_new(_("Command line:"));
        gtk_label_set_xalign(GTK_LABEL(label), 0.0);
        gtk_box_append(xbox, label);
        data->edit_exec = GTK_ENTRY(gtk_entry_new());
        g_signal_connect(data->edit_exec, "changed", G_CALLBACK(on_exec_changed), data);
        if (app && app->exec)
            gtk_editable_set_text(GTK_EDITABLE(data->edit_exec), app->exec);
        gtk_box_append(xbox, GTK_WIDGET(data->edit_exec));
        label = gtk_label_new(_("Options:"));
        gtk_label_set_xalign(GTK_LABEL(label), 0.0);
        gtk_box_append(xbox, label);
    }
    data->edit_tree = GTK_TREE_VIEW(gtk_tree_view_new());
    /* No interactive search: its popover trips the
     * gtk_css_node_insert_after assertion (upstream GTK issue
     * GNOME/gtk#4884) on keypress while the view is focused. */
    gtk_tree_view_set_enable_search(data->edit_tree, FALSE);
    gtk_widget_set_hexpand(GTK_WIDGET(data->edit_tree), TRUE);
    gtk_widget_set_vexpand(GTK_WIDGET(data->edit_tree), TRUE);
    gtk_box_append(xbox, GTK_WIDGET(data->edit_tree));
    gtk_frame_set_child(GTK_FRAME(widget), GTK_WIDGET(xbox));
    gtk_box_append(vbox, widget);
    gtk_tree_view_insert_column_with_attributes(data->edit_tree, 0, NULL,
                                                gtk_cell_renderer_text_new(),
                                                "text", 3, NULL);
    gtk_tree_view_insert_column_with_attributes(data->edit_tree, 1, NULL,
                                                gtk_cell_renderer_text_new(),
                                                "text", 4, NULL);
    gtk_tree_view_set_headers_visible(data->edit_tree, FALSE);
    g_signal_connect(data->edit_tree, "row-activated", G_CALLBACK(on_row_activated), data);

    /* frame with fields for editing, hidden for now */
    data->edit_frame = gtk_frame_new(NULL);
    xbox = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 0));
    /* combobox for option/action name */
    widget = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(widget), _("<b>Name:</b>"));
    gtk_label_set_xalign(GTK_LABEL(widget), 0.0);
    gtk_box_append(xbox, widget);
    widget = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    data->edit_option_name = gtk_label_new(NULL);
    gtk_box_append(GTK_BOX(widget), data->edit_option_name);
    data->edit_actions = GTK_COMBO_BOX(gtk_combo_box_new());
    g_signal_connect(data->edit_actions, "changed",
                     G_CALLBACK(on_option_changed), data);
    column = gtk_cell_renderer_text_new();
    gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(data->edit_actions), column, TRUE);
    gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(data->edit_actions), column,
                                   "text", 0, NULL);
    gtk_box_append(GTK_BOX(widget), GTK_WIDGET(data->edit_actions));
    gtk_widget_set_hexpand(GTK_WIDGET(data->edit_actions), TRUE);
    gtk_box_append(xbox, widget);
    /* entry or combobox for option value */
    data->edit_value_label = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    widget = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(widget), _("<b>Value:</b>"));
    gtk_label_set_xalign(GTK_LABEL(widget), 0.0);
    gtk_box_append(GTK_BOX(data->edit_value_label), widget);
    gtk_box_append(xbox, data->edit_value_label);
    widget = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    data->edit_value = GTK_ENTRY(gtk_entry_new());
    gtk_widget_set_hexpand(GTK_WIDGET(data->edit_value), TRUE);
    gtk_box_append(GTK_BOX(widget), GTK_WIDGET(data->edit_value));
    data->edit_values = GTK_COMBO_BOX(gtk_combo_box_new());
    column = gtk_cell_renderer_text_new();
    gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(data->edit_values), column, TRUE);
    gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(data->edit_values), column,
                                   "text", 0, NULL);
    gtk_widget_set_hexpand(GTK_WIDGET(data->edit_values), TRUE);
    gtk_box_append(GTK_BOX(widget), GTK_WIDGET(data->edit_values));
    g_signal_connect(data->edit_values, "changed",
                     G_CALLBACK(on_value_changed), data);
    data->edit_value_num = gtk_spin_button_new_with_range(-1000.0, +1000.0, 1.0);
    gtk_box_append(GTK_BOX(widget), GTK_WIDGET(data->edit_value_num));
    data->edit_value_num_label = gtk_label_new(NULL);
    gtk_box_append(GTK_BOX(widget), GTK_WIDGET(data->edit_value_num_label));
    gtk_box_append(xbox, widget);
    /* buttons 'Cancel' and 'Apply' */
    widget = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_halign(widget, GTK_ALIGN_END);
    {
        GtkWidget *cancel = gtk_button_new_with_label(_("Cancel"));
        g_signal_connect(cancel, "clicked", G_CALLBACK(on_cancel_button), data);
        gtk_box_append(GTK_BOX(widget), cancel);
        {
            GtkWidget *apply = gtk_button_new_with_label(_("Apply"));
            g_signal_connect(apply, "clicked", G_CALLBACK(on_apply_button), data);
            gtk_box_append(GTK_BOX(widget), apply);
        }
    }
    gtk_box_append(xbox, widget);
    gtk_frame_set_child(GTK_FRAME(data->edit_frame), GTK_WIDGET(xbox));
    gtk_box_append(vbox, data->edit_frame);

    GtkWidget *toolbar = luxe_toolbar_new();
    luxe_toolbar_add_top_bar(GTK_BOX(toolbar), gtk_header_bar_new());
    luxe_toolbar_set_content(GTK_BOX(toolbar), GTK_WIDGET(vbox));
    luxe_window_set_content(GTK_WINDOW(data->edit_window), toolbar);
    gtk_widget_set_visible(data->edit_frame, FALSE);
    /* hide one of AddAction or AddOption */
    if (is_action)
    {
        /* AddOption is visible for exec only */
        gtk_widget_set_visible(data->add_option_button, FALSE);
    }
    else
    {
        /* AddAction is visible for action only */
        gtk_widget_set_visible(data->add_action_button, FALSE);
    }
    g_signal_connect(gtk_tree_view_get_selection(data->edit_tree), "changed",
                     G_CALLBACK(on_selection_changed), data);
    g_signal_connect(data->edit_window, "close-request",
                     G_CALLBACK(on_edit_close), data);
    update_options_tree(data);
    update_edit_toolbar(data);
    gtk_window_present(data->edit_window);
    gtk_widget_grab_focus(GTK_WIDGET(data->edit_tree));
}
