/*
 * When a GDI read of the screen may bring up the share picker.
 *
 * Copyright 2026 Raye Linux
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "private.h"

/* a click or a key this recent counts as the user asking for the screen;
   long enough for the 3 second delay screenshot tools offer */
#define INPUT_MS        5000
/* reads closer than this belong to one run */
#define RUN_GAP_MS      1000
/* a run this long and this busy is a recorder, not a start-up probe */
#define RUN_MS          2000
#define RUN_READS       10

enum rcap_ask rcap_gdi_read_asks( struct rcap_gdi_reads *reads, int64_t now_ms, int64_t last_input_ms )
{
    if (!reads->count || now_ms - reads->last_ms > RUN_GAP_MS)
    {
        reads->first_ms = now_ms;
        reads->count = 0;
    }
    reads->last_ms = now_ms;
    reads->count++;

    if (last_input_ms >= 0 && now_ms >= last_input_ms && now_ms - last_input_ms <= INPUT_MS)
        return RCAP_ASK_AND_WAIT;
    if (reads->count >= RUN_READS && now_ms - reads->first_ms >= RUN_MS)
        return RCAP_ASK_IN_BACKGROUND;
    return RCAP_ASK_NOT;
}
