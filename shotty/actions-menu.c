/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-3.0-or-later */

#include <gtk/gtk.h>
#include "luxe-gtk-ui.h"
#include <glib/gi18n.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <linux/fs.h>
#include <dirent.h>
#include <time.h>
#include <unistd.h>
#include <glib/gstdio.h>

#include "screenshot-app.h"
#include "screenshot-menu.h"
#include "actions-menu.h"

typedef struct {
	GWeakRef window;
	GCancellable *cancellable;
	guint timeout;
} CopyRequest;

#ifndef SHOTTY_CLIPBOARD_TIMEOUT_MS
#define SHOTTY_CLIPBOARD_TIMEOUT_MS 5000
#endif

struct _ActionsMenuPrivate
{
	GSettings *settings;

	GtkWidget *action_save;
	GtkWidget *action_copy;
	GtkWidget *action_open;
	GtkWidget *ok_button;
	CopyRequest *copy_request; /* Borrowed; the async callback owns it. */
	GCancellable *save_cancellable;

	GtkWidget *png_application;

	GtkWidget *preview_drawing_area;
	GtkWidget *preview_aspect_frame;
	guint handoff_source;
	gchar *handoff_file;
};

typedef struct _ActionsMenuPrivate ActionsMenuPrivate;

struct _ActionsMenu
{
	GtkApplicationWindow parent;

	ActionsMenuPrivate *priv;

	GdkPixbuf *screenshot;

	GdkPixbuf *preview_scaled;
	gint preview_alloc_w;
	gint preview_alloc_h;
};

G_DEFINE_TYPE_WITH_PRIVATE (ActionsMenu,
                            actions_menu,
                            GTK_TYPE_APPLICATION_WINDOW);

gboolean
actions_menu_is_handoff (ActionsMenu *win)
{
	return win->priv->handoff_source != 0;
}

typedef struct
{
	ActionsMenu *win;
	GtkFileDialog *dialog;
} SaveRequest;

static void
save_request_free (gpointer data)
{
	SaveRequest *request = data;

	g_clear_object (&request->dialog);
	g_clear_object (&request->win);
	g_free (request);
}

static void
show_error (ActionsMenu *win, const gchar *message, const gchar *detail)
{
	GtkWindow *dialog = luxe_choice_dialog_new (message, detail);
	luxe_choice_add_response (GTK_DIALOG (dialog), "close", _("Close"));
	luxe_dialog_present (dialog, GTK_WIDGET (win));
}

/* The handoff file is already exclusively created; write to that descriptor
 * instead of reopening a pathname that could have changed in the meantime. */
typedef struct { int fd; GCancellable *cancel; } PngWriter;
static gboolean
write_png_chunk (const gchar *buffer, gsize count, GError **error, gpointer data)
{
	PngWriter *writer = data;
	while (count > 0) {
		if (writer->cancel && g_cancellable_set_error_if_cancelled (writer->cancel, error)) return FALSE;
		ssize_t n = write (writer->fd, buffer, count);
		if (n < 0 && errno == EINTR) continue;
		if (n <= 0) {
			int err = n < 0 ? errno : EIO;
			g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (err), "%s", g_strerror (err));
			return FALSE;
		}
		buffer += n; count -= (gsize) n;
	}
	return TRUE;
}

static gboolean
save_pixbuf_to_fd_cancel (GdkPixbuf *pixbuf, int fd, GCancellable *cancel, GError **error)
{
	PngWriter writer = { fd, cancel };
	return gdk_pixbuf_save_to_callback (pixbuf, write_png_chunk, &writer, "png", error, NULL);
}
static gboolean save_pixbuf_to_fd (GdkPixbuf *pixbuf, int fd, GError **error)
{ return save_pixbuf_to_fd_cancel (pixbuf, fd, NULL, error); }

