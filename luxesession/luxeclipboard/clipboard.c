/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 Matthias Clasen
 * Copyright (C) 2007 Anders Carlsson
 * Copyright (C) 2007 Rodrigo Moya
 * Copyright (C) 2007 William Jon McCann <mccann@jhu.edu>
 * Copyright (C) 2011 Nick Schermer <nick@xfce.org>
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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef HAVE_STRING_H
#include <string.h>
#endif

#include <X11/Xlib.h>
#include <X11/Xatom.h>

#include "clipboard.h"

#define CLIPBOARD_MAX_PROPERTY_BYTES (16U * 1024U * 1024U)
#define CLIPBOARD_MAX_TARGETS 4096U
#define CLIPBOARD_MAX_RETAINED_BYTES (32U * 1024U * 1024U)
#define CLIPBOARD_MAX_OUTGOING 32U
#define CLIPBOARD_MAX_OUTGOING_PER_WINDOW 4U
#ifndef CLIPBOARD_RECEIVE_TIMEOUT_MS
#define CLIPBOARD_RECEIVE_TIMEOUT_MS 30000U
#endif
#ifndef CLIPBOARD_SAVE_TIMEOUT_MS
#define CLIPBOARD_SAVE_TIMEOUT_MS 120000U
#endif
#ifndef CLIPBOARD_SEND_TIMEOUT_MS
#define CLIPBOARD_SEND_TIMEOUT_MS 30000U
#endif

/* This single-threaded, headless service owns its X connection. Scoped
 * traps synchronize it and restore the previous process-wide handler. */
static Display *trap_display;
static int trap_error;
static int (*trap_previous_handler)(Display *, XErrorEvent *);
static int clipboard_trap_handler(Display *display, XErrorEvent *error)
{
        if (display == trap_display) {
                trap_error = error->error_code;
                return 0;
        }
        return trap_previous_handler ? trap_previous_handler(display, error) : 0;
}
static inline void
clipboard_error_trap_push (Display *display)
{
        g_assert (trap_display == NULL);
        XSync (display, False);
        trap_display = display;
        trap_error = Success;
        trap_previous_handler = XSetErrorHandler (clipboard_trap_handler);
}
static inline int
clipboard_error_trap_pop (void)
{
        g_assert (trap_display != NULL);
        XSync (trap_display, False);
        XSetErrorHandler (trap_previous_handler);
        trap_display = NULL;
        return trap_error;
}

struct _GsdClipboardManagerPrivate
{
        guint    start_idle_id;
        Display *display;
        Window   window;
        Window   save_window;
        Time     timestamp;

        GSList  *contents;
        GSList  *conversions;

        Window   requestor;
        Atom     property;
        Time     time;
        Atom     pending_target;
        guint    receive_timeout;
        guint    save_timeout;

        /* One owned connection delivers selection and foreign-window events. */
        GSource *xsource;
};

/* Event pump for the private connection: replaces the GDK filter the
 * GTK2/3 build used (gdk_window_add_filter is gone in GTK4). */
typedef struct
{
        GSource              parent;
        Display             *xdisplay;
        GsdClipboardManager *manager;
} ClipboardXSource;

static gboolean clipboard_xsource_prepare(GSource *source, gint *timeout);
static gboolean clipboard_xsource_check(GSource *source);
static gboolean clipboard_xsource_dispatch(GSource *source, GSourceFunc callback, gpointer user_data);

static GSourceFuncs clipboard_xsource_funcs =
{
        clipboard_xsource_prepare,
        clipboard_xsource_check,
        clipboard_xsource_dispatch,
        NULL
};

static Bool
clipboard_manager_process_event (GsdClipboardManager *manager,
                                 XEvent              *xev);

static gboolean
clipboard_xsource_prepare (GSource *source, gint *timeout_)
{
        ClipboardXSource *xs = (ClipboardXSource *) source;
        (void) timeout_;
        if (xs == NULL || xs->xdisplay == NULL)
                return FALSE;
        XFlush (xs->xdisplay);
        return XPending (xs->xdisplay) > 0;
}

static gboolean
clipboard_xsource_check (GSource *source)
{
        ClipboardXSource *xs = (ClipboardXSource *) source;
        return xs != NULL && xs->xdisplay != NULL && XPending (xs->xdisplay) > 0;
}

static gboolean
clipboard_xsource_dispatch (GSource *source, GSourceFunc callback, gpointer user_data)
{
        ClipboardXSource *xs = (ClipboardXSource *) source;
        XEvent xev;
        (void) callback;
        (void) user_data;
        if (xs == NULL || xs->xdisplay == NULL)
                return G_SOURCE_REMOVE;
        while (XPending (xs->xdisplay)) {
                XNextEvent (xs->xdisplay, &xev);
                clipboard_manager_process_event (xs->manager, &xev);
        }
        return G_SOURCE_CONTINUE;
}

/* Benign X error handler for watch (un)selects on the private
 * connection: a requestor may vanish mid-protocol (the old code had
 * no trap around these selects either, but an abort here would take
 * down the whole session daemon). */
static int
clipboard_x_ignore_errors (Display *display, XErrorEvent *error)
{
        (void) display;
        (void) error;
        return 0;
}

typedef struct
{
        guchar *data;
        gulong  length;
        gsize   capacity;
        Atom    target;
        Atom    type;
        gint    format;
        Atom    incr_type;
        gint    incr_format;
        gint    refcount;
} TargetData;

/* TargetData can outlive the current clipboard while an outgoing INCR
 * request still references it, so account until its final unref. */
static gsize retained_target_bytes;

static gboolean
target_reserve (TargetData *data, gsize required)
{
        if (required <= data->capacity) return TRUE;
        if (required > CLIPBOARD_MAX_PROPERTY_BYTES + 1 ||
            required - data->capacity > CLIPBOARD_MAX_RETAINED_BYTES - retained_target_bytes)
                return FALSE;
        gsize capacity = MAX ((gsize) 256, data->capacity);
        while (capacity < required)
                capacity = MIN (capacity * 2, (gsize) CLIPBOARD_MAX_PROPERTY_BYTES + 1);
        if (capacity - data->capacity > CLIPBOARD_MAX_RETAINED_BYTES - retained_target_bytes)
                capacity = required;
        guchar *larger = g_try_realloc (data->data, capacity);
        if (!larger) return FALSE;
        retained_target_bytes += capacity - data->capacity;
        data->data = larger;
        data->capacity = capacity;
        return TRUE;
}

typedef struct
{
        Atom        target;
        TargetData *data;
        Atom        property;
        Window      requestor;
        gint        offset;
        GsdClipboardManager *manager;
        guint       timeout_id;
} IncrConversion;

