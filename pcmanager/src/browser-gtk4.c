#include "luxe-gtk-ui.h"
#include "luxe-spacing.h"
#include "../luxe-about.h"
#include "luxe-dialogs-gtk.h"
/* SPDX-License-Identifier: GPL-2.0-or-later
 * PCManager's GTK4 browsing UI. GIO models/operations, libfm core bookmarks.
 * Copyright 2026 Jerry Howell and contributors. */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include "browser-gtk4.h"
#include "file-ops-gtk4.h"
#include "permissions-gtk4.h"
#include <libfm/fm.h>
#include <glib/gi18n.h>
#include <string.h>
#include <glib/gstdio.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <sys/file.h>

#define FILE_ATTRIBUTES "standard::*,time::modified,access::can-write,access::can-trash,unix::mode"

typedef struct _Browser Browser;
typedef struct _BrowserClass { GtkApplicationWindowClass parent; } BrowserClass;
typedef struct _Tab Tab;
typedef struct _TabClass { GtkBoxClass parent; } TabClass;
struct _Tab {
    GtkBox parent;
    Browser *browser;
    GFile *location;
    GtkDirectoryList *directory;
    GFileMonitor *monitor;
    guint refresh_source;
    gboolean refresh_pending;
    gint64 refresh_started;
    GtkCustomFilter *filter;
    GtkFilterListModel *filtered;
    GtkSortListModel *sorted;
    GtkMultiSelection *selection;
    GtkWidget *views, *list, *grid, *label;
    GtkColumnViewColumn *columns[4];
    GPtrArray *history;
    gint history_pos;
    char *query, *space_text;
    GCancellable *space_query;
    guint space_timer;
#ifdef PCMANAGER_TESTING
    guint space_query_count;
#endif
    gboolean closing;
    /* Pending selection/scroll restore across a model rebuild: basenames to
     * reselect (NULL when no restore is pending) plus the anchor row that
     * was at the top of the viewport. */
    GPtrArray *restore_selection;
    char *restore_anchor;
};
struct _Browser {
    GtkApplicationWindow parent;
    GtkWidget *notebook, *places, *location, *search, *status, *message;
    GtkWidget *operation_box, *operation_label, *operation_progress, *spinner, *empty_trash, *space;
    GSimpleActionGroup *actions;
    FmBookmarks *bookmarks;
    GVolumeMonitor *volumes;
    GCancellable *operation;
    GCancellable *paste_cancel;
    GdkContentProvider *move_clipboard;
    GdkDrop *operation_drop;
    GdkDragAction drop_action;
    gboolean show_hidden, icons, closing, counted;
};
G_DEFINE_TYPE(Tab, pcmgr_tab, GTK_TYPE_BOX)
G_DEFINE_TYPE(Browser, pcmgr_browser, GTK_TYPE_APPLICATION_WINDOW)
static GList *browsers;
static GtkApplication *browser_app = NULL;
static char *config_path;
static void (*idle_callback)(void);
static GdkContentProvider *copy_provider;
static gboolean browser_keyfile_load_bounded(GKeyFile *key, const char *path)
{
    int fd;
    struct stat st;
    GString *data;
    char buffer[4096];
    gboolean ok = TRUE;

    if (key == NULL || path == NULL)
        return FALSE;
    fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0)
        return FALSE;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_nlink != 1 ||
        st.st_size < 0 || st.st_size > 1024 * 1024) {
        close(fd);
        return FALSE;
    }
    data = g_string_sized_new((gsize)st.st_size);
    for (;;) {
        ssize_t n = read(fd, buffer, sizeof buffer);
        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0) { ok = FALSE; break; }
        if (n == 0)
            break;
        if (data->len + (gsize)n > 1024 * 1024) { ok = FALSE; break; }
        g_string_append_len(data, buffer, n);
    }
    if (ok)
        ok = g_key_file_load_from_data(key, data->str, data->len, G_KEY_FILE_NONE, NULL);
    g_string_free(data, TRUE);
    close(fd);
    return ok;
}
static gboolean copy_cut;

static Tab *current_tab(Browser *b);
static void update_window(Browser *b);
static void navigate(Tab *tab, GFile *file, gboolean record);
static Tab *add_tab(Browser *b, GFile *file);
static Browser *new_browser(void);
void pcmgr_browser_set_application(GtkApplication *app) { browser_app = app; }
static void rebuild_places(Browser *b);
static void show_context(Tab *tab, GtkWidget *widget, double x, double y);
static void start_operation(Browser *b, PcmgrFileOperation operation, GPtrArray *sources, GFile *destination);
static void snapshot_state(Tab *tab);
static void restore_state(Tab *tab);
static void refresh(Tab *tab, gboolean preserve);
static void schedule_monitor_refresh(Tab *tab);
static void message(Browser *b, const char *text);
static void show_error(Browser *b, const GError *error);

typedef struct {
    GWeakRef browser;
    char *success_message;
} VolumeRequest;

static VolumeRequest *volume_request_new(Browser *b, const char *success_message)
{
    VolumeRequest *request = g_new0(VolumeRequest, 1);
    g_weak_ref_init(&request->browser, b);
    request->success_message = g_strdup(success_message);
    return request;
}

static void volume_request_free(VolumeRequest *request)
{
    if (!request) return;
    g_weak_ref_clear(&request->browser);
    g_free(request->success_message);
    g_free(request);
}

static void finish_volume_request(GError *error, VolumeRequest *request)
{
    Browser *b = g_weak_ref_get(&request->browser);
    if (b && !b->closing) {
        if (error) show_error(b, error);
        else {
            message(b, request->success_message);
            rebuild_places(b);
        }
    }
    g_clear_object(&b);
    g_clear_error(&error);
    volume_request_free(request);
}

static void volume_mount_finished(GObject *object, GAsyncResult *result, gpointer data)
{
    GError *error = NULL;
    g_volume_mount_finish(G_VOLUME(object), result, &error);
    finish_volume_request(error, data);
}

static void mount_unmount_finished(GObject *object, GAsyncResult *result, gpointer data)
{
    GError *error = NULL;
    g_mount_unmount_with_operation_finish(G_MOUNT(object), result, &error);
    finish_volume_request(error, data);
}

static void mount_eject_finished(GObject *object, GAsyncResult *result, gpointer data)
{
    GError *error = NULL;
    g_mount_eject_with_operation_finish(G_MOUNT(object), result, &error);
    finish_volume_request(error, data);
}

static void drive_eject_finished(GObject *object, GAsyncResult *result, gpointer data)
{
    GError *error = NULL;
    g_drive_eject_with_operation_finish(G_DRIVE(object), result, &error);
    finish_volume_request(error, data);
}

static void message(Browser *b, const char *text)
{
    if (b->closing) return;
    gtk_label_set_text(GTK_LABEL(b->message), text ? text : "");
    gtk_widget_set_visible(b->message, text && *text);
}
static void show_error(Browser *b, const GError *error)
{
    if (error && !g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        message(b, error->message);
}
static GFile *info_file(GFileInfo *info)
{
    return G_FILE(g_file_info_get_attribute_object(info, "standard::file"));
}
static gboolean is_directory(GFileInfo *info)
{
    return g_file_info_get_file_type(info) == G_FILE_TYPE_DIRECTORY;
}
static char *file_title(GFile *file)
{
    char *name = g_file_get_basename(file);
    if (!name || !*name) { g_free(name); return g_file_get_parse_name(file); }
    return name;
}
static Tab *current_tab(Browser *b)
{
    return (Tab *)gtk_notebook_get_nth_page(GTK_NOTEBOOK(b->notebook),
                          gtk_notebook_get_current_page(GTK_NOTEBOOK(b->notebook)));
}
static GPtrArray *selected_files(Tab *tab)
{
    GPtrArray *files = g_ptr_array_new_with_free_func(g_object_unref);
    if (!tab) return files;
    GtkBitset *selection = gtk_selection_model_get_selection(GTK_SELECTION_MODEL(tab->selection));
    GtkBitsetIter iter; guint position;
    for (gboolean valid = gtk_bitset_iter_init_first(&iter, selection, &position); valid;
         valid = gtk_bitset_iter_next(&iter, &position)) {
        GFileInfo *info = g_list_model_get_item(G_LIST_MODEL(tab->sorted), position);
        GFile *file = info ? info_file(info) : NULL;
        if (file) g_ptr_array_add(files, g_object_ref(file));
        g_clear_object(&info);
    }
    gtk_bitset_unref(selection);
    return files;
}
static gboolean extractable(Tab *tab)
{
    if (!tab || !tab->location || !g_file_is_native(tab->location)) return FALSE;
    GtkBitset *set = gtk_selection_model_get_selection(GTK_SELECTION_MODEL(tab->selection));
    gboolean result = FALSE;
    if (gtk_bitset_get_size(set) == 1) {
        GFileInfo *info = g_list_model_get_item(G_LIST_MODEL(tab->sorted), gtk_bitset_get_minimum(set));
        const char *type = info ? g_file_info_get_content_type(info) : NULL;
        const char *types[] = { "application/zip", "application/x-tar", "application/gzip",
            "application/x-compressed-tar", "application/x-bzip", "application/x-bzip2",
            "application/x-bzip-compressed-tar", "application/x-xz", "application/x-xz-compressed-tar",
            "application/x-7z-compressed", "application/vnd.rar", "application/x-rar",
            "application/zstd", "application/x-zstd-compressed-tar", "application/x-lzip",
            "application/x-lzma", "application/x-lzma-compressed-tar", "application/x-lz4",
            "application/x-cpio", "application/x-archive", "application/x-rpm",
            "application/vnd.debian.binary-package", "application/java-archive", NULL };
        if (info && g_file_info_get_file_type(info) == G_FILE_TYPE_REGULAR && type)
            for (int i = 0; types[i]; i++)
                if (g_content_type_is_a(type, types[i])) { result = TRUE; break; }
        g_clear_object(&info);
    }
    gtk_bitset_unref(set);
    return result;
}

static void enable(Browser *b, const char *name, gboolean enabled)
{
    GSimpleAction *action = G_SIMPLE_ACTION(g_action_map_lookup_action(G_ACTION_MAP(b->actions), name));
    if (action) g_simple_action_set_enabled(action, enabled);
}
static void update_window(Browser *b)
{
    if (b->closing) return;
    Tab *tab = current_tab(b);
    if (!tab || !tab->location) return;
    char *title = file_title(tab->location);
    char *full_title = g_strdup_printf("%s — PCManager", title);
    gtk_window_set_title(GTK_WINDOW(b), full_title);
    g_free(title); g_free(full_title);
    GPtrArray *files = selected_files(tab);
    guint count = g_list_model_get_n_items(G_LIST_MODEL(tab->sorted));
    gboolean loading = gtk_directory_list_is_loading(tab->directory);
    char *status = loading ? g_strdup(_("Loading folder…")) :
        files->len ? g_strdup_printf(_("%u selected · %u items"), files->len, count) :
                     g_strdup_printf(ngettext("%u item", "%u items", count), count);
    gtk_label_set_text(GTK_LABEL(b->status), status); g_free(status);
    gtk_label_set_text(GTK_LABEL(b->space), tab->space_text ? tab->space_text : "");
    gtk_widget_set_visible(b->space, tab->space_text != NULL);
    gtk_widget_set_visible(b->spinner, loading);
    if (loading) gtk_spinner_start(GTK_SPINNER(b->spinner));
    else gtk_spinner_stop(GTK_SPINNER(b->spinner));
    gboolean available = !b->operation;
    enable(b, "back", tab->history_pos > 0);
    enable(b, "forward", tab->history_pos + 1 < (gint)tab->history->len);
    GFile *parent = g_file_get_parent(tab->location);
    enable(b, "up", parent != NULL); g_clear_object(&parent);
    enable(b, "open", files->len > 0);
    enable(b, "open-tab", files->len > 0);
    enable(b, "copy", files->len > 0);
    enable(b, "cut", files->len > 0 && available);
    enable(b, "rename", files->len == 1 && available);
    enable(b, "properties", files->len >= 1);
    enable(b, "open-with", files->len == 1);
    enable(b, "extract-here", available && extractable(tab));
    gboolean in_trash = g_file_has_uri_scheme(tab->location, "trash");
    enable(b, "trash", files->len > 0 && available && !in_trash);
    enable(b, "empty-trash", available);
    gtk_widget_set_visible(b->empty_trash, in_trash);
    enable(b, "paste", available && !b->paste_cancel);
    enable(b, "new-folder", available);
    enable(b, "new-file", available);
    g_ptr_array_unref(files);
}
static void model_changed(GListModel *model, guint position, guint removed, guint added, Tab *tab)
{
    (void)model; (void)position; (void)removed; (void)added;
    if (!tab->closing && tab->browser) update_window(tab->browser);
}
static void selection_changed(GtkSelectionModel *model, guint position, guint count, Tab *tab)
{
    (void)model; (void)position; (void)count;
    if (!tab->closing && tab->browser) update_window(tab->browser);
}
static void directory_changed(GObject *object, GParamSpec *pspec, Tab *tab)
{
    (void)object; (void)pspec;
    if (tab->closing || !tab->browser) return;
    if (!gtk_directory_list_is_loading(tab->directory)) {
        if (gtk_directory_list_get_error(tab->directory))
            g_clear_pointer(&tab->restore_selection, g_ptr_array_unref);
        else restore_state(tab);
        if (tab->refresh_pending)
            schedule_monitor_refresh(tab);
    }
    if (current_tab(tab->browser) == tab) {
        const GError *error = gtk_directory_list_get_error(tab->directory);
        if (error) show_error(tab->browser, error);
        update_window(tab->browser);
    }
}
static gboolean filter_file(gpointer item, gpointer data)
{
    Tab *tab = data; GFileInfo *info = item;
    if (!tab->browser) return FALSE;
    if (!tab->browser->show_hidden && (g_file_info_get_is_hidden(info) || g_file_info_get_is_backup(info)))
        return FALSE;
    if (!tab->query || !*tab->query) return TRUE;
    char *name = g_utf8_casefold(g_file_info_get_display_name(info), -1);
    gboolean matches = strstr(name, tab->query) != NULL;
    g_free(name); return matches;
}
static GQuark collate_key_quark = 0;

/* Collation keys are cached on the GFileInfo itself (one computation per
 * item instance, i.e. once per model rebuild) so sorting O(n log n)
 * comparisons reuse strcmp on cached keys instead of recomputing them. */
static const char *collate_key_cached(GFileInfo *info)
{
    if (!collate_key_quark)
        collate_key_quark = g_quark_from_static_string("pcmgr-collate-key");
    char *key = g_object_get_qdata(G_OBJECT(info), collate_key_quark);
    if (!key) {
        const char *display = g_file_info_get_display_name(info);
        if (!display)
            display = g_file_info_get_name(info);
        if (!display)
            display = "";
        key = g_utf8_collate_key_for_filename(display, -1);
        g_object_set_qdata_full(G_OBJECT(info), collate_key_quark, key, g_free);
    }
    return key;
}

static gint compare_files(gconstpointer left, gconstpointer right, gpointer data)
{
    GFileInfo *a = (GFileInfo *)left, *b = (GFileInfo *)right;
    gint column = GPOINTER_TO_INT(data);
    if (column == -1) return (int)is_directory(b) - (int)is_directory(a);
    if (column == 1) {
        goffset av = g_file_info_get_size(a), bv = g_file_info_get_size(b);
        if (av != bv) return av < bv ? -1 : 1;
    } else if (column == 3) {
        guint64 av = g_file_info_get_attribute_uint64(a, G_FILE_ATTRIBUTE_TIME_MODIFIED);
        guint64 bv = g_file_info_get_attribute_uint64(b, G_FILE_ATTRIBUTE_TIME_MODIFIED);
        if (av != bv) return av < bv ? -1 : 1;
    } else if (column == 2) {
        int order = g_strcmp0(g_file_info_get_content_type(a), g_file_info_get_content_type(b));
        if (order) return order;
    }
    return strcmp(collate_key_cached(a), collate_key_cached(b));
}

static gboolean unparent_popover(gpointer data)
{
    if (gtk_widget_get_parent(data)) gtk_widget_unparent(data);
    return G_SOURCE_REMOVE;
}
static void popover_closed(GtkPopover *popover, gpointer data)
{
    (void)data;
    g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, unparent_popover, g_object_ref(popover), g_object_unref);
}
static void row_context(GtkGestureClick *gesture, gint count, double x, double y, Tab *tab)
{
    (void)count;
    GtkWidget *child = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
    GtkListItem *item = g_object_get_data(G_OBJECT(child), "list-item");
    guint position = gtk_list_item_get_position(item);
    if (position == GTK_INVALID_LIST_POSITION) return;
    if (!gtk_selection_model_is_selected(GTK_SELECTION_MODEL(tab->selection), position))
        gtk_selection_model_select_item(GTK_SELECTION_MODEL(tab->selection), position, TRUE);
    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
    double vx, vy;
    GtkWidget *view = gtk_widget_get_ancestor(child, GTK_TYPE_COLUMN_VIEW);
    if (!view) view = gtk_widget_get_ancestor(child, GTK_TYPE_GRID_VIEW);
    if (view && gtk_widget_translate_coordinates(child, view, x, y, &vx, &vy))
        show_context(tab, view, vx, vy);
}
/* GTK4 file drags export a typed file list (and its text/uri-list serializer).
 * Drops inside PCManager run through files_dropped/drop_read_ready, which
 * flags the drag as internally handled. Any other destination that accepts a
 * MOVE owns the data afterwards, so drag-end must delete the source files —
 * through the Trash, keeping a completed external move recoverable. */
