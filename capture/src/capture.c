/*
 * Sessions: one per kind and per process, shared by every caller, kept
 * until the process ends so the user is never asked twice in one run.
 *
 * Copyright 2026 Raye Linux
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "session.h"

#define IDLE_PAUSE_MS 3000

static pthread_mutex_t sessions_lock = PTHREAD_MUTEX_INITIALIZER;
static struct rcap_session *sessions;

void rcap_session_set_state( struct rcap_session *s, enum rcap_state state )
{
    pthread_mutex_lock( &s->lock );
    /* a denied or stopped share stays that way */
    if (s->state == RCAP_STATE_PENDING || s->state == RCAP_STATE_RUNNING) s->state = state;
    pthread_cond_broadcast( &s->cond );
    pthread_mutex_unlock( &s->lock );
}

static void *session_thread( void *arg )
{
    struct rcap_session *s = arg;
    int fd = -1, ret;

    g_main_context_push_thread_default( s->context );
    ret = rcap_portal_start( s, &fd );
    if (!ret) ret = rcap_pipewire_start( s, fd );

    if (ret == -EACCES) rcap_session_set_state( s, RCAP_STATE_DENIED );
    else if (ret) rcap_session_set_state( s, RCAP_STATE_CLOSED );
    else
    {
        int idle;

        pthread_mutex_lock( &s->lock );
        idle = !s->users;
        s->paused = idle;
        pthread_mutex_unlock( &s->lock );
        if (idle) rcap_pipewire_apply_pause( s );
        rcap_session_set_state( s, RCAP_STATE_RUNNING );
    }

    rcap_portal_run( s );
    rcap_pipewire_stop( s );
    rcap_portal_close( s );
    g_main_context_pop_thread_default( s->context );
    return NULL;
}

static void stop_all( void )
{
    struct rcap_session *s;

    pthread_mutex_lock( &sessions_lock );
    for (s = sessions; s; s = s->next)
    {
        s->quit = 1;
        g_main_context_wakeup( s->context );
        pthread_join( s->thread, NULL );
    }
    sessions = NULL;
    pthread_mutex_unlock( &sessions_lock );
}

static struct rcap_session *session_new( enum rcap_kind kind, const char *app_key, const char *app_name )
{
    static int registered;
    struct rcap_session *s = calloc( 1, sizeof(*s) );
    pthread_condattr_t attr;

    if (!s) return NULL;
    s->kind = kind;
    s->app_key = strdup( app_key ? app_key : "" );
    s->app_name = strdup( app_name ? app_name : "" );
    pthread_mutex_init( &s->lock, NULL );
    pthread_condattr_init( &attr );
    pthread_condattr_setclock( &attr, CLOCK_MONOTONIC );
    pthread_cond_init( &s->cond, &attr );
    pthread_condattr_destroy( &attr );
    s->context = g_main_context_new();
    if (pthread_create( &s->thread, NULL, session_thread, s ))
    {
        g_main_context_unref( s->context );
        free( s->app_key );
        free( s->app_name );
        free( s );
        return NULL;
    }
    if (!registered++) atexit( stop_all );
    return s;
}

static gboolean pause_if_idle( gpointer data )
{
    struct rcap_session *s = data;
    int pause;

    struct timespec ts;
    int64_t now, left;

    clock_gettime( CLOCK_MONOTONIC, &ts );
    now = ts.tv_sec * 1000000000ll + ts.tv_nsec;
    pthread_mutex_lock( &s->lock );
    /* a reader came and went since this timer started: wait for its own three seconds */
    left = s->last_unref_ns + IDLE_PAUSE_MS * 1000000ll - now;
    if (!s->users && left > 0)
    {
        GSource *source = g_timeout_source_new( left / 1000000 + 1 );
        g_source_set_callback( source, pause_if_idle, s, NULL );
        g_source_attach( source, s->context );
        g_source_unref( source );
        pthread_mutex_unlock( &s->lock );
        return G_SOURCE_REMOVE;
    }
    pause = !s->users && !s->paused && s->state == RCAP_STATE_RUNNING;
    if (pause) s->paused = 1;
    s->pause_pending = 0;
    pthread_mutex_unlock( &s->lock );
    if (pause) rcap_pipewire_apply_pause( s );
    return G_SOURCE_REMOVE;
}

