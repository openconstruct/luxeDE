/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#define _GNU_SOURCE
#include "permissions-gtk4.h"
#include <glib/gi18n.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

struct _PcmgrPermissionTarget { int fd; guint32 mode; char *name; };
static gboolean system_error(GError **error)
{
    int code = errno;
    g_set_error_literal(error, G_IO_ERROR, g_io_error_from_errno(code), g_strerror(code));
    return FALSE;
}
PcmgrPermissionTarget *pcmgr_permission_target_new(GFile *file, GError **error)
{
    g_autofree char *path = g_file_get_path(file);
    if (!path) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, _("Permission editing is available for local files and folders."));
        return NULL;
    }
    /* O_PATH works even with mode 000. Hold the selected inode, never follow a
     * final symlink or chmod an unrelated replacement after a rename. */
    int fd = open(path, O_PATH | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) { system_error(error); return NULL; }
    struct stat info;
    if (fstat(fd, &info) < 0) { system_error(error); close(fd); return NULL; }
    if (!S_ISREG(info.st_mode) && !S_ISDIR(info.st_mode)) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, _("Select regular files or folders to edit permissions. Symbolic links are not changed."));
        close(fd); return NULL;
    }
    if (geteuid() != 0 && info.st_uid != geteuid()) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED, _("Only the owner can change these permissions."));
        close(fd); return NULL;
    }
    PcmgrPermissionTarget *target = g_new0(PcmgrPermissionTarget, 1);
    target->fd = fd; target->mode = info.st_mode & 07777;
    target->name = g_file_get_parse_name(file); return target;
}
void pcmgr_permission_target_free(PcmgrPermissionTarget *target)
{
    if (!target) return;
    close(target->fd); g_free(target->name); g_free(target);
}
guint32 pcmgr_permission_target_mode(PcmgrPermissionTarget *target) { return target->mode; }
gboolean pcmgr_permission_target_apply(PcmgrPermissionTarget *target, guint32 mask, guint32 bits, GError **error)
{
    if (mask & ~0777u) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, _("Invalid permission change.")); return FALSE;
    }
    struct stat info;
    if (fstat(target->fd, &info) < 0) return system_error(error);
    if (!info.st_nlink) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, _("The selected item was removed.")); return FALSE;
    }
    guint32 mode = ((info.st_mode & 07777) & ~mask) | (bits & mask);
    /* Following our private /proc fd reaches the already-open inode, even if
     * its old pathname has since been replaced with a symlink. */
    char descriptor[64]; g_snprintf(descriptor, sizeof descriptor, "/proc/self/fd/%d", target->fd);
    if (mask && chmod(descriptor, mode) < 0) return system_error(error);
    if (fstat(target->fd, &info) < 0) return system_error(error);
    target->mode = info.st_mode & 07777;
    if ((target->mode & mask) != (bits & mask)) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED, _("The filesystem did not retain the requested permissions.")); return FALSE;
    }
    return TRUE;
}