static GdkContentProvider *drag_prepare(GtkDragSource *source, double x, double y, Tab *tab)
{
    (void)x; (void)y;
    if (tab->closing || !tab->browser || tab->browser->operation) return NULL;
    GtkWidget *widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(source));
    GtkListItem *item = g_object_get_data(G_OBJECT(widget), "list-item");
    guint position = gtk_list_item_get_position(item);
    if (position == GTK_INVALID_LIST_POSITION) return NULL;
    if (!gtk_selection_model_is_selected(GTK_SELECTION_MODEL(tab->selection), position))
        gtk_selection_model_select_item(GTK_SELECTION_MODEL(tab->selection), position, TRUE);
    GPtrArray *files = selected_files(tab);
    /* A monitor rebuild between press and prepare can recycle this row for
     * a different file while the position selection still describes the old
     * one. Never export a stale row's file: fall back to exactly the file
     * this row shows now. */
    GFileInfo *pressed_info = gtk_list_item_get_item(item); /* transfer none */
    GFile *pressed = pressed_info ? info_file(pressed_info) : NULL;
    gboolean have_pressed = FALSE;
    if (pressed) for (guint i = 0; i < files->len; i++)
        if (g_file_equal(pressed, g_ptr_array_index(files, i))) { have_pressed = TRUE; break; }
    if (pressed && !have_pressed) {
        g_ptr_array_set_size(files, 0);
        g_ptr_array_add(files, g_object_ref(pressed));
    }
    if (!files->len) { g_ptr_array_unref(files); return NULL; }
    GdkFileList *list = gdk_file_list_new_from_array((GFile **)files->pdata, files->len);
    GdkContentProvider *provider = gdk_content_provider_new_typed(GDK_TYPE_FILE_LIST, list);
    g_boxed_free(GDK_TYPE_FILE_LIST, list);
    g_object_set_data_full(G_OBJECT(source), "files", files, (GDestroyNotify)g_ptr_array_unref);
    GtkWidget *icon = g_object_get_data(G_OBJECT(widget), "icon");
    GdkPaintable *paintable = gtk_widget_paintable_new(icon ? icon : widget);
    gtk_drag_source_set_icon(source, paintable, 0, 0); g_object_unref(paintable);
    return provider;
}
static void drag_begin(GtkDragSource *source, GdkDrag *drag, gpointer data)
{
    (void)data;
    GPtrArray *files = g_object_get_data(G_OBJECT(source), "files");
    if (files) g_object_set_data_full(G_OBJECT(drag), "pcmgr-files", g_ptr_array_ref(files), (GDestroyNotify)g_ptr_array_unref);
}
typedef struct { Browser *browser; GCancellable *cancel; } DragCleanup;
static void drag_move_done(GObject *object, GAsyncResult *result, gpointer data)
{
    (void)object;
    DragCleanup *cleanup = data;
    Browser *b = cleanup->browser;
    GError *error = NULL;
    gboolean ok = pcmgr_file_operation_finish(result, &error);
    if (!ok && !b->closing) show_error(b, error);
    if (b->operation == cleanup->cancel)
        g_clear_object(&b->operation);
    g_clear_error(&error);
    g_clear_object(&cleanup->cancel);
    g_object_unref(b);
    g_free(cleanup);
}
static void drag_end(GtkDragSource *source, GdkDrag *drag, gboolean delete_data, gpointer data)
{
    (void)data;
    GPtrArray *files = g_object_steal_data(G_OBJECT(source), "files");
    if (files && files->len && delete_data &&
        !g_object_get_data(G_OBJECT(drag), "pcmgr-internal-drop")) {
        /* An external destination accepted a MOVE and now owns the data.
         * PCManager is single-instance, so any same-process drop was flagged
         * above and never reaches this path. Trash (never delete) so a
         * completed external move stays recoverable; failures are reported
         * and the files are left in place. */
        GtkWidget *widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(source));
        GtkRoot *root = widget ? gtk_widget_get_root(widget) : NULL;
        if (root && G_OBJECT_TYPE(root) == pcmgr_browser_get_type()) {
            Browser *browser = (Browser *)root;
            if (!browser->operation) {
                GCancellable *cancel = g_cancellable_new();
                DragCleanup *cleanup = g_new0(DragCleanup, 1);
                cleanup->browser = g_object_ref(browser);
                cleanup->cancel = g_object_ref(cancel);
                browser->operation = g_object_ref(cancel);
                pcmgr_file_operation_async(PCMGR_TRASH, files, NULL, cancel,
                                           drag_move_done, cleanup);
                g_object_unref(cancel);
            }
        }
    }
    if (files)
        g_ptr_array_unref(files);
}
static void replace_drop_highlight(GtkEventController *controller, GWeakRef *ref);
static void clear_drop_highlight(GtkEventController *controller)
{
    GWeakRef *ref = g_object_get_data(G_OBJECT(controller), "highlight");
    GtkWidget *old = ref ? g_weak_ref_get(ref) : NULL;
    if (old) { gtk_widget_unset_state_flags(old, GTK_STATE_FLAG_DROP_ACTIVE); g_object_unref(old); }
    replace_drop_highlight(controller, NULL);
}
static GFile *drop_destination(GtkEventController *controller, double x, double y, Browser **browser)
{
    GtkWidget *widget = gtk_event_controller_get_widget(controller);
    Tab *tab = g_object_get_data(G_OBJECT(widget), "drop-tab");
    *browser = tab ? tab->browser : g_object_get_data(G_OBJECT(widget), "drop-browser");
    if (!*browser || (*browser)->closing || (*browser)->operation || (tab && tab->closing)) return NULL;
    GFile *file = g_object_get_data(G_OBJECT(widget), "file");
    if (file) return g_object_ref(file); /* Places row */
    if (!tab || !tab->location) return NULL;
    if (GTK_IS_COLUMN_VIEW(widget) || GTK_IS_GRID_VIEW(widget)) {
        GtkWidget *picked = gtk_widget_pick(widget, x, y, GTK_PICK_DEFAULT);
        for (; picked && picked != widget; picked = gtk_widget_get_parent(picked)) {
            GtkListItem *item = g_object_get_data(G_OBJECT(picked), "list-item");
            if (!item) continue;
            GFileInfo *info = gtk_list_item_get_item(item);
            if (!info || !is_directory(info) || !info_file(info)) return NULL;
            return g_object_ref(info_file(info));
        }
    }
    return g_object_ref(tab->location); /* Empty area or destination tab */
}
/* Drag-motion stat cache: drop_motion fires at pointer rate, so device
 * lookups for the destination and source parents are cached per path with
 * a short TTL (5 s). Repeated motions over the same folders avoid
 * re-statting; entries expire quickly so remounts do not go stale. */
typedef struct { dev_t dev; gboolean ok; gint64 time; } DropDevEntry;
static GHashTable *drop_dev_cache = NULL;

static gboolean drop_dev_for_path(const char *path, struct stat *out)
{
    gint64 now = g_get_monotonic_time();
    if (!drop_dev_cache)
        drop_dev_cache = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    else if (g_hash_table_size(drop_dev_cache) > 512)
        g_hash_table_remove_all(drop_dev_cache);
    DropDevEntry *cached = path ? g_hash_table_lookup(drop_dev_cache, path) : NULL;
    if (cached && now - cached->time < 5 * G_TIME_SPAN_SECOND) {
        if (!cached->ok)
            return FALSE;
        out->st_dev = cached->dev;
        return TRUE;
    }
    struct stat st;
    gboolean ok = path && g_stat(path, &st) == 0;
    DropDevEntry *entry = g_new(DropDevEntry, 1);
    entry->ok = ok;
    entry->time = now;
    entry->dev = ok ? st.st_dev : 0;
    if (path)
        g_hash_table_insert(drop_dev_cache, g_strdup(path), entry);
    else
        g_free(entry);
    if (!ok)
        return FALSE;
    *out = st;
    return TRUE;
}

static GdkDragAction choose_drop_action(GdkDrop *drop, GFile *destination, GdkModifierType modifiers)
{
    GdkDragAction offered = gdk_drop_get_actions(drop);
    if ((modifiers & GDK_CONTROL_MASK) && (modifiers & GDK_SHIFT_MASK)) return 0; /* no links */
    if (g_file_has_uri_scheme(destination, "trash"))
        return !(modifiers & GDK_CONTROL_MASK) && (offered & GDK_ACTION_MOVE) ? GDK_ACTION_MOVE : 0;
    if (modifiers & GDK_CONTROL_MASK) return offered & GDK_ACTION_COPY;
    if (modifiers & GDK_SHIFT_MASK) return offered & GDK_ACTION_MOVE;
    GdkDrag *drag = gdk_drop_get_drag(drop);
    GPtrArray *files = drag ? g_object_get_data(G_OBJECT(drag), "pcmgr-files") : NULL;
    gboolean same_device = files && files->len;
    char *path = g_file_get_path(destination); struct stat dest;
    if (!drop_dev_for_path(path, &dest)) same_device = FALSE;
    g_free(path);
    for (guint i = 0; same_device && i < files->len; i++) {
        GFile *parent = g_file_get_parent(g_ptr_array_index(files, i));
        char *parent_path = parent ? g_file_get_path(parent) : NULL; struct stat src;
        same_device = drop_dev_for_path(parent_path, &src) && src.st_dev == dest.st_dev;
        g_free(parent_path); g_clear_object(&parent);
    }
    if (same_device && (offered & GDK_ACTION_MOVE)) return GDK_ACTION_MOVE;
    return offered & GDK_ACTION_COPY; /* external/remote drags default to copy */
}
static void drop_highlight_free(gpointer data)
{
    GWeakRef *ref = data; g_weak_ref_clear(ref); g_free(ref);
}
static void replace_drop_highlight(GtkEventController *controller, GWeakRef *ref)
{
    GWeakRef *old = g_object_steal_data(G_OBJECT(controller), "highlight");
    if (old) drop_highlight_free(old);
    if (ref) g_object_set_data_full(G_OBJECT(controller), "highlight", ref, drop_highlight_free);
}
static GdkDragAction drop_motion(GtkDropTargetAsync *target, GdkDrop *drop, double x, double y, gpointer data)
{
    (void)data;
    GtkEventController *controller = GTK_EVENT_CONTROLLER(target);
    clear_drop_highlight(controller);
    Browser *b = NULL; GFile *destination = drop_destination(controller, x, y, &b);
    if (!destination) return 0;
    GdkDragAction action = choose_drop_action(drop, destination, gtk_event_controller_get_current_event_state(controller));
    g_object_unref(destination);
    if (action) {
        GtkWidget *widget = gtk_event_controller_get_widget(controller);
        GtkWidget *highlight = widget;
        for (GtkWidget *picked = gtk_widget_pick(widget, x, y, GTK_PICK_DEFAULT); picked && picked != widget; picked = gtk_widget_get_parent(picked))
            if (g_object_get_data(G_OBJECT(picked), "list-item")) { highlight = picked; break; }
        gtk_widget_set_state_flags(highlight, GTK_STATE_FLAG_DROP_ACTIVE, FALSE);
        GWeakRef *ref = g_new0(GWeakRef, 1); g_weak_ref_init(ref, highlight);
        replace_drop_highlight(controller, ref);
    }
    return action;
}
static void drop_leave(GtkDropTargetAsync *target, GdkDrop *drop, gpointer data)
{
    (void)drop; (void)data; clear_drop_highlight(GTK_EVENT_CONTROLLER(target));
}
typedef struct { Browser *browser; GFile *destination; GdkDrop *drop; GdkDragAction action; } FileDrop;
static void drop_read_ready(GObject *object, GAsyncResult *result, gpointer data)
{
    FileDrop *request = data; Browser *b = request->browser; GError *error = NULL;
    const GValue *value = gdk_drop_read_value_finish(GDK_DROP(object), result, &error);
    GPtrArray *files = g_ptr_array_new_with_free_func(g_object_unref);
    if (value) for (GSList *l = gdk_file_list_get_files(g_value_get_boxed(value)); l; l = l->next)
        g_ptr_array_add(files, g_object_ref(l->data));
    gboolean cancelled = b->operation && g_cancellable_is_cancelled(b->operation);
    g_clear_object(&b->operation);
    if (files->len && !error && !cancelled && !b->closing) {
        /* Same-process drags carry our marker; flag them so drag-end never
         * deletes files this drop already moved, copied, or trashed. */
        GdkDrag *drag = gdk_drop_get_drag(request->drop);
        if (drag && g_object_get_data(G_OBJECT(drag), "pcmgr-files"))
            g_object_set_data(G_OBJECT(drag), "pcmgr-internal-drop", GINT_TO_POINTER(TRUE));
        b->operation_drop = g_object_ref(request->drop);
        b->drop_action = request->action;
        PcmgrFileOperation op = g_file_has_uri_scheme(request->destination, "trash") ? PCMGR_TRASH :
            request->action == GDK_ACTION_MOVE ? PCMGR_MOVE : PCMGR_COPY;
        start_operation(b, op, files, request->destination);
    } else {
        gdk_drop_finish(request->drop, 0);
        if (!b->closing) {
            gtk_widget_set_visible(b->operation_box, FALSE);
            if (error) show_error(b, error);
            else if (!cancelled) message(b, _("The dragged content does not contain files."));
            update_window(b);
        }
    }
    g_ptr_array_unref(files); g_clear_error(&error);
    g_object_unref(request->drop); g_object_unref(request->destination); g_object_unref(b); g_free(request);
}
static gboolean files_dropped(GtkDropTargetAsync *target, GdkDrop *drop, double x, double y, gpointer data)
{
    (void)data;
    GtkEventController *controller = GTK_EVENT_CONTROLLER(target);
    clear_drop_highlight(controller);
    Browser *b = NULL; GFile *destination = drop_destination(controller, x, y, &b);
    if (!destination) return FALSE;
    GdkDragAction action = choose_drop_action(drop, destination, gtk_event_controller_get_current_event_state(controller));
    if (!action) { g_object_unref(destination); return FALSE; }
    FileDrop *request = g_new0(FileDrop, 1);
    request->browser = g_object_ref(b); request->destination = destination;
    request->drop = g_object_ref(drop); request->action = action;
    b->operation = g_cancellable_new();
    gtk_label_set_text(GTK_LABEL(b->operation_label), _("Reading dragged files…"));
    if (b->operation_progress) gtk_widget_set_visible(b->operation_progress, FALSE);
    gtk_widget_set_visible(b->operation_box, TRUE); update_window(b);
    gdk_drop_read_value_async(drop, GDK_TYPE_FILE_LIST, G_PRIORITY_DEFAULT, b->operation, drop_read_ready, request);
    return TRUE;
}
static void add_drop_target(GtkWidget *widget, Tab *tab, Browser *browser)
{
    g_object_set_data(G_OBJECT(widget), "drop-tab", tab);
    g_object_set_data(G_OBJECT(widget), "drop-browser", browser);
    GdkContentFormats *formats = gdk_content_formats_new_for_gtype(GDK_TYPE_FILE_LIST);
    formats = gdk_content_formats_union_deserialize_mime_types(formats);
    GtkDropTargetAsync *target = gtk_drop_target_async_new(formats, GDK_ACTION_COPY | GDK_ACTION_MOVE);
    g_signal_connect(target, "drag-enter", G_CALLBACK(drop_motion), NULL);
    g_signal_connect(target, "drag-motion", G_CALLBACK(drop_motion), NULL);
    g_signal_connect(target, "drag-leave", G_CALLBACK(drop_leave), NULL);
    g_signal_connect(target, "drop", G_CALLBACK(files_dropped), NULL);
    gtk_widget_add_controller(widget, GTK_EVENT_CONTROLLER(target));
}