static void     gsd_clipboard_manager_finalize    (GObject                  *object);
static void     clipboard_manager_stop_watch      (GsdClipboardManager *manager);
static void     request_next_target               (GsdClipboardManager *manager);
static void     clipboard_manager_watch_cb        (GsdClipboardManager *manager,
                                                   Window               window,
                                                   Bool                 is_start,
                                                   long                 mask,
                                                   void                *cb_data);

static gulong SELECTION_MAX_SIZE = 0;

static Atom XA_ATOM_PAIR = None;
static Atom XA_CLIPBOARD_MANAGER = None;
static Atom XA_CLIPBOARD = None;
static Atom XA_DELETE = None;
static Atom XA_INCR = None;
static Atom XA_INSERT_PROPERTY = None;
static Atom XA_INSERT_SELECTION = None;
static Atom XA_MANAGER = None;
static Atom XA_MULTIPLE = None;
static Atom XA_NULL = None;
static Atom XA_SAVE_TARGETS = None;
static Atom XA_TARGETS = None;
static Atom XA_TIMESTAMP = None;

static GObject *clipboard_daemon = NULL;

G_DEFINE_TYPE (GsdClipboardManager, gsd_clipboard_manager, G_TYPE_OBJECT)

static Bool
xfce_xsettings_helper_timestamp_predicate (Display  *xdisplay,
                                           XEvent   *xevent,
                                           XPointer  arg)
{
    Window window = GPOINTER_TO_UINT (arg);

    return (xevent->type == PropertyNotify
            && xevent->xproperty.window == window
            && xevent->xproperty.atom == XInternAtom (xdisplay, "_TIMESTAMP_PROP", False));
}

Time
xfce_xsettings_get_server_time (Display *xdisplay,
                                Window   window)
{
    Atom   timestamp_atom;
    guchar c = 'a';
    XEvent xevent;

    if (xdisplay == NULL || window == None)
        return CurrentTime;

    /* get the current xserver timestamp */
    timestamp_atom = XInternAtom (xdisplay, "_TIMESTAMP_PROP", False);
    XChangeProperty (xdisplay, window, timestamp_atom, timestamp_atom,
                     8, PropModeReplace, &c, 1);
    if (!XIfEvent (xdisplay, &xevent, xfce_xsettings_helper_timestamp_predicate,
                   GUINT_TO_POINTER (window)))
        return CurrentTime;

    return xevent.xproperty.time;
}



static void
gsd_clipboard_manager_class_init (GsdClipboardManagerClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);

        object_class->finalize = gsd_clipboard_manager_finalize;

        g_type_class_add_private (klass, sizeof (GsdClipboardManagerPrivate));
}

static void
gsd_clipboard_manager_init (GsdClipboardManager *manager)
{
        manager->priv = G_TYPE_INSTANCE_GET_PRIVATE (manager,
                                                     GSD_TYPE_CLIPBOARD_MANAGER,
                                                     GsdClipboardManagerPrivate);

}

static void
gsd_clipboard_manager_finalize (GObject *object)
{
        GsdClipboardManager *clipboard_manager = GSD_CLIPBOARD_MANAGER (object);

        if (clipboard_manager->priv->start_idle_id !=0)
                g_source_remove (clipboard_manager->priv->start_idle_id);

        gsd_clipboard_manager_stop (clipboard_manager);

        G_OBJECT_CLASS (gsd_clipboard_manager_parent_class)->finalize (object);
}

/* We need to use reference counting for the target data, since we may
 * need to keep the data around after loosing the CLIPBOARD ownership
 * to complete incremental transfers.
 */
static TargetData *
target_data_ref (TargetData *data)
{
        if (data != NULL)
                g_atomic_int_inc (&data->refcount);
        return data;
}

static void
target_data_unref (TargetData *data)
{
        if (data != NULL && g_atomic_int_dec_and_test (&data->refcount)) {
                g_assert (retained_target_bytes >= data->capacity);
                retained_target_bytes -= data->capacity;
                g_free (data->data);
                g_slice_free (TargetData, data);
        }
}

static void
conversion_free (IncrConversion *rdata)
{
        if (rdata == NULL)
                return;
        if (rdata->timeout_id)
                g_source_remove (rdata->timeout_id);
        if (rdata->data)
                target_data_unref (rdata->data);
        g_slice_free (IncrConversion, rdata);
}

static void
finish_outgoing (GsdClipboardManager *manager, IncrConversion *data)
{
        Window requestor = data->requestor;
        manager->priv->conversions = g_slist_remove (manager->priv->conversions, data);
        conversion_free (data);
        clipboard_manager_watch_cb (manager, requestor,
                                    manager->priv->requestor == requestor,
                                    StructureNotifyMask, NULL);
}

static gboolean
outgoing_timed_out (gpointer user_data)
{
        IncrConversion *data = user_data;
        data->timeout_id = 0;
        finish_outgoing (data->manager, data);
        return G_SOURCE_REMOVE;
}

static void
refresh_outgoing_timeout (IncrConversion *data)
{
        if (data->timeout_id) g_source_remove (data->timeout_id);
        data->timeout_id = g_timeout_add (CLIPBOARD_SEND_TIMEOUT_MS, outgoing_timed_out, data);
}

static gboolean
outgoing_allowed (GsdClipboardManager *manager, Window requestor, Atom property)
{
        guint total = 0, per_window = 0;
        for (GSList *node = manager->priv->conversions; node; node = node->next) {
                IncrConversion *item = node->data;
                total++;
                if (item->requestor == requestor) {
                        per_window++;
                        if (item->property == property) return FALSE;
                }
        }
        return total < CLIPBOARD_MAX_OUTGOING && per_window < CLIPBOARD_MAX_OUTGOING_PER_WINDOW;
}

static void
send_selection_notify (GsdClipboardManager *manager,
                       Bool                 success)
{
        if (manager == NULL || manager->priv == NULL ||
            manager->priv->display == NULL)
                return;

        XSelectionEvent notify;

        notify.type = SelectionNotify;
        notify.serial = 0;
        notify.send_event = True;
        notify.display = manager->priv->display;
        notify.requestor = manager->priv->requestor;
        notify.selection = XA_CLIPBOARD_MANAGER;
        notify.target = XA_SAVE_TARGETS;
        notify.property = success ? manager->priv->property : None;
        notify.time = manager->priv->time;

        clipboard_error_trap_push (manager->priv->display);

        XSendEvent (manager->priv->display,
                    manager->priv->requestor,
                    False,
                    NoEventMask,
                    (XEvent *)&notify);
        XSync (manager->priv->display, False);

        clipboard_error_trap_pop ();
}

