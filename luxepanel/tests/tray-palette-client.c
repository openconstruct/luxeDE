/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
/* Real XEmbed clients: inherited palette versus deliberately opaque white. */
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <glib.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    g_assert_cmpint(argc, ==, 3);
    Display *dpy = XOpenDisplay(NULL);
    g_assert_nonnull(dpy);
    Window root = DefaultRootWindow(dpy), manager = None;
    char *name = g_strdup_printf("_NET_SYSTEM_TRAY_S%d", DefaultScreen(dpy));
    Atom selection = XInternAtom(dpy, name, False);
    g_free(name);
    for(int i = 0; i < 100 && manager == None; i++) {
        manager = XGetSelectionOwner(dpy, selection); g_usleep(20000);
    }
    g_assert_cmpuint(manager, !=, None);
    unsigned rgb = (unsigned)strtoul(argv[1] + 1, NULL, 16);
    unsigned edge_rgb = (unsigned)strtoul(argv[2] + 1, NULL, 16);
    int inset_y = -1, inset_height = -1;
    for(int index = 0; index < 3; index++) {
        gboolean opaque = index != 0;
        gboolean edge_aware = index == 2;
        Window client = XCreateSimpleWindow(dpy, root, 0, 0, 24, 24, 0, 0,
                                            WhitePixel(dpy, DefaultScreen(dpy)));
        if(!opaque) XSetWindowBackgroundPixmap(dpy, client, ParentRelative);
        if(edge_aware) {
            unsigned long enabled = 1;
            XChangeProperty(dpy, client, XInternAtom(dpy, "_LUXE_TRAY_EDGE_AWARE", False),
                            XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&enabled, 1);
        }
        Atom info = XInternAtom(dpy, "_XEMBED_INFO", False);
        unsigned long mapped[] = {0, 1};
        XChangeProperty(dpy, client, info, info, 32, PropModeReplace,
                        (unsigned char *)mapped, 2);
        XEvent event = {0};
        event.xclient.type = ClientMessage;
        event.xclient.window = manager;
        event.xclient.message_type = XInternAtom(dpy, "_NET_SYSTEM_TRAY_OPCODE", False);
        event.xclient.format = 32;
        event.xclient.data.l[1] = 0;
        event.xclient.data.l[2] = client;
        XSendEvent(dpy, manager, False, NoEventMask, &event); XFlush(dpy);
        XWindowAttributes attr;
        gboolean visible = FALSE;
        for(int i = 0; i < 100; i++) {
            XSync(dpy, False);
            if(XGetWindowAttributes(dpy, client, &attr) && attr.map_state == IsViewable) {
                visible = TRUE; break;
            }
            g_usleep(20000);
        }
        g_assert_true(visible);
        /* The normal client must leave the top/bottom inset outline visible;
         * an edge-aware client keeps the complete slot for its own line. */
        Window frame = None, panel = None, ignored_root, *children = NULL;
        unsigned int count = 0;
        g_assert_true(XQueryTree(dpy, client, &ignored_root, &frame, &children, &count));
        if(children) XFree(children);
        children = NULL;
        g_assert_true(XQueryTree(dpy, frame, &ignored_root, &panel, &children, &count));
        if(children) XFree(children);
        XWindowAttributes frame_attr, panel_attr;
        g_assert_true(XGetWindowAttributes(dpy, frame, &frame_attr));
        g_assert_true(XGetWindowAttributes(dpy, panel, &panel_attr));
        int fx, fy, px, py;
        Window ignored_child;
        g_assert_true(XTranslateCoordinates(dpy, frame, root, 0, 0, &fx, &fy, &ignored_child));
        g_assert_true(XTranslateCoordinates(dpy, panel, root, 0, 0, &px, &py, &ignored_child));
        if(!edge_aware) {
            g_assert_cmpint(fy, >=, py + 1);
            g_assert_cmpint(fy + frame_attr.height, <=, py + panel_attr.height - 1);
            inset_y = fy; inset_height = frame_attr.height;
            XImage *rim = XGetImage(dpy, root, fx + frame_attr.width/2, py,
                                    1, 1, AllPlanes, ZPixmap);
            g_assert_nonnull(rim);
            XColor edge = {.pixel = XGetPixel(rim, 0, 0)};
            XQueryColor(dpy, DefaultColormap(dpy, DefaultScreen(dpy)), &edge);
            g_assert_cmpint(abs((int)edge.red - (int)(((edge_rgb >> 16) & 255)*257)), <, 1028);
            g_assert_cmpint(abs((int)edge.green - (int)(((edge_rgb >> 8) & 255)*257)), <, 1028);
            g_assert_cmpint(abs((int)edge.blue - (int)((edge_rgb & 255)*257)), <, 1028);
            XDestroyImage(rim);
        } else {
            g_assert_cmpint(fy, <, inset_y);
            g_assert_cmpint(frame_attr.height, >, inset_height);
        }
        XClearWindow(dpy, client); XSync(dpy, False);
        XImage *image = XGetImage(dpy, client, 4, 4, 1, 1, AllPlanes, ZPixmap);
        g_assert_nonnull(image);
        XColor color = {.pixel = XGetPixel(image, 0, 0)};
        XQueryColor(dpy, attr.colormap, &color);
        unsigned expected = opaque ? 0xffffff : rgb;
        g_assert_cmpint(abs((int)color.red - (int)(((expected >> 16) & 255) * 257)), <, 512);
        g_assert_cmpint(abs((int)color.green - (int)(((expected >> 8) & 255) * 257)), <, 512);
        g_assert_cmpint(abs((int)color.blue - (int)((expected & 255) * 257)), <, 512);
        XDestroyImage(image); XDestroyWindow(dpy, client); XSync(dpy, False);
    }
    XCloseDisplay(dpy);
    g_print("PASS: inherited tray palette and opaque client preservation\n");
    return 0;
}