static void factory_setup(GtkSignalListItemFactory *factory, GtkListItem *item, Tab *tab)
{
    gint column = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(factory), "column"));
    GtkWidget *child;
    if (column == 0 || column == 4) {
        child = gtk_box_new(column == 4 ? GTK_ORIENTATION_VERTICAL : GTK_ORIENTATION_HORIZONTAL, 6);
        GtkWidget *image = gtk_image_new();
        gtk_image_set_pixel_size(GTK_IMAGE(image), column == 4 ? 48 : 24);
        GtkWidget *label = gtk_label_new(NULL);
        gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_MIDDLE);
        if (column == 4) {
            gtk_widget_set_size_request(child, 112, 92);
            gtk_label_set_max_width_chars(GTK_LABEL(label), 16);
            gtk_label_set_wrap(GTK_LABEL(label), TRUE);
            gtk_label_set_lines(GTK_LABEL(label), 2);
            gtk_widget_set_margin_top(child, 10); gtk_widget_set_margin_bottom(child, 10);
        } else {
            gtk_label_set_xalign(GTK_LABEL(label), 0);
            gtk_widget_set_hexpand(label, TRUE);
        }
        gtk_box_append(GTK_BOX(child), image); gtk_box_append(GTK_BOX(child), label);
        g_object_set_data(G_OBJECT(child), "icon", image);
        g_object_set_data(G_OBJECT(child), "label", label);
    } else {
        child = gtk_label_new(NULL);
        gtk_label_set_xalign(GTK_LABEL(child), column == 1 ? 1 : 0);
        g_object_set_data(G_OBJECT(child), "label", child);
    }
    gtk_widget_set_margin_start(child, 6); gtk_widget_set_margin_end(child, 6);
    g_object_set_data(G_OBJECT(child), "list-item", item);
    GtkGesture *gesture = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gesture), 3);
    gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(gesture), GTK_PHASE_CAPTURE);
    g_signal_connect(gesture, "pressed", G_CALLBACK(row_context), tab);
    gtk_widget_add_controller(child, GTK_EVENT_CONTROLLER(gesture));
    GtkDragSource *drag = gtk_drag_source_new();
    gtk_drag_source_set_actions(drag, GDK_ACTION_COPY | GDK_ACTION_MOVE);
    g_signal_connect(drag, "prepare", G_CALLBACK(drag_prepare), tab);
    g_signal_connect(drag, "drag-begin", G_CALLBACK(drag_begin), NULL);
    g_signal_connect(drag, "drag-end", G_CALLBACK(drag_end), NULL);
    gtk_widget_add_controller(child, GTK_EVENT_CONTROLLER(drag));
    gtk_list_item_set_child(item, child);
}
static void factory_bind(GtkSignalListItemFactory *factory, GtkListItem *item, gpointer data)
{
    (void)data;
    GFileInfo *info = gtk_list_item_get_item(item);
    GtkWidget *child = gtk_list_item_get_child(item);
    GtkLabel *label = g_object_get_data(G_OBJECT(child), "label");
    gint column = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(factory), "column"));
    char *text = NULL;
    if (column == 0 || column == 4) {
        GtkImage *image = g_object_get_data(G_OBJECT(child), "icon");
        GIcon *icon = g_file_info_get_icon(info);
        if (icon) gtk_image_set_from_gicon(image, icon);
        else gtk_image_set_from_icon_name(image, is_directory(info) ? "folder" : "text-x-generic");
        text = g_strdup(g_file_info_get_display_name(info));
        gtk_widget_set_tooltip_text(child, text);
    } else if (column == 1) text = is_directory(info) ? g_strdup("—") : g_format_size(g_file_info_get_size(info));
    else if (column == 2) {
        const char *type = g_file_info_get_content_type(info);
        text = is_directory(info) ? g_strdup(_("Folder")) : type ? g_content_type_get_description(type) : g_strdup(_("File"));
    } else {
        GDateTime *date = g_file_info_get_modification_date_time(info);
        text = date ? g_date_time_format(date, "%x %H:%M") : g_strdup("—");
        if (date) g_date_time_unref(date);
    }
    gtk_label_set_text(label, text); g_free(text);
}
static GtkListItemFactory *make_factory(Tab *tab, gint column)
{
    GtkListItemFactory *factory = gtk_signal_list_item_factory_new();
    g_object_set_data(G_OBJECT(factory), "column", GINT_TO_POINTER(column));
    g_signal_connect(factory, "setup", G_CALLBACK(factory_setup), tab);
    g_signal_connect(factory, "bind", G_CALLBACK(factory_bind), NULL);
    return factory;
}
static void launched(GObject *object, GAsyncResult *result, gpointer data)
{
    (void)object;
    Browser *b = data; GError *error = NULL;
    g_app_info_launch_default_for_uri_finish(result, &error);
    show_error(b, error); g_clear_error(&error); g_object_unref(b);
}
static void launch_file(Browser *b, GFile *file)
{
    char *uri = g_file_get_uri(file);
    GAppLaunchContext *context = G_APP_LAUNCH_CONTEXT(gdk_display_get_app_launch_context(
                                                      gtk_widget_get_display(GTK_WIDGET(b))));
    g_app_info_launch_default_for_uri_async(uri, context, NULL, launched, g_object_ref(b));
    g_object_unref(context); g_free(uri);
}
static void activate_file(GtkWidget *view, guint position, Tab *tab)
{
    (void)view;
    GFileInfo *info = g_list_model_get_item(G_LIST_MODEL(tab->sorted), position);
    if (!info) return;
    GFile *file = info_file(info);
    if (file) {
        if (is_directory(info)) navigate(tab, file, TRUE);
        else launch_file(tab->browser, file);
    }
    g_object_unref(info);
}
typedef struct { GWeakRef tab; GCancellable *cancel; } SpaceRequest;
static void space_ready(GObject *source, GAsyncResult *result, gpointer data)
{
    SpaceRequest *request = data;
    GError *error = NULL;
    GFileInfo *info = g_file_query_filesystem_info_finish(G_FILE(source), result, &error);
    Tab *tab = g_weak_ref_get(&request->tab);
    if (tab && !tab->closing && tab->space_query == request->cancel &&
        !g_cancellable_is_cancelled(request->cancel)) {
        g_clear_pointer(&tab->space_text, g_free);
        if (info && g_file_info_has_attribute(info, G_FILE_ATTRIBUTE_FILESYSTEM_FREE) &&
            g_file_info_has_attribute(info, G_FILE_ATTRIBUTE_FILESYSTEM_SIZE)) {
            guint64 size = g_file_info_get_attribute_uint64(info, G_FILE_ATTRIBUTE_FILESYSTEM_SIZE);
            guint64 free_bytes = g_file_info_get_attribute_uint64(info, G_FILE_ATTRIBUTE_FILESYSTEM_FREE);
            guint64 used = g_file_info_has_attribute(info, G_FILE_ATTRIBUTE_FILESYSTEM_USED) ?
                g_file_info_get_attribute_uint64(info, G_FILE_ATTRIBUTE_FILESYSTEM_USED) :
                size - MIN(size, free_bytes);
            char *used_text = g_format_size(used), *free_text = g_format_size(free_bytes);
            tab->space_text = g_strdup_printf(_("%s used · %s free"), used_text, free_text);
            g_free(used_text); g_free(free_text);
        }
        g_clear_object(&tab->space_query);
        if (tab->browser) update_window(tab->browser);
    }
    g_clear_object(&tab); g_clear_object(&info); g_clear_error(&error);
    g_weak_ref_clear(&request->tab); g_object_unref(request->cancel); g_free(request);
}
static void query_space(Tab *tab)
{
    if (tab->closing || !tab->location) return;
#ifdef PCMANAGER_TESTING
    ++tab->space_query_count;
#endif
    if (tab->space_query) g_cancellable_cancel(tab->space_query);
    g_clear_object(&tab->space_query);
    /* Virtual locations such as Trash have no single filesystem capacity. */
    if (!g_file_is_native(tab->location)) {
        g_clear_pointer(&tab->space_text, g_free);
        return;
    }
    tab->space_query = g_cancellable_new();
    SpaceRequest *request = g_new0(SpaceRequest, 1);
    g_weak_ref_init(&request->tab, tab); request->cancel = g_object_ref(tab->space_query);
    g_file_query_filesystem_info_async(tab->location,
        G_FILE_ATTRIBUTE_FILESYSTEM_SIZE "," G_FILE_ATTRIBUTE_FILESYSTEM_FREE "," G_FILE_ATTRIBUTE_FILESYSTEM_USED,
        G_PRIORITY_DEFAULT, tab->space_query, space_ready, request);
}
static gboolean space_tick(gpointer data)
{
    Tab *tab = data;
    if (!tab->closing && tab->browser && current_tab(tab->browser) == tab && !tab->space_query)
        query_space(tab);
    return G_SOURCE_CONTINUE;
}

static GtkWidget *current_view(Tab *tab)
{
    const char *name = gtk_stack_get_visible_child_name(GTK_STACK(tab->views));
    if (name && !strcmp(name, "icons")) return tab->grid;
    return tab->list;
}
/* Lowest model position among mapped row widgets: the anchor that was at
 * the top of the viewport. Unmapped (virtualized) rows prune themselves,
 * so this only ever sees what the user was actually looking at. */
static void collect_anchor(GtkWidget *w, guint *best, char **name)
{
    if (!gtk_widget_get_mapped(w)) return;
    GtkListItem *item = g_object_get_data(G_OBJECT(w), "list-item");
    if (item) {
        guint position = gtk_list_item_get_position(item);
        if (position != GTK_INVALID_LIST_POSITION && position < *best) {
            GFileInfo *info = gtk_list_item_get_item(item);
            if (info) {
                *best = position;
                g_free(*name); *name = g_strdup(g_file_info_get_name(info));
            }
        }
        return;
    }
    for (GtkWidget *c = gtk_widget_get_first_child(w); c; c = gtk_widget_get_next_sibling(c))
        collect_anchor(c, best, name);
}
/* A model rebuild drops the position-based selection and jumps the scroll
 * offset. Snapshot the selected basenames plus the viewport anchor before
 * the swap; directory_changed() restores both once loading finishes. */