static gboolean
save_pixbuf_atomically (GdkPixbuf *pixbuf, const gchar *path, GCancellable *cancel, GError **error)
{
	if (cancel && g_cancellable_set_error_if_cancelled (cancel, error)) return FALSE;
	struct stat before, current, fd_stat;
	gboolean exists = g_lstat (path, &before) == 0;
	if (!exists && errno != ENOENT) {
		int err = errno;
		g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (err), "%s", g_strerror (err));
		return FALSE;
	}
	if (exists && (!S_ISREG (before.st_mode) || before.st_uid != getuid ())) {
		g_set_error_literal (error, G_FILE_ERROR, G_FILE_ERROR_ACCES,
		                     _("The destination must be a regular file owned by you."));
		return FALSE;
	}
	g_autofree char *directory = g_path_get_dirname (path);
	g_autofree char *temp = g_build_filename (directory, ".shotty-save-XXXXXX", NULL);
	int fd = g_mkstemp_full (temp, O_RDWR | O_CLOEXEC, 0600);
	if (fd < 0) {
		int err = errno;
		g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (err), "%s", g_strerror (err));
		return FALSE;
	}
	if (fstat (fd, &fd_stat) < 0) {
		int err = errno; close (fd); g_unlink (temp);
		g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (err), "%s", g_strerror (err));
		return FALSE;
	}
	gboolean ok = TRUE;
	gboolean preserve_temp = FALSE;
	gboolean exchanged = FALSE;
	gboolean linked_new = FALSE;
	if (ok) ok = save_pixbuf_to_fd_cancel (pixbuf, fd, cancel, error);
	if (ok && exists && fchmod (fd, before.st_mode & 0777) < 0) {
		int err = errno;
		g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (err), "%s", g_strerror (err));
		ok = FALSE;
	}
	if (ok && fsync (fd) < 0) {
		int err = errno;
		g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (err),
		             "Could not make screenshot data durable: %s", g_strerror (err));
		ok = FALSE;
	}
	if (close (fd) < 0 && ok) {
		int err = errno;
		g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (err), "%s", g_strerror (err));
		ok = FALSE;
	}
	if (ok) {
		gboolean now_exists = g_lstat (path, &current) == 0;
		int inspect_errno = errno;
		if ((!now_exists && inspect_errno != ENOENT) || now_exists != exists ||
		    (exists && (before.st_dev != current.st_dev || before.st_ino != current.st_ino ||
		                before.st_size != current.st_size || before.st_mtim.tv_sec != current.st_mtim.tv_sec ||
		                before.st_mtim.tv_nsec != current.st_mtim.tv_nsec))) {
			g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_BUSY,
			                     _("The destination changed while the screenshot was being saved."));
			ok = FALSE;
		}
	}
	if (ok && cancel && g_cancellable_set_error_if_cancelled (cancel, error)) ok = FALSE;
	if (ok) {
		if (!exists) {
			/* link() gives a race-free no-replace publish for a new path. */
			if (link (temp, path) < 0) {
				int err = errno;
				g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (err), "%s", g_strerror (err));
				ok = FALSE;
			} else linked_new = TRUE;
		} else {
			/* Exchange preserves the old entry until its identity is checked
			 * after the atomic swap, closing the final check/replace race. */
#ifdef SYS_renameat2
			if (syscall (SYS_renameat2, AT_FDCWD, temp, AT_FDCWD, path, RENAME_EXCHANGE) < 0) {
				int err = errno;
				g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (err), "%s", g_strerror (err));
				ok = FALSE;
			} else {
				struct stat displaced, published;
				gboolean still_ours = g_lstat (temp, &displaced) == 0 &&
					before.st_dev == displaced.st_dev && before.st_ino == displaced.st_ino &&
					g_lstat (path, &published) == 0 && fd_stat.st_dev == published.st_dev &&
					fd_stat.st_ino == published.st_ino;
				if (!still_ours) {
					if (g_lstat (path, &published) == 0 && fd_stat.st_dev == published.st_dev &&
					    fd_stat.st_ino == published.st_ino &&
					    syscall (SYS_renameat2, AT_FDCWD, temp, AT_FDCWD, path, RENAME_EXCHANGE) == 0) {
						g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_BUSY,
						                     _("The destination changed during save; the original was restored."));
					} else {
						g_set_error (error, G_IO_ERROR, G_IO_ERROR_BUSY,
						             _("The destination changed during save; review the preserved file at %s."), temp);
						preserve_temp = TRUE;
					}
					ok = FALSE;
				} else exchanged = TRUE;
			}
#else
			g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
			                     _("Safe replacement is unavailable on this system."));
			ok = FALSE;
