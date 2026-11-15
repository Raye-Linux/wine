/*
 * Copyright 2026 Raye Linux
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef RAYE_CAPTURE_SESSION_H
#define RAYE_CAPTURE_SESSION_H

#include <pthread.h>
#include <stdint.h>
#include <gio/gio.h>
#include <pipewire/pipewire.h>
#include <spa/param/video/raw.h>

#include "private.h"

struct stream
{
    struct rcap_session *session;
    struct rcap_stream_info info;
    struct pw_stream *pw;
    struct spa_hook listener;
    enum rcap_pixel_order order;
    int have_format;

    /* latest image as BGRA, under session->lock */
    int width, height;                    /* negotiated size */
    uint8_t *pixels;
    size_t alloc;
    int pix_width, pix_height, stride;    /* size of what pixels holds */
    struct rcap_frame_info frame;
    int stale;                            /* kept from before a pause */

    uint8_t *cursor;                      /* BGRA, straight alpha */
    size_t cursor_alloc;
};

/* a screen's part of a stream that holds the whole desktop */
#define RCAP_PART_BASE 0x10000
#define RCAP_MAX_PARTS 16

struct part
{
    int stream;
    struct rcap_rect rect;  /* in the stream's own coordinates (logical, from 0,0) */
};

struct rcap_session
{
    struct rcap_session *next;
    enum rcap_kind kind;
    char *app_key, *app_name;
    int users;
    int paused, pause_pending;
    int64_t last_unref_ns;

    pthread_mutex_t lock;
    pthread_cond_t cond;
    enum rcap_state state;
    struct stream *streams;
    int stream_count;
    int cursor_metadata;
    struct part parts[RCAP_MAX_PARTS];
    int part_count;

    pthread_t thread;
    volatile int quit;
    GMainContext *context;
    GDBusConnection *bus;
    char *session_handle;
    unsigned int closed_sub;

    struct pw_thread_loop *loop;
    struct pw_context *pw_context;
    struct pw_core *core;
};

void rcap_session_set_state( struct rcap_session *s, enum rcap_state state );

int rcap_portal_start( struct rcap_session *s, int *pw_fd );
void rcap_portal_run( struct rcap_session *s );
void rcap_portal_close( struct rcap_session *s );

int rcap_pipewire_start( struct rcap_session *s, int fd );
void rcap_pipewire_apply_pause( struct rcap_session *s );
void rcap_pipewire_stop( struct rcap_session *s );

#endif