static void snapshot_state(Tab *tab)
{
    g_clear_pointer(&tab->restore_selection, g_ptr_array_unref);
    g_free(tab->restore_anchor); tab->restore_anchor = NULL;
    if (!tab->selection || !tab->sorted) return;
    GPtrArray *names = g_ptr_array_new_with_free_func(g_free);
    GtkBitset *set = gtk_selection_model_get_selection(GTK_SELECTION_MODEL(tab->selection));
    GtkBitsetIter iter; guint position;
    if (gtk_bitset_iter_init_first(&iter, set, &position)) do {
        GFileInfo *info = g_list_model_get_item(G_LIST_MODEL(tab->sorted), position);
        if (info) {
            g_ptr_array_add(names, g_strdup(g_file_info_get_name(info)));
            g_object_unref(info);
        }
    } while (gtk_bitset_iter_next(&iter, &position));
    gtk_bitset_unref(set);
    guint best = G_MAXUINT;
    collect_anchor(current_view(tab), &best, &tab->restore_anchor);
    tab->restore_selection = names;
    gtk_selection_model_unselect_all(GTK_SELECTION_MODEL(tab->selection));
}
static guint find_name(Tab *tab, const char *name)
{
    guint n = g_list_model_get_n_items(G_LIST_MODEL(tab->sorted));
    for (guint i = 0; i < n; i++) {
        GFileInfo *info = g_list_model_get_item(G_LIST_MODEL(tab->sorted), i);
        if (!info) continue;
        gboolean match = !strcmp(g_file_info_get_name(info), name);
        g_object_unref(info);
        if (match) return i;
    }
    return GTK_INVALID_LIST_POSITION;
}
static void restore_state(Tab *tab)
{
    if (!tab->restore_selection || !tab->selection || !tab->sorted) {
        g_clear_pointer(&tab->restore_selection, g_ptr_array_unref);
        g_free(tab->restore_anchor); tab->restore_anchor = NULL;
        return;
    }
    GHashTable *wanted = g_hash_table_new(g_str_hash, g_str_equal);
    for (guint i = 0; i < tab->restore_selection->len; i++)
        g_hash_table_add(wanted, g_ptr_array_index(tab->restore_selection, i));
    guint n = g_list_model_get_n_items(G_LIST_MODEL(tab->sorted));
    for (guint i = 0; i < n; i++) {
        GFileInfo *info = g_list_model_get_item(G_LIST_MODEL(tab->sorted), i);
        if (!info) continue;
        if (g_hash_table_contains(wanted, g_file_info_get_name(info)))
            gtk_selection_model_select_item(GTK_SELECTION_MODEL(tab->selection), i, FALSE);
        g_object_unref(info);
    }
    g_hash_table_unref(wanted);
    /* Scroll the anchor (or the first surviving selection) back into view.
     * scroll_to() realizes what it needs, so unlike raw adjustment writes
     * this works with virtualized, not-yet-measured rows. */
    guint target = GTK_INVALID_LIST_POSITION;
    if (tab->restore_anchor)
        target = find_name(tab, tab->restore_anchor);
    if (target == GTK_INVALID_LIST_POSITION && tab->restore_selection->len)
        target = find_name(tab, g_ptr_array_index(tab->restore_selection, 0));
    if (target != GTK_INVALID_LIST_POSITION) {
        const char *visible = gtk_stack_get_visible_child_name(GTK_STACK(tab->views));
        if (visible && !strcmp(visible, "icons"))
            gtk_grid_view_scroll_to(GTK_GRID_VIEW(tab->grid), target, GTK_LIST_SCROLL_NONE, NULL);
        else
            gtk_column_view_scroll_to(GTK_COLUMN_VIEW(tab->list), target, NULL, GTK_LIST_SCROLL_NONE, NULL);
    }
    g_clear_pointer(&tab->restore_selection, g_ptr_array_unref);
    g_free(tab->restore_anchor); tab->restore_anchor = NULL;
}
static void refresh(Tab *tab, gboolean preserve)
{
    if (!tab || !tab->location) return;
    if (preserve && gtk_directory_list_is_loading(tab->directory)) {
        tab->refresh_pending = TRUE;
        schedule_monitor_refresh(tab);
        return;
    }
    if (preserve) snapshot_state(tab);
    else { g_clear_pointer(&tab->restore_selection, g_ptr_array_unref); g_free(tab->restore_anchor); tab->restore_anchor = NULL; }
    /* Full model replacement per event is heavy, but a true incremental
     * update of GtkDirectoryList/filter/sort/selection is too invasive
     * here; keep the debounced, selection/scroll-preserving rebuild and
     * rely on WATCH_MOVES so renames arrive as moves (coalesced below). */
    /* A fresh model also drops stale monitor events from the previous load. */
    g_signal_handlers_disconnect_by_data(tab->directory, tab);
    GtkDirectoryList *directory = gtk_directory_list_new(FILE_ATTRIBUTES, NULL);
    gtk_directory_list_set_monitored(directory, FALSE);
    gtk_directory_list_set_file(directory, tab->location);
    gtk_filter_list_model_set_model(tab->filtered, G_LIST_MODEL(directory));
    g_set_object(&tab->directory, directory); g_object_unref(directory);
    g_signal_connect(tab->directory, "notify::loading", G_CALLBACK(directory_changed), tab);
    g_signal_connect(tab->directory, "notify::error", G_CALLBACK(directory_changed), tab);
}
static gboolean monitor_refresh(gpointer data)
{
    Tab *tab = data; tab->refresh_source = 0;
    if (tab->closing || !tab->browser || current_tab(tab->browser) != tab)
        return G_SOURCE_REMOVE;
    if (gtk_directory_list_is_loading(tab->directory))
        return G_SOURCE_REMOVE;
    tab->refresh_pending = FALSE;
    tab->refresh_started = 0;
    refresh(tab, TRUE);
    return G_SOURCE_REMOVE;
}
static void schedule_monitor_refresh(Tab *tab)
{
    if (tab->closing || !tab->browser || current_tab(tab->browser) != tab || !tab->refresh_pending)
        return;
    gint64 now = g_get_monotonic_time();
    if (!tab->refresh_started) tab->refresh_started = now;
    gint64 deadline = MIN(now + 250000, tab->refresh_started + 2000000);
    guint delay = (guint)MAX((gint64)1, (deadline - now + 999) / 1000);
    if (tab->refresh_source) g_source_remove(tab->refresh_source);
    tab->refresh_source = g_timeout_add(delay, monitor_refresh, tab);
}
static void folder_changed(GFileMonitor *monitor, GFile *file, GFile *other, GFileMonitorEvent event, Tab *tab)
{
    (void)monitor; (void)file; (void)other; (void)event;
    if (!tab->closing) {
        tab->refresh_pending = TRUE;
        schedule_monitor_refresh(tab);
    }
}
static void navigate(Tab *tab, GFile *file, gboolean record)
{
    if (tab->closing) return;
    g_clear_pointer(&tab->space_text, g_free);
    g_set_object(&tab->location, file);
    if (record && (tab->history_pos < 0 || !g_file_equal(file, g_ptr_array_index(tab->history, tab->history_pos)))) {
        g_ptr_array_set_size(tab->history, tab->history_pos + 1);
        g_ptr_array_add(tab->history, g_object_ref(file));
        tab->history_pos++;
    }
    g_clear_pointer(&tab->query, g_free);
    gtk_filter_changed(GTK_FILTER(tab->filter), GTK_FILTER_CHANGE_DIFFERENT);
    char *name = file_title(file); gtk_label_set_text(GTK_LABEL(tab->label), name); g_free(name);
    if (tab->browser && current_tab(tab->browser) == tab) {
        char *path = g_file_get_parse_name(file);
        gtk_editable_set_text(GTK_EDITABLE(tab->browser->location), path); g_free(path);
        gtk_editable_set_text(GTK_EDITABLE(tab->browser->search), "");
        message(tab->browser, NULL);
    }
    if (tab->refresh_source) { g_source_remove(tab->refresh_source); tab->refresh_source = 0; }
    tab->refresh_pending = FALSE;
    tab->refresh_started = 0;
    if (tab->monitor) g_file_monitor_cancel(tab->monitor);
    g_clear_object(&tab->monitor);
    tab->monitor = g_file_monitor_directory(file, G_FILE_MONITOR_WATCH_MOVES, NULL, NULL);
    if (tab->monitor) {
        g_file_monitor_set_rate_limit(tab->monitor, 250);
        g_signal_connect(tab->monitor, "changed", G_CALLBACK(folder_changed), tab);
    }
    query_space(tab);
    if (!tab->space_timer) tab->space_timer = g_timeout_add_seconds(10, space_tick, tab);
    refresh(tab, FALSE);
    if (tab->browser) update_window(tab->browser);
}
static void tab_close_clicked(GtkButton *button, Tab *tab)
{
    (void)button;
    Browser *b = tab->browser;
    if (b->operation) { message(b, _("Wait for the file operation to finish, or cancel it first.")); return; }
    if (gtk_notebook_get_n_pages(GTK_NOTEBOOK(b->notebook)) == 1) gtk_window_close(GTK_WINDOW(b));
    else gtk_notebook_remove_page(GTK_NOTEBOOK(b->notebook),
                                  gtk_notebook_page_num(GTK_NOTEBOOK(b->notebook), GTK_WIDGET(tab)));
}
static void empty_context(GtkGestureClick *gesture, gint count, double x, double y, Tab *tab)
{
    (void)count;
    if (gtk_gesture_is_recognized(GTK_GESTURE(gesture)))
        show_context(tab, gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture)), x, y);
}
static void pcmgr_tab_dispose(GObject *object)
{
    Tab *tab = (Tab *)object;
    tab->closing = TRUE; tab->browser = NULL;
    if (tab->space_timer) { g_source_remove(tab->space_timer); tab->space_timer = 0; }
    if (tab->space_query) g_cancellable_cancel(tab->space_query);
    g_clear_object(&tab->space_query);
    g_clear_pointer(&tab->space_text, g_free);
    if (tab->refresh_source) { g_source_remove(tab->refresh_source); tab->refresh_source = 0; }
    if (tab->monitor) g_file_monitor_cancel(tab->monitor);
    g_clear_object(&tab->monitor);
    if (tab->directory) g_signal_handlers_disconnect_by_data(tab->directory, tab);
    if (tab->sorted) g_signal_handlers_disconnect_by_data(tab->sorted, tab);
    if (tab->selection) g_signal_handlers_disconnect_by_data(tab->selection, tab);
    g_clear_object(&tab->directory); g_clear_object(&tab->filter);
    g_clear_object(&tab->filtered); g_clear_object(&tab->sorted); g_clear_object(&tab->selection);
    g_clear_object(&tab->location); g_clear_pointer(&tab->history, g_ptr_array_unref);
    g_clear_pointer(&tab->query, g_free);
    g_clear_pointer(&tab->restore_selection, g_ptr_array_unref);
    g_free(tab->restore_anchor); tab->restore_anchor = NULL;
    G_OBJECT_CLASS(pcmgr_tab_parent_class)->dispose(object);
}
static void pcmgr_tab_class_init(TabClass *klass)
{
    G_OBJECT_CLASS(klass)->dispose = pcmgr_tab_dispose;
}
static void pcmgr_tab_init(Tab *tab)
{
    gtk_orientable_set_orientation(GTK_ORIENTABLE(tab), GTK_ORIENTATION_VERTICAL);
    tab->history = g_ptr_array_new_with_free_func(g_object_unref); tab->history_pos = -1;
    tab->directory = gtk_directory_list_new(FILE_ATTRIBUTES, NULL);
    gtk_directory_list_set_monitored(tab->directory, FALSE);
    tab->filter = gtk_custom_filter_new(filter_file, tab, NULL);
    tab->filtered = gtk_filter_list_model_new(G_LIST_MODEL(g_object_ref(tab->directory)), GTK_FILTER(g_object_ref(tab->filter)));
    tab->sorted = gtk_sort_list_model_new(G_LIST_MODEL(g_object_ref(tab->filtered)), NULL);
    tab->selection = gtk_multi_selection_new(G_LIST_MODEL(g_object_ref(tab->sorted)));
    tab->views = gtk_stack_new();
    tab->list = gtk_column_view_new(GTK_SELECTION_MODEL(g_object_ref(tab->selection)));
    const char *titles[] = { N_("Name"), N_("Size"), N_("Type"), N_("Modified") };
    for (guint i = 0; i < 4; i++) {
        GtkColumnViewColumn *column = gtk_column_view_column_new(_(titles[i]), make_factory(tab, i));
        GtkSorter *sorter = GTK_SORTER(gtk_custom_sorter_new(compare_files, GINT_TO_POINTER(i), NULL));
        gtk_column_view_column_set_sorter(column, sorter); g_object_unref(sorter);
        gtk_column_view_column_set_resizable(column, TRUE);
        gtk_column_view_column_set_expand(column, i == 0);
        gtk_column_view_append_column(GTK_COLUMN_VIEW(tab->list), column);
        tab->columns[i] = column; g_object_unref(column);
    }
    GtkMultiSorter *sorter = gtk_multi_sorter_new();
    gtk_multi_sorter_append(sorter, GTK_SORTER(gtk_custom_sorter_new(compare_files, GINT_TO_POINTER(-1), NULL)));
    gtk_multi_sorter_append(sorter, g_object_ref(gtk_column_view_get_sorter(GTK_COLUMN_VIEW(tab->list))));
    gtk_sort_list_model_set_sorter(tab->sorted, GTK_SORTER(sorter)); g_object_unref(sorter);
    gtk_column_view_sort_by_column(GTK_COLUMN_VIEW(tab->list), tab->columns[0], GTK_SORT_ASCENDING);
    tab->grid = gtk_grid_view_new(GTK_SELECTION_MODEL(g_object_ref(tab->selection)), make_factory(tab, 4));
    gtk_grid_view_set_max_columns(GTK_GRID_VIEW(tab->grid), 20);
    GtkWidget *views[] = { tab->list, tab->grid };
    for (guint i = 0; i < 2; i++) {
        GtkWidget *scroll = gtk_scrolled_window_new();
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
        gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), views[i]);
        gtk_stack_add_named(GTK_STACK(tab->views), scroll, i == 0 ? "list" : "icons");
        add_drop_target(views[i], tab, NULL);
        g_signal_connect(views[i], "activate", G_CALLBACK(activate_file), tab);
        GtkGesture *gesture = gtk_gesture_click_new();
        gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gesture), 3);
        g_signal_connect(gesture, "pressed", G_CALLBACK(empty_context), tab);
        gtk_widget_add_controller(views[i], GTK_EVENT_CONTROLLER(gesture));
    }
    gtk_widget_set_vexpand(tab->views, TRUE); gtk_box_append(GTK_BOX(tab), tab->views);
    g_signal_connect(tab->directory, "notify::loading", G_CALLBACK(directory_changed), tab);
    g_signal_connect(tab->directory, "notify::error", G_CALLBACK(directory_changed), tab);
    g_signal_connect(tab->sorted, "items-changed", G_CALLBACK(model_changed), tab);
    g_signal_connect(tab->selection, "selection-changed", G_CALLBACK(selection_changed), tab);
}
static Tab *add_tab(Browser *b, GFile *file)
{
    Tab *tab = g_object_new(pcmgr_tab_get_type(), NULL); tab->browser = b;
    GtkWidget *label = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    tab->label = gtk_label_new(_("Folder"));
    gtk_label_set_width_chars(GTK_LABEL(tab->label), 14);
    gtk_label_set_max_width_chars(GTK_LABEL(tab->label), 22);
    gtk_label_set_ellipsize(GTK_LABEL(tab->label), PANGO_ELLIPSIZE_MIDDLE);
    GtkWidget *close = gtk_button_new_from_icon_name("window-close-symbolic");
    gtk_widget_add_css_class(close, "flat"); gtk_widget_set_tooltip_text(close, _("Close tab"));
    g_signal_connect(close, "clicked", G_CALLBACK(tab_close_clicked), tab);
    gtk_box_append(GTK_BOX(label), tab->label); gtk_box_append(GTK_BOX(label), close);
    add_drop_target(label, tab, NULL);
    int page = gtk_notebook_append_page(GTK_NOTEBOOK(b->notebook), GTK_WIDGET(tab), label);
    gtk_notebook_set_tab_reorderable(GTK_NOTEBOOK(b->notebook), GTK_WIDGET(tab), TRUE);
    gtk_notebook_set_current_page(GTK_NOTEBOOK(b->notebook), page);
    gtk_stack_set_visible_child_name(GTK_STACK(tab->views), b->icons ? "icons" : "list");
    navigate(tab, file, TRUE); return tab;
}

static void operation_done(GObject *object, GAsyncResult *result, gpointer data)
{
    (void)object;
    Browser *b = data; GError *error = NULL;
    gboolean ok = pcmgr_file_operation_finish(result, &error);
    if (b->operation_drop) {
        gdk_drop_finish(b->operation_drop, ok ? b->drop_action : 0);
        g_clear_object(&b->operation_drop);
    }
    if (ok && b->move_clipboard) {
        GdkClipboard *clipboard = gtk_widget_get_clipboard(GTK_WIDGET(b));
        if (gdk_clipboard_get_content(clipboard) == b->move_clipboard)
            gdk_clipboard_set_content(clipboard, NULL);
    }
    g_clear_object(&b->move_clipboard); g_clear_object(&b->operation);
    if (!b->closing) {
        gtk_widget_set_visible(b->operation_box, FALSE);
        for (int i = 0; i < gtk_notebook_get_n_pages(GTK_NOTEBOOK(b->notebook)); i++)
            refresh((Tab *)gtk_notebook_get_nth_page(GTK_NOTEBOOK(b->notebook), i), TRUE);
        if (!ok) {
            char *text = g_strdup_printf(_("%s\nSome files may already have been processed. Check the folders before retrying."), error->message);
            message(b, text); g_free(text);
        } else message(b, NULL);
        update_window(b);
    }
    g_clear_error(&error); g_object_unref(b);
}
static void operation_progress_cb(double fraction, guint completed, guint total, gpointer data)
{
    Browser *b = data;
    if (b->closing || !b->operation_progress)
        return;
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(b->operation_progress), fraction);
    char *text = g_strdup_printf(_("%u of %u · %.0f%%"), completed, total, fraction * 100.0);
    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(b->operation_progress), text);
    g_free(text);
    if (completed == total)
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(b->operation_progress), 1.0);
}

static void start_operation(Browser *b, PcmgrFileOperation operation, GPtrArray *sources, GFile *destination)
{
    if (b->closing || b->operation) return;
    const char *labels[] = { N_("Copying files…"), N_("Moving files…"), N_("Moving to Trash…"),
        N_("Creating folder…"), N_("Creating file…"), N_("Renaming…"), N_("Emptying Trash…") };
    b->operation = g_cancellable_new();
    gtk_label_set_text(GTK_LABEL(b->operation_label), _(labels[operation]));
    gboolean batch = operation == PCMGR_COPY || operation == PCMGR_MOVE ||
        operation == PCMGR_TRASH;
    if (b->operation_progress) {
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(b->operation_progress), 0.0);
        gtk_progress_bar_set_text(GTK_PROGRESS_BAR(b->operation_progress), "");
        gtk_widget_set_visible(b->operation_progress, batch);
    }
    gtk_widget_set_visible(b->operation_box, TRUE); message(b, NULL); update_window(b);
    if (batch)
        pcmgr_file_operation_async_with_progress(operation, sources, destination,
            b->operation, operation_progress_cb, b, operation_done, g_object_ref(b));
    else
        pcmgr_file_operation_async(operation, sources, destination, b->operation, operation_done, g_object_ref(b));
}
static void cancel_operation(GtkButton *button, Browser *b)
{
    (void)button;
    if (b->operation) g_cancellable_cancel(b->operation);
}

typedef struct {
    Browser *browser;
    GPtrArray *sources;
    GFile *parent;
    GdkContentProvider *move_clipboard;
    GCancellable *paste_cancel;
    GtkWidget *entry, *error, *dialog;
    PcmgrFileOperation operation;
} Prompt;
static void prompt_free(gpointer data)
{
    Prompt *p = data;
    if (p->paste_cancel && p->browser->paste_cancel == p->paste_cancel) {
        g_clear_object(&p->browser->paste_cancel);
        if (!p->browser->closing) update_window(p->browser);
    }
    g_clear_object(&p->paste_cancel);
    g_object_unref(p->browser); g_clear_pointer(&p->sources, g_ptr_array_unref);
    g_clear_object(&p->parent); g_clear_object(&p->move_clipboard); g_free(p);
}
/* GTK4 has no non-deprecated GtkDialog; the rename/create prompt is a plain
 * modal GtkWindow with header-bar actions (same validation, same layout). */
