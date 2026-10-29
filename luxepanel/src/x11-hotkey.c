/* x11-hotkey.c -- X11 global hotkey support for luxepanel (GTK4 build).
 *
 * Replaces libkeybinder, which has no GTK4-compatible release.
 * Uses a private X display connection + GSource pump so that grabbed
 * key events never interfere with GDK's event handling on the default
 * connection (GTK4 has no XEvent filter API).
 *
 * X11-gated: on non-X11 displays binding simply fails, like keybinder
 * failing to grab.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "x11-hotkey.h"

#include <gtk/gtk.h>
#include <gdk/x11/gdkx.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

struct _X11Hotkey
{
    char *keystring;
    guint keyval;
    guint mods;                  /* GDK modifier mask */
    guint keycode;               /* X hardware keycode */
    guint xmods;                 /* resolved X modifier mask */
    void (*handler)(const char *keystring, gpointer user_data);
    gpointer user_data;
};

typedef struct
{
    Display *xdisplay;           /* private connection */
    GSource *source;             /* pump for the private connection */
    guint super_mask;
    guint hyper_mask;
    guint meta_mask;
    guint alt_mask;              /* typically Mod1, verified via mapping */
    GHashTable *by_keycode;      /* keycode -> GList of X11Hotkey* */
} X11HotkeyContext;

typedef struct
{
    GSource source;
    GPollFD poll_fd;
    Display *xdisplay;
} X11EventSource;

static X11HotkeyContext *ctx = NULL;

static gboolean x11_event_source_check(GSource *source);
static gboolean x11_event_source_dispatch(GSource *source, GSourceFunc callback, gpointer data);

static GSourceFuncs x11_event_source_funcs = {
    NULL,
    x11_event_source_check,
    x11_event_source_dispatch,
    NULL, NULL, NULL
};

/* Resolve the X modifier mask holding the given keysym (XK_Super_L etc.) */
static guint x11_modifier_mask_for(Display *dpy, KeySym keysym)
{
    XModifierKeymap *mk;
    KeyCode want;
    guint mask = 0;
    int m, i;

    mk = XGetModifierMapping(dpy);
    if (mk == NULL)
        return 0;
    want = XKeysymToKeycode(dpy, keysym);
    for (m = 0; m < 8; m++)
    {
        for (i = 0; i < mk->max_keypermod; i++)
        {
            KeyCode code = mk->modifiermap[m * mk->max_keypermod + i];
            if (want != 0 && code == want)
                mask |= (1 << m);
        }
    }
    XFreeModifiermap(mk);
    return mask;
}

static void x11_context_init(void)
{
    X11EventSource *src;

    if (ctx != NULL && ctx->xdisplay != NULL)
        return;
    if (ctx != NULL)
    {
        g_free(ctx);
        ctx = NULL;
    }

    ctx = g_new0(X11HotkeyContext, 1);
    ctx->xdisplay = XOpenDisplay(NULL);
    if (ctx->xdisplay == NULL)
    {
        g_warning("x11-hotkey: cannot open private X display; global hotkeys disabled");
        g_free(ctx);
        ctx = NULL;
        return;
    }

    ctx->super_mask = x11_modifier_mask_for(ctx->xdisplay, XK_Super_L);
    if (ctx->super_mask == 0)
        ctx->super_mask = x11_modifier_mask_for(ctx->xdisplay, XK_Super_R);
    ctx->hyper_mask = x11_modifier_mask_for(ctx->xdisplay, XK_Hyper_L);
    if (ctx->hyper_mask == 0)
        ctx->hyper_mask = x11_modifier_mask_for(ctx->xdisplay, XK_Hyper_R);
    ctx->meta_mask = x11_modifier_mask_for(ctx->xdisplay, XK_Meta_L);
    if (ctx->meta_mask == 0)
        ctx->meta_mask = x11_modifier_mask_for(ctx->xdisplay, XK_Meta_R);
    ctx->alt_mask = x11_modifier_mask_for(ctx->xdisplay, XK_Alt_L);
    if (ctx->alt_mask == 0)
        ctx->alt_mask = x11_modifier_mask_for(ctx->xdisplay, XK_Alt_R);

    ctx->by_keycode = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, NULL);

    /* GSource pumping the private connection */
    src = (X11EventSource *) g_source_new(&x11_event_source_funcs, sizeof(X11EventSource));
    src->xdisplay = ctx->xdisplay;
    src->poll_fd.fd = ConnectionNumber(ctx->xdisplay);
    src->poll_fd.events = G_IO_IN;
    g_source_add_poll((GSource *) src, &src->poll_fd);
    g_source_attach((GSource *) src, NULL);
    ctx->source = (GSource *) src;
}

