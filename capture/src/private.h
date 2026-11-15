/*
 * Copyright 2026 Raye Linux
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef RAYE_CAPTURE_PRIVATE_H
#define RAYE_CAPTURE_PRIVATE_H

#include <stddef.h>
#include <stdint.h>
#include "raye-capture.h"

/* match.c: which shared stream shows which screen */
void rcap_match_monitors( const struct rcap_rect *streams, int stream_count,
                          const struct rcap_rect *monitors, int monitor_count, int *map );
int rcap_workspace_crop( const struct rcap_rect *stream, const struct rcap_rect *monitors, int monitor_count,
                         int monitor, struct rcap_rect *crop );

/* tokens.c: restore tokens, one line "key<TAB>token" per program */
char *rcap_tokens_path( void );
int rcap_tokens_get( const char *path, const char *key, char *token, size_t size );
int rcap_tokens_set( const char *path, const char *key, const char *token );
void rcap_tokens_key( enum rcap_kind kind, const char *app_key, char *key, size_t size );

/* pixels.c */
enum rcap_pixel_order
{
    RCAP_ORDER_BGRX, /* bytes B G R X: what Windows calls BGRA */
    RCAP_ORDER_RGBX,
    RCAP_ORDER_XRGB,
    RCAP_ORDER_XBGR,
};

void rcap_convert_row( uint8_t *dst, const uint8_t *src, int width, enum rcap_pixel_order order );
void rcap_blend_cursor( uint8_t *dst, int dst_stride, int dst_width, int dst_height,
                        const uint8_t *cursor, int cursor_stride, int cursor_width, int cursor_height,
                        int x, int y );

#endif