static void name_accept(Prompt *p)
{
    if (!p || p->browser->closing) {
        if (p && p->dialog) gtk_window_destroy(GTK_WINDOW(p->dialog));
        return;
    }
    const char *name = gtk_editable_get_text(GTK_EDITABLE(p->entry));
    GError *error = NULL;
    if (!*name || strchr(name, '/') || !strcmp(name, ".") || !strcmp(name, "..")) {
        gtk_label_set_text(GTK_LABEL(p->error), _("Enter a name without slashes.")); return;
    }
    GFile *target = g_file_get_child_for_display_name(p->parent, name, &error);
    if (!target) {
        gtk_label_set_text(GTK_LABEL(p->error), error->message); g_error_free(error); return;
    }
    start_operation(p->browser, p->operation, p->sources, target); g_object_unref(target);
    gtk_window_destroy(GTK_WINDOW(p->dialog));
}
static void name_accept_clicked(GtkButton *button, Prompt *p)
{
    (void)button; name_accept(p);
}
static void name_entry_activate(GtkEntry *entry, Prompt *p)
{
    (void)entry; name_accept(p);
}
static void name_cancel_clicked(GtkButton *button, Prompt *p)
{
    (void)p;
    GtkWidget *dialog = GTK_WIDGET(gtk_widget_get_root(GTK_WIDGET(button)));
    gtk_window_destroy(GTK_WINDOW(dialog));
}
static void name_prompt(Browser *b, PcmgrFileOperation operation)
{
    Tab *tab = current_tab(b); if (!tab) return;
    Prompt *p = g_new0(Prompt, 1); p->browser = g_object_ref(b); p->operation = operation;
    p->sources = selected_files(tab); p->parent = g_object_ref(tab->location);
    const char *title = operation == PCMGR_RENAME ? _("Rename") :
                        operation == PCMGR_MKDIR ? _("New Folder") : _("New File");
    const char *accept_label = operation == PCMGR_RENAME ? _("Rename") : _("Create");
    GtkWidget *dialog = gtk_window_new();
    p->dialog = dialog;
    gtk_window_set_title(GTK_WINDOW(dialog), title);
    gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(b));
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    gtk_window_set_destroy_with_parent(GTK_WINDOW(dialog), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 380, -1);
    GtkWidget *header = gtk_header_bar_new();
    gtk_window_set_titlebar(GTK_WINDOW(dialog), header);
    GtkWidget *cancel = gtk_button_new_with_label(_("Cancel"));
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), cancel);
    GtkWidget *accept = gtk_button_new_with_label(accept_label);
    gtk_widget_add_css_class(accept, "suggested-action");
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), accept);
    g_signal_connect(cancel, "clicked", G_CALLBACK(name_cancel_clicked), p);
    g_signal_connect(accept, "clicked", G_CALLBACK(name_accept_clicked), p);
    gtk_window_set_default_widget(GTK_WINDOW(dialog), accept);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, LUXE_SPACING_INNER);
    gtk_widget_set_margin_start(box, 16); gtk_widget_set_margin_end(box, 16);
    gtk_widget_set_margin_top(box, 16); gtk_widget_set_margin_bottom(box, 16);
    gtk_window_set_child(GTK_WINDOW(dialog), box);
    p->entry = gtk_entry_new(); p->error = gtk_label_new("");
    gtk_label_set_wrap(GTK_LABEL(p->error), TRUE);
    gtk_entry_set_activates_default(GTK_ENTRY(p->entry), TRUE);
    g_signal_connect(p->entry, "activate", G_CALLBACK(name_entry_activate), p);
    if (operation == PCMGR_RENAME && p->sources->len == 1) {
        GFile *source = g_ptr_array_index(p->sources, 0);
        g_clear_object(&p->parent); p->parent = g_file_get_parent(source);
        char *name = file_title(source); gtk_editable_set_text(GTK_EDITABLE(p->entry), name); g_free(name);
    }
    gtk_box_append(GTK_BOX(box), p->entry); gtk_box_append(GTK_BOX(box), p->error);
    g_object_set_data_full(G_OBJECT(dialog), "prompt", p, prompt_free);
    gtk_window_present(GTK_WINDOW(dialog)); gtk_widget_grab_focus(p->entry);
}
static void trash_choose(GObject *source, GAsyncResult *result, gpointer data)
{
    Prompt *p = data;
    int idx = gtk_alert_dialog_choose_finish(GTK_ALERT_DIALOG(source), result, NULL);
    if (idx == 1 && !p->browser->closing && !p->browser->operation) {
        if (p->move_clipboard) g_set_object(&p->browser->move_clipboard, p->move_clipboard);
        start_operation(p->browser, PCMGR_TRASH, p->sources, NULL);
    }
    prompt_free(p);
}
static void confirm_trash(Prompt *p, gboolean from_clipboard)
{
    GtkAlertDialog *dialog = gtk_alert_dialog_new(ngettext("Move %u item to Trash?", "Move %u items to Trash?", p->sources->len), p->sources->len);
    if (from_clipboard)
        gtk_alert_dialog_set_detail(dialog, _("The original files will be moved from their current locations to Trash, including files copied to the clipboard."));
    gtk_alert_dialog_set_modal(dialog, TRUE);
    const char *const buttons[] = {_("Cancel"), _("Move to Trash"), NULL};
    gtk_alert_dialog_set_buttons(dialog, buttons);
    gtk_alert_dialog_set_cancel_button(dialog, 0);
    gtk_alert_dialog_set_default_button(dialog, 0);
    gtk_alert_dialog_choose(dialog, GTK_WINDOW(p->browser), NULL, trash_choose, p);
    g_object_unref(dialog);
}
static void trash_prompt(Browser *b)
{
    Prompt *p = g_new0(Prompt, 1); p->browser = g_object_ref(b); p->sources = selected_files(current_tab(b));
    confirm_trash(p, FALSE);
}
typedef struct { Browser *browser; } EmptyTrash;
static void empty_trash_choose(GObject *source, GAsyncResult *result, gpointer data)
{
    EmptyTrash *e = data;
    int idx = gtk_alert_dialog_choose_finish(GTK_ALERT_DIALOG(source), result, NULL);
    if (idx == 1 && !e->browser->closing) start_operation(e->browser, PCMGR_EMPTY_TRASH, NULL, NULL);
    g_object_unref(e->browser); g_free(e);
}
static void empty_trash_prompt(Browser *b)
{
    EmptyTrash *e = g_new(EmptyTrash, 1); e->browser = g_object_ref(b);
    GtkAlertDialog *dialog = gtk_alert_dialog_new("%s", _("Empty Trash?"));
    gtk_alert_dialog_set_modal(dialog, TRUE);
    gtk_alert_dialog_set_detail(dialog, _("All items in Trash will be permanently deleted. This cannot be undone."));
    const char *const buttons[] = {_("Cancel"), _("Empty Trash"), NULL};
    gtk_alert_dialog_set_buttons(dialog, buttons);
    gtk_alert_dialog_set_cancel_button(dialog, 0);
    gtk_alert_dialog_set_default_button(dialog, 0);
    gtk_alert_dialog_choose(dialog, GTK_WINDOW(b), NULL, empty_trash_choose, e);
    g_object_unref(dialog);
}

static void copy_files(Browser *b, gboolean cut)
{
    GPtrArray *files = selected_files(current_tab(b));
    if (!files->len) { g_ptr_array_unref(files); return; }
    GdkFileList *list = gdk_file_list_new_from_array((GFile **)files->pdata, files->len);
    GdkContentProvider *providers[2];
    providers[0] = gdk_content_provider_new_typed(GDK_TYPE_FILE_LIST, list);
    g_boxed_free(GDK_TYPE_FILE_LIST, list);
    GString *text = g_string_new(cut ? "cut" : "copy");
    for (guint i = 0; i < files->len; i++) {
        char *uri = g_file_get_uri(g_ptr_array_index(files, i));
        g_string_append_c(text, '\n'); g_string_append(text, uri); g_free(uri);
    }
    GBytes *bytes = g_bytes_new(text->str, text->len); g_string_free(text, TRUE);
    providers[1] = gdk_content_provider_new_for_bytes("x-special/gnome-copied-files", bytes); g_bytes_unref(bytes);
    GdkContentProvider *provider = gdk_content_provider_new_union(providers, 2);
    if (gdk_clipboard_set_content(gtk_widget_get_clipboard(GTK_WIDGET(b)), provider)) {
        g_set_object(&copy_provider, provider); copy_cut = cut;
        message(b, cut ? _("Files ready to move. Open the destination and Paste.") : _("Files copied to clipboard."));
    }
    g_object_unref(provider); g_ptr_array_unref(files);
}
static void paste_files(Prompt *p, GPtrArray *files, gboolean move, GdkContentProvider *owned_provider)
{
    Browser *b = p->browser;
    if (b->closing || b->operation || !files->len) return;
    if (g_file_has_uri_scheme(p->parent, "trash")) {
        Prompt *confirmation = g_new0(Prompt, 1);
        confirmation->browser = g_object_ref(b);
        confirmation->sources = g_ptr_array_ref(files);
        if (move && owned_provider) confirmation->move_clipboard = g_object_ref(owned_provider);
        confirm_trash(confirmation, TRUE);
    } else {
        if (move && owned_provider) g_set_object(&b->move_clipboard, owned_provider);
        start_operation(b, move ? PCMGR_MOVE : PCMGR_COPY, files, p->parent);
    }
}

static void paste_ready(GObject *object, GAsyncResult *result, gpointer data)
{
    Prompt *p = data; Browser *b = p->browser; GError *error = NULL;
    const GValue *value = gdk_clipboard_read_value_finish(GDK_CLIPBOARD(object), result, &error);
    if (!b->closing && !b->operation && value) {
        GdkFileList *list = g_value_get_boxed(value);
        GPtrArray *files = g_ptr_array_new_with_free_func(g_object_unref);
        for (GSList *l = gdk_file_list_get_files(list); l; l = l->next) g_ptr_array_add(files, g_object_ref(l->data));
        gboolean move = copy_cut && copy_provider && gdk_clipboard_get_content(GDK_CLIPBOARD(object)) == copy_provider;
        paste_files(p, files, move, move ? copy_provider : NULL);
        g_ptr_array_unref(files);
    } else if (error && !b->closing && !g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        message(b, _("The clipboard does not contain files to paste."));
    g_clear_error(&error); prompt_free(p);
}

#define PCMGR_CLIPBOARD_LIMIT (1024U * 1024U)
#define PCMGR_CLIPBOARD_FILES 4096U
typedef struct {
    Prompt *prompt;
    GdkClipboard *clipboard;
    GInputStream *stream;
    GByteArray *bytes;
    GCancellable *cancel;
    gulong changed_handler;
} PasteRead;

static void paste_read_free(PasteRead *read)
{
    if (read->changed_handler) g_signal_handler_disconnect(read->clipboard, read->changed_handler);
    g_clear_object(&read->clipboard); g_clear_object(&read->stream);
    g_clear_object(&read->cancel); g_clear_pointer(&read->bytes, g_byte_array_unref);
    prompt_free(read->prompt); g_free(read);
}

static void paste_clipboard_changed(GdkClipboard *clipboard, gpointer data)
{
    (void)clipboard;
    PasteRead *read = data;
    g_cancellable_cancel(read->cancel);
}

static GPtrArray *parse_copied_files(const guint8 *data, gsize length, gboolean *move)
{
    if (!length || length > PCMGR_CLIPBOARD_LIMIT || memchr(data, 0, length) ||
        !g_utf8_validate((const char *)data, length, NULL)) return NULL;
    g_autofree char *text = g_strndup((const char *)data, length);
    g_auto(GStrv) lines = g_strsplit(text, "\n", -1);
    gsize first_length = strlen(lines[0]);
    if (first_length && lines[0][first_length - 1] == '\r') lines[0][first_length - 1] = 0;
    if (g_str_equal(lines[0], "cut")) *move = TRUE;
    else if (g_str_equal(lines[0], "copy")) *move = FALSE;
    else return NULL;
    GPtrArray *files = g_ptr_array_new_with_free_func(g_object_unref);
    g_autoptr(GHashTable) seen = g_hash_table_new_full(g_file_hash, (GEqualFunc)g_file_equal, g_object_unref, NULL);
    for (guint i = 1; lines[i]; i++) {
        gsize line_length = strlen(lines[i]);
        if (line_length && lines[i][line_length - 1] == '\r') lines[i][line_length - 1] = 0;
        if (!*lines[i] && !lines[i + 1]) break;
        g_autofree char *scheme = g_uri_parse_scheme(lines[i]);
        if (!scheme || !*lines[i] || files->len >= PCMGR_CLIPBOARD_FILES ||
            strpbrk(lines[i], "\r\n\t ") != NULL) goto invalid;
        g_autoptr(GError) error = NULL;
        g_autoptr(GUri) uri = g_uri_parse(lines[i], G_URI_FLAGS_ENCODED, &error);
        if (!uri || !g_uri_get_scheme(uri)) goto invalid;
        GFile *file = g_file_new_for_uri(lines[i]);
        if (!g_hash_table_add(seen, g_object_ref(file))) { g_object_unref(file); goto invalid; }
        g_ptr_array_add(files, file);
    }
    if (files->len) return files;
invalid:
    g_ptr_array_unref(files);
    return NULL;
}

static void paste_read_next(PasteRead *read);
static void paste_read_chunk(GObject *source, GAsyncResult *result, gpointer data)
{
    PasteRead *read = data;
    g_autoptr(GError) error = NULL;
    g_autoptr(GBytes) chunk = g_input_stream_read_bytes_finish(G_INPUT_STREAM(source), result, &error);
    if (!chunk || g_cancellable_is_cancelled(read->cancel)) {
        if (!read->prompt->browser->closing && !g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
            message(read->prompt->browser, _("Could not read files from the clipboard."));
        paste_read_free(read); return;
    }
    gsize size; const guint8 *bytes = g_bytes_get_data(chunk, &size);
    if (size > PCMGR_CLIPBOARD_LIMIT - read->bytes->len) {
        if (!read->prompt->browser->closing) message(read->prompt->browser, _("The clipboard file list is too large."));
        paste_read_free(read); return;
    }
    if (size) { g_byte_array_append(read->bytes, bytes, size); paste_read_next(read); return; }
    gboolean move = FALSE;
    g_autoptr(GPtrArray) files = parse_copied_files(read->bytes->data, read->bytes->len, &move);
    if (files) {
        /* Keep the provider identity for external Cut as well as our own. A
         * successful move may clear the clipboard only while that exact
         * provider still owns it; a changed clipboard is never discarded. */
        GdkContentProvider *owner = move ? gdk_clipboard_get_content(read->clipboard) : NULL;
        if (owner) g_object_ref(owner);
        paste_files(read->prompt, files, move, owner);
        g_clear_object(&owner);
    } else if (!read->prompt->browser->closing)
        message(read->prompt->browser, _("The clipboard file list is invalid."));
    paste_read_free(read);
}

static void paste_read_next(PasteRead *read)
{
    g_input_stream_read_bytes_async(read->stream, 65536, G_PRIORITY_DEFAULT, read->cancel, paste_read_chunk, read);
}

static void paste_stream_opened(GObject *source, GAsyncResult *result, gpointer data)
{
    PasteRead *read = data;
    g_autoptr(GError) error = NULL;
    const char *mime = NULL;
    read->stream = gdk_clipboard_read_finish(GDK_CLIPBOARD(source), result, &mime, &error);
    if (!read->stream || g_cancellable_is_cancelled(read->cancel)) {
        if (!read->prompt->browser->closing && !g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
            message(read->prompt->browser, _("Could not read files from the clipboard."));
        paste_read_free(read); return;
    }
    paste_read_next(read);
}

static void start_paste(Prompt *prompt, GdkClipboard *clipboard)
{
    Browser *browser = prompt->browser;
    if (browser->closing || browser->operation || browser->paste_cancel) { prompt_free(prompt); return; }
    prompt->paste_cancel = g_cancellable_new();
    g_set_object(&browser->paste_cancel, prompt->paste_cancel);
    update_window(browser);
    GdkContentFormats *formats = gdk_clipboard_get_formats(clipboard);
    if (!gdk_content_formats_contain_mime_type(formats, "x-special/gnome-copied-files")) {
        gdk_clipboard_read_value_async(clipboard, GDK_TYPE_FILE_LIST, G_PRIORITY_DEFAULT, prompt->paste_cancel, paste_ready, prompt);
        return;
    }
    PasteRead *read = g_new0(PasteRead, 1);
    read->prompt = prompt; read->clipboard = g_object_ref(clipboard);
    read->cancel = g_object_ref(prompt->paste_cancel); read->bytes = g_byte_array_new();
    read->changed_handler = g_signal_connect(clipboard, "changed", G_CALLBACK(paste_clipboard_changed), read);
    static const char *mime_types[] = {"x-special/gnome-copied-files", NULL};
    gdk_clipboard_read_async(clipboard, mime_types, G_PRIORITY_DEFAULT, read->cancel, paste_stream_opened, read);
}
typedef struct {
    Browser *browser;
    GtkWidget *window;
    GtkWidget *disk_value;
    GtkWidget *contents_value;
    GCancellable *cancellable;
    gulong destroy_handler;
    gboolean closed;
    guint pending;
    gboolean all_files;
    gboolean has_directory;
    guint64 disk_usage;
    guint64 file_count;
    guint64 directory_count;
} PropertyJob;

typedef struct {
    gboolean ok;
    guint64 disk_usage;
    guint64 file_count;
    guint64 directory_count;
    char *error;
} PropertyMeasureResult;

static void property_measure_result_free(gpointer data)
{
    PropertyMeasureResult *measure = data;

    if (measure == NULL)
        return;
    g_free(measure->error);
    g_free(measure);
}

static void property_job_free(PropertyJob *job)
{
    if (g_signal_handler_is_connected(job->window, job->destroy_handler))
        g_signal_handler_disconnect(job->window, job->destroy_handler);
    g_object_unref(job->browser);
    g_object_unref(job->window);
    g_object_unref(job->cancellable);
    g_free(job);
}

static void property_measure_cancel(GtkWidget *window, gpointer data)
{
    PropertyJob *job = data;
    (void) window;
    job->closed = TRUE;
    g_cancellable_cancel(job->cancellable);
}

static void property_measure_thread(GTask *task, gpointer source, gpointer data, GCancellable *cancellable)
{
    PropertyMeasureResult *measure = g_new0(PropertyMeasureResult, 1);
    guint64 disk_usage = 0, files = 0, dirs = 0;
    GError *error = NULL;

    (void) data;
    measure->ok = g_file_measure_disk_usage(G_FILE(source), G_FILE_MEASURE_NONE, cancellable,
                                            NULL, NULL, &disk_usage, &dirs, &files, &error);
    if (measure->ok) {
        measure->disk_usage = disk_usage;
        measure->directory_count = dirs;
        measure->file_count = files;
    } else if (error != NULL) {
        measure->error = g_strdup(error->message);
    }
    g_task_return_pointer(task, measure, property_measure_result_free);
}

static void property_measure_done(GObject *source, GAsyncResult *result, gpointer data)
{
    PropertyJob *job = data;
    GError *error = NULL;
    PropertyMeasureResult *measure = g_task_propagate_pointer(G_TASK(result), &error);
    gboolean ok = measure != NULL && measure->ok && !job->closed;

    if (error && !job->closed && !g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        show_error(job->browser, error);
    g_clear_error(&error);
    if (measure != NULL) {
        job->disk_usage += measure->disk_usage;
        job->file_count += measure->file_count;
        job->directory_count += measure->directory_count;
    }

    if (job->pending > 0)
        job->pending--;
    if (job->pending == 0) {
        if (ok) {
            char *text = g_format_size(job->disk_usage);
            gtk_label_set_text(GTK_LABEL(job->disk_value), text); g_free(text);
            if (job->has_directory) {
                if (job->file_count == 1 && job->directory_count == 1)
                    text = g_strdup(_("1 file, 1 folder"));
                else if (job->directory_count == 1)
                    text = g_strdup_printf(_("%" G_GUINT64_FORMAT " files, 1 folder"), job->file_count);
                else if (job->file_count == 1)
                    text = g_strdup_printf(_("1 file, %" G_GUINT64_FORMAT " folders"), job->directory_count);
                else
                    text = g_strdup_printf(_("%" G_GUINT64_FORMAT " files, %" G_GUINT64_FORMAT " folders"), job->file_count, job->directory_count);
                gtk_label_set_text(GTK_LABEL(job->contents_value), text); g_free(text);
            }
        }
        property_job_free(job);
    }
}

static void properties(Browser *b)
{
    GtkWidget *grid, *label;
    Tab *tab = current_tab(b);
    GPtrArray *files = selected_files(tab);
    guint grid_row = 0;
    guint64 size_sum = 0;
    gboolean has_directory = FALSE;

    if (!files->len) { g_ptr_array_unref(files); return; }
    if (files->len > 64) {
        g_ptr_array_set_size(files, 64);
        message(b, _("Only the first 64 selected items are measured."));
    }

    for (guint i = 0; i < files->len; i++) {
        GFileType kind = g_file_query_file_type(g_ptr_array_index(files, i), G_FILE_QUERY_INFO_NONE, NULL);
        if (kind == G_FILE_TYPE_DIRECTORY)
            has_directory = TRUE;
    }

    char *name = files->len == 1 ? file_title(g_ptr_array_index(files, 0)) : g_strdup_printf(_("%u items selected"), files->len);
    GtkWidget *window = gtk_window_new();

    PropertyJob *job = g_new0(PropertyJob, 1);
    job->browser = g_object_ref(b);
    job->cancellable = g_cancellable_new();
    job->pending = files->len;
    job->all_files = !has_directory;
    job->has_directory = has_directory;
    job->window = g_object_ref(window);
    job->destroy_handler = g_signal_connect(window, "destroy", G_CALLBACK(property_measure_cancel), job);

    gtk_window_set_title(GTK_WINDOW(window), name);
    gtk_window_set_transient_for(GTK_WINDOW(window), GTK_WINDOW(b));
    gtk_window_set_modal(GTK_WINDOW(window), TRUE);
    gtk_window_set_destroy_with_parent(GTK_WINDOW(window), TRUE);
    gtk_window_set_resizable(GTK_WINDOW(window), TRUE);

    grid = gtk_grid_new();
    gtk_widget_set_margin_top(grid, 14); gtk_widget_set_margin_bottom(grid, 14);
    gtk_widget_set_margin_start(grid, 10); gtk_widget_set_margin_end(grid, 10);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6); gtk_grid_set_column_spacing(GTK_GRID(grid), 12);

    label = gtk_label_new(_("Name")); gtk_widget_set_halign(label, GTK_ALIGN_END); gtk_widget_add_css_class(label, "dim-label");
    gtk_grid_attach(GTK_GRID(grid), label, 0, grid_row, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new(name), 1, grid_row, 1, 1);
    grid_row++;

    if (files->len == 1) {
        GFileInfo *info = g_file_query_info(g_ptr_array_index(files, 0),
                            G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE,
                            G_FILE_QUERY_INFO_NONE, NULL, NULL);
        const char *type = info ? g_file_info_get_content_type(info) : NULL;
        char *description = g_strdup(type && *type ? g_content_type_get_description(type) : _("Unknown"));
        label = gtk_label_new(_("Type")); gtk_widget_set_halign(label, GTK_ALIGN_END); gtk_widget_add_css_class(label, "dim-label");
        gtk_grid_attach(GTK_GRID(grid), label, 0, grid_row, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), gtk_label_new(description), 1, grid_row, 1, 1);
        grid_row++;
        g_free(description);
        g_clear_object(&info);
    }

    if (!has_directory) {
        for (guint i = 0; i < files->len; i++) {
            GFileInfo *info = g_file_query_info(g_ptr_array_index(files, i), G_FILE_ATTRIBUTE_STANDARD_SIZE,
                                G_FILE_QUERY_INFO_NONE, NULL, NULL);
            size_sum += info ? g_file_info_get_size(info) : 0;
            g_clear_object(&info);
        }
    }

    label = gtk_label_new(_("Size")); gtk_widget_set_halign(label, GTK_ALIGN_END); gtk_widget_add_css_class(label, "dim-label");
    gtk_grid_attach(GTK_GRID(grid), label, 0, grid_row, 1, 1);
    if (!has_directory) {
        char *text = g_format_size(size_sum);
        gtk_grid_attach(GTK_GRID(grid), gtk_label_new(text), 1, grid_row, 1, 1);
        g_free(text);
    } else {
        gtk_grid_attach(GTK_GRID(grid), gtk_label_new("—"), 1, grid_row, 1, 1);
    }
    grid_row++;

    label = gtk_label_new(_("Size on disk")); gtk_widget_set_halign(label, GTK_ALIGN_END); gtk_widget_add_css_class(label, "dim-label");
    gtk_grid_attach(GTK_GRID(grid), label, 0, grid_row, 1, 1);
    job->disk_value = gtk_label_new(_("Calculating…")); gtk_label_set_xalign(GTK_LABEL(job->disk_value), 0);
    gtk_grid_attach(GTK_GRID(grid), job->disk_value, 1, grid_row, 1, 1);
    grid_row++;

    if (files->len == 1) {
        GFile *file = g_ptr_array_index(files, 0);
        GFile *parent = g_file_get_parent(file);
        char *location = parent ? g_file_get_parse_name(parent) : g_file_get_parse_name(file);
        label = gtk_label_new(_("Location")); gtk_widget_set_halign(label, GTK_ALIGN_END); gtk_widget_add_css_class(label, "dim-label");
        gtk_grid_attach(GTK_GRID(grid), label, 0, grid_row, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), gtk_label_new(location), 1, grid_row, 1, 1);
        grid_row++;
        g_free(location);
        g_clear_object(&parent);
    }

    for (guint i = 0; i < files->len; i++) {
        GFile *file = g_ptr_array_index(files, i);
        GTask *task = g_task_new(file, job->cancellable, property_measure_done, job);
        g_task_set_return_on_cancel(task, FALSE);
        g_task_set_task_data(task, g_object_ref(file), g_object_unref);
        g_task_run_in_thread(task, property_measure_thread);
        g_object_unref(task);
    }

    if (has_directory) {
        job->contents_value = gtk_label_new(_("Calculating…")); gtk_label_set_xalign(GTK_LABEL(job->contents_value), 0);
        label = gtk_label_new(_("Contents")); gtk_widget_set_halign(label, GTK_ALIGN_END); gtk_widget_add_css_class(label, "dim-label");
        gtk_grid_attach(GTK_GRID(grid), label, 0, grid_row, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), job->contents_value, 1, grid_row, 1, 1);
        grid_row++;
    }

    {
        GtkWidget *apply;
        GtkWidget *permissions = pcmgr_permissions_editor(GTK_WINDOW(window), files, &apply);
        gtk_widget_set_margin_top(permissions, 14);
        gtk_grid_attach(GTK_GRID(grid), permissions, 0, grid_row++, 2, 1);
        GtkWidget *buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        GtkWidget *close = gtk_button_new_with_label(_("Close"));
        gtk_widget_set_halign(buttons, GTK_ALIGN_END);
        gtk_widget_set_margin_top(buttons, 6);
        g_signal_connect_swapped(close, "clicked", G_CALLBACK(gtk_window_destroy), window);
        gtk_box_append(GTK_BOX(buttons), apply);
        gtk_box_append(GTK_BOX(buttons), close);
        gtk_grid_attach(GTK_GRID(grid), buttons, 0, grid_row, 2, 1);
        grid_row++;
    }

    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(scroll), TRUE);
    gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(scroll), 540);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), grid);
    gtk_window_set_child(GTK_WINDOW(window), scroll);
    gtk_window_present(GTK_WINDOW(window));
    g_free(name);
    g_ptr_array_unref(files);
}

