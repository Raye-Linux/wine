/*
 * Screen capture for Windows programs: the desktop's screens and windows,
 * through the ScreenCast portal and PipeWire.
 *
 * The Windows layer loads this library on first use (dlopen) from win32u
 * (GDI reads of the desktop) and from rayecap (DXGI duplication). Both end
 * up in the same copy of the library, so a program sees one share picker
 * for all its capture calls, and the choice is remembered for that program
 * with a portal restore token.
 *
 * Nothing here blocks on the user: rcap_session_open() returns at once and
 * the picker runs in the background. Until the user has picked, waits time
 * out and copies return black, the way a Windows program sees a screen
 * that has nothing new to show.
 *
 * Copyright 2026 Raye Linux
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef RAYE_CAPTURE_H
#define RAYE_CAPTURE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RCAP_ABI_VERSION 1

/* what a session asks the portal for */
enum rcap_kind
{
    RCAP_KIND_SCREENS = 1,  /* every screen the program may read (DXGI, GDI) */
    RCAP_KIND_PICK    = 2,  /* one screen or one window, chosen in the picker */
};

enum rcap_state
{
    RCAP_STATE_PENDING = 0, /* waiting for the portal or for the user */
    RCAP_STATE_RUNNING,
    RCAP_STATE_DENIED,      /* the user said no; not asked again in this process */
    RCAP_STATE_CLOSED,      /* sharing stopped from the desktop, or no portal */
};

enum rcap_source
{
    RCAP_SOURCE_MONITOR = 1,
    RCAP_SOURCE_WINDOW  = 2,
};

struct rcap_rect
{
    int x, y, width, height;
};

struct rcap_stream_info
{
    struct rcap_rect rect;  /* place in the desktop, as the portal gives it */
    unsigned int source;    /* enum rcap_source */
    uint32_t node_id;
};

struct rcap_frame_info
{
    int width, height;      /* size of the frame in pixels */
    uint64_t seq;           /* bumps on every new image, never on cursor moves */
    int64_t pts_ns;         /* compositor time of the image, CLOCK_MONOTONIC */
    int64_t arrival_ns;     /* when it reached this process, CLOCK_MONOTONIC */
    int cursor_visible;
    int cursor_x, cursor_y; /* hot spot, in frame pixels */
    int cursor_hot_x, cursor_hot_y;
    int cursor_width, cursor_height;
    uint64_t cursor_seq;    /* bumps when the cursor image changes */
    int64_t cursor_ns;      /* last cursor move */
};

/* rcap_copy_frame() flags */
#define RCAP_COPY_CURSOR 0x1    /* draw the pointer into the copy */

struct rcap_session;

/*
 * Opens (or joins) the session of this kind for this process. app_key names
 * the program for the restore token, usually its Windows path; app_name is
 * what the picker can show. Returns 0 or a negative errno.
 */
int rcap_session_open( enum rcap_kind kind, const char *app_key, const char *app_name,
                       struct rcap_session **session );
void rcap_session_unref( struct rcap_session *session );

enum rcap_state rcap_session_state( struct rcap_session *session );
int rcap_session_stream_count( struct rcap_session *session );
int rcap_session_stream_info( struct rcap_session *session, int index, struct rcap_stream_info *info );

/*
 * The stream that shows the screen at this place of the desktop, or -1.
 * monitors[] are all the screens the program knows, so a screen the user did
 * not share is not mistaken for its neighbour.
 */
int rcap_session_find_stream( struct rcap_session *session, const struct rcap_rect *monitors,
                              int monitor_count, int monitor );

/*
 * Waits for an image newer than after_seq, or, when after_cursor_ns is not
 * -1, for a pointer move after that time. 0, or -ETIMEDOUT, -EACCES, -EPIPE.
 */
int rcap_wait_frame( struct rcap_session *session, int index, uint64_t after_seq, int64_t after_cursor_ns,
                     int timeout_ms, struct rcap_frame_info *info );

/*
 * Copies part of the latest image as 32-bit BGRA (alpha 255). Pixels the
 * stream does not cover are black. 0 or a negative errno.
 */
int rcap_copy_frame( struct rcap_session *session, int index, const struct rcap_rect *src,
                     void *dst, int dst_stride, unsigned int flags, struct rcap_frame_info *info );

/* Copies the pointer image, 32-bit BGRA with straight alpha. */
int rcap_copy_cursor( struct rcap_session *session, int index, void *dst, int dst_stride,
                      int max_width, int max_height, struct rcap_frame_info *info );

/*
 * Whether a GDI read of the screen should bring up the share picker. Programs
 * read the screen for many small reasons at start (Chromium and WebView2 do),
 * so a lone read never asks. It asks when the user has just clicked or typed
 * in the program, and then waits for the answer like a screenshot tool would,
 * or when the program keeps reading like a recorder, without waiting.
 * Start with reads zeroed; times are milliseconds, last_input_ms -1 for none.
 */
struct rcap_gdi_reads
{
    int64_t first_ms, last_ms;
    int count;
};

enum rcap_ask
{
    RCAP_ASK_NOT = 0,
    RCAP_ASK_AND_WAIT,
    RCAP_ASK_IN_BACKGROUND,
};

enum rcap_ask rcap_gdi_read_asks( struct rcap_gdi_reads *reads, int64_t now_ms, int64_t last_input_ms );

#ifdef __cplusplus
}
#endif

#endif
