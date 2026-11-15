/*
 * Tests for the parts that do not need a portal: screen matching,
 * restore tokens and pixel work.
 *
 * Copyright 2026 Raye Linux
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "private.h"

static int failures;

#define check(cond) do { if (!(cond)) { fprintf( stderr, "%s:%d: %s\n", __FILE__, __LINE__, #cond ); failures++; } } while (0)

static void test_match_exact(void)
{
    /* the test VM: 1280x800 on the left, 1920x1080 on the right */
    struct rcap_rect streams[] = { {1280, 0, 1920, 1080}, {0, 0, 1280, 800} };
    struct rcap_rect monitors[] = { {1280, 0, 1920, 1080}, {0, 0, 1280, 800} };
    int map[2];

    rcap_match_monitors( streams, 2, monitors, 2, map );
    check( map[0] == 0 );
    check( map[1] == 1 );
}

static void test_match_windows_coords(void)
{
    /* what the program sees on the test VM: main screen at 0,0, the other on its left */
    struct rcap_rect streams[] = { {0, 0, 1280, 800}, {1280, 0, 1920, 1080} };
    struct rcap_rect monitors[] = { {0, 0, 1920, 1080}, {-1280, 0, 1280, 800} };
    int map[2];

    rcap_match_monitors( streams, 2, monitors, 2, map );
    check( map[0] == 1 );
    check( map[1] == 0 );

    /* only the main screen shared */
    rcap_match_monitors( streams + 1, 1, monitors, 2, map );
    check( map[0] == 0 );
    check( map[1] == -1 );

    /* only the left screen shared */
    rcap_match_monitors( streams, 1, monitors, 2, map );
    check( map[0] == -1 );
    check( map[1] == 0 );
}

static void test_match_unshared(void)
{
    /* only the right screen was shared: the left one must not get it */
    struct rcap_rect streams[] = { {1280, 0, 1920, 1080} };
    struct rcap_rect monitors[] = { {0, 0, 1280, 800}, {1280, 0, 1920, 1080} };
    int map[2];

    rcap_match_monitors( streams, 1, monitors, 2, map );
    check( map[0] == -1 );
    check( map[1] == 0 );
}

static void test_match_scaled(void)
{
    /* the program sees physical pixels, the portal logical ones (150 % on the right) */
    struct rcap_rect streams[] = { {0, 0, 1280, 800}, {1280, 0, 1707, 960} };
    struct rcap_rect monitors[] = { {0, 0, 1280, 800}, {1280, 0, 2560, 1440} };
    int map[2];

    rcap_match_monitors( streams, 2, monitors, 2, map );
    check( map[0] == 0 );
    check( map[1] == 1 );
}

static void test_match_no_overlap(void)
{
    /* coordinates that share nothing: fall back to left-to-right order */
    struct rcap_rect streams[] = { {5000, 0, 800, 600}, {3000, 0, 800, 600}, {4000, 0, 800, 600} };
    struct rcap_rect monitors[] = { {-800, 0, 800, 600}, {800, 0, 800, 600}, {0, 0, 800, 600} };
    int map[3];

    rcap_match_monitors( streams, 3, monitors, 3, map );
    check( map[0] == 1 );
    check( map[2] == 2 );
    check( map[1] == 0 );
}

static void test_match_ten_screens(void)
{
    struct rcap_rect streams[10], monitors[10];
    int map[10], i;

    for (i = 0; i < 10; i++)
    {
        streams[9 - i] = (struct rcap_rect){ i * 1920, 0, 1920, 1080 };
        monitors[i] = (struct rcap_rect){ i * 1920, 0, 1920, 1080 };
    }
    rcap_match_monitors( streams, 10, monitors, 10, map );
    for (i = 0; i < 10; i++) check( map[i] == 9 - i );
}