int rcap_session_open( enum rcap_kind kind, const char *app_key, const char *app_name,
                       struct rcap_session **session )
{
    struct rcap_session *s;
    int resume;

    pthread_mutex_lock( &sessions_lock );
    for (s = sessions; s; s = s->next) if (s->kind == kind) break;
    if (!s)
    {
        if (!(s = session_new( kind, app_key, app_name )))
        {
            pthread_mutex_unlock( &sessions_lock );
            return -ENOMEM;
        }
        s->next = sessions;
        sessions = s;
    }
    pthread_mutex_unlock( &sessions_lock );

    pthread_mutex_lock( &s->lock );
    s->users++;
    if ((resume = s->paused && s->state == RCAP_STATE_RUNNING))
    {
        int i;
        /* what was kept from before the pause is old: readers wait for a fresh one */
        for (i = 0; i < s->stream_count; i++) s->streams[i].stale = 1;
        s->paused = 0;
    }
    pthread_mutex_unlock( &s->lock );
    if (resume) rcap_pipewire_apply_pause( s );
    *session = s;
    return 0;
}

/*
 * Streams pause a few seconds after the last reader goes, so an idle
 * program costs nothing, and one that reads in bursts (a GDI capture loop
 * opens and drops the session on every read) does not stop and restart them.
 */
void rcap_session_unref( struct rcap_session *s )
{
    int schedule;

    if (!s) return;
    pthread_mutex_lock( &s->lock );
    schedule = s->users && !--s->users && !s->pause_pending;
    if (schedule) s->pause_pending = 1;
    if (!s->users)
    {
        struct timespec ts;
        clock_gettime( CLOCK_MONOTONIC, &ts );
        s->last_unref_ns = ts.tv_sec * 1000000000ll + ts.tv_nsec;
    }
    pthread_mutex_unlock( &s->lock );
    if (schedule)
    {
        GSource *source = g_timeout_source_new( IDLE_PAUSE_MS );
        g_source_set_callback( source, pause_if_idle, s, NULL );
        g_source_attach( source, s->context );
        g_source_unref( source );
    }
}

enum rcap_state rcap_session_state( struct rcap_session *s )
{
    enum rcap_state state;

    pthread_mutex_lock( &s->lock );
    state = s->state;
    pthread_mutex_unlock( &s->lock );
    return state;
}

int rcap_session_stream_count( struct rcap_session *s )
{
    return rcap_session_state( s ) == RCAP_STATE_RUNNING ? s->stream_count : 0;
}

/*
 * Indexes from RCAP_PART_BASE up name a screen's part of a desktop-wide
 * stream. Under s->lock: gives the real stream and the part in frame pixels
 * (the whole frame for a plain index). -1 if the index means nothing.
 */
static int resolve( struct rcap_session *s, int index, struct rcap_rect *px )
{
    const struct part *part;
    const struct stream *st;
    int w, h;

    if (index >= 0 && index < s->stream_count)
    {
        st = &s->streams[index];
        px->x = px->y = 0;
        px->width = st->pix_width;
        px->height = st->pix_height;
        return index;
    }
    if (index < RCAP_PART_BASE || index - RCAP_PART_BASE >= s->part_count) return -1;
    part = &s->parts[index - RCAP_PART_BASE];
    st = &s->streams[part->stream];
    w = st->info.rect.width;
    h = st->info.rect.height;
    px->x = (int64_t)part->rect.x * st->pix_width / w;
    px->y = (int64_t)part->rect.y * st->pix_height / h;
    px->width = (int64_t)part->rect.width * st->pix_width / w;
    px->height = (int64_t)part->rect.height * st->pix_height / h;
    /* rounding must not reach past the image */
    if (px->x + px->width > st->pix_width) px->width = st->pix_width - px->x;
    if (px->y + px->height > st->pix_height) px->height = st->pix_height - px->y;
    if (px->width < 0) px->width = 0;
    if (px->height < 0) px->height = 0;
    return part->stream;
}