static void
finish_selection_request (GsdClipboardManager *manager,
                          XEvent              *xev,
                          Bool                 success)
{
        if (manager == NULL || manager->priv == NULL || xev == NULL ||
            manager->priv->display == NULL)
                return;

        XSelectionEvent notify;

        notify.type = SelectionNotify;
        notify.serial = 0;
        notify.send_event = True;
        notify.display = xev->xselectionrequest.display;
        notify.requestor = xev->xselectionrequest.requestor;
        notify.selection = xev->xselectionrequest.selection;
        notify.target = xev->xselectionrequest.target;
        notify.property = success ? xev->xselectionrequest.property : None;
        notify.time = xev->xselectionrequest.time;

        clipboard_error_trap_push (manager->priv->display);

        XSendEvent (xev->xselectionrequest.display,
                    xev->xselectionrequest.requestor,
                    False, NoEventMask, (XEvent *) &notify);
        XSync (manager->priv->display, False);

        clipboard_error_trap_pop ();
}

static int
clipboard_bytes_per_item (int format)
{
        switch (format) {
        case 8: return sizeof (char);
        case 16: return sizeof (short);
        case 32: return sizeof (long);
        default: ;
        }

        return 0;
}

static int find_content_target (TargetData *tdata, Atom *target);

static gboolean
create_save_window (GsdClipboardManager *manager)
{
        Display *display = manager->priv->display;
        clipboard_error_trap_push (display);
        Window window = XCreateSimpleWindow (display, DefaultRootWindow (display),
                                             0, 0, 1, 1, 0, 0, 0);
        XSelectInput (display, window, PropertyChangeMask);
        if (clipboard_error_trap_pop () != Success) return FALSE;
        manager->priv->save_window = window;
        return TRUE;
}

static void
destroy_save_window (GsdClipboardManager *manager)
{
        if (manager->priv->save_window != None && manager->priv->display != NULL) {
                XDestroyWindow (manager->priv->display, manager->priv->save_window);
                manager->priv->save_window = None;
        }
}

static void
clear_receive_timeout (GsdClipboardManager *manager)
{
        if (manager->priv->receive_timeout) {
                g_source_remove (manager->priv->receive_timeout);
                manager->priv->receive_timeout = 0;
        }
        manager->priv->pending_target = None;
}

static void
clear_save_timeout (GsdClipboardManager *manager)
{
        if (manager->priv->save_timeout) {
                g_source_remove (manager->priv->save_timeout);
                manager->priv->save_timeout = 0;
        }
}

static gboolean
save_timed_out (gpointer user_data)
{
        GsdClipboardManager *manager = user_data;
        manager->priv->save_timeout = 0;
        if (manager->priv->requestor == None) return G_SOURCE_REMOVE;
        if (manager->priv->pending_target != None)
                XDeleteProperty (manager->priv->display, manager->priv->save_window,
                                 manager->priv->pending_target);
        clear_receive_timeout (manager);
        g_slist_free_full (manager->priv->contents, (GDestroyNotify)target_data_unref);
        manager->priv->contents = NULL;
        send_selection_notify (manager, False);
        clipboard_manager_watch_cb (manager, manager->priv->requestor, False, 0, NULL);
        manager->priv->requestor = None;
        destroy_save_window (manager);
        return G_SOURCE_REMOVE;
}

static void
start_save_timeout (GsdClipboardManager *manager)
{
        clear_save_timeout (manager);
        manager->priv->save_timeout = g_timeout_add (CLIPBOARD_SAVE_TIMEOUT_MS,
                                                    save_timed_out, manager);
}

static gboolean
receive_timed_out (gpointer user_data)
{
        GsdClipboardManager *manager = user_data;
        Atom target = manager->priv->pending_target;
        manager->priv->receive_timeout = 0;
        manager->priv->pending_target = None;
        if (manager->priv->requestor == None || target == None) return G_SOURCE_REMOVE;
        XDeleteProperty (manager->priv->display, manager->priv->save_window, target);
        if (target == XA_TARGETS) {
                clear_save_timeout (manager);
                send_selection_notify (manager, False);
                clipboard_manager_watch_cb (manager, manager->priv->requestor, False, 0, NULL);
                manager->priv->requestor = None;
                destroy_save_window (manager);
        } else {
                GSList *item = g_slist_find_custom (manager->priv->contents, &target,
                                                   (GCompareFunc) find_content_target);
                if (item) {
                        TargetData *data = item->data;
                        manager->priv->contents = g_slist_delete_link (manager->priv->contents, item);
                        target_data_unref (data);
                }
                request_next_target (manager);
        }
        return G_SOURCE_REMOVE;
}

static void
set_receive_timeout (GsdClipboardManager *manager, Atom target)
{
        clear_receive_timeout (manager);
        manager->priv->pending_target = target;
        manager->priv->receive_timeout = g_timeout_add (CLIPBOARD_RECEIVE_TIMEOUT_MS,
                                                       receive_timed_out, manager);
}

static void
save_targets (GsdClipboardManager *manager,
              Atom                *targets,
              int                  nitems)
{
        gint        i;
        TargetData *tdata;
        g_autoptr(GHashTable) seen = g_hash_table_new (g_direct_hash, g_direct_equal);

        if (manager == NULL || manager->priv == NULL || (targets == NULL && nitems != 0) ||
            nitems < 0 || (gulong)nitems > CLIPBOARD_MAX_TARGETS)
                return;

        for (i = 0; i < nitems; i++) {
                if (targets[i] != None && g_hash_table_add (seen, GSIZE_TO_POINTER ((gsize)targets[i])) &&
                    targets[i] != XA_TARGETS &&
                    targets[i] != XA_MULTIPLE &&
                    targets[i] != XA_DELETE &&
                    targets[i] != XA_INSERT_PROPERTY &&
                    targets[i] != XA_INSERT_SELECTION &&
                    targets[i] != XA_PIXMAP) {
                        tdata = g_slice_new0 (TargetData);
                        tdata->data = NULL;
                        tdata->length = 0;
                        tdata->target = targets[i];
                        tdata->type = None;
                        tdata->format = 0;
                        tdata->refcount = 1;
                        manager->priv->contents = g_slist_prepend (manager->priv->contents, tdata);

                }
        }

        if (targets != NULL) XFree (targets);

        request_next_target (manager);
}

/* Serialize source conversions: parallel large text encodings can block
 * GTK's producer streams on the main context and retain multiple buffers.
 * MULTIPLE is still supported for clients reading the persisted selection. */
