/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#include <gtk/gtk.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>

static gboolean stored;
static gboolean completed;
static void store_done(GObject *source, GAsyncResult *result, gpointer unused)
{
    (void) unused;
    GError *error = NULL;
    stored = gdk_clipboard_store_finish(GDK_CLIPBOARD(source), result, &error);
    if (error) g_printerr("store: %s\n", error->message);
    g_clear_error(&error);
    completed = TRUE;
}

static int owner(const char *kind)
{
    gtk_init();
    GdkClipboard *clipboard = gdk_display_get_clipboard(gdk_display_get_default());
    if (g_str_equal(kind, "image")) {
        static const guint8 pixels[] = {255, 0, 0, 255, 0, 255, 0, 255,
                                        0, 0, 255, 255, 255, 255, 255, 255};
        GBytes *bytes = g_bytes_new_static(pixels, sizeof pixels);
        GdkTexture *texture = gdk_memory_texture_new(2, 2, GDK_MEMORY_R8G8B8A8, bytes, 8);
        gdk_clipboard_set_texture(clipboard, texture);
        g_object_unref(texture);
        g_bytes_unref(bytes);
    } else {
        gsize size = g_str_equal(kind, "large") ? 2 * 1024 * 1024 : 25;
        char *text = g_malloc(size + 1);
        memset(text, 'x', size);
        text[size] = 0;
        if (g_str_equal(kind, "large")) {
            /* Raw-byte providers isolate the INCR protocol from GTK's
             * converter stream close path for multi-encoding text values. */
            GBytes *bytes = g_bytes_new(text, size);
            GdkContentProvider *provider = gdk_content_provider_new_for_bytes("text/plain;charset=utf-8", bytes);
            g_assert_true(gdk_clipboard_set_content(clipboard, provider));
            g_object_unref(provider);
            g_bytes_unref(bytes);
        } else gdk_clipboard_set_text(clipboard, text);
        g_free(text);
    }
    gdk_clipboard_store_async(clipboard, G_PRIORITY_DEFAULT, NULL, store_done, NULL);
    gint64 deadline = g_get_monotonic_time() + 10 * G_TIME_SPAN_SECOND;
    while (!completed && g_get_monotonic_time() < deadline) {
        while (g_main_context_iteration(NULL, FALSE));
        g_usleep(1000);
    }
    return completed && stored ? 0 : 1;
}

static GByteArray *read_selection(Display *display, Window window, const char *target)
{
    Atom clipboard = XInternAtom(display, "CLIPBOARD", False);
    Atom requested = XInternAtom(display, target, False);
    Atom property = XInternAtom(display, "_LUXE_TEST_RESULT", False);
    Atom incr = XInternAtom(display, "INCR", False);
    XDeleteProperty(display, window, property);
    XConvertSelection(display, clipboard, requested, property, window, CurrentTime);
    XFlush(display);
    GByteArray *bytes = g_byte_array_new();
    gboolean incremental = FALSE;
    gint64 deadline = g_get_monotonic_time() + 10 * G_TIME_SPAN_SECOND;
    while (g_get_monotonic_time() < deadline) {
        if (!XPending(display)) { g_usleep(1000); continue; }
        XEvent event;
        XNextEvent(display, &event);
        gboolean ready = event.type == SelectionNotify;
        if (ready && event.xselection.property == None) {
            g_byte_array_unref(bytes);
            return NULL;
        }
        if (incremental && event.type == PropertyNotify &&
            event.xproperty.atom == property && event.xproperty.state == PropertyNewValue)
            ready = TRUE;
        if (!ready) continue;
        Atom actual;
        int format;
        unsigned long count, remaining;
        unsigned char *data = NULL;
        g_assert_cmpint(XGetWindowProperty(display, window, property, 0, 4 * 1024 * 1024,
            True, AnyPropertyType, &actual, &format, &count, &remaining, &data), ==, Success);
        g_assert_cmpuint(remaining, ==, 0);
        if (actual == incr) incremental = TRUE;
        else {
            g_assert_cmpint(format, ==, 8);
            g_byte_array_append(bytes, data, count);
            if (!incremental || count == 0) { XFree(data); return bytes; }
        }
        XFree(data);
        XFlush(display);
    }
    g_error("selection timed out: %s", target);
    return NULL;
}

