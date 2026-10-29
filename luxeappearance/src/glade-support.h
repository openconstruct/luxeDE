/*
 * glade-support.h — GTK4 port (see glade-support.c).
 */

#ifndef LUXE_GLADE_SUPPORT_H
#define LUXE_GLADE_SUPPORT_H

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gtk/gtk.h>

/*
 * Standard gettext macros.
 */
#ifdef ENABLE_NLS
#  include <libintl.h>
#  undef _
#  define _(String) dgettext (PACKAGE, String)
#  ifndef Q_
#    define Q_(String) g_strip_context ((String), gettext (String))
#  endif
#  ifdef gettext_noop
#    define N_(String) gettext_noop (String)
#  else
#    define N_(String) (String)
#  endif
#else
#  define textdomain(String) (String)
#  define gettext(String) (String)
#  define dgettext(Domain,Message) (Message)
#  define dcgettext(Domain,Message,Type) (Message)
#  define bindtextdomain(Domain,Directory) (Domain)
#  define _(String) (String)
#  ifndef Q_
#    define Q_(String) g_strip_context ((String), (String))
#  endif
#  define N_(String) (String)
#endif


/*
 * Public Functions.
 */

/* Register a loaded GtkBuilder so lookup_widget() can find its objects.
 * The builder is owned by the registry until glade_clear_builders() runs;
 * callers must not unref it. */
void        glade_register_builder       (GtkBuilder      *builder);

/* Remove one builder from the registry and release its reference. This is
 * used when a UI file parsed successfully but did not contain its expected
 * root, so a failed secondary builder cannot invalidate the main dialog. */
void        glade_unregister_builder     (GtkBuilder      *builder);

/* Release every registered builder and reset the registry. Call when the
 * window is destroyed so a later activation cannot resolve widgets owned
 * by the previous instance. */
void        glade_clear_builders          (void);

/* Load a UI file from the installed data dir and register it.
 * Returns NULL with a warning when the installed file is missing. */
GtkBuilder* glade_load_ui                (const gchar     *filename);

/* X11 helpers (see glade-support.c). */
#include <gdk/gdk.h>
gboolean    have_x11                     (void);
#ifdef GDK_WINDOWING_X11
#include <X11/Xlib.h>
Display*    xdisplay                      (void);
#endif

/*
 * This function returns a widget loaded from one of the registered
 * builders. The widget parameter is ignored (kept for compatibility
 * with the Glade-generated call sites).
 */
GtkWidget*  lookup_widget              (GtkWidget       *widget,
                                        const gchar     *widget_name);

#endif /* LUXE_GLADE_SUPPORT_H */
