/*
 * Copyright 2026 Raye Linux
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define _GNU_SOURCE
#include <stdlib.h>
#include <string.h>

#include "private.h"

static double overlap( const struct rcap_rect *a, const struct rcap_rect *b )
{
    long left = a->x > b->x ? a->x : b->x;
    long top = a->y > b->y ? a->y : b->y;
    long right = a->x + a->width < b->x + b->width ? a->x + a->width : b->x + b->width;
    long bottom = a->y + a->height < b->y + b->height ? a->y + a->height : b->y + b->height;
    double inter, uni;

    if (right <= left || bottom <= top) return 0;
    inter = (double)(right - left) * (bottom - top);
    uni = (double)a->width * a->height + (double)b->width * b->height - inter;
    return inter / uni;
}

static int cmp_place( const void *a, const void *b, void *rects )
{
    const struct rcap_rect *ra = (const struct rcap_rect *)rects + *(const int *)a;
    const struct rcap_rect *rb = (const struct rcap_rect *)rects + *(const int *)b;
    if (ra->x != rb->x) return ra->x < rb->x ? -1 : 1;
    if (ra->y != rb->y) return ra->y < rb->y ? -1 : 1;
    return 0;
}

static void order_by_place( const struct rcap_rect *rects, int count, int *order )
{
    int i;
    for (i = 0; i < count; i++) order[i] = i;
    qsort_r( order, count, sizeof(*order), cmp_place, (void *)rects );
}

/*
 * Windows puts the main screen at 0,0, so a screen on its left has
 * negative coordinates; the compositor starts its layout at 0,0. Moving
 * the program's screens so their bounding box starts at 0,0 puts both in
 * the same place, whichever screens the user chose to share.
 *
 * Then screens are paired by how much they overlap, best pairs first. The program may
 * see physical pixels and the portal logical ones, so an exact match is not
 * required. When nothing overlaps at all the two sides use unrelated
 * coordinates, and left-to-right order is the only thing they share.
 */
void rcap_match_monitors( const struct rcap_rect *streams, int stream_count,
                          const struct rcap_rect *monitors, int monitor_count, int *map )
{
    char *taken = calloc( stream_count > 0 ? stream_count : 1, 1 );
    struct rcap_rect *moved = malloc( (monitor_count > 0 ? monitor_count : 1) * sizeof(*moved) );
    int m, s, any = 0, left = 0, top = 0;

    for (m = 0; m < monitor_count; m++)
    {
        map[m] = -1;
        if (!m || monitors[m].x < left) left = monitors[m].x;
        if (!m || monitors[m].y < top) top = monitors[m].y;
    }
    for (m = 0; m < monitor_count; m++)
    {
        moved[m] = monitors[m];
        moved[m].x -= left;
        moved[m].y -= top;
    }

    for (;;)
    {
        double best = 0;
        int best_m = -1, best_s = -1;

        for (m = 0; m < monitor_count; m++)
        {
            if (map[m] != -1) continue;
            for (s = 0; s < stream_count; s++)
            {
                double o;
                if (taken[s]) continue;
                o = overlap( &moved[m], &streams[s] );
                if (o > best) { best = o; best_m = m; best_s = s; }
            }
        }
        if (best_m == -1) break;
        map[best_m] = best_s;
        taken[best_s] = 1;
        any = 1;
    }

    if (!any && stream_count > 0 && monitor_count > 0)
    {
        int *mo = malloc( (size_t)monitor_count * sizeof(int) ), *so = malloc( (size_t)stream_count * sizeof(int) );
        if (mo && so)
        {
            order_by_place( monitors, monitor_count, mo );
            order_by_place( streams, stream_count, so );
            for (m = 0; m < monitor_count && m < stream_count; m++) map[mo[m]] = so[m];
        }
        free( mo );
        free( so );
    }
    free( moved );
    free( taken );
}

static int same_ratio( long w1, long h1, long w2, long h2 )
{
    /* within 2 % */
    return w1 > 0 && h1 > 0 && w2 > 0 && h2 > 0 && labs( w1 * h2 - w2 * h1 ) * 50 <= w1 * h2;
}

/*
 * "Share full Workspace" (and the like) gives one stream holding every
 * screen. A program still reads its screens one by one, so each one gets its
 * own part of that image. Returns 1 and the part, in stream coordinates, when
 * the stream is the whole desktop; the program may count physical pixels and
 * the portal logical ones, so the desktop is scaled to the stream's size.
 */
int rcap_workspace_crop( const struct rcap_rect *stream, const struct rcap_rect *monitors, int monitor_count,
                         int monitor, struct rcap_rect *crop )
{
    long left = 0, top = 0, right = 0, bottom = 0, w, h;
    int m;

    if (monitor_count < 2 || monitor < 0 || monitor >= monitor_count) return 0;
    if (stream->width <= 0 || stream->height <= 0) return 0;
    for (m = 0; m < monitor_count; m++)
    {
        if (!m || monitors[m].x < left) left = monitors[m].x;
        if (!m || monitors[m].y < top) top = monitors[m].y;
        if (!m || monitors[m].x + monitors[m].width > right) right = monitors[m].x + monitors[m].width;
        if (!m || monitors[m].y + monitors[m].height > bottom) bottom = monitors[m].y + monitors[m].height;
    }
    w = right - left;
    h = bottom - top;
    if (!same_ratio( stream->width, stream->height, w, h )) return 0;
    /* a single screen of the same shape as the desktop is just that screen */
    for (m = 0; m < monitor_count; m++)
    {
        struct rcap_rect moved = monitors[m];
        moved.x -= left;
        moved.y -= top;
        if (overlap( stream, &moved ) >= 0.9 && (moved.width != w || moved.height != h)) return 0;
        if (same_ratio( stream->width, stream->height, monitors[m].width, monitors[m].height )) return 0;
    }
    crop->x = (monitors[monitor].x - left) * stream->width / w;
    crop->y = (monitors[monitor].y - top) * stream->height / h;
    crop->width = (long)monitors[monitor].width * stream->width / w;
    crop->height = (long)monitors[monitor].height * stream->height / h;
    return crop->width > 0 && crop->height > 0;
}
