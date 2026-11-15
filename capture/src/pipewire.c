/*
 * PipeWire side: one stream per shared screen or window. Each new image is
 * copied once into the session's latest-frame buffer, as BGRA, which is
 * what both GDI and DXGI hand to Windows programs.
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
#include <spa/buffer/meta.h>
#include <spa/param/video/format-utils.h>
#include <spa/param/buffers.h>
#include <spa/pod/builder.h>
#include <spa/utils/result.h>

#include "session.h"

#define MAX_CURSOR 256
#define CURSOR_META_SIZE(w, h) (sizeof(struct spa_meta_cursor) + sizeof(struct spa_meta_bitmap) + (w) * (h) * 4)

static int64_t now_ns( void )
{
    struct timespec ts;
    clock_gettime( CLOCK_MONOTONIC, &ts );
    return ts.tv_sec * 1000000000ll + ts.tv_nsec;
}

static enum rcap_pixel_order pixel_order( uint32_t format )
{
    switch (format)
    {
    case SPA_VIDEO_FORMAT_RGBx: case SPA_VIDEO_FORMAT_RGBA: return RCAP_ORDER_RGBX;
    case SPA_VIDEO_FORMAT_xRGB: case SPA_VIDEO_FORMAT_ARGB: return RCAP_ORDER_XRGB;
    case SPA_VIDEO_FORMAT_xBGR: case SPA_VIDEO_FORMAT_ABGR: return RCAP_ORDER_XBGR;
    default: return RCAP_ORDER_BGRX;
    }
}

static void on_state_changed( void *data, enum pw_stream_state old, enum pw_stream_state state, const char *error )
{
    struct stream *st = data;

    if (state == PW_STREAM_STATE_ERROR || state == PW_STREAM_STATE_UNCONNECTED)
    {
        if (error) fprintf( stderr, "raye-capture: stream %u: %s\n", st->info.node_id, error );
        rcap_session_set_state( st->session, RCAP_STATE_CLOSED );
    }
}

static void on_param_changed( void *data, uint32_t id, const struct spa_pod *param )
{
    struct stream *st = data;
    struct spa_video_info_raw raw;
    uint8_t buffer[1024];
    struct spa_pod_builder b = SPA_POD_BUILDER_INIT( buffer, sizeof(buffer) );
    const struct spa_pod *params[3];

    if (id != SPA_PARAM_Format || !param) return;
    if (spa_format_video_raw_parse( param, &raw ) < 0) return;

    pthread_mutex_lock( &st->session->lock );
    st->order = pixel_order( raw.format );
    st->width = raw.size.width;
    st->height = raw.size.height;
    st->have_format = 1;
    pthread_mutex_unlock( &st->session->lock );

    params[0] = spa_pod_builder_add_object( &b, SPA_TYPE_OBJECT_ParamBuffers, SPA_PARAM_Buffers,
                    SPA_PARAM_BUFFERS_buffers, SPA_POD_CHOICE_RANGE_Int( 4, 2, 8 ),
                    SPA_PARAM_BUFFERS_dataType, SPA_POD_CHOICE_FLAGS_Int( (1 << SPA_DATA_MemFd) |
                                                                         (1 << SPA_DATA_MemPtr) ) );
    params[1] = spa_pod_builder_add_object( &b, SPA_TYPE_OBJECT_ParamMeta, SPA_PARAM_Meta,
                    SPA_PARAM_META_type, SPA_POD_Id( SPA_META_Header ),
                    SPA_PARAM_META_size, SPA_POD_Int( sizeof(struct spa_meta_header) ) );
    params[2] = spa_pod_builder_add_object( &b, SPA_TYPE_OBJECT_ParamMeta, SPA_PARAM_Meta,
                    SPA_PARAM_META_type, SPA_POD_Id( SPA_META_Cursor ),
                    SPA_PARAM_META_size, SPA_POD_CHOICE_RANGE_Int( CURSOR_META_SIZE( 64, 64 ),
                                                                   CURSOR_META_SIZE( 1, 1 ),
                                                                   CURSOR_META_SIZE( MAX_CURSOR, MAX_CURSOR ) ) );
    pw_stream_update_params( st->pw, params, 3 );
}

static void read_cursor( struct stream *st, struct spa_buffer *buf, int64_t now )
{
    struct spa_meta *meta;
    struct spa_meta_cursor *mc;
    struct spa_meta_bitmap *bm;
    int y;

    if (!(meta = spa_buffer_find_meta( buf, SPA_META_Cursor )) || meta->size < sizeof(*mc)) return;
    mc = meta->data;
    if (!spa_meta_cursor_is_valid( mc ))
    {
        st->frame.cursor_visible = 0;
        return;
    }
    st->frame.cursor_visible = 1;
    st->frame.cursor_x = mc->position.x;
    st->frame.cursor_y = mc->position.y;
    st->frame.cursor_ns = now;
    if (!mc->bitmap_offset) return;

    if ((uint64_t)mc->bitmap_offset + sizeof(*bm) > meta->size) return;
    bm = SPA_PTROFF( mc, mc->bitmap_offset, struct spa_meta_bitmap );
    if (bm->size.width && bm->size.height &&
        (bm->stride < (int32_t)bm->size.width * 4 ||
         (uint64_t)mc->bitmap_offset + bm->offset + (uint64_t)bm->stride * bm->size.height > meta->size))
        return;
    if (!spa_meta_bitmap_is_valid( bm ) || !bm->size.width || !bm->size.height ||
        bm->size.width > MAX_CURSOR || bm->size.height > MAX_CURSOR)
    {
        /* a valid cursor without an image means it is hidden, a text field for example */
        if (bm->size.width == 0) st->frame.cursor_visible = 0;
        return;
    }
    if (st->cursor_alloc < bm->size.width * bm->size.height * 4)
    {
        free( st->cursor );
        st->cursor_alloc = bm->size.width * bm->size.height * 4;
        if (!(st->cursor = malloc( st->cursor_alloc )))
        {
            st->cursor_alloc = 0;
            return;
        }
    }
    for (y = 0; y < (int)bm->size.height; y++)
        rcap_convert_row( st->cursor + y * bm->size.width * 4,
                          SPA_PTROFF( bm, bm->offset + y * bm->stride, uint8_t ),
                          bm->size.width, pixel_order( bm->format ) );
    /* rcap_convert_row makes it opaque: put the cursor's own alpha back */
    if (bm->format == SPA_VIDEO_FORMAT_RGBA || bm->format == SPA_VIDEO_FORMAT_BGRA ||
        bm->format == SPA_VIDEO_FORMAT_ARGB || bm->format == SPA_VIDEO_FORMAT_ABGR)
    {
        int alpha = bm->format == SPA_VIDEO_FORMAT_ARGB || bm->format == SPA_VIDEO_FORMAT_ABGR ? 0 : 3, x;
        for (y = 0; y < (int)bm->size.height; y++)
        {
            const uint8_t *src = SPA_PTROFF( bm, bm->offset + y * bm->stride, uint8_t );
            for (x = 0; x < (int)bm->size.width; x++)
                st->cursor[(y * bm->size.width + x) * 4 + 3] = src[x * 4 + alpha];
        }
    }
    st->frame.cursor_width = bm->size.width;
    st->frame.cursor_height = bm->size.height;
    st->frame.cursor_hot_x = mc->hotspot.x;
    st->frame.cursor_hot_y = mc->hotspot.y;
    st->frame.cursor_seq++;
}

