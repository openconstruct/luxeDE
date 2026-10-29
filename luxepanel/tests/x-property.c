/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#include <gtk/gtk.h>
#include <gdk/x11/gdkx.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <string.h>

#include "misc.h"
#include "../plugins/task-icon-property.h"
#include "../plugins/task-button.c"

void x11_root_watch_window(Window window) { (void)window; }

int
main(int argc, char **argv)
{
    GdkDisplay *display;
    Display *dpy;
    Window root, window;
    Atom utf8;
    int count = 0;
    unsigned long bad_16[] = { 1, 2 };
    unsigned char text[] = { 'a', 'b', 'c' };
    char **list;
    void *value;

    gtk_init();
    display = gdk_display_get_default();
    g_assert_true(display != NULL && GDK_IS_X11_DISPLAY(display));
    dpy = GDK_DISPLAY_XDISPLAY(display);
    root = DefaultRootWindow(dpy);
    window = XCreateSimpleWindow(dpy, root, -10, -10, 1, 1, 0, 0, 0);
    utf8 = XInternAtom(dpy, "UTF8_STRING", False);
    a_UTF8_STRING = utf8;

    XChangeProperty(dpy, window, XA_WM_NAME, XA_CARDINAL, 16,
                    PropModeReplace, (unsigned char *)bad_16, 2);
    value = get_xaproperty(window, XA_WM_NAME, XA_CARDINAL, &count);
    g_assert_null(value);
    g_assert_cmpint(count, ==, 0);

    Atom icon=XInternAtom(dpy,"_NET_WM_ICON",False);
    a_NET_WM_ICON=icon;
    unsigned long n=0,*pixels;gulong w=0,h=0;
    unsigned char invalid8[]={1,2,3,4};
    XChangeProperty(dpy,window,icon,XA_CARDINAL,8,PropModeReplace,invalid8,4);
    g_assert_null(task_icon_read_property(dpy,window,icon,XA_CARDINAL,16,3,&n));
    XChangeProperty(dpy,window,icon,XA_CARDINAL,16,PropModeReplace,(unsigned char *)bad_16,2);
    g_assert_null(task_icon_read_property(dpy,window,icon,XA_CARDINAL,16,3,&n));
    unsigned long too_short[]={1};
    XChangeProperty(dpy,window,icon,XA_CARDINAL,32,PropModeReplace,(unsigned char *)too_short,1);
    g_assert_null(task_icon_read_property(dpy,window,icon,XA_CARDINAL,16,2,&n));
    unsigned long valid[]={1,1,0xff112233,2,1,0xff445566,0xff778899};
    XChangeProperty(dpy,window,icon,XA_CARDINAL,32,PropModeReplace,(unsigned char *)valid,7);
    g_assert_null(task_icon_read_property(dpy,window,icon,XA_CARDINAL,3,3,&n));
    pixels=task_icon_read_property(dpy,window,icon,XA_CARDINAL,16,3,&n);
    g_assert_nonnull(pixels);g_assert_cmpuint(n,==,7);
    unsigned long *chosen=task_icon_select(pixels,n,2,1,&w,&h);
    g_assert_nonnull(chosen);g_assert_cmpuint(w,==,2);g_assert_cmpuint(h,==,1);
    g_assert_cmphex((guint32)chosen[0],==,0xff445566);XFree(pixels);
    unsigned long truncated[]={2,2,0xff112233};
    g_assert_null(task_icon_select(truncated,3,1,1,&w,&h));
    unsigned long trailing_truncated[]={1,1,0xff112233,1};
    g_assert_null(task_icon_select(trailing_truncated,4,1,1,&w,&h));
    unsigned long exact_then_invalid[]={1,1,0xff112233,G_MAXUINT32,1,0};
    g_assert_null(task_icon_select(exact_then_invalid,6,1,1,&w,&h));
    unsigned long invalid_size[]={G_MAXUINT32,G_MAXUINT32,0};
    g_assert_null(task_icon_select(invalid_size,3,1,1,&w,&h));
    g_assert_true(task_icon_geometry_allowed(1024,1024));
    g_assert_false(task_icon_geometry_allowed(0,1));
    g_assert_false(task_icon_geometry_allowed(1025,1));
    Atom legacy=XInternAtom(dpy,"KWM_WIN_ICON",False);
    XChangeProperty(dpy,window,legacy,legacy,32,PropModeReplace,(unsigned char *)too_short,1);
    g_assert_null(task_icon_read_property(dpy,window,legacy,legacy,2,2,&n));
    unsigned long pair[]={123,456};
    XChangeProperty(dpy,window,legacy,legacy,32,PropModeReplace,(unsigned char *)pair,2);
    pixels=task_icon_read_property(dpy,window,legacy,legacy,2,2,&n);
    g_assert_nonnull(pixels);g_assert_cmpuint(pixels[0],==,123);g_assert_cmpuint(pixels[1],==,456);XFree(pixels);

    /* Drive the production task-icon update path with an invalid property.
     * The WM-hints and legacy pixmap paths also fail, so the generic task
     * fallback must become the image's actual paintable. */
    GtkWidget *fallback_parent=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,0);
    g_object_ref_sink(fallback_parent);
    TaskButton *task=g_object_new(PANEL_TYPE_TASK_BUTTON,NULL);
    g_object_ref_sink(task);
    task->image=gtk_image_new();task->icon_size=16;
    gtk_button_set_child(GTK_BUTTON(task),task->image);
    gtk_box_append(GTK_BOX(fallback_parent),GTK_WIDGET(task));
    unsigned char malformed[]={1,2,3,4};
    XChangeProperty(dpy,window,icon,XA_CARDINAL,16,PropModeReplace,malformed,2);
    TaskDetails details={0};details.win=window;
    _task_update_icon(task,&details,None);
    g_assert_nonnull(gtk_image_get_paintable(GTK_IMAGE(task->image)));
    unsigned long valid_icons[]={1,1,0xff112233,2,1,0xff445566,0xff778899};
    XChangeProperty(dpy,window,icon,XA_CARDINAL,32,PropModeReplace,
                    (unsigned char *)valid_icons,G_N_ELEMENTS(valid_icons));
    _task_update_icon(task,&details,None);
    g_assert_cmpuint(details.image_source,==,icon);
    g_assert_nonnull(gtk_image_get_paintable(GTK_IMAGE(task->image)));
    /* Verify the production updater renders the selected X property pixels,
     * rather than merely returning some fallback paintable. */
    unsigned long rendered_icon[]={1,1,0xff336699};
    XChangeProperty(dpy,window,icon,XA_CARDINAL,32,PropModeReplace,
                    (unsigned char *)rendered_icon,G_N_ELEMENTS(rendered_icon));
    TaskDetails render_details={0};render_details.win=window;
    _task_update_icon(task,&render_details,None);
    GdkPaintable *rendered=gtk_image_get_paintable(GTK_IMAGE(task->image));
    g_assert_true(GDK_IS_TEXTURE(rendered));
    int rendered_width=gdk_paintable_get_intrinsic_width(rendered);
    int rendered_height=gdk_paintable_get_intrinsic_height(rendered);
    g_assert_cmpint(rendered_width,==,16);g_assert_cmpint(rendered_height,==,16);
    g_autofree guchar *rendered_pixels=g_malloc0((gsize)rendered_width*rendered_height*4);
    gdk_texture_download(GDK_TEXTURE(rendered),rendered_pixels,rendered_width*4);
    g_assert_cmpuint(rendered_pixels[0],==,0x99);
    g_assert_cmpuint(rendered_pixels[1],==,0x66);
    g_assert_cmpuint(rendered_pixels[2],==,0x33);
    g_clear_object(&render_details.icon);
    for (guint i=0;i<50;i++) {
        XChangeProperty(dpy,window,icon,XA_CARDINAL,16,PropModeReplace,malformed,2);
        _task_update_icon(task,&details,None);
        g_assert_nonnull(gtk_image_get_paintable(GTK_IMAGE(task->image)));
        XChangeProperty(dpy,window,icon,XA_CARDINAL,32,PropModeReplace,
                        (unsigned char *)rendered_icon,G_N_ELEMENTS(rendered_icon));
        _task_update_icon(task,&details,None);
        g_assert_cmpuint(details.image_source,==,icon);
    }

    /* Oversized real pixmap geometry is rejected before XGetImage allocation. */
    Pixmap oversized_pixmap=XCreatePixmap(dpy,root,1025,1,DefaultDepth(dpy,DefaultScreen(dpy)));
    XWMHints *wm_hints=XAllocWMHints();
    wm_hints->flags=IconPixmapHint;wm_hints->icon_pixmap=oversized_pixmap;
    XSetWMHints(dpy,window,wm_hints);XFree(wm_hints);XSync(dpy,False);
    Atom current_source=None;
    g_assert_null(get_wm_icon(window,16,16,XA_WM_HINTS,&current_source,task));
    XFreePixmap(dpy,oversized_pixmap);

    /* A destroyed client window must fall through the scoped X error trap. */
    Window vanished=XCreateSimpleWindow(dpy,root,0,0,1,1,0,0,0);
    XDestroyWindow(dpy,vanished);XSync(dpy,False);
    current_source=None;
    g_assert_null(get_wm_icon(vanished,16,16,None,&current_source,task));
    gtk_box_remove(GTK_BOX(fallback_parent),GTK_WIDGET(task));
    g_object_unref(task);
    g_object_unref(fallback_parent);

    XChangeProperty(dpy, window, XA_WM_CLASS, utf8, 8,
                    PropModeReplace, text, sizeof(text));
    list = get_utf8_property_list(window, XA_WM_CLASS, &count);
    g_assert_nonnull(list);
    g_assert_cmpint(count, ==, 1);
    g_assert_cmpstr(list[0], ==, "abc");
    g_strfreev(list);

    XDestroyWindow(dpy, window);
    XFlush(dpy);
    (void)argc;
    (void)argv;
    return 0;
}
