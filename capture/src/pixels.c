/*
 * Copyright 2026 Raye Linux
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <string.h>

#include "private.h"

void rcap_convert_row( uint8_t *dst, const uint8_t *src, int width, enum rcap_pixel_order order )
{
    int i;

    switch (order)
    {
    case RCAP_ORDER_BGRX:
        for (i = 0; i < width; i++, dst += 4, src += 4)
        {
            dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2]; dst[3] = 255;
        }
        break;
    case RCAP_ORDER_RGBX:
        for (i = 0; i < width; i++, dst += 4, src += 4)
        {
            dst[0] = src[2]; dst[1] = src[1]; dst[2] = src[0]; dst[3] = 255;
        }
        break;
    case RCAP_ORDER_XRGB:
        for (i = 0; i < width; i++, dst += 4, src += 4)
        {
            dst[0] = src[3]; dst[1] = src[2]; dst[2] = src[1]; dst[3] = 255;
        }
        break;
    case RCAP_ORDER_XBGR:
        for (i = 0; i < width; i++, dst += 4, src += 4)
        {
            dst[0] = src[1]; dst[1] = src[2]; dst[2] = src[3]; dst[3] = 255;
        }
        break;
    }
}

/* cursor is BGRA with straight alpha, (x, y) is its top left corner */
void rcap_blend_cursor( uint8_t *dst, int dst_stride, int dst_width, int dst_height,
                        const uint8_t *cursor, int cursor_stride, int cursor_width, int cursor_height,
                        int x, int y )
{
    int cx0 = x < 0 ? -x : 0, cy0 = y < 0 ? -y : 0;
    int cx1 = x + cursor_width > dst_width ? dst_width - x : cursor_width;
    int cy1 = y + cursor_height > dst_height ? dst_height - y : cursor_height;
    int cx, cy, c;

    for (cy = cy0; cy < cy1; cy++)
    {
        const uint8_t *s = cursor + cy * cursor_stride + cx0 * 4;
        uint8_t *d = dst + (y + cy) * dst_stride + (x + cx0) * 4;

        for (cx = cx0; cx < cx1; cx++, s += 4, d += 4)
        {
            unsigned int a = s[3];
            if (!a) continue;
            if (a == 255) { d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; continue; }
            for (c = 0; c < 3; c++) d[c] = (s[c] * a + d[c] * (255 - a) + 127) / 255;
        }
    }
}