/* Translate GDK modifiers to an X modifier mask */
static guint xmods_from_gdk(guint mods)
{
    guint m = 0;
    if (mods & GDK_SHIFT_MASK)
        m |= ShiftMask;
    if (mods & GDK_LOCK_MASK)
        m |= LockMask;
    if (mods & GDK_CONTROL_MASK)
        m |= ControlMask;
    if (mods & GDK_ALT_MASK)
        m |= (ctx->alt_mask != 0) ? ctx->alt_mask : Mod1Mask;
    if (mods & GDK_SUPER_MASK)
        m |= (ctx->super_mask != 0) ? ctx->super_mask : Mod4Mask;
    if (mods & GDK_HYPER_MASK)
        m |= ctx->hyper_mask;
    if (mods & GDK_META_MASK)
        m |= (ctx->meta_mask != 0) ? ctx->meta_mask : Mod1Mask;
    return m;
}

static guint gdk_mods_from_xstate(guint state)
{
    guint m = 0;
    if (state & ShiftMask)
        m |= GDK_SHIFT_MASK;
    if (state & ControlMask)
        m |= GDK_CONTROL_MASK;
    if ((state & Mod1Mask) || (ctx->alt_mask != 0 && (state & ctx->alt_mask)))
        m |= GDK_ALT_MASK;
    if (ctx->super_mask != 0 && (state & ctx->super_mask))
        m |= GDK_SUPER_MASK;
    if (ctx->hyper_mask != 0 && (state & ctx->hyper_mask))
        m |= GDK_HYPER_MASK;
    if (ctx->meta_mask != 0 && (state & ctx->meta_mask))
        m |= GDK_META_MASK;
    return m;
}

/* Xlib error handlers have no closure; park state in statics. */
static gboolean grab_error = FALSE;
static Display *grab_error_display = NULL;

static int x11_grab_error_handler(Display *dpy, XErrorEvent *ev)
{
    if (dpy == grab_error_display && ev->error_code == BadAccess)
        grab_error = TRUE;
    return 0;
}

gboolean x11_hotkey_bind(const char *keystring,
                         void (*handler)(const char *keystring, gpointer user_data),
                         gpointer user_data,
                         X11Hotkey **hotkey_out)
{
    X11Hotkey *hk;
    GList *list;
    guint keyval, mods, keycode, xmods;
    guint i;
    static const guint locks[] = { 0, LockMask, Mod2Mask, LockMask | Mod2Mask,
                                   Mod5Mask, Mod5Mask | LockMask,
                                   Mod5Mask | Mod2Mask, Mod5Mask | Mod2Mask | LockMask };

    if (keystring == NULL || handler == NULL)
        return FALSE;
    x11_context_init();
    if (ctx == NULL || ctx->xdisplay == NULL)
        return FALSE;

    if (!gtk_accelerator_parse(keystring, &keyval, (GdkModifierType *) &mods))
        return FALSE;
    if (keyval == 0)
        return FALSE;

    keycode = XKeysymToKeycode(ctx->xdisplay, keyval);
    if (keycode == 0)
        return FALSE;

    xmods = xmods_from_gdk(mods);

    grab_error = FALSE;
    grab_error_display = ctx->xdisplay;
    XErrorHandler old = XSetErrorHandler(x11_grab_error_handler);

    /* grab for all num/caps/scroll-lock combinations */
    for (i = 0; i < G_N_ELEMENTS(locks); i++)
        XGrabKey(ctx->xdisplay, keycode, xmods | locks[i],
                 DefaultRootWindow(ctx->xdisplay), True, GrabModeAsync, GrabModeAsync);

    XSync(ctx->xdisplay, False);
    XSetErrorHandler(old);
    grab_error_display = NULL;

    if (grab_error)
    {
        for (i = 0; i < G_N_ELEMENTS(locks); i++)
            XUngrabKey(ctx->xdisplay, keycode, xmods | locks[i],
                       DefaultRootWindow(ctx->xdisplay));
        XSync(ctx->xdisplay, False);
        return FALSE;
    }

    hk = g_new0(X11Hotkey, 1);
    hk->keystring = g_strdup(keystring);
    hk->keyval = keyval;
    hk->mods = mods;
    hk->keycode = keycode;
    hk->xmods = xmods;
    hk->handler = handler;
    hk->user_data = user_data;

    list = g_hash_table_lookup(ctx->by_keycode, GUINT_TO_POINTER(keycode));
    list = g_list_append(list, hk);
    g_hash_table_steal(ctx->by_keycode, GUINT_TO_POINTER(keycode));
    g_hash_table_insert(ctx->by_keycode, GUINT_TO_POINTER(keycode), list);

    if (hotkey_out != NULL)
        *hotkey_out = hk;
    return TRUE;
}