static void
request_next_target (GsdClipboardManager *manager)
{
        clear_receive_timeout (manager);
        for (GSList *list = manager->priv->contents; list; list = list->next) {
                TargetData *data = list->data;
                if (data->type == None) {
                        set_receive_timeout (manager, data->target);
                        XConvertSelection (manager->priv->display, XA_CLIPBOARD,
                                           data->target, data->target,
                                           manager->priv->save_window, manager->priv->time);
                        return;
                }
        }
        if (manager->priv->contents != NULL)
                XSetSelectionOwner (manager->priv->display, XA_CLIPBOARD,
                                    manager->priv->window, manager->priv->time);
        clear_save_timeout (manager);
        send_selection_notify (manager, manager->priv->contents != NULL);
        clipboard_manager_watch_cb (manager, manager->priv->requestor, False, 0, NULL);
        manager->priv->requestor = None;
        destroy_save_window (manager);
}

static int
find_content_target (TargetData *tdata,
                     Atom       *target)
{
        return !(tdata->target == *target);
}

static int
find_conversion_requestor (IncrConversion *rdata,
                           XEvent         *xev)
{
        return !(rdata->requestor == xev->xproperty.window
                 && rdata->property == xev->xproperty.atom);
}

static void
get_property (TargetData          *tdata,
              GsdClipboardManager *manager)
{
        Atom    type = None;
        gint    format = 0;
        gulong  length = 0;
        gulong  remaining = 0;
        guchar *data = NULL;
        gulong  bytes;
        int     bytes_per_item;
        int     status;

        if (tdata == NULL || manager == NULL || manager->priv == NULL ||
            manager->priv->display == NULL)
                return;
        status = XGetWindowProperty (manager->priv->display,
                                     manager->priv->save_window,
                                     tdata->target,
                                     0,
                                     CLIPBOARD_MAX_PROPERTY_BYTES / 4,
                                     True,
                                     AnyPropertyType,
                                     &type,
                                     &format,
                                     &length,
                                     &remaining,
                                     &data);
        if (status != Success || type == None || remaining != 0) {
                if (data != NULL)
                        XFree (data);
                manager->priv->contents = g_slist_remove (manager->priv->contents, tdata);
                target_data_unref (tdata);
                return;
        }
        if (type == XA_INCR) {
                if (format != 32 || length != 1 || data == NULL ||
                    *(const unsigned long *)data > CLIPBOARD_MAX_PROPERTY_BYTES) {
                        if (data != NULL) XFree (data);
                        manager->priv->contents = g_slist_remove (manager->priv->contents, tdata);
                        target_data_unref (tdata);
                        return;
                }
                tdata->type = type;
                tdata->length = 0;
                XFree (data);
                return;
        }
        bytes_per_item = clipboard_bytes_per_item (format);
        if (bytes_per_item == 0 || length > G_MAXSIZE / bytes_per_item ||
            (length * bytes_per_item) > CLIPBOARD_MAX_PROPERTY_BYTES) {
                if (data != NULL)
                        XFree (data);
                manager->priv->contents = g_slist_remove (manager->priv->contents, tdata);
                target_data_unref (tdata);
                return;
        }
        bytes = length * bytes_per_item;
        if (!target_reserve (tdata, bytes + 1)) {
                if (data != NULL) XFree (data);
                manager->priv->contents = g_slist_remove (manager->priv->contents, tdata);
                target_data_unref (tdata);
                return;
        }
        if (bytes > 0 && data != NULL)
                memcpy (tdata->data, data, bytes);
        tdata->data[bytes] = '\0';
        XFree (data);
        tdata->type = type;
        tdata->length = bytes;
        tdata->format = format;
}

static Bool
receive_incrementally (GsdClipboardManager *manager,
                       XEvent              *xev)
{
        GSList     *list;
        TargetData *tdata;
        Atom        type = None;
        gint        format = 0;
        gulong      nitems, remaining;
        guchar     *data = NULL;
        gulong      bytes_per_item;
        gulong      bytes;
        int         status;

        if (manager == NULL || manager->priv == NULL || xev == NULL ||
            xev->xproperty.window != manager->priv->save_window ||
            xev->xproperty.atom != manager->priv->pending_target)
                return False;

        list = g_slist_find_custom (manager->priv->contents,
                                    &xev->xproperty.atom,
                                    (GCompareFunc) find_content_target);
        if (!list)
                return False;
        tdata = (TargetData *) list->data;
        if (tdata == NULL || tdata->type != XA_INCR)
                return False;

        status = XGetWindowProperty (xev->xproperty.display,
                                     xev->xproperty.window,
                                     xev->xproperty.atom,
                                     0, CLIPBOARD_MAX_PROPERTY_BYTES / 4, True, AnyPropertyType,
                                     &type, &format, &nitems, &remaining, &data);
        if (status != Success || remaining != 0 || (type == None && nitems != 0)) {
                if (data != NULL)
                        XFree (data);
                goto failed;
        }
        if (nitems == 0) {
                if (type == None || clipboard_bytes_per_item (format) == 0 ||
                    (tdata->incr_type != None &&
                     (type != tdata->incr_type || format != tdata->incr_format))) {
                        if (data != NULL) XFree (data);
                        goto failed;
                }
                tdata->type = type;
                tdata->format = format;
                request_next_target (manager);
                XFree (data);
                return True;
        }
        bytes_per_item = clipboard_bytes_per_item (format);
        if (bytes_per_item == 0 || nitems > G_MAXSIZE / bytes_per_item ||
            (tdata->incr_type != None &&
             (type != tdata->incr_type || format != tdata->incr_format)) ||
            (bytes = nitems * bytes_per_item) > CLIPBOARD_MAX_PROPERTY_BYTES ||
            tdata->length > CLIPBOARD_MAX_PROPERTY_BYTES - bytes) {
                XFree (data);
                goto failed;
        }
        if (tdata->incr_type == None) {
                tdata->incr_type = type;
                tdata->incr_format = format;
        }
        if (!target_reserve (tdata, tdata->length + bytes + 1)) {
                XFree (data);
                goto failed;
        }
        memcpy (tdata->data + tdata->length, data, bytes);
        tdata->data[tdata->length + bytes] = '\0';
        tdata->length += bytes;
        set_receive_timeout (manager, tdata->target);
        XFree (data);

        return True;

failed:
        XDeleteProperty (manager->priv->display, manager->priv->save_window, tdata->target);
        manager->priv->contents = g_slist_delete_link (manager->priv->contents, list);
        target_data_unref (tdata);
        request_next_target (manager);
        return True;
}

