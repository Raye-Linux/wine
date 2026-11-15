/*
 * Opens a capture session the way a Windows program does, then reports
 * what arrives: streams, frame rate, and how old each image is.
 *
 *   raye-capture-probe [--pick] [--app KEY] [--seconds N] [--dump DIR]
 *
 * Copyright 2026 Raye Linux
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "raye-capture.h"

static int64_t now_ns( void )
{
    struct timespec ts;
    clock_gettime( CLOCK_MONOTONIC, &ts );
    return ts.tv_sec * 1000000000ll + ts.tv_nsec;
}

static void dump( struct rcap_session *s, int index, const char *dir )
{
    struct rcap_frame_info info;
    struct rcap_rect rect = { 0, 0, 0, 0 };
    unsigned char *pixels;
    char path[512];
    FILE *f;
    int i;

    rcap_wait_frame( s, index, 0, -1, 1000, &info );
    rect.width = info.width;
    rect.height = info.height;
    if (!rect.width || !(pixels = malloc( (size_t)rect.width * rect.height * 4 ))) return;
    rcap_copy_frame( s, index, &rect, pixels, rect.width * 4, RCAP_COPY_CURSOR, &info );
    snprintf( path, sizeof(path), "%s/stream%d.ppm", dir, index );
    if ((f = fopen( path, "wb" )))
    {
        fprintf( f, "P6\n%d %d\n255\n", rect.width, rect.height );
        for (i = 0; i < rect.width * rect.height; i++)
        {
            fputc( pixels[i * 4 + 2], f );
            fputc( pixels[i * 4 + 1], f );
            fputc( pixels[i * 4 + 0], f );
        }
        fclose( f );
        printf( "saved %s\n", path );
    }
    free( pixels );
}

int main( int argc, char **argv )
{
    enum rcap_kind kind = RCAP_KIND_SCREENS;
    const char *app = "raye-capture-probe", *dir = NULL;
    struct rcap_session *s;
    int seconds = 10, i, n;
    int64_t start;

    for (i = 1; i < argc; i++)
    {
        if (!strcmp( argv[i], "--pick" )) kind = RCAP_KIND_PICK;
        else if (!strcmp( argv[i], "--app" ) && i + 1 < argc) app = argv[++i];
        else if (!strcmp( argv[i], "--seconds" ) && i + 1 < argc) seconds = atoi( argv[++i] );
        else if (!strcmp( argv[i], "--dump" ) && i + 1 < argc) dir = argv[++i];
        else { fprintf( stderr, "usage: %s [--pick] [--app KEY] [--seconds N] [--dump DIR]\n", argv[0] ); return 2; }
    }

    start = now_ns();
    if (rcap_session_open( kind, app, app, &s )) return 1;
    while (rcap_session_state( s ) == RCAP_STATE_PENDING)
    {
        struct timespec ts = { 0, 20000000 };
        nanosleep( &ts, NULL );
    }
    printf( "state %d after %.0f ms\n", rcap_session_state( s ), (now_ns() - start) / 1e6 );
    if (rcap_session_state( s ) != RCAP_STATE_RUNNING) return 1;

    n = rcap_session_stream_count( s );
    for (i = 0; i < n; i++)
    {
        struct rcap_stream_info info;
        rcap_session_stream_info( s, i, &info );
        printf( "stream %d: node %u %s at %d,%d %dx%d\n", i, info.node_id,
                info.source == RCAP_SOURCE_WINDOW ? "window" : "screen",
                info.rect.x, info.rect.y, info.rect.width, info.rect.height );
    }

    for (i = 0; i < n; i++)
    {
        struct rcap_frame_info info = { 0 };
        uint64_t seq = 0, first = 0;
        int64_t end, lat_sum = 0, lat_max = 0, wake_sum = 0, wake_max = 0, t0 = 0;
        int frames = 0, ret;

        rcap_wait_frame( s, i, 0, -1, 3000, &info );
        seq = info.seq;
        first = seq;
        end = now_ns() + (int64_t)seconds * 1000000000ll;
        while (now_ns() < end)
        {
            if ((ret = rcap_wait_frame( s, i, seq, -1, 500, &info )) == -ETIMEDOUT) continue;
            if (ret) { printf( "stream %d: error %d\n", i, ret ); break; }
            int64_t t = now_ns(), lat = info.arrival_ns - info.pts_ns, wake = t - info.arrival_ns;
            if (!frames) t0 = t;
            frames++;
            if (lat > 0 && lat < 1000000000ll) { lat_sum += lat; if (lat > lat_max) lat_max = lat; }
            wake_sum += wake;
            if (wake > wake_max) wake_max = wake;
            seq = info.seq;
        }
        printf( "stream %d: %dx%d, %d images in %d s (%.1f fps, %llu skipped), "
                "compositor to here %.2f ms avg %.2f max, here to reader %.2f ms avg %.2f max\n",
                i, info.width, info.height, frames, seconds,
                frames > 1 ? (frames - 1) / ((now_ns() - t0) / 1e9) : 0.0,
                (unsigned long long)(seq - first - frames),
                frames ? lat_sum / 1e6 / frames : 0, lat_max / 1e6,
                frames ? wake_sum / 1e6 / frames : 0, wake_max / 1e6 );
        if (dir) dump( s, i, dir );
    }
    rcap_session_unref( s );
    return 0;
}