#endif
		}
	}
	if (ok) {
		int dir_fd = open (directory, O_RDONLY | O_CLOEXEC | O_DIRECTORY | O_NOFOLLOW);
		if (dir_fd < 0 || fsync (dir_fd) < 0) {
			int err = errno;
			g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (err),
			             "Could not make the screenshot directory durable: %s", g_strerror (err));
			ok = FALSE;
			if (exchanged) {
				struct stat current_path;
				if (g_lstat (path, &current_path) == 0 && fd_stat.st_dev == current_path.st_dev &&
				    fd_stat.st_ino == current_path.st_ino &&
				    syscall (SYS_renameat2, AT_FDCWD, temp, AT_FDCWD, path, RENAME_EXCHANGE) == 0) {
					exchanged = FALSE;
					if (dir_fd >= 0) (void) fsync (dir_fd);
				} else {
					preserve_temp = TRUE;
					g_prefix_error (error, "Original remains at %s; ", temp);
				}
			} else if (linked_new) {
				struct stat current_path;
				if (g_lstat (path, &current_path) == 0 && fd_stat.st_dev == current_path.st_dev &&
				    fd_stat.st_ino == current_path.st_ino) {
					(void) g_unlink (path);
					if (dir_fd >= 0) (void) fsync (dir_fd);
				}
			}
		}
		if (dir_fd >= 0) close (dir_fd);
	}
	if (ok && (exchanged || linked_new)) {
		if (g_unlink (temp) < 0) {
			g_warning ("Screenshot was saved but staging cleanup failed for %s: %s", temp, g_strerror (errno));
		} else {
			int dir_fd = open (directory, O_RDONLY | O_CLOEXEC | O_DIRECTORY | O_NOFOLLOW);
			if (dir_fd >= 0) { (void) fsync (dir_fd); close (dir_fd); }
		}
	}
	if (!ok && !preserve_temp) g_unlink (temp);
	return ok;
}

typedef struct { GWeakRef window; GApplication *app; GdkPixbuf *pixbuf; char *path; } SaveWork;
static void save_work_free (gpointer data)
{
	SaveWork *work = data;
	g_weak_ref_clear (&work->window); g_clear_object (&work->pixbuf);
	g_clear_object (&work->app); g_free (work->path); g_free (work);
}
static void save_worker (GTask *task, gpointer source, gpointer data, GCancellable *cancel)
{
	(void) source; SaveWork *work = data; g_autoptr(GError) error = NULL;
	if (save_pixbuf_atomically (work->pixbuf, work->path, cancel, &error)) g_task_return_boolean (task, TRUE);
	else g_task_return_error (task, g_steal_pointer (&error));
}
static void save_completed (GObject *source, GAsyncResult *result, gpointer data)
{
	(void) source; (void) data; GTask *task = G_TASK (result);
	SaveWork *work = g_task_get_task_data (task); g_autoptr(GError) error = NULL;
	gboolean ok = g_task_propagate_boolean (task, &error);
	ActionsMenu *win = g_weak_ref_get (&work->window);
	if (win && win->priv->save_cancellable == g_task_get_cancellable (task)) {
		g_clear_object (&win->priv->save_cancellable);
		gtk_widget_set_sensitive (win->priv->ok_button, TRUE);
		if (ok) gtk_window_destroy (GTK_WINDOW (win));
		else if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
			show_error (win, _("Saving the screenshot failed"), error->message);
	}
	if (win) g_object_unref (win);
	g_application_release (work->app);
}
static void save_async (ActionsMenu *win, const char *path)
{
	if (win->priv->save_cancellable) return;
	SaveWork *work = g_new0 (SaveWork, 1);
	g_weak_ref_init (&work->window, win);
	work->pixbuf = g_object_ref (win->screenshot); work->path = g_strdup (path);
	work->app = G_APPLICATION (g_object_ref (gtk_window_get_application (GTK_WINDOW (win))));
	g_application_hold (work->app);
	win->priv->save_cancellable = g_cancellable_new ();
	gtk_widget_set_sensitive (win->priv->ok_button, FALSE);
	GTask *task = g_task_new (NULL, win->priv->save_cancellable, save_completed, NULL);
	g_task_set_task_data (task, work, save_work_free);
	g_task_run_in_thread (task, save_worker); g_object_unref (task);
}

static void
on_save_response (GObject      *source,
                  GAsyncResult *result,
                  gpointer      data)
{
	(void) source;
	SaveRequest *request = data;
	GFile *file;
	GError *error = NULL;

	file = gtk_file_dialog_save_finish (request->dialog, result, &error);
	if (gtk_window_get_application (GTK_WINDOW (request->win)) == NULL) {
		g_clear_object (&file);
		g_clear_error (&error);
		save_request_free (request);
		return;
	}
	gtk_widget_set_sensitive (GTK_WIDGET (request->win), TRUE);

	if (file != NULL)
	{
		gchar *path = g_file_get_path (file);
		if (path == NULL)
		{
			/* Non-native destinations (trash://, remote URIs, ...)
			 * have no local path; report instead of handing NULL
			 * to the saver, and stay open so another location can
			 * be chosen. */
			show_error (request->win, _("Saving the screenshot failed"),
			            _("The chosen location is not a local file."));
			g_object_unref (file);
		}
		else
		{
			save_async (request->win, path);
			g_free (path);
			g_object_unref (file);

		}
	}
	else if (! g_error_matches (error, GTK_DIALOG_ERROR, GTK_DIALOG_ERROR_DISMISSED))
	{
		show_error (request->win, _("Choosing a save location failed"), error->message);
	}

	g_clear_error (&error);
	save_request_free (request);
}