static Bool
send_incrementally (GsdClipboardManager *manager,
                    XEvent              *xev)
{
        GSList         *list;
        IncrConversion *rdata;
        gulong          length;
        gulong          items;
        guchar         *data;

        if (manager == NULL || manager->priv == NULL ||
            manager->priv->display == NULL || xev == NULL)
                return False;
        list = g_slist_find_custom (manager->priv->conversions, xev,
                                    (GCompareFunc) find_conversion_requestor);
        if (list == NULL)
                return False;

        rdata = (IncrConversion *) list->data;
        if (rdata == NULL || rdata->data == NULL || rdata->offset < 0 ||
            (gulong)rdata->offset > rdata->data->length ||
            clipboard_bytes_per_item (rdata->data->format) == 0)
                return False;

        data = rdata->data->length > (gulong)rdata->offset ?
               rdata->data->data + rdata->offset : NULL;
        length = rdata->data->length - (gulong)rdata->offset;
        if (SELECTION_MAX_SIZE > 0 && length > SELECTION_MAX_SIZE)
                length = SELECTION_MAX_SIZE;
        if (length > 0) {
                length -= length % clipboard_bytes_per_item (rdata->data->format);
                if (length == 0) {
                        finish_outgoing (manager, rdata);
                        return True;
                }
        }

        rdata->offset += (gint)length;
        items = length / clipboard_bytes_per_item (rdata->data->format);
        clipboard_error_trap_push (manager->priv->display);
        XChangeProperty (manager->priv->display, rdata->requestor,
                         rdata->property, rdata->data->type,
                         rdata->data->format, PropModeAppend,
                         data, items);
        if (clipboard_error_trap_pop () != Success) {
                finish_outgoing (manager, rdata);
                return True;
        }

        if (length == 0) {
                finish_outgoing (manager, rdata);
        } else {
                refresh_outgoing_timeout (rdata);
        }

        return True;
}

static void
convert_clipboard_manager (GsdClipboardManager *manager,
                           XEvent              *xev)
{
        Atom    type = None;
        gint    format;
        gulong  nitems;
        gulong  remaining;
        Atom   *targets = NULL;
        Atom    targets2[3];
        gint    n_targets;

        if (xev->xselectionrequest.target == XA_SAVE_TARGETS) {
                if (manager->priv->requestor != None || manager->priv->contents != NULL) {
                        /* We're in the middle of a conversion request, or own
                         * the CLIPBOARD already
                         */
                        finish_selection_request (manager, xev, False);
                } else {
                        XWindowAttributes attributes;
                        clipboard_error_trap_push (manager->priv->display);
                        if (!XGetWindowAttributes (manager->priv->display,
                                                   xev->xselectionrequest.requestor, &attributes)) {
                                clipboard_error_trap_pop ();
                                return;
                        }
                        XSelectInput (manager->priv->display,
                                      xev->xselectionrequest.requestor,
                                      attributes.your_event_mask | StructureNotifyMask);
                        XSync (manager->priv->display, False);

                        if (clipboard_error_trap_pop () != Success)
                                return;

                        if (xev->xselectionrequest.property != None) {
                                clipboard_error_trap_push (manager->priv->display);
                                XGetWindowProperty (manager->priv->display,
                                                    xev->xselectionrequest.requestor,
                                                    xev->xselectionrequest.property,
                                                    0, CLIPBOARD_MAX_TARGETS, False, XA_ATOM,
                                                    &type, &format, &nitems, &remaining,
                                                    (guchar **) &targets);

                                if (clipboard_error_trap_pop () != Success ||
                                    type != XA_ATOM || format != 32 ||
                                    remaining != 0 || nitems > CLIPBOARD_MAX_TARGETS) {
                                        if (targets)
                                                XFree (targets);
                                        finish_selection_request (manager, xev, False);
                                        return;
                                }
                        }

                        manager->priv->requestor = xev->xselectionrequest.requestor;
                        manager->priv->property = xev->xselectionrequest.property;
                        manager->priv->time = xev->xselectionrequest.time;
                        if (!create_save_window (manager)) {
                                if (targets) XFree (targets);
                                send_selection_notify (manager, False);
                                clipboard_manager_watch_cb (manager, manager->priv->requestor, False, 0, NULL);
                                manager->priv->requestor = None;
                                return;
                        }
                        start_save_timeout (manager);

                        if (type == None) {
                                set_receive_timeout (manager, XA_TARGETS);
                                XConvertSelection (manager->priv->display, XA_CLIPBOARD,
                                                   XA_TARGETS, XA_TARGETS,
                                                   manager->priv->save_window, manager->priv->time);
                        } else
                                save_targets (manager, targets, nitems);
                }
        } else if (xev->xselectionrequest.target == XA_TIMESTAMP) {
                XChangeProperty (manager->priv->display,
                                 xev->xselectionrequest.requestor,
                                 xev->xselectionrequest.property,
                                 XA_INTEGER, 32, PropModeReplace,
                                 (guchar *) &manager->priv->timestamp, 1);

                finish_selection_request (manager, xev, True);
        } else if (xev->xselectionrequest.target == XA_TARGETS) {
                n_targets = 0;
                targets2[n_targets++] = XA_TARGETS;
                targets2[n_targets++] = XA_TIMESTAMP;
                targets2[n_targets++] = XA_SAVE_TARGETS;

                XChangeProperty (manager->priv->display,
                                 xev->xselectionrequest.requestor,
                                 xev->xselectionrequest.property,
                                 XA_ATOM, 32, PropModeReplace,
                                 (guchar *) targets2, n_targets);

                finish_selection_request (manager, xev, True);
        } else
                finish_selection_request (manager, xev, False);
}