void x11_hotkey_unbind(X11Hotkey *hk)
{
    GList *list;
    guint i;
    static const guint locks[] = { 0, LockMask, Mod2Mask, LockMask | Mod2Mask,
                                   Mod5Mask, Mod5Mask | LockMask,
                                   Mod5Mask | Mod2Mask, Mod5Mask | Mod2Mask | LockMask };

    if (hk == NULL)
        return;
    if (ctx == NULL || ctx->xdisplay == NULL)
    {
        g_free(hk->keystring);
        g_free(hk);
        return;
    }

    for (i = 0; i < G_N_ELEMENTS(locks); i++)
        XUngrabKey(ctx->xdisplay, hk->keycode, hk->xmods | locks[i],
                   DefaultRootWindow(ctx->xdisplay));
    XSync(ctx->xdisplay, False);

    list = g_hash_table_lookup(ctx->by_keycode, GUINT_TO_POINTER(hk->keycode));
    if (list != NULL)
    {
        list = g_list_remove(list, hk);
        if (list == NULL)
            g_hash_table_remove(ctx->by_keycode, GUINT_TO_POINTER(hk->keycode));
        else
            g_hash_table_insert(ctx->by_keycode, GUINT_TO_POINTER(hk->keycode), list);
    }
    g_free(hk->keystring);
    g_free(hk);
}

static void x11_dispatch_key(XKeyEvent *ev)
{
    GList *l;
    guint mods;

    if (ev == NULL || ctx == NULL || ctx->xdisplay == NULL)
        return;
    mods = gdk_mods_from_xstate(ev->state);
    GList *list = g_hash_table_lookup(ctx->by_keycode, GUINT_TO_POINTER(ev->keycode));
    for (l = list; l != NULL; l = l->next)
    {
        X11Hotkey *hk = l->data;
        if (hk->mods == mods && hk->handler != NULL)
        {
            hk->handler(hk->keystring, hk->user_data);
            return;
        }
    }
}

static gboolean x11_event_source_check(GSource *source)
{
    X11EventSource *src = (X11EventSource *) source;
    if (src == NULL || src->xdisplay == NULL || !(src->poll_fd.revents & G_IO_IN))
        return FALSE;
    return XPending(src->xdisplay) > 0;
}static gboolean x11_event_source_dispatch(GSource *source, GSourceFunc callback, gpointer data)
{
    X11EventSource *src = (X11EventSource *) source;
    if (src == NULL || src->xdisplay == NULL)
        return G_SOURCE_REMOVE;
    while (XPending(src->xdisplay) > 0)
    {
        XEvent ev;
        XNextEvent(src->xdisplay, &ev);
        switch (ev.type)
        {
            case KeyPress:
                x11_dispatch_key(&ev.xkey);
                break;
            case MappingNotify:
                XRefreshKeyboardMapping(&ev.xmapping);
                break;
            default:
                break;
        }
    }
    return TRUE;
}
