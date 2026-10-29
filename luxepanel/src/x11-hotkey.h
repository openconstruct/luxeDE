/* x11-hotkey.h -- X11 global hotkey support (GTK4 build of luxepanel).
 * Replacement for libkeybinder (no GTK4 release). X11-gated. */

#ifndef __LUXEPANEL_X11_HOTKEY_H__
#define __LUXEPANEL_X11_HOTKEY_H__

#include <glib.h>

G_BEGIN_DECLS

typedef struct _X11Hotkey X11Hotkey;

/* Bind a global hotkey described by a GTK accelerator string.
 * On success stores an opaque handle in hotkey_out (may be NULL) and
 * returns TRUE.  Returns FALSE if the keystring is invalid or the grab
 * fails (e.g. already owned by someone else, or no X display). */
gboolean x11_hotkey_bind(const char *keystring,
                         void (*handler)(const char *keystring, gpointer user_data),
                         gpointer user_data,
                         X11Hotkey **hotkey_out);

void x11_hotkey_unbind(X11Hotkey *hk);

G_END_DECLS

#endif /* __LUXEPANEL_X11_HOTKEY_H__ */