static void
action_save (ActionsMenu *win)
{
	SaveRequest *request;
	gchar *filename;
	GDateTime *now;
	GFile *cwd_folder;

	request = g_new0 (SaveRequest, 1);
	request->win = g_object_ref (win);
	request->dialog = gtk_file_dialog_new ();

	gtk_file_dialog_set_title (request->dialog, _("Save Screenshot"));

	/* Pin the dialog to the process working directory. Adw startup
	 * activates xdg-desktop-portal (Settings portal), which flips
	 * GtkFileDialog to the out-of-process backend; without an explicit
	 * folder that backend opens in its own cwd instead of ours. */
	cwd_folder = g_file_new_for_path (".");
	gtk_file_dialog_set_initial_folder (request->dialog, cwd_folder);
	g_object_unref (cwd_folder);

	// Generate and set the suggested filename
	now = g_date_time_new_now_local ();
	filename = g_date_time_format (now, "Screenshot_%Y-%m-%d_%H-%M-%S.png");
	gtk_file_dialog_set_initial_name (request->dialog, filename);
	g_date_time_unref (now);
	g_free (filename);

	// Ask for a destination and save asynchronously
	gtk_widget_set_sensitive (GTK_WIDGET (win), FALSE);
	gtk_file_dialog_save (request->dialog,
	                      GTK_WINDOW (win),
	                      NULL,
	                      on_save_response,
	                      request);
}

static void
finish_copy (CopyRequest *request, gboolean stored)
{
	ActionsMenu *win = g_weak_ref_get (&request->window);
	if (win == NULL)
		return;
	GtkApplication *app = gtk_window_get_application (GTK_WINDOW (win));
	if (app != NULL && win->priv->copy_request == request) {
		win->priv->copy_request = NULL;
		gtk_widget_set_sensitive (win->priv->ok_button, TRUE);
		if (stored)
			gtk_window_destroy (GTK_WINDOW (win));
		else
			show_error (win, _("Screenshot copied"),
			            _("The clipboard manager could not retain the image. You can still paste it while this window is open."));
	}
	g_object_unref (win);
}

static gboolean
clipboard_store_timeout (gpointer data)
{
	CopyRequest *request = data;
	request->timeout = 0;
	/* Restore usable UI even if a broken backend never completes cancellation. */
	finish_copy (request, FALSE);
	g_cancellable_cancel (request->cancellable);
	return G_SOURCE_REMOVE;
}

static void
clipboard_stored (GObject *source, GAsyncResult *result, gpointer data)
{
	CopyRequest *request = data;
	g_autoptr(GError) error = NULL;
	gboolean stored = gdk_clipboard_store_finish (GDK_CLIPBOARD (source), result, &error);
	if (request->timeout) g_source_remove (request->timeout);
	/* A timed-out or closed window no longer owns this request. A late
	 * success must not quit the app while the user is saving/taking another shot. */
	finish_copy (request, stored);
	g_weak_ref_clear (&request->window);
	g_object_unref (request->cancellable);
	g_free (request);
}

static void
action_copy (ActionsMenu *win)
{
	GdkClipboard *clipboard;
	GdkContentProvider *content;
	g_autoptr(GError) error = NULL;
	gchar *png = NULL;
	gsize png_size = 0;
	if (win->priv->copy_request != NULL)
		return;

	// Get the default clipboard object
	clipboard = gtk_widget_get_clipboard (GTK_WIDGET (win));

	/* Offer one interoperable image representation. Offering the pixbuf's
	 * many converted formats can exhaust a clipboard manager's aggregate
	 * budget before it saves PNG, while still reporting a successful store. */
	if (!gdk_pixbuf_save_to_buffer (win->screenshot, &png, &png_size, "png", &error, NULL)) {
		show_error (win, _("Copying the screenshot failed"), error->message);
		return;
	}
	GBytes *bytes = g_bytes_new_take (png, png_size);
	content = gdk_content_provider_new_for_bytes ("image/png", bytes);
	g_bytes_unref (bytes);
	gboolean copied = gdk_clipboard_set_content (clipboard, content);
	g_object_unref (content);
	if (!copied) {
		show_error (win, _("Copying the screenshot failed"), _("The clipboard could not accept the image."));
		return;
	}
	CopyRequest *request = g_new0 (CopyRequest, 1);
	g_weak_ref_init (&request->window, win);
	request->cancellable = g_cancellable_new ();
	win->priv->copy_request = request;
	/* Disable only duplicate submission, never the close/back controls. */
	gtk_widget_set_sensitive (win->priv->ok_button, FALSE);
	request->timeout = g_timeout_add (SHOTTY_CLIPBOARD_TIMEOUT_MS, clipboard_store_timeout, request);
	gdk_clipboard_store_async (clipboard, G_PRIORITY_DEFAULT, request->cancellable,
	                           clipboard_stored, request);
}