static void
convert_clipboard_target (IncrConversion      *rdata,
                          GsdClipboardManager *manager)
{
        TargetData        *tdata;
        Atom              *targets;
        gint               n_targets;
        GSList            *list;
        gulong             items;
        XWindowAttributes  atts;

        if (rdata == NULL || manager == NULL || manager->priv == NULL ||
            manager->priv->display == NULL)
                return;

        if (rdata->target == XA_TARGETS) {
                n_targets = g_slist_length (manager->priv->contents) + 2;
                targets = g_new (Atom, n_targets);

                n_targets = 0;
                targets[n_targets++] = XA_TARGETS;
                targets[n_targets++] = XA_MULTIPLE;

                for (list = manager->priv->contents; list; list = list->next) {
                        tdata = (TargetData *) list->data;
                        targets[n_targets++] = tdata->target;
                }

                XChangeProperty (manager->priv->display, rdata->requestor,
                                 rdata->property,
                                 XA_ATOM, 32, PropModeReplace,
                                 (guchar *) targets, n_targets);
                g_free (targets);
        } else  {
                /* Convert from stored CLIPBOARD data */
                list = g_slist_find_custom (manager->priv->contents,
                                            &rdata->target,
                                            (GCompareFunc) find_content_target);

                /* We got a target that we don't support */
                if (!list) {
                        rdata->property = None;
                        return;
                }

                tdata = (TargetData *)list->data;
                if (tdata->type == XA_INCR) {
                        /* we haven't completely received this target yet  */
                        rdata->property = None;
                        return;
                }

                rdata->data = target_data_ref (tdata);
                int item_size = clipboard_bytes_per_item (tdata->format);
                if (item_size == 0 || tdata->length % item_size != 0) {
                        rdata->property = None;
                        return;
                }
                items = tdata->length / item_size;
                if (tdata->length <= SELECTION_MAX_SIZE)
                        XChangeProperty (manager->priv->display, rdata->requestor,
                                         rdata->property,
                                         tdata->type, tdata->format, PropModeReplace,
                                         tdata->data, items);
                else {
                        /* start incremental transfer */
                        if (!outgoing_allowed (manager, rdata->requestor, rdata->property)) {
                                rdata->property = None;
                                return;
                        }
                        rdata->offset = 0;

                        clipboard_error_trap_push (manager->priv->display);

                        if (XGetWindowAttributes (manager->priv->display,
                                                  rdata->requestor, &atts) == 0)
                        {
                                rdata->property = None;
                                rdata->offset = -1;
                                clipboard_error_trap_pop ();
                                return;
                        }
                        XSelectInput (manager->priv->display, rdata->requestor,
                                      atts.your_event_mask | PropertyChangeMask | StructureNotifyMask);

                        XChangeProperty (manager->priv->display, rdata->requestor,
                                         rdata->property,
                                         XA_INCR, 32, PropModeReplace,
                                         (guchar *) &items, 1);

                        XSync (manager->priv->display, False);

                        if (clipboard_error_trap_pop () != Success) {
                                rdata->property = None;
                                rdata->offset = -1;
                                return;
                        }
                        rdata->manager = manager;
                        manager->priv->conversions = g_slist_prepend (manager->priv->conversions, rdata);
                        refresh_outgoing_timeout (rdata);
                }
        }
}

static void
collect_incremental (IncrConversion      *rdata,
                     GsdClipboardManager *manager)
{
        (void) manager;
        if (rdata->offset < 0)
                conversion_free (rdata);
}

static void
convert_clipboard (GsdClipboardManager *manager,
                   XEvent              *xev)
{
        GSList         *list;
        GSList         *conversions = NULL;
        IncrConversion *rdata;
        Atom            type = None;
        gint            format;
        gulong          i, nitems;
        gulong          remaining;
        Atom           *multiple;

        if (xev->xselectionrequest.target == XA_MULTIPLE) {
                XGetWindowProperty (xev->xselectionrequest.display,
                                    xev->xselectionrequest.requestor,
                                    xev->xselectionrequest.property,
                                    0, CLIPBOARD_MAX_TARGETS * 2, False, XA_ATOM_PAIR,
                                    &type, &format, &nitems, &remaining,
                                    (guchar **) &multiple);

                if (type != XA_ATOM_PAIR || format != 32 || nitems == 0 ||
                    (nitems % 2) != 0 || remaining != 0 || nitems > CLIPBOARD_MAX_TARGETS * 2) {
                        if (multiple)
                                XFree (multiple);
                        finish_selection_request (manager, xev, False);
                        return;
                }

                for (i = 0; i < nitems; i += 2) {
                        rdata = g_slice_new0 (IncrConversion);
                        rdata->requestor = xev->xselectionrequest.requestor;
                        rdata->target = multiple[i];
                        rdata->property = multiple[i+1];
                        rdata->data = NULL;
                        rdata->offset = -1;
                        conversions = g_slist_prepend (conversions, rdata);
                }
        } else {
                multiple = NULL;

                rdata = g_slice_new0 (IncrConversion);
                rdata->requestor = xev->xselectionrequest.requestor;
                rdata->target = xev->xselectionrequest.target;
                rdata->property = xev->xselectionrequest.property;
                rdata->data = NULL;
                rdata->offset = -1;
                conversions = g_slist_prepend (conversions, rdata);
        }

        /* MULTIPLE's returned pairs must retain the request's order. */
        conversions = g_slist_reverse (conversions);
        g_slist_foreach (conversions, (GFunc) convert_clipboard_target, manager);

        if (conversions->next == NULL &&
            ((IncrConversion *) conversions->data)->property == None) {
                finish_selection_request (manager, xev, False);
        } else {
                if (multiple) {
                        i = 0;
                        for (list = conversions; list; list = list->next) {
                                rdata = (IncrConversion *)list->data;
                                multiple[i++] = rdata->target;
                                multiple[i++] = rdata->property;
                        }
                        XChangeProperty (xev->xselectionrequest.display,
                                         xev->xselectionrequest.requestor,
                                         xev->xselectionrequest.property,
                                         XA_ATOM_PAIR, 32, PropModeReplace,
                                         (guchar *) multiple, nitems);
                }
                finish_selection_request (manager, xev, True);
        }

        g_slist_foreach (conversions, (GFunc) collect_incremental, manager);
        g_slist_free (conversions);

        if (multiple != NULL)
                XFree (multiple);
}