static void read_multiple(Display *display, Window window)
{
    Atom clipboard = XInternAtom(display, "CLIPBOARD", False);
    Atom multiple = XInternAtom(display, "MULTIPLE", False);
    Atom pair_type = XInternAtom(display, "ATOM_PAIR", False);
    Atom property = XInternAtom(display, "_LUXE_MULTIPLE_RESULT", False);
    Atom pairs[] = {XInternAtom(display, "UTF8_STRING", False),
        XInternAtom(display, "_LUXE_MULTIPLE_TEXT", False),
        XInternAtom(display, "_LUXE_UNSUPPORTED_TARGET", False),
        XInternAtom(display, "_LUXE_MULTIPLE_BAD", False)};
    XChangeProperty(display, window, property, pair_type, 32, PropModeReplace, (guint8 *)pairs, 4);
    XConvertSelection(display, clipboard, multiple, property, window, CurrentTime);
    XFlush(display);
    gboolean notified = FALSE;
    gint64 deadline = g_get_monotonic_time() + 5 * G_TIME_SPAN_SECOND;
    while (g_get_monotonic_time() < deadline) {
        if (!XPending(display)) { g_usleep(1000); continue; }
        XEvent event; XNextEvent(display, &event);
        if (event.type == SelectionNotify) {
            g_assert_cmpuint(event.xselection.property, ==, property);
            notified = TRUE; break;
        }
    }
    g_assert_true(notified);
    Atom actual; int format; unsigned long count, remaining; guint8 *data = NULL;
    XGetWindowProperty(display, window, property, 0, 4, True, pair_type,
        &actual, &format, &count, &remaining, &data);
    g_assert_cmpint(format, ==, 32); g_assert_cmpuint(count, ==, 4);
    Atom *returned = (Atom *)data;
    g_assert_cmpuint(returned[0], ==, pairs[0]);
    g_assert_cmpuint(returned[1], ==, pairs[1]);
    g_assert_cmpuint(returned[2], ==, pairs[2]);
    g_assert_cmpuint(returned[3], ==, None);
    XFree(data);
    XGetWindowProperty(display, window, pairs[1], 0, 1024, True, AnyPropertyType,
        &actual, &format, &count, &remaining, &data);
    g_assert_cmpint(format, ==, 8); g_assert_cmpuint(count, ==, 25);
    for (guint i = 0; i < count; i++) g_assert_cmpint(data[i], ==, 'x');
    XFree(data);
}

static void malicious_owner(Display *display, Window requestor)
{
    Window root = DefaultRootWindow(display);
    Window owner = XCreateSimpleWindow(display, root, 0, 0, 1, 1, 0, 0, 0);
    Atom clipboard = XInternAtom(display, "CLIPBOARD", False);
    Atom manager = XInternAtom(display, "CLIPBOARD_MANAGER", False);
    Atom save = XInternAtom(display, "SAVE_TARGETS", False);
    Atom incr = XInternAtom(display, "INCR", False);
    Atom good = XInternAtom(display, "_LUXE_VALID_TARGET", False);
    Atom bad = XInternAtom(display, "_LUXE_BAD_TARGET", False);
    Atom property = XInternAtom(display, "_LUXE_SAVE_TARGETS", False);
    Atom targets[] = { good, bad }; /* Manager processes the last target first. */
    XSetSelectionOwner(display, clipboard, owner, CurrentTime);
    g_assert_cmpuint(XGetSelectionOwner(display, clipboard), ==, owner);
    XChangeProperty(display, requestor, property, XA_ATOM, 32, PropModeReplace,
                    (unsigned char *)targets, 2);
    XConvertSelection(display, manager, save, property, requestor, CurrentTime);
    XFlush(display);
    Window receiver = None;
    int bad_stage = 0;
    gboolean saved = FALSE;
    gint64 deadline = g_get_monotonic_time() + 8 * G_TIME_SPAN_SECOND;
    while (!saved && g_get_monotonic_time() < deadline) {
        if (!XPending(display)) { g_usleep(1000); continue; }
        XEvent event; XNextEvent(display, &event);
        if (event.type == SelectionRequest && event.xselectionrequest.owner == owner) {
            XSelectionRequestEvent *request = &event.xselectionrequest;
            XSelectionEvent reply = {0};
            reply.type = SelectionNotify; reply.display = display;
            reply.requestor = request->requestor; reply.selection = request->selection;
            reply.target = request->target; reply.property = request->property; reply.time = request->time;
            if (request->target == bad) {
                receiver = request->requestor;
                XSelectInput(display, receiver, PropertyChangeMask);
                unsigned long announced = 2;
                XChangeProperty(display, receiver, bad, incr, 32, PropModeReplace,
                                (unsigned char *)&announced, 1);
            } else if (request->target == good) {
                const unsigned char bytes[] = "OK";
                XChangeProperty(display, request->requestor, request->property, XA_STRING, 8,
                                PropModeReplace, bytes, 2);
            } else reply.property = None;
            XSendEvent(display, request->requestor, False, NoEventMask, (XEvent *)&reply);
            XFlush(display);
        } else if (event.type == PropertyNotify && event.xproperty.window == receiver &&
                   event.xproperty.atom == bad && event.xproperty.state == PropertyDelete) {
            if (bad_stage == 0) {
                const unsigned char byte = 'x';
                XChangeProperty(display, receiver, bad, XA_STRING, 8, PropModeReplace, &byte, 1);
                bad_stage++;
            } else if (bad_stage == 1) {
                unsigned long word = 1;
                XChangeProperty(display, receiver, bad, XA_ATOM, 32, PropModeReplace,
                                (unsigned char *)&word, 1);
                bad_stage++;
            }
            XFlush(display);
        } else if (event.type == SelectionNotify && event.xselection.requestor == requestor &&
                   event.xselection.selection == manager && event.xselection.target == save) {
            g_assert_cmpuint(event.xselection.property, ==, property);
            saved = TRUE;
        }
    }
    g_assert_true(saved);
    g_assert_cmpint(bad_stage, ==, 2);
    GByteArray *bytes = read_selection(display, requestor, "_LUXE_VALID_TARGET");
    g_assert_nonnull(bytes);
    g_assert_cmpmem(bytes->data, bytes->len, "OK", 2);
    g_byte_array_unref(bytes);
    XDestroyWindow(display, owner);
}