static void chooser_response(GtkDialog *dialog, int response, Prompt *p)
{
    if (response == GTK_RESPONSE_OK && !p->browser->closing) {
        /* GtkAppChooser is deprecated in GTK 4.10+ without a replacement;
         * a custom picker risks behavior change, so keep it suppressed. */
G_GNUC_BEGIN_IGNORE_DEPRECATIONS
        GAppInfo *app = gtk_app_chooser_get_app_info(GTK_APP_CHOOSER(dialog));
G_GNUC_END_IGNORE_DEPRECATIONS
        if (app) {
            GList *files = NULL; GError *error = NULL;
            for (guint i = 0; i < p->sources->len; i++) files = g_list_append(files, g_ptr_array_index(p->sources, i));
            GAppLaunchContext *context = G_APP_LAUNCH_CONTEXT(gdk_display_get_app_launch_context(gtk_widget_get_display(GTK_WIDGET(p->browser))));
            g_app_info_launch(app, files, context, &error); show_error(p->browser, error);
            g_clear_error(&error); g_list_free(files); g_object_unref(context); g_object_unref(app);
        }
    }
    gtk_window_destroy(GTK_WINDOW(dialog));
}
static void open_with(Browser *b)
{
    Prompt *p = g_new0(Prompt, 1); p->browser = g_object_ref(b); p->sources = selected_files(current_tab(b));
    if (!p->sources->len) { prompt_free(p); return; }
    /* GtkAppChooserDialog has no non-deprecated replacement (see frame);
     * keep the stock dialog so default-app handling stays identical. */
G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    GtkWidget *dialog = gtk_app_chooser_dialog_new(GTK_WINDOW(b), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, g_ptr_array_index(p->sources, 0));
    GtkWidget *chooser = gtk_app_chooser_dialog_get_widget(GTK_APP_CHOOSER_DIALOG(dialog));
    gtk_app_chooser_widget_set_show_default(GTK_APP_CHOOSER_WIDGET(chooser), TRUE);
G_GNUC_END_IGNORE_DEPRECATIONS
    g_object_set_data_full(G_OBJECT(dialog), "prompt", p, prompt_free);
    g_signal_connect(dialog, "response", G_CALLBACK(chooser_response), p); gtk_window_present(GTK_WINDOW(dialog));
}
enum { PREF_HIDDEN = 1, PREF_VIEW = 2, PREF_GEOMETRY = 4 };
static void save_preferences(Browser *b, unsigned int changed)
{
    if (!config_path) return;
    char *parent = g_path_get_dirname(config_path);
    GError *error = NULL;
    struct stat parent_stat;
    int lock_fd = -1;
    if (g_mkdir_with_parents(parent, 0700) == 0 &&
        lstat(parent, &parent_stat) == 0 && S_ISDIR(parent_stat.st_mode) &&
        parent_stat.st_uid == getuid() && (parent_stat.st_mode & 077) == 0) {
        g_autofree char *lock_path = g_strconcat(config_path, ".lock", NULL);
        lock_fd = open(lock_path, O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
        struct stat lock_stat;
        if (lock_fd < 0 || fstat(lock_fd, &lock_stat) < 0 ||
            !S_ISREG(lock_stat.st_mode) || lock_stat.st_uid != getuid() ||
            (lock_stat.st_mode & 077) != 0 || flock(lock_fd, LOCK_EX) < 0) {
            g_set_error_literal(&error, G_IO_ERROR, G_IO_ERROR_FAILED,
                                _("The preferences lock is unavailable or unsafe."));
        } else {
            /* Reload only after acquiring the lock so independent windows
             * merge their changed keys with the newest on-disk preferences. */
            GKeyFile *key = g_key_file_new();
            browser_keyfile_load_bounded(key, config_path);
            if (changed & PREF_HIDDEN) g_key_file_set_boolean(key, "ui", "show_hidden", b->show_hidden);
            if (changed & PREF_VIEW) g_key_file_set_string(key, "ui", "view_mode", b->icons ? "icon" : "list");
            int width = gtk_widget_get_width(GTK_WIDGET(b)), height = gtk_widget_get_height(GTK_WIDGET(b));
            if ((changed & PREF_GEOMETRY) && width > 0 && height > 0 && !gtk_window_is_maximized(GTK_WINDOW(b))) {
                g_key_file_set_integer(key, "ui", "win_width", width);
                g_key_file_set_integer(key, "ui", "win_height", height);
            }
            gsize length = 0;
            gchar *serialized = g_key_file_to_data(key, &length, &error);
            if (serialized != NULL) {
                g_file_set_contents_full(config_path, serialized, length,
                        G_FILE_SET_CONTENTS_CONSISTENT | G_FILE_SET_CONTENTS_DURABLE,
                        0600, &error);
                g_free(serialized);
            }
            g_key_file_unref(key);
        }
    } else {
        g_set_error_literal(&error, G_IO_ERROR, G_IO_ERROR_FAILED,
                            _("The preferences folder is unavailable or has unsafe permissions."));
    }
    if (lock_fd >= 0) { (void)flock(lock_fd, LOCK_UN); close(lock_fd); }
    if (error) {
        char *text = g_strdup_printf(_("Cannot save preferences: %s"), error->message);
        message(b, text);
        g_warning("Cannot save PCManager preferences: %s", error->message);
        g_free(text); g_error_free(error);
    }
    g_free(parent);
}
static void set_view(Browser *b, gboolean icons)
{
    b->icons = icons;
    for (int i = 0; i < gtk_notebook_get_n_pages(GTK_NOTEBOOK(b->notebook)); i++) {
        Tab *tab = (Tab *)gtk_notebook_get_nth_page(GTK_NOTEBOOK(b->notebook), i);
        gtk_stack_set_visible_child_name(GTK_STACK(tab->views), icons ? "icons" : "list");
    }
    save_preferences(b, PREF_VIEW);
}
static void extraction_done(GObject *object, GAsyncResult *result, gpointer data)
{
    Browser *b = data;
    GError *error = NULL;
    g_subprocess_wait_check_finish(G_SUBPROCESS(object), result, &error);
    if (!b->closing) {
        if (error) show_error(b, error);
        Tab *tab = current_tab(b);
        if (tab) { query_space(tab); refresh(tab, TRUE); }
    }
    g_clear_error(&error);
    g_object_unref(b);
}
static void extract_here(Browser *b)
{
    Tab *tab = current_tab(b);
    if (!extractable(tab)) return;
    GPtrArray *files = selected_files(tab);
    GFile *archive = g_ptr_array_index(files, 0);
    char *path = g_file_get_path(archive);
    GFile *parent = g_file_get_parent(archive);
    char *folder = parent ? g_file_get_path(parent) : NULL;
    if (path && folder) {
        GError *error = NULL;
        GSubprocessLauncher *launcher = g_subprocess_launcher_new(G_SUBPROCESS_FLAGS_NONE);
        g_subprocess_launcher_set_cwd(launcher, folder);
        /* Separate argv values preserve spaces, quotes and shell characters. */
        GSubprocess *process = g_subprocess_launcher_spawn(launcher, &error,
            "xarchiver", "--extract-to", folder, "--", path, NULL);
        if (process) {
            g_subprocess_wait_check_async(process, NULL, extraction_done, g_object_ref(b));
            g_object_unref(process);
        } else show_error(b, error);
        g_clear_error(&error); g_object_unref(launcher);
    }
    g_free(path); g_free(folder); g_clear_object(&parent); g_ptr_array_unref(files);
}

static void action_activate(GSimpleAction *action, GVariant *parameter, Browser *b)
{
    (void)parameter;
    const char *name = g_action_get_name(G_ACTION(action)); Tab *tab = current_tab(b);
    if (!strcmp(name, "about")) { luxe_show_about(GTK_WINDOW(b), "PCManager", VERSION, "File manager for Luxe, derived from PCManFM."); return; }
    if (!tab || !tab->location) return;
    if (!strcmp(name, "back") || !strcmp(name, "forward")) {
        gint next = tab->history_pos + (!strcmp(name, "back") ? -1 : 1);
        if (next < 0 || next >= (gint)tab->history->len)
            return;
        tab->history_pos = next;
        navigate(tab, g_ptr_array_index(tab->history, tab->history_pos), FALSE);
    } else if (!strcmp(name, "up")) {
        GFile *parent = g_file_get_parent(tab->location); if (parent) { navigate(tab, parent, TRUE); g_object_unref(parent); }
    } else if (!strcmp(name, "home")) {
        GFile *home = g_file_new_for_path(g_get_home_dir()); navigate(tab, home, TRUE); g_object_unref(home);
    } else if (!strcmp(name, "refresh")) { query_space(tab); refresh(tab, TRUE); }
    else if (!strcmp(name, "location")) { gtk_widget_grab_focus(b->location); gtk_editable_select_region(GTK_EDITABLE(b->location), 0, -1); }
    else if (!strcmp(name, "search")) gtk_widget_grab_focus(b->search);
    else if (!strcmp(name, "new-tab")) add_tab(b, tab->location);
    else if (!strcmp(name, "new-window")) { Browser *other = new_browser(); add_tab(other, tab->location); gtk_window_present(GTK_WINDOW(other)); }
    else if (!strcmp(name, "close")) tab_close_clicked(NULL, tab);
    else if (!strcmp(name, "icons")) set_view(b, TRUE);
    else if (!strcmp(name, "list")) set_view(b, FALSE);
    else if (!strcmp(name, "hidden")) {
        b->show_hidden = !b->show_hidden;
        g_simple_action_set_state(action, g_variant_new_boolean(b->show_hidden));
        for (int i = 0; i < gtk_notebook_get_n_pages(GTK_NOTEBOOK(b->notebook)); i++)
            gtk_filter_changed(GTK_FILTER(((Tab *)gtk_notebook_get_nth_page(GTK_NOTEBOOK(b->notebook), i))->filter), GTK_FILTER_CHANGE_DIFFERENT);
        save_preferences(b, PREF_HIDDEN);
    } else if (!strcmp(name, "select-all")) gtk_selection_model_select_all(GTK_SELECTION_MODEL(tab->selection));
    else if (!strcmp(name, "copy") || !strcmp(name, "cut")) copy_files(b, !strcmp(name, "cut"));
    else if (!strcmp(name, "paste")) {
        Prompt *p = g_new0(Prompt, 1); p->browser = g_object_ref(b); p->parent = g_object_ref(tab->location);
        start_paste(p, gtk_widget_get_clipboard(GTK_WIDGET(b)));
    } else if (!strcmp(name, "new-folder")) name_prompt(b, PCMGR_MKDIR);
    else if (!strcmp(name, "new-file")) name_prompt(b, PCMGR_NEW_FILE);
    else if (!strcmp(name, "rename")) name_prompt(b, PCMGR_RENAME);
    else if (!strcmp(name, "trash")) trash_prompt(b);
    else if (!strcmp(name, "empty-trash")) empty_trash_prompt(b);
    else if (!strcmp(name, "extract-here")) extract_here(b);
    else if (!strcmp(name, "properties")) properties(b);
    else if (!strcmp(name, "open-with")) open_with(b);
    else if (!strcmp(name, "bookmark")) {
        FmPath *path = fm_path_new_for_gfile(tab->location); char *title = file_title(tab->location);
        fm_bookmarks_append(b->bookmarks, path, title); fm_path_unref(path); g_free(title);
    } else if (!strcmp(name, "terminal")) {
        char *cwd = g_file_get_path(tab->location); GError *error = NULL;
        /* LuxeTerm forwards the caller working directory through its socket. */
        char *argv[] = { "luxeterm", NULL };
        if (cwd) { g_spawn_async(cwd, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, &error); show_error(b, error); }
        else message(b, _("A terminal can only be opened in a local folder."));
        g_clear_error(&error); g_free(cwd);
    } else if (!strcmp(name, "open") || !strcmp(name, "open-tab")) {
        GtkBitset *set = gtk_selection_model_get_selection(GTK_SELECTION_MODEL(tab->selection));
        GPtrArray *infos = g_ptr_array_new_with_free_func(g_object_unref); GtkBitsetIter iter; guint position;
        for (gboolean valid = gtk_bitset_iter_init_first(&iter, set, &position); valid; valid = gtk_bitset_iter_next(&iter, &position))
            g_ptr_array_add(infos, g_list_model_get_item(G_LIST_MODEL(tab->sorted), position));
        gtk_bitset_unref(set);
        for (guint i = 0; i < infos->len; i++) {
            GFileInfo *info = g_ptr_array_index(infos, i); GFile *file = info_file(info);
            if (is_directory(info)) {
                if (infos->len == 1 && !strcmp(name, "open")) navigate(tab, file, TRUE); else add_tab(b, file);
            } else launch_file(b, file);
        }
        g_ptr_array_unref(infos);
    }
}
static GMenu *file_menu(void)
{
    GMenu *menu = g_menu_new();
    g_menu_append(menu, _("New Window"), "win.new-window"); g_menu_append(menu, _("New Tab"), "win.new-tab");
    g_menu_append(menu, _("New Folder…"), "win.new-folder"); g_menu_append(menu, _("New File…"), "win.new-file");
    g_menu_append(menu, _("Open in LuxeTerm (F4)"), "win.terminal"); g_menu_append(menu, _("Close Tab"), "win.close"); return menu;
}
static GMenu *edit_menu(void)
{
    GMenu *menu = g_menu_new();
    g_menu_append(menu, _("Cut"), "win.cut"); g_menu_append(menu, _("Copy"), "win.copy"); g_menu_append(menu, _("Paste"), "win.paste");
    g_menu_append(menu, _("Select All"), "win.select-all"); g_menu_append(menu, _("Rename…"), "win.rename");
    g_menu_append(menu, _("Move to Trash…"), "win.trash"); g_menu_append(menu, _("Empty Trash…"), "win.empty-trash"); g_menu_append(menu, _("Properties"), "win.properties"); return menu;
}
static void show_context(Tab *tab, GtkWidget *widget, double x, double y)
{
    GMenu *menu = g_menu_new(), *edit = edit_menu();
    g_menu_append(menu, _("Open"), "win.open"); g_menu_append(menu, _("Open in New Tab"), "win.open-tab");
    if (extractable(tab)) g_menu_append(menu, _("Extract Here"), "win.extract-here");
    g_menu_append(menu, _("Open With…"), "win.open-with"); g_menu_append_section(menu, NULL, G_MENU_MODEL(edit)); g_object_unref(edit);
    GMenu *create = g_menu_new(); g_menu_append(create, _("New Folder…"), "win.new-folder");
    g_menu_append(create, _("New File…"), "win.new-file");
    g_menu_append(create, _("Open in LuxeTerm (F4)"), "win.terminal"); g_menu_append_section(menu, NULL, G_MENU_MODEL(create)); g_object_unref(create);
    GtkWidget *popover = gtk_popover_menu_new_from_model(G_MENU_MODEL(menu)); g_object_unref(menu);
    gtk_widget_set_parent(popover, widget); gtk_popover_set_has_arrow(GTK_POPOVER(popover), FALSE);
    GdkRectangle rect = { (int)x, (int)y, 1, 1 }; gtk_popover_set_pointing_to(GTK_POPOVER(popover), &rect);
    g_signal_connect(popover, "closed", G_CALLBACK(popover_closed), NULL); update_window(tab->browser); gtk_popover_popup(GTK_POPOVER(popover));
}
static void device_button_clicked(GtkButton *button, Browser *b)
{
    GMount *mount = g_object_get_data(G_OBJECT(button), "mount");
    GVolume *volume = g_object_get_data(G_OBJECT(button), "volume");
    GDrive *drive = volume ? g_volume_get_drive(volume) : NULL;
    if (mount && g_mount_can_eject(mount)) {
        g_mount_eject_with_operation(mount, G_MOUNT_UNMOUNT_NONE, NULL, NULL,
            mount_eject_finished, volume_request_new(b, _("Device ejected")));
    } else if (drive && g_drive_can_eject(drive)) {
        g_drive_eject_with_operation(drive, G_MOUNT_UNMOUNT_NONE, NULL, NULL,
            drive_eject_finished, volume_request_new(b, _("Device ejected")));
    } else if (mount && g_mount_can_unmount(mount)) {
        g_mount_unmount_with_operation(mount, G_MOUNT_UNMOUNT_NONE, NULL, NULL,
            mount_unmount_finished, volume_request_new(b, _("Device unmounted")));
    } else if (volume && g_volume_can_mount(volume)) {
        g_volume_mount(volume, G_MOUNT_MOUNT_NONE, NULL, NULL,
            volume_mount_finished, volume_request_new(b, _("Device mounted")));
    }
    g_clear_object(&drive);
}

static void place_add(Browser *b, const char *name, const char *icon, GFile *file,
                      GMount *mount, GVolume *volume)
{
    GtkWidget *row = gtk_list_box_row_new(), *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_margin_start(box, 12); gtk_widget_set_margin_end(box, 12);
    gtk_widget_set_margin_top(box, 9); gtk_widget_set_margin_bottom(box, 9);
    gtk_box_append(GTK_BOX(box), gtk_image_new_from_icon_name(icon));
    GtkWidget *label = gtk_label_new(name); gtk_label_set_xalign(GTK_LABEL(label), 0);
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END); gtk_widget_set_hexpand(label, TRUE);
    gtk_box_append(GTK_BOX(box), label); gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), box);
    if (file) {
        g_object_set_data_full(G_OBJECT(row), "file", g_object_ref(file), g_object_unref);
        char *path = g_file_get_parse_name(file); gtk_widget_set_tooltip_text(row, path); g_free(path);
        add_drop_target(row, NULL, b);
    }
    if (mount || volume) {
        GDrive *drive = volume ? g_volume_get_drive(volume) : NULL;
        gboolean eject = (mount && g_mount_can_eject(mount)) ||
            (drive && g_drive_can_eject(drive));
        GtkWidget *button = gtk_button_new_from_icon_name(eject ? "media-eject-symbolic" :
            mount ? "media-eject-symbolic" : "media-mount-symbolic");
        gtk_widget_add_css_class(button, "flat");
        gtk_widget_set_tooltip_text(button, eject ? _("Eject") :
            mount ? _("Unmount") : _("Mount"));
        if (mount) g_object_set_data_full(G_OBJECT(button), "mount", g_object_ref(mount), g_object_unref);
        if (volume) g_object_set_data_full(G_OBJECT(button), "volume", g_object_ref(volume), g_object_unref);
        g_signal_connect(button, "clicked", G_CALLBACK(device_button_clicked), b);
        gtk_box_append(GTK_BOX(box), button);
        if (volume) g_object_set_data_full(G_OBJECT(row), "volume", g_object_ref(volume), g_object_unref);
        g_clear_object(&drive);
    }
    gtk_list_box_append(GTK_LIST_BOX(b->places), row);
}
static void rebuild_places(Browser *b)
{
    if (b->closing) return;
    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(b->places))) gtk_list_box_remove(GTK_LIST_BOX(b->places), child);
    GFile *file = g_file_new_for_path(g_get_home_dir()); place_add(b, _("Home"), "user-home-symbolic", file, NULL, NULL); g_object_unref(file);
    GUserDirectory dirs[] = { G_USER_DIRECTORY_DESKTOP, G_USER_DIRECTORY_DOCUMENTS, G_USER_DIRECTORY_DOWNLOAD };
    const char *names[] = { N_("Desktop"), N_("Documents"), N_("Downloads") };
    const char *icons[] = { "user-desktop-symbolic", "folder-documents-symbolic", "folder-download-symbolic" };
    for (guint i = 0; i < G_N_ELEMENTS(dirs); i++) {
        const char *path = g_get_user_special_dir(dirs[i]);
        if (path && strcmp(path, g_get_home_dir())) { file = g_file_new_for_path(path); place_add(b, _(names[i]), icons[i], file, NULL, NULL); g_object_unref(file); }
    }
    file = g_file_new_for_path("/"); place_add(b, _("File System"), "drive-harddisk-symbolic", file, NULL, NULL); g_object_unref(file);
    file = g_file_new_for_uri("trash:///"); place_add(b, _("Trash"), "user-trash-symbolic", file, NULL, NULL); g_object_unref(file);
    for (const GList *l = fm_bookmarks_list_all(b->bookmarks); l; l = l->next) {
        FmBookmarkItem *item = l->data; file = fm_path_to_gfile(item->path);
        place_add(b, item->name, "folder-symbolic", file, NULL, NULL); g_object_unref(file);
    }
    GList *mounts = g_volume_monitor_get_mounts(b->volumes);
    for (GList *l = mounts; l; l = l->next) {
        GMount *mount = l->data; if (g_mount_is_shadowed(mount)) continue;
        file = g_mount_get_root(mount); char *name = g_mount_get_name(mount);
        GVolume *volume = g_mount_get_volume(mount);
        place_add(b, name, "drive-removable-media-symbolic", file, mount, volume);
        g_clear_object(&volume); g_free(name); g_object_unref(file);
    }
    g_list_free_full(mounts, g_object_unref);

    GList *volumes = g_volume_monitor_get_volumes(b->volumes);
    for (GList *l = volumes; l; l = l->next) {
        GVolume *volume = l->data;
        GMount *mount = g_volume_get_mount(volume);
        if (!mount && g_volume_can_mount(volume)) {
            char *name = g_volume_get_name(volume);
            place_add(b, name, "drive-removable-media-symbolic", NULL, NULL, volume);
            g_free(name);
        }
        g_clear_object(&mount);
    }
    g_list_free_full(volumes, g_object_unref);
}
static void bookmarks_changed(FmBookmarks *bookmarks, Browser *b) { (void)bookmarks; rebuild_places(b); }
static void mounts_changed(GVolumeMonitor *monitor, GMount *mount, Browser *b) { (void)monitor; (void)mount; rebuild_places(b); }
static void volumes_changed(GVolumeMonitor *monitor, GVolume *volume, Browser *b) { (void)monitor; (void)volume; rebuild_places(b); }
static void volume_added(GVolumeMonitor *monitor, GVolume *volume, Browser *b)
{
    (void)monitor;
    rebuild_places(b);
    if (!browser_app || !g_volume_can_mount(volume)) return;
    GMount *mount = g_volume_get_mount(volume);
    if (mount) { g_object_unref(mount); return; }
    char *name = g_volume_get_name(volume);
    GNotification *notification = g_notification_new(_("Removable media available"));
    g_notification_set_body(notification, name);
    g_notification_set_priority(notification, G_NOTIFICATION_PRIORITY_NORMAL);
    g_application_send_notification(G_APPLICATION(browser_app), "removable-media", notification);
    g_object_unref(notification);
    g_free(name);
}
static void place_activated(GtkListBox *box, GtkListBoxRow *row, Browser *b)
{
    (void)box; GFile *file = g_object_get_data(G_OBJECT(row), "file");
    GVolume *volume = g_object_get_data(G_OBJECT(row), "volume");
    if (volume && g_volume_can_mount(volume)) {
        g_volume_mount(volume, G_MOUNT_MOUNT_NONE, NULL, NULL,
            volume_mount_finished, volume_request_new(b, _("Device mounted")));
        return;
    }
    Tab *tab = current_tab(b); if (tab && file) navigate(tab, file, TRUE);
}
typedef struct { GWeakRef tab; } OpenRequest;
static void open_location_ready(GObject *object, GAsyncResult *result, gpointer data)
{
    OpenRequest *request = data; Tab *tab = g_weak_ref_get(&request->tab);
    GError *error = NULL; GFileInfo *info = g_file_query_info_finish(G_FILE(object), result, &error);
    if (tab && !tab->closing && tab->browser && g_file_equal(tab->location, G_FILE(object))) {
        if (info && !is_directory(info)) {
            GFile *parent = g_file_get_parent(G_FILE(object));
            if (parent) { navigate(tab, parent, TRUE); g_object_unref(parent); }
            launch_file(tab->browser, G_FILE(object));
        } else if (error) show_error(tab->browser, error);
    }
    g_clear_object(&info); g_clear_object(&tab); g_clear_error(&error);
    g_weak_ref_clear(&request->tab); g_free(request);
}
static void check_location(Tab *tab, GFile *file)
{
    OpenRequest *request = g_new0(OpenRequest, 1); g_weak_ref_init(&request->tab, tab);
    g_file_query_info_async(file, G_FILE_ATTRIBUTE_STANDARD_TYPE, G_FILE_QUERY_INFO_NONE,
        G_PRIORITY_DEFAULT, NULL, open_location_ready, request);
}
static void location_activate(GtkEntry *entry, Browser *b)
{
    Tab *tab = current_tab(b); const char *text = gtk_editable_get_text(GTK_EDITABLE(entry));
    if (!tab || !*text) return;
    GFile *file;
    if (text[0] == '~' && (text[1] == '/' || !text[1])) {
        char *path = g_build_filename(g_get_home_dir(), text[1] ? text + 2 : "", NULL); file = g_file_new_for_path(path); g_free(path);
    } else if (g_path_is_absolute(text) || g_uri_peek_scheme(text)) file = g_file_new_for_commandline_arg(text);
    else file = g_file_resolve_relative_path(tab->location, text);
    navigate(tab, file, TRUE); check_location(tab, file); g_object_unref(file);
}
static void search_changed(GtkEditable *entry, Browser *b)
{
    Tab *tab = current_tab(b); if (!tab) return;
    g_free(tab->query); tab->query = g_utf8_casefold(gtk_editable_get_text(entry), -1);
    gtk_filter_changed(GTK_FILTER(tab->filter), GTK_FILTER_CHANGE_DIFFERENT);
}
static void switch_page(GtkNotebook *notebook, GtkWidget *page, guint number, Browser *b)
{
    (void)notebook; (void)number; Tab *tab = (Tab *)page;
    if (b->closing || !tab->location) return;
    char *path = g_file_get_parse_name(tab->location);
    gtk_editable_set_text(GTK_EDITABLE(b->location), path); g_free(path);
    /* The signal runs before current-page changes. Avoid editing the old tab. */
    g_signal_handlers_block_by_func(b->search, search_changed, b);
    gtk_editable_set_text(GTK_EDITABLE(b->search), tab->query ? tab->query : "");
    g_signal_handlers_unblock_by_func(b->search, search_changed, b);
    message(b, NULL);
}
static void page_changed(GObject *object, GParamSpec *pspec, Browser *b)
{
    (void)object; (void)pspec;
    Tab *tab = current_tab(b);
    if (tab && tab->refresh_pending) schedule_monitor_refresh(tab);
    update_window(b);
}
static gboolean close_requested(GtkWindow *window, gpointer data)
{
    (void)data; Browser *b = (Browser *)window;
    if (b->operation) { message(b, _("Wait for the file operation to finish, or cancel it first.")); return TRUE; }
    if (b->paste_cancel) g_cancellable_cancel(b->paste_cancel);
    save_preferences(b, PREF_GEOMETRY); return FALSE;
}
static void pcmgr_browser_dispose(GObject *object)
{
    Browser *b = (Browser *)object;
    b->closing = TRUE;
    if (b->bookmarks) g_signal_handlers_disconnect_by_data(b->bookmarks, b);
    if (b->volumes) g_signal_handlers_disconnect_by_data(b->volumes, b);
    g_clear_object(&b->bookmarks); g_clear_object(&b->volumes);
    g_clear_object(&b->actions); g_clear_object(&b->move_clipboard);
    if (b->operation) g_cancellable_cancel(b->operation);
    if (b->paste_cancel) g_cancellable_cancel(b->paste_cancel);
    g_clear_object(&b->paste_cancel);
    g_clear_object(&b->operation);
    if (b->counted) {
        b->counted = FALSE; browsers = g_list_remove(browsers, b);
        if (!browsers && idle_callback) idle_callback();
    }
    G_OBJECT_CLASS(pcmgr_browser_parent_class)->dispose(object);
}
static void pcmgr_browser_class_init(BrowserClass *klass) { G_OBJECT_CLASS(klass)->dispose = pcmgr_browser_dispose; }
static GtkWidget *tool_button(const char *icon, const char *tooltip, const char *action)
{
    GtkWidget *button = gtk_button_new_from_icon_name(icon);
    gtk_widget_set_tooltip_text(button, tooltip); gtk_actionable_set_action_name(GTK_ACTIONABLE(button), action); return button;
}
static void pcmgr_browser_init(Browser *b)
{
    /* Instance initializer; public initialization uses pcmgr_browser_setup. */
    b->icons = TRUE;
    GKeyFile *key = g_key_file_new();
    if (config_path) browser_keyfile_load_bounded(key, config_path);
    b->show_hidden = g_key_file_get_boolean(key, "ui", "show_hidden", NULL);
    char *mode = g_key_file_get_string(key, "ui", "view_mode", NULL);
    if (mode) b->icons = !strcmp(mode, "icon");
    g_free(mode);
    int width = g_key_file_get_integer(key, "ui", "win_width", NULL), height = g_key_file_get_integer(key, "ui", "win_height", NULL);
    gtk_window_set_default_size(GTK_WINDOW(b), width >= 480 ? width : 960, height >= 320 ? height : 640); g_key_file_unref(key);
    gtk_window_set_icon_name(GTK_WINDOW(b), "pcmanager"); gtk_window_set_title(GTK_WINDOW(b), "PCManager");
    b->actions = g_simple_action_group_new();
    const char *names[] = { "back", "forward", "up", "home", "refresh", "location", "search", "new-tab", "new-window", "close",
        "icons", "list", "hidden", "select-all", "copy", "cut", "paste", "new-folder", "new-file", "rename", "trash", "empty-trash", "properties",
        "open", "open-tab", "open-with", "extract-here", "bookmark", "terminal", "about" };
    for (guint i = 0; i < G_N_ELEMENTS(names); i++) {
        GSimpleAction *action = !strcmp(names[i], "hidden") ? g_simple_action_new_stateful(names[i], NULL, g_variant_new_boolean(b->show_hidden)) : g_simple_action_new(names[i], NULL);
        g_signal_connect(action, "activate", G_CALLBACK(action_activate), b);
        g_action_map_add_action(G_ACTION_MAP(b->actions), G_ACTION(action)); g_object_unref(action);
    }
    gtk_widget_insert_action_group(GTK_WIDGET(b), "win", G_ACTION_GROUP(b->actions));
    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    /* GtkApplicationWindow hosts the header bar in an GtkBox,
     * never via gtk_window_set_titlebar. */
    GtkWidget *toolbar_view = luxe_toolbar_new();
    /* R2 headerbar chrome: hamburger menu carries the former menubar
     * groups as sections; the toolbar stays as content. */
    GMenu *bar = g_menu_new(), *file = file_menu(), *edit = edit_menu(), *view = g_menu_new(), *go = g_menu_new();
    g_menu_append(view, _("Icons"), "win.icons"); g_menu_append(view, _("Detailed List"), "win.list");
    g_menu_append(view, _("Show Hidden Files"), "win.hidden"); g_menu_append(view, _("Refresh"), "win.refresh");
    g_menu_append(go, _("Home"), "win.home"); g_menu_append(go, _("Parent Folder"), "win.up");
    g_menu_append(go, _("Bookmark This Folder"), "win.bookmark");
    g_menu_append_section(bar, NULL, G_MENU_MODEL(file));
    g_menu_append_section(bar, NULL, G_MENU_MODEL(edit));
    g_menu_append_section(bar, NULL, G_MENU_MODEL(view));
    g_menu_append_section(bar, NULL, G_MENU_MODEL(go));
    GMenu *help2 = g_menu_new(); g_menu_append(help2, _("About PCManager"), "win.about");
    g_menu_append_section(bar, NULL, G_MENU_MODEL(help2)); g_object_unref(help2);
    GtkWidget *header = gtk_header_bar_new();
    GtkWidget *menu_button = gtk_menu_button_new();
    gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(menu_button), "open-menu-symbolic");
    gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(menu_button), G_MENU_MODEL(bar));
    gtk_widget_set_tooltip_text(menu_button, _("Main menu"));
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), menu_button);
    luxe_toolbar_add_top_bar(GTK_BOX(toolbar_view), header);
    luxe_toolbar_set_content(GTK_BOX(toolbar_view), root);
    luxe_window_set_content(GTK_WINDOW(b), toolbar_view);
    g_object_unref(file); g_object_unref(edit); g_object_unref(view); g_object_unref(go); g_object_unref(bar);
    GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, LUXE_SPACING_INNER);
    gtk_widget_set_margin_start(toolbar, LUXE_SPACING_OUTER); gtk_widget_set_margin_end(toolbar, LUXE_SPACING_OUTER);
    gtk_widget_set_margin_top(toolbar, LUXE_SPACING_OUTER); gtk_widget_set_margin_bottom(toolbar, LUXE_SPACING_OUTER);
    gtk_box_append(GTK_BOX(toolbar), tool_button("go-previous-symbolic", _("Back (Alt+Left)"), "win.back"));
    gtk_box_append(GTK_BOX(toolbar), tool_button("go-next-symbolic", _("Forward (Alt+Right)"), "win.forward"));
    gtk_box_append(GTK_BOX(toolbar), tool_button("go-up-symbolic", _("Parent Folder (Alt+Up)"), "win.up"));
    gtk_box_append(GTK_BOX(toolbar), tool_button("go-home-symbolic", _("Home"), "win.home"));
    b->location = gtk_entry_new(); gtk_widget_set_hexpand(b->location, TRUE);
    gtk_widget_set_tooltip_text(b->location, _("Folder path or URI (Ctrl+L)")); gtk_box_append(GTK_BOX(toolbar), b->location);
    b->search = gtk_search_entry_new(); g_object_set(b->search, "placeholder-text", _("Filter this folder"), NULL);
    gtk_widget_set_size_request(b->search, 180, -1); gtk_box_append(GTK_BOX(toolbar), b->search);
    b->empty_trash = gtk_button_new_with_label(_("Empty Trash…"));
    gtk_actionable_set_action_name(GTK_ACTIONABLE(b->empty_trash), "win.empty-trash");
    gtk_widget_add_css_class(b->empty_trash, "destructive-action");
    gtk_widget_set_visible(b->empty_trash, FALSE);
    gtk_box_append(GTK_BOX(toolbar), b->empty_trash);
    gtk_box_append(GTK_BOX(root), toolbar);
    GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL); gtk_widget_set_vexpand(paned, TRUE);
    b->places = gtk_list_box_new(); gtk_list_box_set_activate_on_single_click(GTK_LIST_BOX(b->places), TRUE);
    gtk_widget_add_css_class(b->places, "navigation-sidebar");
    GtkWidget *scroll = gtk_scrolled_window_new(); gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(scroll, 160, -1); gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), b->places);
    gtk_paned_set_start_child(GTK_PANED(paned), scroll); gtk_paned_set_resize_start_child(GTK_PANED(paned), FALSE); gtk_paned_set_position(GTK_PANED(paned), 190);
    b->notebook = gtk_notebook_new(); gtk_notebook_set_scrollable(GTK_NOTEBOOK(b->notebook), TRUE);
    gtk_paned_set_end_child(GTK_PANED(paned), b->notebook); gtk_box_append(GTK_BOX(root), paned);
    b->message = gtk_label_new(NULL); gtk_label_set_wrap(GTK_LABEL(b->message), TRUE); gtk_label_set_xalign(GTK_LABEL(b->message), 0);
    gtk_widget_set_margin_start(b->message, 12); gtk_widget_set_margin_end(b->message, 12);
    gtk_widget_set_visible(b->message, FALSE); gtk_box_append(GTK_BOX(root), b->message);
    b->operation_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10); b->operation_label = gtk_label_new(NULL);
    gtk_widget_set_hexpand(b->operation_label, TRUE); gtk_box_append(GTK_BOX(b->operation_box), b->operation_label);
    b->operation_progress = gtk_progress_bar_new();
    gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(b->operation_progress), TRUE);
    gtk_widget_set_size_request(b->operation_progress, 220, -1);
    gtk_widget_set_valign(b->operation_progress, GTK_ALIGN_CENTER);
    gtk_widget_set_visible(b->operation_progress, FALSE);
    gtk_box_append(GTK_BOX(b->operation_box), b->operation_progress);
    GtkWidget *cancel = gtk_button_new_with_label(_("Cancel")); gtk_box_append(GTK_BOX(b->operation_box), cancel);
    g_signal_connect(cancel, "clicked", G_CALLBACK(cancel_operation), b);
    gtk_widget_set_visible(b->operation_box, FALSE); gtk_box_append(GTK_BOX(root), b->operation_box);
    GtkWidget *status = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8); b->spinner = gtk_spinner_new(); b->status = gtk_label_new(NULL);
    gtk_widget_set_margin_start(status, 12); gtk_widget_set_margin_top(status, 6); gtk_widget_set_margin_bottom(status, 6);
    gtk_box_append(GTK_BOX(status), b->spinner); gtk_box_append(GTK_BOX(status), b->status);
    gtk_widget_set_hexpand(b->status, TRUE); gtk_label_set_xalign(GTK_LABEL(b->status), 0);
    b->space = gtk_label_new(NULL); gtk_widget_set_margin_end(b->space, 12);
    gtk_label_set_ellipsize(GTK_LABEL(b->space), PANGO_ELLIPSIZE_END);
    gtk_widget_set_tooltip_text(b->space, _("Disk usage for the current folder's filesystem"));
    gtk_box_append(GTK_BOX(status), b->space); gtk_box_append(GTK_BOX(root), status);
    b->bookmarks = fm_bookmarks_dup(); b->volumes = g_volume_monitor_get(); rebuild_places(b);
    g_signal_connect(b->bookmarks, "changed", G_CALLBACK(bookmarks_changed), b);
    g_signal_connect(b->volumes, "mount-added", G_CALLBACK(mounts_changed), b);
    g_signal_connect(b->volumes, "mount-removed", G_CALLBACK(mounts_changed), b);
    g_signal_connect(b->volumes, "mount-changed", G_CALLBACK(mounts_changed), b);
    g_signal_connect(b->volumes, "volume-added", G_CALLBACK(volume_added), b);
    g_signal_connect(b->volumes, "volume-removed", G_CALLBACK(volumes_changed), b);
    g_signal_connect(b->volumes, "volume-changed", G_CALLBACK(volumes_changed), b);
    g_signal_connect(b->places, "row-activated", G_CALLBACK(place_activated), b);
    g_signal_connect(b->location, "activate", G_CALLBACK(location_activate), b);
    g_signal_connect(b->search, "changed", G_CALLBACK(search_changed), b);
    g_signal_connect(b->notebook, "switch-page", G_CALLBACK(switch_page), b);
    g_signal_connect(b->notebook, "notify::page", G_CALLBACK(page_changed), b);
    g_signal_connect(b, "close-request", G_CALLBACK(close_requested), NULL);
    GtkEventController *controller = gtk_shortcut_controller_new();
    gtk_shortcut_controller_set_scope(GTK_SHORTCUT_CONTROLLER(controller), GTK_SHORTCUT_SCOPE_MANAGED);
    const char *keys[][2] = { {"<Control>l", "location"}, {"<Control>f", "search"}, {"<Control>t", "new-tab"},
        {"<Control>n", "new-window"}, {"<Control>w", "close"}, {"<Alt>Left", "back"}, {"<Alt>Right", "forward"},
        {"<Alt>Up", "up"}, {"<Alt>Home", "home"}, {"F5", "refresh"}, {"<Control>h", "hidden"},
        {"<Control>1", "icons"}, {"<Control>2", "list"}, {"<Control>c", "copy"}, {"<Control>x", "cut"},
        {"<Control>v", "paste"}, {"<Control>a", "select-all"}, {"Delete", "trash"}, {"F2", "rename"},
        {"<Control><Shift>n", "new-folder"}, {"<Alt>Return", "properties"}, {"<Control>d", "bookmark"}, {"F4", "terminal"} };
    for (guint i = 0; i < G_N_ELEMENTS(keys); i++) {
        char *action = g_strconcat("win.", keys[i][1], NULL);
        gtk_shortcut_controller_add_shortcut(GTK_SHORTCUT_CONTROLLER(controller),
            gtk_shortcut_new(gtk_shortcut_trigger_parse_string(keys[i][0]), gtk_named_action_new(action))); g_free(action);
    }
    gtk_widget_add_controller(GTK_WIDGET(b), controller);
}
static Browser *new_browser(void)
{
    Browser *b;
    if (browser_app)
        b = g_object_new(pcmgr_browser_get_type(), "application", browser_app, NULL);
    else
        b = g_object_new(pcmgr_browser_get_type(), NULL);
    b->counted = TRUE; browsers = g_list_prepend(browsers, b); return b;
}
static gboolean browser_profile_valid(const char *value)
{
    if (value == NULL || *value == '\0' || strlen(value) > 64)
        return FALSE;
    for (const char *p = value; *p; ++p)
        if (!(g_ascii_isalnum(*p) || *p == '-' || *p == '_'))
            return FALSE;
    return TRUE;
}