/* what a reader of the part sees: its size and the pointer moved into it */
static void part_frame( const struct rcap_rect *px, struct rcap_frame_info *info )
{
    info->width = px->width;
    info->height = px->height;
    info->cursor_x -= px->x;
    info->cursor_y -= px->y;
    if (info->cursor_x < 0 || info->cursor_y < 0 || info->cursor_x >= px->width || info->cursor_y >= px->height)
        info->cursor_visible = 0;
}

int rcap_session_stream_info( struct rcap_session *s, int index, struct rcap_stream_info *info )
{
    int ret = 0;

    if (!rcap_session_stream_count( s )) return -EINVAL;
    pthread_mutex_lock( &s->lock );
    if (index >= 0 && index < s->stream_count) *info = s->streams[index].info;
    else if (index >= RCAP_PART_BASE && index - RCAP_PART_BASE < s->part_count)
    {
        const struct part *part = &s->parts[index - RCAP_PART_BASE];
        *info = s->streams[part->stream].info;
        info->rect.x += part->rect.x;
        info->rect.y += part->rect.y;
        info->rect.width = part->rect.width;
        info->rect.height = part->rect.height;
    }
    else ret = -EINVAL;
    pthread_mutex_unlock( &s->lock );
    return ret;
}

/* the index of this part of that stream, made once and kept for the session */
static int part_index( struct rcap_session *s, int stream, const struct rcap_rect *rect )
{
    int i, ret = -1;

    pthread_mutex_lock( &s->lock );
    for (i = 0; i < s->part_count; i++)
        if (s->parts[i].stream == stream && !memcmp( &s->parts[i].rect, rect, sizeof(*rect) )) break;
    if (i == s->part_count && i < RCAP_MAX_PARTS)
    {
        s->parts[i].stream = stream;
        s->parts[i].rect = *rect;
        s->part_count++;
    }
    if (i < s->part_count) ret = RCAP_PART_BASE + i;
    pthread_mutex_unlock( &s->lock );
    return ret;
}

int rcap_session_find_stream( struct rcap_session *s, const struct rcap_rect *monitors,
                              int monitor_count, int monitor )
{
    struct rcap_rect *rects;
    int count = rcap_session_stream_count( s ), *map, i, ret;

    if (monitor < 0 || monitor >= monitor_count || !count) return -1;
    rects = malloc( count * sizeof(*rects) );
    map = malloc( monitor_count * sizeof(*map) );
    if (!rects || !map)
    {
        free( rects );
        free( map );
        return -1;
    }
    for (i = 0; i < count; i++) rects[i] = s->streams[i].info.rect;
    rcap_match_monitors( rects, count, monitors, monitor_count, map );
    ret = map[monitor];
    for (i = 0; i < count; i++)
    {
        struct rcap_rect crop;
        if (s->streams[i].info.source != RCAP_SOURCE_MONITOR) continue;
        if (!rcap_workspace_crop( &rects[i], monitors, monitor_count, monitor, &crop )) continue;
        /* a screen shared on its own wins over its part of the desktop */
        if (ret >= 0 && ret != i) break;
        ret = part_index( s, i, &crop );
        break;
    }
    free( rects );
    free( map );
    return ret;
}

static int check_state( struct rcap_session *s )
{
    switch (s->state)
    {
    case RCAP_STATE_DENIED: return -EACCES;
    case RCAP_STATE_CLOSED: return -EPIPE;
    default: return 0;
    }
}

