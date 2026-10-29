/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef LUXEPANEL_TASK_ICON_PROPERTY_H
#define LUXEPANEL_TASK_ICON_PROPERTY_H
#include <glib.h>
#include <X11/Xlib.h>

static gboolean task_icon_geometry_allowed(guint w,guint h)
{ return w>0 && h>0 && w<=1024 && h<=1024; }

/* Xlib returns format-32 properties as native unsigned longs on LP64. */
static unsigned long *
task_icon_read_property(Display *display, Window window, Atom property, Atom expected,
                        unsigned long max_items, unsigned long min_items, unsigned long *count)
{
    Atom type=None; int format=0; unsigned long items=0, remaining=0;
    unsigned char *data=NULL; *count=0;
    int result=XGetWindowProperty(display,window,property,0,(long)max_items,False,expected,
                                  &type,&format,&items,&remaining,&data);
    if(result!=Success || type!=expected || format!=32 || !data ||
       items<min_items || items>max_items || remaining!=0) {
        if(data)XFree(data);
        return NULL;
    }
    *count=items; return (unsigned long *)data;
}

static unsigned long *
task_icon_select(unsigned long *data, unsigned long count, guint want_w, guint want_h,
                 gulong *chosen_w, gulong *chosen_h)
{
    unsigned long pos=0, best_pixels=0; unsigned long *best=NULL;
    gboolean best_exact=FALSE;
    *chosen_w=*chosen_h=0;
    while(pos<count) {
        if(count-pos<3) return NULL;
        gulong w=data[pos++], h=data[pos++];
        if(!w || !h || w>1024 || h>1024) return NULL;
        gulong pixels=w*h;
        if(pixels>count-pos) return NULL;
        gboolean exact=w==want_w && h==want_h;
        if(exact || (!best_exact && pixels>best_pixels)) {
            best=data+pos; *chosen_w=w; *chosen_h=h;
            best_pixels=pixels; best_exact=exact;
        }
        pos+=pixels;
    }
    return best;
}
#endif