static void test_workspace(void)
{
    /* the test VM, "Share full Workspace": one 3200x1080 stream for both screens */
    struct rcap_rect ws = { 0, 0, 3200, 1080 };
    struct rcap_rect monitors[] = { {0, 0, 1280, 800}, {1280, 0, 1920, 1080} };
    /* the same, as the program sees it: main screen at 0,0, the other on its left */
    struct rcap_rect win[] = { {0, 0, 1920, 1080}, {-1280, 0, 1280, 800} };
    struct rcap_rect one = { 1280, 0, 1920, 1080 }, half = { 0, 0, 1600, 540 }, crop;

    check( rcap_workspace_crop( &ws, monitors, 2, 0, &crop ) );
    check( crop.x == 0 && crop.y == 0 && crop.width == 1280 && crop.height == 800 );
    check( rcap_workspace_crop( &ws, monitors, 2, 1, &crop ) );
    check( crop.x == 1280 && crop.y == 0 && crop.width == 1920 && crop.height == 1080 );
    check( rcap_workspace_crop( &ws, win, 2, 0, &crop ) );
    check( crop.x == 1280 && crop.width == 1920 );
    check( rcap_workspace_crop( &ws, win, 2, 1, &crop ) );
    check( crop.x == 0 && crop.width == 1280 && crop.height == 800 );

    /* the portal counts logical pixels at 200 %: the parts are halved */
    check( rcap_workspace_crop( &half, monitors, 2, 1, &crop ) );
    check( crop.x == 640 && crop.width == 960 && crop.height == 540 );

    /* a single screen is never taken for the desktop */
    check( !rcap_workspace_crop( &one, monitors, 2, 1, &crop ) );
    check( !rcap_workspace_crop( &one, monitors, 2, 0, &crop ) );
    /* one screen only: nothing to cut */
    check( !rcap_workspace_crop( &ws, monitors + 1, 1, 0, &crop ) );

    /* four equal screens in a square: the desktop has a screen's shape, so stay safe */
    {
        struct rcap_rect grid[] = { {0, 0, 1920, 1080}, {1920, 0, 1920, 1080}, {0, 1080, 1920, 1080}, {1920, 1080, 1920, 1080} };
        struct rcap_rect all = { 0, 0, 3840, 2160 };
        check( !rcap_workspace_crop( &grid[0], grid, 4, 0, &crop ) );
        check( !rcap_workspace_crop( &all, grid, 4, 3, &crop ) );
    }
}

static void test_tokens(void)
{
    char dir[] = "/tmp/rcap-test-XXXXXX", path[256], token[128], key[256];
    struct stat st;

    check( mkdtemp( dir ) != NULL );
    snprintf( path, sizeof(path), "%s/sub/restore-tokens", dir );

    check( rcap_tokens_get( path, "a", token, sizeof(token) ) < 0 );
    check( rcap_tokens_set( path, "a", "token-a" ) == 0 );
    check( rcap_tokens_set( path, "b", "token-b" ) == 0 );
    check( rcap_tokens_set( path, "a", "token-a2" ) == 0 );
    check( rcap_tokens_get( path, "a", token, sizeof(token) ) == 0 && !strcmp( token, "token-a2" ) );
    check( rcap_tokens_get( path, "b", token, sizeof(token) ) == 0 && !strcmp( token, "token-b" ) );
    check( stat( path, &st ) == 0 && (st.st_mode & 0777) == 0600 );

    /* an empty token forgets the program */
    check( rcap_tokens_set( path, "a", "" ) == 0 );
    check( rcap_tokens_get( path, "a", token, sizeof(token) ) < 0 );
    check( rcap_tokens_get( path, "b", token, sizeof(token) ) == 0 );

    /* tabs and new lines in a Windows path cannot break the file */
    rcap_tokens_key( RCAP_KIND_SCREENS, "C:\\Program Files\\OBS\tx\n", key, sizeof(key) );
    check( !strchr( key, '\t' ) && !strchr( key, '\n' ) );
    check( !strncmp( key, "screens:", 8 ) );
    rcap_tokens_key( RCAP_KIND_PICK, "c:\\program files\\obs\\obs64.exe", token, sizeof(token) );
    rcap_tokens_key( RCAP_KIND_PICK, "C:\\Program Files\\OBS\\obs64.exe", key, sizeof(key) );
    check( !strcmp( key, token ) ); /* Windows paths ignore case */
    check( rcap_tokens_set( path, key, "t" ) == 0 );
    check( rcap_tokens_get( path, key, token, sizeof(token) ) == 0 && !strcmp( token, "t" ) );

    snprintf( key, sizeof(key), "rm -rf %s", dir );
    check( system( key ) == 0 );
}

static void test_convert(void)
{
    const uint8_t rgbx[] = { 10, 20, 30, 0, 40, 50, 60, 0 };
    const uint8_t xrgb[] = { 0, 10, 20, 30 };
    const uint8_t xbgr[] = { 0, 30, 20, 10 };
    uint8_t out[8];

    rcap_convert_row( out, rgbx, 2, RCAP_ORDER_RGBX );
    check( out[0] == 30 && out[1] == 20 && out[2] == 10 && out[3] == 255 );
    check( out[4] == 60 && out[5] == 50 && out[6] == 40 && out[7] == 255 );
    rcap_convert_row( out, rgbx, 2, RCAP_ORDER_BGRX );
    check( out[0] == 10 && out[1] == 20 && out[2] == 30 && out[3] == 255 );
    rcap_convert_row( out, xrgb, 1, RCAP_ORDER_XRGB );
    check( out[0] == 30 && out[1] == 20 && out[2] == 10 && out[3] == 255 );
    rcap_convert_row( out, xbgr, 1, RCAP_ORDER_XBGR );
    check( out[0] == 30 && out[1] == 20 && out[2] == 10 && out[3] == 255 );
}

