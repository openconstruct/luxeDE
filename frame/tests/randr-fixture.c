/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-3.0-or-later */
/* Model RandR's documented initial notification on re-selection after a
 * startup layout change. No display layout is modified by this fixture. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>

void XRRSelectInput(Display *display,Window window,int mask)
{
    static void (*select_input)(Display *,Window,int);
    static unsigned selections;
    if(!select_input) select_input=dlsym(RTLD_NEXT,"XRRSelectInput");
    select_input(display,window,mask);
    if(++selections<2 || !(mask&RRScreenChangeNotifyMask)) return;
    int event_base,error_base;
    if(!XRRQueryExtension(display,&event_base,&error_base)) return;
    XRRScreenChangeNotifyEvent event={.type=event_base+RRScreenChangeNotify,
        .display=display,.window=window,.root=window,.rotation=RR_Rotate_0,
        .width=DisplayWidth(display,DefaultScreen(display)),
        .height=DisplayHeight(display,DefaultScreen(display)),
        .mwidth=DisplayWidthMM(display,DefaultScreen(display)),
        .mheight=DisplayHeightMM(display,DefaultScreen(display))};
    XEvent queued={0};
    *(XRRScreenChangeNotifyEvent *)&queued=event;
    XPutBackEvent(display,&queued);
}