/* There is no generic viewer-open acknowledgement. Keep the private handoff
 * cache for a documented day, then remove expired generated files on the
 * next launch. */
#define SHOTTY_HANDOFF_RETENTION_SECONDS (24 * 60 * 60)
#define SHOTTY_HANDOFF_MAX_FILES 32
#define SHOTTY_HANDOFF_MAX_BYTES (512ULL * 1024 * 1024)
static gchar *handoff_cache_dir (const gchar *base, GError **error);

static gboolean
handoff_name_is_owned (const gchar *name)
{
	if (strlen (name) != strlen ("shotty-") + 6 + strlen (".png") ||
	    !g_str_has_prefix (name, "shotty-") || !g_str_has_suffix (name, ".png"))
		return FALSE;
	for (const gchar *p = name + strlen ("shotty-"); p < name + strlen (name) - strlen (".png"); p++)
		if (!g_ascii_isalnum (*p)) return FALSE;
	return TRUE;
}

static gboolean
handoff_cache_prepare (const gchar *directory, guint64 reserve_bytes, GError **error)
{
	int dir_fd = open (directory, O_RDONLY | O_CLOEXEC | O_DIRECTORY | O_NOFOLLOW);
	if (dir_fd < 0) {
		int err = errno;
		g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (err), "%s", g_strerror (err));
		return FALSE;
	}
	DIR *dir = fdopendir (dup (dir_fd));
	if (!dir) {
		int err = errno; close (dir_fd);
		g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (err), "%s", g_strerror (err));
		return FALSE;
	}
	guint count = 0;
	guint64 bytes = 0;
	time_t cutoff = time (NULL) - SHOTTY_HANDOFF_RETENTION_SECONDS;
	struct dirent *entry;
	while ((entry = readdir (dir)) != NULL) {
		if (!handoff_name_is_owned (entry->d_name)) continue;
		struct stat st;
		if (fstatat (dir_fd, entry->d_name, &st, AT_SYMLINK_NOFOLLOW) < 0 ||
		    !S_ISREG (st.st_mode) || st.st_uid != getuid () || st.st_nlink != 1)
			continue;
		if (st.st_mtime < cutoff) {
			if (unlinkat (dir_fd, entry->d_name, 0) == 0) continue;
		}
		count++;
		if (st.st_size > 0) bytes += (guint64) st.st_size;
	}
	closedir (dir);
	close (dir_fd);
	if ((reserve_bytes && count >= SHOTTY_HANDOFF_MAX_FILES) || reserve_bytes > SHOTTY_HANDOFF_MAX_BYTES ||
	    bytes > SHOTTY_HANDOFF_MAX_BYTES - reserve_bytes) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NO_SPACE,
		                     _("Screenshot handoffs are using the 512 MiB or 32-file cache limit. Remove old files from the Shotty cache and try again."));
		return FALSE;
	}
	return TRUE;
}

gboolean
shotty_cleanup_handoff_cache (GError **error)
{
	gchar *directory = handoff_cache_dir (g_get_user_cache_dir (), error);
	if (!directory) return FALSE;
	gboolean ok = handoff_cache_prepare (directory, 0, error);
	g_free (directory);
	return ok;
}

static gboolean
finish_open_handoff (gpointer data)
{
	ActionsMenu *win = data;
	win->priv->handoff_source = 0;
	g_unlink (win->priv->handoff_file);
	g_clear_pointer (&win->priv->handoff_file, g_free);
	gtk_window_destroy (GTK_WINDOW (win));
	return G_SOURCE_REMOVE;
}

static gchar *
handoff_cache_dir (const gchar *base, GError **error)
{
	gchar *dir = g_build_filename (base, "shotty", NULL);
	if (g_mkdir_with_parents (dir, 0700) < 0) {
		int err = errno;
		g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (err),
		             _("Cannot create screenshot cache: %s"), g_strerror (err));
		g_free (dir);
		return NULL;
	}
	struct stat st;
	if (g_lstat (dir, &st) < 0) {
		int err = errno;
		g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (err),
		             _("Cannot inspect screenshot cache: %s"), g_strerror (err));
		g_free (dir);
		return NULL;
	}
	if (!S_ISDIR (st.st_mode) || st.st_uid != getuid () || (st.st_mode & 077) != 0) {
		g_set_error_literal (error, G_FILE_ERROR, G_FILE_ERROR_ACCES,
		                     _("Screenshot cache must be a private directory owned by the current user."));
		g_free (dir);
		return NULL;
	}
	return dir;
}