typedef struct {
    GtkWidget *checks[9], *controls, *status, *apply;
    GPtrArray *targets;
    GCancellable *cancel;
    guint32 mask, bits;
    gboolean updating, applying, closed;
} PermissionEditor;
static const guint32 permission_bits[] = {0400, 0200, 0100, 0040, 0020, 0010, 0004, 0002, 0001};
static void editor_free(gpointer data)
{
    PermissionEditor *e = data;
    g_ptr_array_unref(e->targets); g_object_unref(e->cancel); g_free(e);
}
static void editor_closed(GtkWidget *window, gpointer data)
{
    (void)window; PermissionEditor *e = data; e->closed = TRUE; g_cancellable_cancel(e->cancel);
}
static void editor_refresh(PermissionEditor *e)
{
    if (!e->targets->len) return;
    e->updating = TRUE;
    guint32 all = 0777, any = 0;
    for (guint i = 0; i < e->targets->len; ++i) {
        guint32 mode = pcmgr_permission_target_mode(g_ptr_array_index(e->targets, i));
        all &= mode; any |= mode;
    }
    for (guint i = 0; i < 9; ++i) {
        gtk_check_button_set_active(GTK_CHECK_BUTTON(e->checks[i]), !!(all & permission_bits[i]));
        gtk_check_button_set_inconsistent(GTK_CHECK_BUTTON(e->checks[i]), !!((all ^ any) & permission_bits[i]));
    }
    e->mask = e->bits = 0; e->updating = FALSE; gtk_widget_set_sensitive(e->apply, FALSE);
}
static void editor_changed(GtkCheckButton *check, gpointer data)
{
    PermissionEditor *e = data; if (e->updating) return;
    guint32 bit = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(check), "permission-bit"));
    gtk_check_button_set_inconsistent(check, FALSE); e->mask |= bit;
    if (gtk_check_button_get_active(check)) e->bits |= bit; else e->bits &= ~bit;
    gtk_widget_set_sensitive(e->apply, !e->applying && e->mask != 0);
    gtk_label_set_text(GTK_LABEL(e->status), _("Changes apply only to the selected items, not their contents."));
}
static void editor_apply_thread(GTask *task, gpointer source, gpointer data, GCancellable *cancel)
{
    (void)source; PermissionEditor *e = data; GString *errors = g_string_new(NULL);
    for (guint i = 0; i < e->targets->len; ++i) {
        if (g_cancellable_is_cancelled(cancel)) break;
        PcmgrPermissionTarget *target = g_ptr_array_index(e->targets, i); GError *error = NULL;
        if (!pcmgr_permission_target_apply(target, e->mask, e->bits, &error)) {
            g_string_append_printf(errors, "%s: %s\n", target->name, error->message); g_clear_error(&error);
        }
    }
    g_task_return_pointer(task, g_string_free(errors, FALSE), g_free);
}
static void editor_apply_done(GObject *source, GAsyncResult *result, gpointer data)
{
    (void)source; PermissionEditor *e = data;
    g_autofree char *errors = g_task_propagate_pointer(G_TASK(result), NULL);
    e->applying = FALSE; if (e->closed) return;
    gtk_widget_set_sensitive(e->controls, TRUE);
    editor_refresh(e);
    gtk_label_set_text(GTK_LABEL(e->status), errors && *errors ? errors : _("Permissions updated."));
    if (errors && *errors) gtk_widget_add_css_class(e->status, "error");
    else gtk_widget_remove_css_class(e->status, "error");
}
static void editor_apply(GtkButton *button, gpointer data)
{
    PermissionEditor *e = data; if (!e->mask || e->applying) return;
    e->applying = TRUE; gtk_widget_set_sensitive(e->controls, FALSE); gtk_widget_set_sensitive(e->apply, FALSE);
    gtk_label_set_text(GTK_LABEL(e->status), _("Updating permissions…"));
    /* The task owns the window until completion, keeping its editor alive. */
    GTask *task = g_task_new(gtk_widget_get_root(GTK_WIDGET(button)), e->cancel, editor_apply_done, e);
    g_task_set_task_data(task, e, NULL); g_task_set_return_on_cancel(task, FALSE);
    g_task_run_in_thread(task, editor_apply_thread); g_object_unref(task);
}
GtkWidget *pcmgr_permissions_editor(GtkWindow *window, GPtrArray *files, GtkWidget **apply)
{
    PermissionEditor *e = g_new0(PermissionEditor, 1);
    e->targets = g_ptr_array_new_with_free_func((GDestroyNotify)pcmgr_permission_target_free);
    e->cancel = g_cancellable_new();
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    GtkWidget *heading = gtk_label_new(_("Permissions")); gtk_label_set_xalign(GTK_LABEL(heading), 0);
    gtk_widget_add_css_class(heading, "heading"); gtk_box_append(GTK_BOX(box), heading);
    e->controls = gtk_grid_new(); gtk_grid_set_row_spacing(GTK_GRID(e->controls), 6);
    gtk_grid_set_column_spacing(GTK_GRID(e->controls), 12); gtk_box_append(GTK_BOX(box), e->controls);
    const char *roles[] = {_("Owner"), _("Group"), _("Others")};
    const char *access[] = {_("Read"), _("Write"), _("Execute / Access")};
    for (guint col = 0; col < 3; ++col) gtk_grid_attach(GTK_GRID(e->controls), gtk_label_new(access[col]), col + 1, 0, 1, 1);
    for (guint row = 0; row < 3; ++row) {
        gtk_grid_attach(GTK_GRID(e->controls), gtk_label_new(roles[row]), 0, row + 1, 1, 1);
        for (guint col = 0; col < 3; ++col) {
            guint i = row * 3 + col; e->checks[i] = gtk_check_button_new();
            gtk_widget_set_halign(e->checks[i], GTK_ALIGN_CENTER);
            g_autofree char *description = g_strdup_printf("%s: %s", roles[row], access[col]);
            gtk_accessible_update_property(GTK_ACCESSIBLE(e->checks[i]), GTK_ACCESSIBLE_PROPERTY_LABEL, description, -1);
            g_object_set_data(G_OBJECT(e->checks[i]), "permission-bit", GUINT_TO_POINTER(permission_bits[i]));
            g_signal_connect(e->checks[i], "toggled", G_CALLBACK(editor_changed), e);
            gtk_grid_attach(GTK_GRID(e->controls), e->checks[i], col + 1, row + 1, 1, 1);
        }
    }
    GtkWidget *hint = gtk_label_new(_("Execute allows running a file as a program, or accessing a folder. Mixed boxes keep each item's current permission until changed."));
    gtk_label_set_wrap(GTK_LABEL(hint), TRUE); gtk_label_set_xalign(GTK_LABEL(hint), 0);
    gtk_label_set_max_width_chars(GTK_LABEL(hint), 55); gtk_box_append(GTK_BOX(box), hint);
    e->status = gtk_label_new(_("Changes apply only to the selected items, not their contents."));
    gtk_label_set_wrap(GTK_LABEL(e->status), TRUE); gtk_label_set_xalign(GTK_LABEL(e->status), 0);
    gtk_label_set_max_width_chars(GTK_LABEL(e->status), 55); gtk_box_append(GTK_BOX(box), e->status);
    e->apply = gtk_button_new_with_label(_("Apply permissions")); *apply = e->apply;
    gtk_widget_add_css_class(e->apply, "suggested-action"); gtk_widget_set_sensitive(e->apply, FALSE);
    g_signal_connect(e->apply, "clicked", G_CALLBACK(editor_apply), e);
    GError *error = NULL;
    for (guint i = 0; i < files->len; ++i) {
        PcmgrPermissionTarget *target = pcmgr_permission_target_new(g_ptr_array_index(files, i), &error);
        if (!target) break;
        g_ptr_array_add(e->targets, target);
    }
    if (error) {
        gtk_widget_set_sensitive(e->controls, FALSE);
        gtk_label_set_text(GTK_LABEL(e->status), error->message); g_clear_error(&error);
    } else editor_refresh(e);
    g_object_set_data_full(G_OBJECT(window), "permission-editor", e, editor_free);
    g_signal_connect(window, "destroy", G_CALLBACK(editor_closed), e);
    return box;
}