static void test_cursor(void)
{
    uint8_t frame[4 * 4 * 4], cursor[2 * 2 * 4];
    int i;

    memset( frame, 0, sizeof(frame) );
    for (i = 0; i < 4; i++)
    {
        cursor[i * 4 + 0] = 200; cursor[i * 4 + 1] = 100; cursor[i * 4 + 2] = 50;
        cursor[i * 4 + 3] = i == 3 ? 0 : (i == 2 ? 128 : 255);
    }
    /* partly off the frame at the bottom right */
    rcap_blend_cursor( frame, 16, 4, 4, cursor, 8, 2, 2, 3, 2 );
    check( frame[(2 * 4 + 3) * 4 + 0] == 200 );   /* opaque pixel */
    check( frame[(3 * 4 + 3) * 4 + 0] > 95 && frame[(3 * 4 + 3) * 4 + 0] < 105 ); /* half */
    check( frame[(2 * 4 + 2) * 4 + 0] == 0 );     /* left of the cursor untouched */

    /* fully outside: nothing happens */
    memset( frame, 7, sizeof(frame) );
    rcap_blend_cursor( frame, 16, 4, 4, cursor, 8, 2, 2, -5, 1 );
    rcap_blend_cursor( frame, 16, 4, 4, cursor, 8, 2, 2, 1, 9 );
    for (i = 0; i < (int)sizeof(frame); i++) check( frame[i] == 7 );
}

static void test_startup_reads_do_not_ask(void)
{
    struct rcap_gdi_reads reads = { 0 };
    int i;

    /* a program that reads the whole screen a few times while it starts */
    for (i = 0; i < 6; i++) check( rcap_gdi_read_asks( &reads, 5000 + i * 150, -1 ) == RCAP_ASK_NOT );
    /* the same, with the pointer used long before */
    check( rcap_gdi_read_asks( &reads, 9000, 1000 ) == RCAP_ASK_NOT );
    /* nor a handful spread over a few seconds */
    for (i = 0; i < 5; i++) check( rcap_gdi_read_asks( &reads, 12000 + i * 700, -1 ) == RCAP_ASK_NOT );
    /* one read every few seconds is not a recorder either */
    for (i = 0; i < 30; i++) check( rcap_gdi_read_asks( &reads, 20000 + i * 1500, -1 ) == RCAP_ASK_NOT );
}

static void test_click_then_read_asks(void)
{
    struct rcap_gdi_reads reads = { 0 };

    check( rcap_gdi_read_asks( &reads, 10000, 9700 ) == RCAP_ASK_AND_WAIT );
    /* a screenshot taken 3 s after the click that asked for it */
    check( rcap_gdi_read_asks( &reads, 20000, 17000 ) == RCAP_ASK_AND_WAIT );
    check( rcap_gdi_read_asks( &reads, 40000, 34000 ) == RCAP_ASK_NOT );
    /* input stamped after the read (clock read in another thread) */
    check( rcap_gdi_read_asks( &reads, 30000, 30005 ) == RCAP_ASK_NOT );
}

static void test_recorder_asks(void)
{
    struct rcap_gdi_reads reads = { 0 };
    enum rcap_ask ask = RCAP_ASK_NOT;
    int i;

    /* 30 reads a second, no input: asks once two seconds of it went by */
    for (i = 0; i < 59; i++) check( rcap_gdi_read_asks( &reads, 1000 + i * 33, -1 ) == RCAP_ASK_NOT );
    for (i = 59; i < 70 && !ask; i++) ask = rcap_gdi_read_asks( &reads, 1000 + i * 33, -1 );
    check( ask == RCAP_ASK_IN_BACKGROUND );
    /* a pause starts a new run */
    check( rcap_gdi_read_asks( &reads, 10000, -1 ) == RCAP_ASK_NOT );
}

int main(void)
{
    test_match_exact();
    test_match_windows_coords();
    test_match_unshared();
    test_match_scaled();
    test_match_no_overlap();
    test_match_ten_screens();
    test_workspace();
    test_tokens();
    test_convert();
    test_cursor();
    test_startup_reads_do_not_ask();
    test_click_then_read_asks();
    test_recorder_asks();
    if (failures) fprintf( stderr, "%d failure(s)\n", failures );
    else printf( "all tests passed\n" );
    return failures ? 1 : 0;
}
