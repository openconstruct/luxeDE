/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef LUXERANDR_WINDOW_PLACEMENT_H
#define LUXERANDR_WINDOW_PLACEMENT_H

#include <glib.h>

typedef struct { gint x,y,width,height; } LuxeRandRRect;

static gboolean luxerandr_titlebar_fits(gint x,gint y,gint width,gint height,
                                         const LuxeRandRRect *region,
                                         gint title_width,gint title_height)
{
    gint w=MIN(width,title_width),h=MIN(height,title_height);
    return x>=region->x && y>=region->y &&
        (gint64)x+w<=(gint64)region->x+region->width &&
        (gint64)y+h<=(gint64)region->y+region->height;
}

static gboolean luxerandr_recovery_position(gint x,gint y,gint width,gint height,
                                             const LuxeRandRRect *regions,guint count,
                                             gint title_width,gint title_height,
                                             gint *new_x,gint *new_y)
{
    if (!regions || !count || width<=0 || height<=0 || !new_x || !new_y) return FALSE;
    for (guint i=0;i<count;i++) {
        if (luxerandr_titlebar_fits(x,y,width,height,&regions[i],title_width,title_height)) {
            *new_x=x;*new_y=y;return TRUE;
        }
    }
    long double best=1.0e100L;gboolean found=FALSE;
    for (guint i=0;i<count;i++) {
        gint visible_w=MIN(width,MIN(title_width,regions[i].width));
        gint visible_h=MIN(height,MIN(title_height,regions[i].height));
        if (visible_w<=0 || visible_h<=0) continue;
        gint64 min_x=(gint64)regions[i].x-width+visible_w;
        gint64 max_x=(gint64)regions[i].x+regions[i].width-visible_w;
        gint64 min_y=regions[i].y;
        gint64 max_y=(gint64)regions[i].y+regions[i].height-visible_h;
        gint64 candidate_x=CLAMP((gint64)x,min_x,max_x);
        gint64 candidate_y=CLAMP((gint64)y,min_y,max_y);
        gint64 dx=candidate_x-x,dy=candidate_y-y;
        long double distance=(long double)dx*dx+(long double)dy*dy;
        if (!found || distance<best) {
            best=distance;found=TRUE;
            *new_x=(gint)CLAMP(candidate_x,G_MININT,G_MAXINT);
            *new_y=(gint)CLAMP(candidate_y,G_MININT,G_MAXINT);
        }
    }
    return found;
}

#endif