static Bool
clipboard_manager_process_event (GsdClipboardManager *manager,
                                 XEvent              *xev)
{
        Atom    type;
        gint    format;
        gulong  nitems;
        gulong  remaining;
        Atom   *targets = NULL;

        if (manager == NULL || manager->priv == NULL || xev == NULL)
                return False;
        switch (xev->xany.type) {
        case DestroyNotify:
                for (GSList *item = manager->priv->conversions, *next; item; item = next) {
                        next = item->next;
                        IncrConversion *data = item->data;
                        if (data->requestor == xev->xdestroywindow.window)
                                finish_outgoing (manager, data);
                }
                if (xev->xdestroywindow.window == manager->priv->requestor) {
                        clear_receive_timeout (manager);
                        clear_save_timeout (manager);
                        g_slist_foreach (manager->priv->contents, (GFunc) target_data_unref, NULL);
                        g_slist_free (manager->priv->contents);
                        manager->priv->contents = NULL;

                        clipboard_manager_watch_cb (manager,
                                                    manager->priv->requestor,
                                                    False,
                                                    0,
                                                    NULL);
                        manager->priv->requestor = None;
                        destroy_save_window (manager);
                }
                break;

        case PropertyNotify:
                if (xev->xproperty.state == PropertyNewValue) {
                        return receive_incrementally (manager, xev);
                } else {
                        return send_incrementally (manager, xev);
                }
                break;

        case SelectionClear:
                if (xev->xany.window != manager->priv->window)
                        return False;

                if (xev->xselectionclear.selection == XA_CLIPBOARD_MANAGER) {
                        clear_receive_timeout (manager);
                        clear_save_timeout (manager);
                        if (manager->priv->requestor != None) {
                                send_selection_notify (manager, False);
                                clipboard_manager_watch_cb (manager, manager->priv->requestor, False, 0, NULL);
                                manager->priv->requestor = None;
                        }
                        destroy_save_window (manager);
                        /* We lost the manager selection */
                        if (manager->priv->contents) {
                                g_slist_foreach (manager->priv->contents, (GFunc) target_data_unref, NULL);
                                g_slist_free (manager->priv->contents);
                                manager->priv->contents = NULL;

                                XSetSelectionOwner (manager->priv->display,
                                                    XA_CLIPBOARD,
                                                    None, manager->priv->time);
                        }

                        return True;
                }
                if (xev->xselectionclear.selection == XA_CLIPBOARD) {
                        clear_receive_timeout (manager);
                        clear_save_timeout (manager);
                        if (manager->priv->requestor != None)
                                send_selection_notify (manager, False);
                        /* We lost the clipboard selection */
                        g_slist_foreach (manager->priv->contents, (GFunc) target_data_unref, NULL);
                        g_slist_free (manager->priv->contents);
                        manager->priv->contents = NULL;
                        clipboard_manager_watch_cb (manager,
                                                    manager->priv->requestor,
                                                    False,
                                                    0,
                                                    NULL);
                        manager->priv->requestor = None;
                        destroy_save_window (manager);

                        return True;
                }
                break;

        case SelectionNotify:
                if (xev->xselection.requestor != manager->priv->save_window ||
                    manager->priv->save_window == None)
                        return False;

                if (xev->xselection.selection == XA_CLIPBOARD) {
                        if (xev->xselection.target != manager->priv->pending_target)
                                return True;
                        /* a CLIPBOARD conversion is done */
                        if (xev->xselection.property == XA_TARGETS) {
                                XGetWindowProperty (xev->xselection.display,
                                                    xev->xselection.requestor,
                                                    xev->xselection.property,
                                                    0, CLIPBOARD_MAX_TARGETS, True, XA_ATOM,
                                                    &type, &format, &nitems, &remaining,
                                                    (guchar **) &targets);

                                if (type != XA_ATOM || format != 32 || remaining != 0 ||
                                    nitems > CLIPBOARD_MAX_TARGETS) {
                                        clear_receive_timeout (manager);
                                        clear_save_timeout (manager);
                                        if (targets) XFree (targets);
                                        send_selection_notify (manager, False);
                                        clipboard_manager_watch_cb (manager, manager->priv->requestor, False, 0, NULL);
                                        manager->priv->requestor = None;
                                        destroy_save_window (manager);
                                } else save_targets (manager, targets, nitems);
                        } else {
                                GSList *item = g_slist_find_custom (manager->priv->contents,
                                    &xev->xselection.target, (GCompareFunc) find_content_target);
                                if (item != NULL) {
                                        TargetData *data = item->data;
                                        if (xev->xselection.property == None) {
                                                manager->priv->contents = g_slist_delete_link (manager->priv->contents, item);
                                                target_data_unref (data);
                                        } else {
                                                get_property (data, manager);
                                                item = g_slist_find_custom (manager->priv->contents,
                                                    &xev->xselection.target, (GCompareFunc) find_content_target);
                                                if (item && ((TargetData *)item->data)->type == XA_INCR) {
                                                        set_receive_timeout (manager, xev->xselection.target);
                                                        return True;
                                                }
                                        }
                                }
                                request_next_target (manager);
                        }

                        return True;
                }
                break;

        case SelectionRequest:
                if (xev->xany.window != manager->priv->window) {
                        return False;
                }

                if (xev->xselectionrequest.selection == XA_CLIPBOARD_MANAGER) {
                        convert_clipboard_manager (manager, xev);
                        return True;
                } else if (xev->xselectionrequest.selection == XA_CLIPBOARD) {
                        convert_clipboard (manager, xev);
                        return True;
                }
                break;

        default:;
        }

        return False;
}

static void
clipboard_manager_watch_cb (GsdClipboardManager *manager,
                            Window               window,
                            Bool                 is_start,
                            long                 mask,
                            void                *cb_data)
{
        Display *xdisplay;
    if (manager == NULL || manager->priv == NULL)
        return;
    xdisplay = manager->priv->display;
        int (*old_handler)(Display *, XErrorEvent *);

        (void) cb_data;

        /* GTK4 removed gdk_window_add_filter, so watches are plain
         * XSelectInputs on the private connection (see pump above).
         * Swallow errors: the window may be gone already. */
        if (xdisplay == NULL)
                return;
        if (!is_start) mask = 0;
        for (GSList *item = manager->priv->conversions; item; item = item->next) {
                IncrConversion *data = item->data;
                if (data->requestor == window) {
                        mask |= PropertyChangeMask | StructureNotifyMask;
                        break;
                }
        }
        old_handler = XSetErrorHandler (clipboard_x_ignore_errors);
        XSelectInput (xdisplay, window, mask);
        XSync (xdisplay, False);
        XSetErrorHandler (old_handler);
}

static void
init_atoms (Display *display)
{
    gulong max_request_size;

    if (display == NULL)
                return;
    if (SELECTION_MAX_SIZE > 0)
      return;

    XA_ATOM_PAIR = XInternAtom (display, "ATOM_PAIR", False);
    XA_CLIPBOARD_MANAGER = XInternAtom (display, "CLIPBOARD_MANAGER", False);
    XA_CLIPBOARD = XInternAtom (display, "CLIPBOARD", False);
    XA_DELETE = XInternAtom (display, "DELETE", False);
    XA_INCR = XInternAtom (display, "INCR", False);
    XA_INSERT_PROPERTY = XInternAtom (display, "INSERT_PROPERTY", False);
    XA_INSERT_SELECTION = XInternAtom (display, "INSERT_SELECTION", False);
    XA_MANAGER = XInternAtom (display, "MANAGER", False);
    XA_MULTIPLE = XInternAtom (display, "MULTIPLE", False);
    XA_NULL = XInternAtom (display, "NULL", False);
    XA_SAVE_TARGETS = XInternAtom (display, "SAVE_TARGETS", False);
    XA_TARGETS = XInternAtom (display, "TARGETS", False);
    XA_TIMESTAMP = XInternAtom (display, "TIMESTAMP", False);

    max_request_size = XExtendedMaxRequestSize (display);
    if (max_request_size == 0)
      max_request_size = XMaxRequestSize (display);

    if (max_request_size < 100)
            SELECTION_MAX_SIZE = 4096;
    else
            SELECTION_MAX_SIZE = max_request_size - 100;
    if (SELECTION_MAX_SIZE > 262144)
      SELECTION_MAX_SIZE =  262144;
}