static void on_process( void *data )
{
    struct stream *st = data;
    struct pw_buffer *b = NULL, *next;
    struct spa_meta_header *header;
    struct spa_buffer *buf;
    struct spa_data *d;
    int64_t now;

    /* only the newest image matters: hand older ones straight back */
    while ((next = pw_stream_dequeue_buffer( st->pw )))
    {
        if (b) pw_stream_queue_buffer( st->pw, b );
        b = next;
    }
    if (!b) return;

    now = now_ns();
    buf = b->buffer;
    d = &buf->datas[0];
    header = spa_buffer_find_meta_data( buf, SPA_META_Header, sizeof(*header) );

    pthread_mutex_lock( &st->session->lock );
    read_cursor( st, buf, now );

    if (d->data && d->chunk->size && !(header && (header->flags & SPA_META_HEADER_FLAG_CORRUPTED)) &&
        !(d->chunk->flags & SPA_CHUNK_FLAG_CORRUPTED))
    {
        int width = st->width, height = st->height, stride = d->chunk->stride, y;

        if (!stride) stride = width * 4;
        if (width && height && stride >= width * 4 &&
            (uint64_t)d->chunk->offset + (uint64_t)stride * height <= d->maxsize)
        {
            size_t need = (size_t)width * height * 4;
            if (st->alloc < need)
            {
                free( st->pixels );
                st->pixels = malloc( need );
                st->alloc = st->pixels ? need : 0;
            }
            if (st->pixels)
            {
                const uint8_t *src = SPA_PTROFF( d->data, d->chunk->offset, uint8_t );
                for (y = 0; y < height; y++)
                    rcap_convert_row( st->pixels + (size_t)y * width * 4, src + (size_t)y * stride, width, st->order );
                st->stride = width * 4;
                st->frame.width = st->pix_width = width;
                st->frame.height = st->pix_height = height;
                st->stale = 0;
                st->frame.seq++;
                st->frame.pts_ns = header ? header->pts : now;
                st->frame.arrival_ns = now;
            }
        }
    }
    pthread_cond_broadcast( &st->session->cond );
    pthread_mutex_unlock( &st->session->lock );

    pw_stream_queue_buffer( st->pw, b );
}