static gboolean
action_open (ActionsMenu *win)
{
	GAppInfo *appinfo;
	gchar *filename;
	GError *error = NULL;
	GList *file = NULL;
	gchar *uri;
	int fd;

	// Get the selected appliaction information
	// GtkAppChooser is deprecated in GTK 4.18 without a replacement;
	// a custom picker is future work.
	G_GNUC_BEGIN_IGNORE_DEPRECATIONS
	appinfo = gtk_app_chooser_get_app_info (GTK_APP_CHOOSER (win->priv->png_application));
	G_GNUC_END_IGNORE_DEPRECATIONS

	// Nothing selected in the picker: a NULL appinfo would abort the launch.
	if (appinfo == NULL)
	{
		show_error (win, _("No application selected"), _("Choose an application to open the screenshot."));
		return FALSE;
	}

	/* Hand-off uses an exclusively created file in the user's cache
	 * directory: the old fixed /tmp/Screenshot_<second> name was predictable
	 * (collision/overwrite) and left screenshots world-visible in /tmp.
	 * The file is removed right after the viewer opens it. */
	gchar *cache_dir = handoff_cache_dir (g_get_user_cache_dir (), &error);
	if (!cache_dir) {
		show_error (win, _("Creating a temporary screenshot failed"), error->message);
		g_clear_error (&error);
		g_object_unref (appinfo);
		return FALSE;
	}
	guint64 reserve = (guint64) gdk_pixbuf_get_byte_length (win->screenshot) + 1024 * 1024;
	if (!handoff_cache_prepare (cache_dir, reserve, &error)) {
		show_error (win, _("Screenshot cache is full"), error->message);
		g_clear_error (&error);
		g_free (cache_dir);
		g_object_unref (appinfo);
		return FALSE;
	}
	gchar *template = g_build_filename (cache_dir, "shotty-XXXXXX.png", NULL);
	fd = g_mkstemp (template);
	g_free (cache_dir);

	if (fd < 0)
	{
		show_error (win, _("Creating a temporary screenshot failed"), g_strerror (errno));
		g_free (template);
		g_clear_object (&appinfo);
		return FALSE;
	}

	/* Keep restrictive permissions and use the original exclusive descriptor. */
	filename = template;
	gboolean saved = save_pixbuf_to_fd (win->screenshot, fd, &error);
	if (close (fd) < 0 && saved) {
		int err = errno;
		g_set_error (&error, G_FILE_ERROR, g_file_error_from_errno (err), "%s", g_strerror (err));
		saved = FALSE;
	}
	if (!saved) {
		show_error (win, _("Saving the screenshot failed"), error->message);
		g_clear_error (&error);
		g_unlink (filename);
		g_free (filename);
		g_object_unref (appinfo);
		return FALSE;
	}

	// Launch the appliacation with screnshot URI
	uri = g_filename_to_uri (filename, NULL, NULL);
	file = g_list_append (file, uri);
	if (! g_app_info_launch_uris (appinfo, file, NULL, &error))
	{
		/* Nothing will ever read the file; remove it right away. */
		show_error (win, _("Launching the application failed"), error->message);
		g_clear_error (&error);
		g_list_free_full (file, g_free);
		g_clear_object (&appinfo);
		g_unlink (filename);
		g_free (filename);
		return FALSE;
	}
	g_list_free_full (file, g_free);
	g_clear_object (&appinfo);

	/* Launch accepted: hide the window and hold the file for the grace
	 * period documented above; the timer owns filename from here on. */
	gtk_widget_set_visible (GTK_WIDGET (win), FALSE);
	win->priv->handoff_file = filename;
	win->priv->handoff_source = g_timeout_add_seconds (SHOTTY_HANDOFF_RETENTION_SECONDS, finish_open_handoff, win);
	return TRUE;
}