gboolean
gsd_clipboard_manager_start (GsdClipboardManager *manager,
                             gboolean             replace)
{
        XClientMessageEvent xev;

        if (manager == NULL || manager->priv == NULL)
                return FALSE;
        if (manager->priv->display == NULL)
                manager->priv->display = XOpenDisplay (NULL);
        if (manager->priv->display == NULL)
                return FALSE;
        init_atoms (manager->priv->display);

        /* check if there is a clipboard manager running */
        if (!replace
            && XGetSelectionOwner (manager->priv->display, XA_CLIPBOARD_MANAGER)) {
                return FALSE;
        }

        manager->priv->contents = NULL;
        manager->priv->conversions = NULL;
        manager->priv->requestor = None;

        /* Pump the same connection that owns CLIPBOARD_MANAGER. Selection
         * events go to the owner, not to a second connection watching it. */
        {
                GSource *src = g_source_new (&clipboard_xsource_funcs,
                                             sizeof (ClipboardXSource));
                ClipboardXSource *xs = (ClipboardXSource *) src;
                xs->xdisplay = manager->priv->display;
                xs->manager = manager;
                g_source_add_unix_fd (src, ConnectionNumber (manager->priv->display), G_IO_IN);
                g_source_attach (src, NULL);
                manager->priv->xsource = src;
        }

        manager->priv->window = XCreateSimpleWindow (manager->priv->display,
                                                     DefaultRootWindow (manager->priv->display),
                                                     0, 0, 10, 10, 0,
                                                     WhitePixel (manager->priv->display,
                                                                 DefaultScreen (manager->priv->display)),
                                                     WhitePixel (manager->priv->display,
                                                                 DefaultScreen (manager->priv->display)));
        clipboard_manager_watch_cb (manager,
                                    manager->priv->window,
                                    True,
                                    PropertyChangeMask,
                                    NULL);
        XSelectInput (manager->priv->display,
                      manager->priv->window,
                      PropertyChangeMask);
        manager->priv->timestamp = xfce_xsettings_get_server_time (manager->priv->display, manager->priv->window);

        XSetSelectionOwner (manager->priv->display,
                            XA_CLIPBOARD_MANAGER,
                            manager->priv->window,
                            manager->priv->timestamp);

        /* Check to see if we managed to claim the selection. If not,
         * we treat it as if we got it then immediately lost it
         */
        if (XGetSelectionOwner (manager->priv->display, XA_CLIPBOARD_MANAGER) == manager->priv->window) {
                xev.type = ClientMessage;
                xev.window = DefaultRootWindow (manager->priv->display);
                xev.message_type = XA_MANAGER;
                xev.format = 32;
                xev.data.l[0] = manager->priv->timestamp;
                xev.data.l[1] = XA_CLIPBOARD_MANAGER;
                xev.data.l[2] = manager->priv->window;
                xev.data.l[3] = 0;      /* manager specific data */
                xev.data.l[4] = 0;      /* manager specific data */

                XSendEvent (manager->priv->display,
                            DefaultRootWindow (manager->priv->display),
                            False,
                            StructureNotifyMask,
                            (XEvent *)&xev);
        } else {
                clipboard_manager_watch_cb (manager,
                                            manager->priv->window,
                                            False,
                                            0,
                                            NULL);
        }

        manager->priv->start_idle_id = 0;

        return TRUE;
}

/* Tear down the private connection pump (idempotent). */
static void
clipboard_manager_stop_watch (GsdClipboardManager *manager)
{
        if (manager->priv->xsource != NULL) {
                g_source_destroy (manager->priv->xsource);
                g_source_unref (manager->priv->xsource);
                manager->priv->xsource = NULL;
        }
}

void
gsd_clipboard_manager_stop (GsdClipboardManager *manager)
{
        if (manager == NULL || manager->priv == NULL)
                return;
        clear_receive_timeout (manager);
        clear_save_timeout (manager);
        destroy_save_window (manager);
        clipboard_manager_stop_watch (manager);
        if (manager->priv->window != None) {
                clipboard_manager_watch_cb (manager,
                                            manager->priv->window,
                                            False,
                                            0,
                                            NULL);
                if (manager->priv->display != NULL)
                        XDestroyWindow (manager->priv->display, manager->priv->window);
                manager->priv->window = None;
        }

        if (manager->priv->conversions != NULL) {
                g_slist_foreach (manager->priv->conversions, (GFunc) conversion_free, NULL);
                g_slist_free (manager->priv->conversions);
                manager->priv->conversions = NULL;
        }

        if (manager->priv->contents != NULL) {
                g_slist_foreach (manager->priv->contents, (GFunc) target_data_unref, NULL);
                g_slist_free (manager->priv->contents);
                manager->priv->contents = NULL;
        }
        if (manager->priv->display != NULL) {
                XCloseDisplay (manager->priv->display);
                manager->priv->display = NULL;
        }
}

void clipboard_start ()
{
        if (g_getenv ("XFSETTINGSD_NO_CLIPBOARD") == NULL &&
            clipboard_daemon == NULL)
        {
                clipboard_daemon = g_object_new (GSD_TYPE_CLIPBOARD_MANAGER, NULL);
                if (!gsd_clipboard_manager_start (GSD_CLIPBOARD_MANAGER (clipboard_daemon), FALSE))
                {
                        g_object_unref (G_OBJECT (clipboard_daemon));
                        clipboard_daemon = NULL;

                        g_printerr ("Another clipboard manager is already running.");
                }
        }
}

void clipboard_stop ()
{
        if (G_LIKELY (clipboard_daemon != NULL))
        {
                gsd_clipboard_manager_stop (GSD_CLIPBOARD_MANAGER (clipboard_daemon));
                g_object_unref (G_OBJECT (clipboard_daemon));
                clipboard_daemon = NULL;
        }

}