void pcmgr_browser_setup(const char *profile, GCallback callback)
{
    const char *safe_profile = browser_profile_valid(profile) ? profile : "default";
    g_free(config_path);
    config_path = g_build_filename(g_get_user_config_dir(), "pcmanager", safe_profile, "pcmanager.conf", NULL);
    idle_callback = (void (*)(void))callback;
}
guint pcmgr_browser_count(void) { return g_list_length(browsers); }
void pcmgr_browser_open(const char * const *paths, const char *cwd, gboolean new_window)
{
    Browser *b = new_window || !browsers ? new_browser() : browsers->data;
    if (!paths || !paths[0]) {
        GFile *home = g_file_new_for_path(g_get_home_dir()); add_tab(b, home); g_object_unref(home);
    } else for (guint i = 0; paths[i]; i++) {
        GFile *file = g_file_new_for_commandline_arg_and_cwd(paths[i], cwd ? cwd : g_get_home_dir());
        Tab *tab = add_tab(b, file); check_location(tab, file); g_object_unref(file);
    }
    gtk_window_present(GTK_WINDOW(b));
}
void pcmgr_browser_preferences(void)
{
    if (!browsers) pcmgr_browser_open(NULL, NULL, TRUE);
    Browser *b = browsers->data;
    luxe_show_message(GTK_WINDOW(b), _("Browser Preferences"),
        _("Use the menu to choose icons or a detailed list and show hidden files. These choices and the window size are saved automatically."));
}