static void
preview_draw_func (GtkDrawingArea *widget,
                   cairo_t        *cr,
                   gint            width,
                   gint            height,
                   gpointer        data)
{
	(void) widget;
	ActionsMenu *win = ACTIONS_MENU (data);
	gdouble scale;
	gint pw, ph, dw, dh;
	gint offset_x, offset_y;

	if (win->screenshot == NULL)
		return;

	/* Cache the scaled pixbuf; only re-scale when the allocation changes. */
	if (win->preview_scaled == NULL
	    || win->preview_alloc_w != width
	    || win->preview_alloc_h != height)
	{
		// Scale the screenshot to fit inside of the drawing area
		pw = gdk_pixbuf_get_width (win->screenshot);
		ph = gdk_pixbuf_get_height (win->screenshot);
		scale = MIN ((gdouble) width / pw, (gdouble) height / ph);
		dw = MAX (1, (gint) (pw * scale));
		dh = MAX (1, (gint) (ph * scale));

		g_clear_object (&win->preview_scaled);
		win->preview_scaled = gdk_pixbuf_scale_simple (win->screenshot,
		                                               dw, dh,
		                                               GDK_INTERP_BILINEAR);
		win->preview_alloc_w = width;
		win->preview_alloc_h = height;
	}

	dw = gdk_pixbuf_get_width (win->preview_scaled);
	dh = gdk_pixbuf_get_height (win->preview_scaled);
	offset_x = (width - dw) / 2;
	offset_y = (height - dh) / 2;

	// Render the pixbuf inside of the drawing area
	gdk_cairo_set_source_pixbuf (cr, win->preview_scaled, offset_x, offset_y);
	cairo_paint (cr);
}

static void
action_open_toggled (GtkWidget   *widget,
                     ActionsMenu *win)
{
	gtk_widget_set_sensitive (GTK_WIDGET (win->priv->png_application),
	                          gtk_check_button_get_active (GTK_CHECK_BUTTON (widget)));
}

/* Existing profiles can contain conflicting legacy booleans. Prefer an
 * explicitly selected action, with open, copy, save as deterministic order. */
static void
migrate_action_setting (GSettings *settings)
{
	static const char *keys[] = { "action-open", "action-copy", "action-save" };
	static const char *values[] = { "open", "copy", "save" };
	g_autoptr(GVariant) current = g_settings_get_user_value (settings, "action");
	if (current != NULL)
		return;
	for (guint i = 0; i < G_N_ELEMENTS (keys); i++) {
		g_autoptr(GVariant) old = g_settings_get_user_value (settings, keys[i]);
		if (old != NULL && g_variant_get_boolean (old)) {
			g_settings_set_string (settings, "action", values[i]);
			return;
		}
	}
	g_settings_set_string (settings, "action", "save");
}

static void
action_setting_changed (GSettings *settings, const char *key, ActionsMenu *win)
{
	(void) key;
	g_autofree char *action = g_settings_get_string (settings, "action");
	GtkWidget *button = g_str_equal (action, "open") ? win->priv->action_open :
	                    g_str_equal (action, "copy") ? win->priv->action_copy :
	                    win->priv->action_save;
	gtk_check_button_set_active (GTK_CHECK_BUTTON (button), TRUE);
}

static void
action_selected (GtkCheckButton *button, ActionsMenu *win)
{
	if (!gtk_check_button_get_active (button))
		return;
	const char *action = GTK_WIDGET (button) == win->priv->action_open ? "open" :
	                     GTK_WIDGET (button) == win->priv->action_copy ? "copy" : "save";
	g_settings_set_string (win->priv->settings, "action", action);
}

static void
back_button_clicked (GtkWidget   *widget,
                     ActionsMenu *win)
{
	(void) widget;
	GtkApplication *app;
	ScreenshotMenu *new_win;

	// Create a new screenshot menu window and add it to the application
	app = gtk_window_get_application (GTK_WINDOW (win));
	new_win = screenshot_menu_new (SCREENSHOT_APP (app));

	// Close the actions menu window (which removes it from the application)
	gtk_window_destroy (GTK_WINDOW (win));

	// Show the screenshot menu window
	gtk_window_present (GTK_WINDOW (new_win));
}

static void
ok_button_clicked (GtkWidget   *widget,
                   ActionsMenu *win)
{
	(void) widget;

	if (gtk_check_button_get_active (GTK_CHECK_BUTTON (win->priv->action_save)))
	{
		// The save dialog decides when to quit, in its own response handler
		action_save (win);
		return;
	}

	if (gtk_check_button_get_active (GTK_CHECK_BUTTON (win->priv->action_copy)))
	{
		action_copy (win);
		return;
	}

	if (gtk_check_button_get_active (GTK_CHECK_BUTTON (win->priv->action_open)))
	{
		action_open (win);
	}
}