int main(int argc, char **argv)
{
    if (argc == 3 && g_str_equal(argv[1], "--owner")) return owner(argv[2]);
    g_assert_cmpint(argc, ==, 2);
    Display *display = XOpenDisplay(NULL);
    g_assert_nonnull(display);
    GPid manager;
    char *manager_argv[] = {argv[1], NULL};
    g_assert_true(g_spawn_async(NULL, manager_argv, NULL, G_SPAWN_DO_NOT_REAP_CHILD,
                               NULL, NULL, &manager, NULL));
    Atom selection = XInternAtom(display, "CLIPBOARD_MANAGER", False);
    gint64 deadline = g_get_monotonic_time() + 5 * G_TIME_SPAN_SECOND;
    while (XGetSelectionOwner(display, selection) == None && g_get_monotonic_time() < deadline)
        g_usleep(1000);
    g_assert_cmpuint(XGetSelectionOwner(display, selection), !=, None);
    Window window = XCreateSimpleWindow(display, DefaultRootWindow(display), 0, 0, 1, 1, 0, 0, 0);
    XSelectInput(display, window, PropertyChangeMask);
    malicious_owner(display, window);
    const char *kinds[] = {"text", "large", "image", "text"};
    for (guint i = 0; i < G_N_ELEMENTS(kinds); i++) {
        char *owner_argv[] = {argv[0], "--owner", (char *) kinds[i], NULL};
        int status = -1;
        g_assert_true(g_spawn_sync(NULL, owner_argv, NULL, 0, NULL, NULL, NULL, NULL, &status, NULL));
        g_assert_true(g_spawn_check_wait_status(status, NULL));
        /* The source application has exited: only the service can answer. */
        GByteArray *bytes = read_selection(display, window,
            g_str_equal(kinds[i], "image") ? "image/png" : "UTF8_STRING");
        g_assert_nonnull(bytes);
        if (g_str_equal(kinds[i], "image")) {
            const guint8 signature[] = {137, 80, 78, 71, 13, 10, 26, 10};
            g_assert_cmpuint(bytes->len, >, sizeof signature);
            g_assert_cmpmem(bytes->data, sizeof signature, signature, sizeof signature);
        } else {
            g_assert_cmpuint(bytes->len, ==, g_str_equal(kinds[i], "large") ? 2 * 1024 * 1024 : 25);
            for (guint j = 0; j < bytes->len; j++) g_assert_cmpint(bytes->data[j], ==, 'x');
        }
        g_byte_array_unref(bytes);
        g_assert_null(read_selection(display, window, "_LUXE_UNSUPPORTED_TARGET"));
    }
    read_multiple(display, window);
    kill(manager, SIGTERM);
    waitpid(manager, NULL, 0);
    g_spawn_close_pid(manager);
    XDestroyWindow(display, window);
    XCloseDisplay(display);
    g_print("PASS: text/image persistence after exit, MULTIPLE, INCR, replacement, unsupported target\n");
    return 0;
}