static const struct pw_stream_events stream_events =
{
    PW_VERSION_STREAM_EVENTS,
    .state_changed = on_state_changed,
    .param_changed = on_param_changed,
    .process = on_process,
};

static int connect_stream( struct rcap_session *s, struct stream *st )
{
    uint8_t buffer[1024];
    struct spa_pod_builder b = SPA_POD_BUILDER_INIT( buffer, sizeof(buffer) );
    const struct spa_pod *params[1];

    st->pw = pw_stream_new( s->core, "Windows program capture",
                            pw_properties_new( PW_KEY_MEDIA_TYPE, "Video", PW_KEY_MEDIA_CATEGORY, "Capture",
                                               PW_KEY_MEDIA_ROLE, "Screen", NULL ) );
    if (!st->pw) return -ENOMEM;
    pw_stream_add_listener( st->pw, &st->listener, &stream_events, st );

    params[0] = spa_pod_builder_add_object( &b, SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat,
                    SPA_FORMAT_mediaType, SPA_POD_Id( SPA_MEDIA_TYPE_video ),
                    SPA_FORMAT_mediaSubtype, SPA_POD_Id( SPA_MEDIA_SUBTYPE_raw ),
                    SPA_FORMAT_VIDEO_format, SPA_POD_CHOICE_ENUM_Id( 7, SPA_VIDEO_FORMAT_BGRx,
                        SPA_VIDEO_FORMAT_BGRx, SPA_VIDEO_FORMAT_BGRA, SPA_VIDEO_FORMAT_RGBx,
                        SPA_VIDEO_FORMAT_RGBA, SPA_VIDEO_FORMAT_xRGB, SPA_VIDEO_FORMAT_xBGR ),
                    SPA_FORMAT_VIDEO_size, SPA_POD_CHOICE_RANGE_Rectangle( &SPA_RECTANGLE( 1920, 1080 ),
                        &SPA_RECTANGLE( 1, 1 ), &SPA_RECTANGLE( 16384, 16384 ) ),
                    SPA_FORMAT_VIDEO_framerate, SPA_POD_CHOICE_RANGE_Fraction( &SPA_FRACTION( 60, 1 ),
                        &SPA_FRACTION( 0, 1 ), &SPA_FRACTION( 360, 1 ) ) );

    return pw_stream_connect( st->pw, PW_DIRECTION_INPUT, st->info.node_id,
                              PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS, params, 1 );
}

int rcap_pipewire_start( struct rcap_session *s, int fd )
{
    int i, ret = 0;

    pw_init( NULL, NULL );
    if (!(s->loop = pw_thread_loop_new( "raye-capture", NULL ))) { close( fd ); return -ENOMEM; }
    pw_thread_loop_lock( s->loop );
    s->pw_context = pw_context_new( pw_thread_loop_get_loop( s->loop ), NULL, 0 );
    if (!s->pw_context)
    {
        pw_thread_loop_unlock( s->loop );
        close( fd );
        return -EPIPE;
    }
    /* the core owns fd from here, even when it fails */
    if (!(s->core = pw_context_connect_fd( s->pw_context, fd, NULL, 0 )))
    {
        pw_thread_loop_unlock( s->loop );
        return -EPIPE;
    }
    for (i = 0; i < s->stream_count && !ret; i++) ret = connect_stream( s, &s->streams[i] );
    if (!ret) ret = pw_thread_loop_start( s->loop );
    pw_thread_loop_unlock( s->loop );
    return ret;
}

/*
 * Reads the wanted state under the loop lock, so two callers racing (a pause
 * timer and a new reader) cannot leave the streams in the wrong state.
 */
void rcap_pipewire_apply_pause( struct rcap_session *s )
{
    int i, active;

    if (!s->loop) return;
    pw_thread_loop_lock( s->loop );
    pthread_mutex_lock( &s->lock );
    active = !s->paused;
    pthread_mutex_unlock( &s->lock );
    for (i = 0; i < s->stream_count; i++) if (s->streams[i].pw) pw_stream_set_active( s->streams[i].pw, active );
    pw_thread_loop_unlock( s->loop );
}

void rcap_pipewire_stop( struct rcap_session *s )
{
    int i;

    if (!s->loop) return;
    pw_thread_loop_lock( s->loop );
    for (i = 0; i < s->stream_count; i++) if (s->streams[i].pw) pw_stream_destroy( s->streams[i].pw );
    pw_thread_loop_unlock( s->loop );
    pw_thread_loop_stop( s->loop );
    if (s->core) pw_core_disconnect( s->core );
    if (s->pw_context) pw_context_destroy( s->pw_context );
    pw_thread_loop_destroy( s->loop );
    s->loop = NULL;
}