static void
actions_menu_dispose (GObject *object)
{
	ActionsMenuPrivate *priv = ACTIONS_MENU (object)->priv;
	if (priv->save_cancellable) {
		g_cancellable_cancel (priv->save_cancellable);
		g_clear_object (&priv->save_cancellable);
	}
	if (priv->copy_request != NULL) {
		CopyRequest *request = priv->copy_request;
		priv->copy_request = NULL;
		if (request->timeout) {
			g_source_remove (request->timeout);
			request->timeout = 0;
		}
		g_cancellable_cancel (request->cancellable);
	}
	if (priv->handoff_source) {
		g_source_remove (priv->handoff_source);
		priv->handoff_source = 0;
	}
	/* A viewer may still open this path after Shotty exits. Expiration cleanup
	 * on the next launch removes it; dispose must never shorten the contract. */
	g_clear_pointer (&priv->handoff_file, g_free);
	// Dispose of settings object
	g_clear_object (&ACTIONS_MENU (object)->priv->settings);

	// Dispose of parent class
	G_OBJECT_CLASS (actions_menu_parent_class)->dispose (object);
}

static void
actions_menu_finalize (GObject *object)
{
	g_clear_object (&ACTIONS_MENU (object)->screenshot);
	g_clear_object (&ACTIONS_MENU (object)->preview_scaled);

	// Dispose of parent class
	G_OBJECT_CLASS (actions_menu_parent_class)->finalize (object);
}

static void
actions_menu_init (ActionsMenu *win)
{
	// Set private window instance
	win->priv = actions_menu_get_instance_private (win);

	// Initialize window from the template
	gtk_widget_init_template (GTK_WIDGET (win));

	// Preview cache starts empty; first draw populates it
	win->preview_scaled = NULL;
	win->preview_alloc_w = -1;
	win->preview_alloc_h = -1;

	// Initialize GSettings
	win->priv->settings = g_settings_new ("org.luxe.Shotty");

	migrate_action_setting (win->priv->settings);
	action_setting_changed (win->priv->settings, "action", win);
	g_signal_connect_object (win->priv->settings, "changed::action",
	                         G_CALLBACK (action_setting_changed), win, 0);
	g_signal_connect (win->priv->action_save, "toggled", G_CALLBACK (action_selected), win);
	g_signal_connect (win->priv->action_copy, "toggled", G_CALLBACK (action_selected), win);
	g_signal_connect (win->priv->action_open, "toggled", G_CALLBACK (action_selected), win);

	// GtkAppChooserButton in GTK4 has no bindable selection property, so the
	// chosen application is not persisted (custom picker is future work).

	// Update UI elements
	action_open_toggled (GTK_WIDGET (win->priv->action_open), win);
}

static void
actions_menu_class_init (ActionsMenuClass *class)
{
	// Assign GObject entry points
	G_OBJECT_CLASS (class)->dispose = actions_menu_dispose;
	G_OBJECT_CLASS (class)->finalize = actions_menu_finalize;

	// Set window template from a resource
	gtk_widget_class_set_template_from_resource (GTK_WIDGET_CLASS (class), "/org/luxe/shotty/actions-menu.ui");

	// Bind widgets from the template
	gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (class), ActionsMenu, action_save);
	gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (class), ActionsMenu, action_copy);
	gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (class), ActionsMenu, action_open);
	gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (class), ActionsMenu, ok_button);

	gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (class), ActionsMenu, png_application);

	gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (class), ActionsMenu, preview_drawing_area);
	gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (class), ActionsMenu, preview_aspect_frame);

	// Bind signals from the template
	gtk_widget_class_bind_template_callback (GTK_WIDGET_CLASS (class), back_button_clicked);
	gtk_widget_class_bind_template_callback (GTK_WIDGET_CLASS (class), ok_button_clicked);

	gtk_widget_class_bind_template_callback (GTK_WIDGET_CLASS (class), action_open_toggled);
}

ActionsMenu *
actions_menu_new (GdkPixbuf *screenshot,
                  ScreenshotApp *app)
{
	ActionsMenu *win;
	gint width, height, scale;

	win = g_object_new (actions_menu_get_type (),
	                             "application",
	                             app, NULL);

	win->screenshot = g_object_ref (screenshot);

	// Screenshot preview scale 1:5 relative to its real size
	scale = 5 * gtk_widget_get_scale_factor (GTK_WIDGET (win));

	// Get screenshot dimensions and scale them
	width = MAX (1, gdk_pixbuf_get_width (screenshot) / scale);
	height = MAX (1, gdk_pixbuf_get_height (screenshot) / scale);

	// Set drawing preview area dimensions
	gtk_widget_set_size_request (GTK_WIDGET (win->priv->preview_drawing_area),
	                             width, height);

	// Register the preview draw function
	gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA (win->priv->preview_drawing_area),
	                                preview_draw_func,
	                                win,
	                                NULL);

	// Set preview aspect frame ratio
	gtk_aspect_frame_set_ratio (GTK_ASPECT_FRAME (win->priv->preview_aspect_frame),
	                            (gfloat) width / (gfloat) height);

	return win;
}