int rcap_wait_frame( struct rcap_session *s, int index, uint64_t after_seq, int64_t after_cursor_ns,
                     int timeout_ms, struct rcap_frame_info *info )
{
    struct timespec deadline;
    int ret = 0;

    clock_gettime( CLOCK_MONOTONIC, &deadline );
    deadline.tv_sec += timeout_ms / 1000;
    deadline.tv_nsec += (timeout_ms % 1000) * 1000000l;
    if (deadline.tv_nsec >= 1000000000l) { deadline.tv_sec++; deadline.tv_nsec -= 1000000000l; }

    struct rcap_rect px;
    int real = -1;

    pthread_mutex_lock( &s->lock );
    for (;;)
    {
        if ((ret = check_state( s ))) break;
        if (s->state == RCAP_STATE_RUNNING)
        {
            if ((real = resolve( s, index, &px )) < 0) { ret = -EINVAL; break; }
            if (s->streams[real].frame.seq > after_seq && !s->streams[real].stale) break;
            if (after_cursor_ns != -1 && s->streams[real].frame.cursor_ns > after_cursor_ns) break;
        }
        if (timeout_ms < 0) pthread_cond_wait( &s->cond, &s->lock );
        else if (pthread_cond_timedwait( &s->cond, &s->lock, &deadline ) == ETIMEDOUT) { ret = -ETIMEDOUT; break; }
    }
    if (info && s->state == RCAP_STATE_RUNNING && (real = resolve( s, index, &px )) >= 0)
    {
        *info = s->streams[real].frame;
        if (index != real) part_frame( &px, info );
    }
    pthread_mutex_unlock( &s->lock );
    return ret;
}

int rcap_copy_frame( struct rcap_session *s, int index, const struct rcap_rect *src,
                     void *dst, int dst_stride, unsigned int flags, struct rcap_frame_info *info )
{
    struct rcap_rect px;
    struct stream *st;
    int y, x0, x1, real, ret;

    for (y = 0; y < src->height; y++) memset( (uint8_t *)dst + (size_t)y * dst_stride, 0, src->width * 4 );

    pthread_mutex_lock( &s->lock );
    if ((ret = check_state( s ))) goto done;
    if (s->state != RCAP_STATE_RUNNING) { ret = -EAGAIN; goto done; }
    if ((real = resolve( s, index, &px )) < 0) { ret = -EINVAL; goto done; }
    st = &s->streams[real];
    if (info)
    {
        *info = st->frame;
        if (index != real) part_frame( &px, info );
    }
    if (!st->frame.seq) { ret = -EAGAIN; goto done; }

    /* src is in the part's coordinates; only the part's own pixels are read */
    x0 = src->x < 0 ? -src->x : 0;
    x1 = src->x + src->width > px.width ? px.width - src->x : src->width;
    for (y = 0; y < src->height && x1 > x0; y++)
    {
        int sy = src->y + y;
        if (sy < 0 || sy >= px.height) continue;
        memcpy( (uint8_t *)dst + (size_t)y * dst_stride + x0 * 4,
                st->pixels + (size_t)(px.y + sy) * st->stride + (size_t)(px.x + src->x + x0) * 4, (x1 - x0) * 4 );
    }
    if ((flags & RCAP_COPY_CURSOR) && st->frame.cursor_visible && st->cursor)
        rcap_blend_cursor( dst, dst_stride, src->width, src->height, st->cursor, st->frame.cursor_width * 4,
                           st->frame.cursor_width, st->frame.cursor_height,
                           st->frame.cursor_x - px.x - st->frame.cursor_hot_x - src->x,
                           st->frame.cursor_y - px.y - st->frame.cursor_hot_y - src->y );
done:
    pthread_mutex_unlock( &s->lock );
    return ret;
}

int rcap_copy_cursor( struct rcap_session *s, int index, void *dst, int dst_stride,
                      int max_width, int max_height, struct rcap_frame_info *info )
{
    struct rcap_rect px;
    struct stream *st;
    int y, real, ret;

    pthread_mutex_lock( &s->lock );
    if ((ret = check_state( s ))) goto done;
    if (s->state != RCAP_STATE_RUNNING || (real = resolve( s, index, &px )) < 0) { ret = -EINVAL; goto done; }
    st = &s->streams[real];
    if (info)
    {
        *info = st->frame;
        if (index != real) part_frame( &px, info );
    }
    if (!st->cursor) { ret = -ENOENT; goto done; }
    if (st->frame.cursor_width > max_width || st->frame.cursor_height > max_height) { ret = -ENOSPC; goto done; }
    for (y = 0; y < st->frame.cursor_height; y++)
        memcpy( (uint8_t *)dst + (size_t)y * dst_stride, st->cursor + (size_t)y * st->frame.cursor_width * 4,
                st->frame.cursor_width * 4 );
done:
    pthread_mutex_unlock( &s->lock );
    return ret;
}
