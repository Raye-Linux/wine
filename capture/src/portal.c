/*
 * The ScreenCast portal: create a session, let the user pick, open the
 * PipeWire remote. Runs on the session's own thread with its own GLib
 * context, so it never touches the program's threads or main loop.
 *
 * Copyright 2026 Raye Linux
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gio/gio.h>
#include <gio/gunixfdlist.h>

#include "session.h"

#define PORTAL_BUS   "org.freedesktop.portal.Desktop"
#define PORTAL_PATH  "/org/freedesktop/portal/desktop"
#define SCREENCAST   "org.freedesktop.portal.ScreenCast"
#define REQUEST      "org.freedesktop.portal.Request"

/* portal values */
#define SOURCE_MONITOR    1
#define SOURCE_WINDOW     2
#define CURSOR_EMBEDDED   2
#define CURSOR_METADATA   4
#define PERSIST_FOREVER   2

struct request
{
    gboolean done;
    guint32 code;
    GVariant *results;
};

static void on_response( GDBusConnection *bus, const char *sender, const char *path, const char *iface,
                         const char *signal, GVariant *params, gpointer data )
{
    struct request *req = data;

    g_variant_get( params, "(u@a{sv})", &req->code, &req->results );
    req->done = TRUE;
}

static void on_closed( GDBusConnection *bus, const char *sender, const char *path, const char *iface,
                       const char *signal, GVariant *params, gpointer data )
{
    rcap_session_set_state( data, RCAP_STATE_CLOSED );
}

static char *request_path( struct rcap_session *s, const char *token )
{
    char *sender = g_strdup( g_dbus_connection_get_unique_name( s->bus ) + 1 ), *p, *path;

    for (p = sender; *p; p++) if (*p == '.') *p = '_';
    path = g_strdup_printf( PORTAL_PATH "/request/%s/%s", sender, token );
    g_free( sender );
    return path;
}

/*
 * Calls a portal method that answers through a Request object, and waits
 * for that answer. The signal is subscribed before the call, as the portal
 * documentation asks, or a fast answer is lost.
 */
static int call_request( struct rcap_session *s, const char *method, GVariant *args,
                         const char *token, GVariant **results )
{
    struct request req = { 0 };
    char *path = request_path( s, token );
    GError *error = NULL;
    GVariant *reply;
    guint sub;

    sub = g_dbus_connection_signal_subscribe( s->bus, PORTAL_BUS, REQUEST, "Response", path, NULL,
                                              G_DBUS_SIGNAL_FLAGS_NO_MATCH_RULE, on_response, &req, NULL );
    reply = g_dbus_connection_call_sync( s->bus, PORTAL_BUS, PORTAL_PATH, SCREENCAST, method, args,
                                         NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error );
    if (!reply)
    {
        fprintf( stderr, "raye-capture: %s failed: %s\n", method, error->message );
        g_error_free( error );
        g_dbus_connection_signal_unsubscribe( s->bus, sub );
        g_free( path );
        return -ENOENT;
    }
    g_variant_unref( reply );

    while (!req.done && !s->quit) g_main_context_iteration( s->context, TRUE );
    g_dbus_connection_signal_unsubscribe( s->bus, sub );
    g_free( path );

    if (!req.done) return -ECANCELED;
    if (req.code)
    {
        if (req.results) g_variant_unref( req.results );
        return req.code == 1 ? -EACCES : -EPIPE;
    }
    *results = req.results;
    return 0;
}

static guint32 cursor_modes( struct rcap_session *s )
{
    GVariant *reply, *value;
    guint32 modes = 0;

    reply = g_dbus_connection_call_sync( s->bus, PORTAL_BUS, PORTAL_PATH, "org.freedesktop.DBus.Properties",
                                         "Get", g_variant_new( "(ss)", SCREENCAST, "AvailableCursorModes" ),
                                         G_VARIANT_TYPE( "(v)" ), G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL );
    if (!reply) return 0;
    g_variant_get( reply, "(v)", &value );
    if (g_variant_is_of_type( value, G_VARIANT_TYPE_UINT32 )) modes = g_variant_get_uint32( value );
    g_variant_unref( value );
    g_variant_unref( reply );
    return modes;
}

/*
 * The portal names a program by its systemd scope. The Windows runner can
 * also hand us an application id, which the host registry accepts once per
 * connection, before any other call.
 */
static void register_app( struct rcap_session *s )
{
    const char *id = getenv( "RAYE_WINDOWS_APP_ID" );
    GVariant *reply;

    if (!id || !g_application_id_is_valid( id )) return;
    reply = g_dbus_connection_call_sync( s->bus, PORTAL_BUS, PORTAL_PATH, "org.freedesktop.host.portal.Registry",
                                         "Register", g_variant_new( "(sa{sv})", id, NULL ),
                                         NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL );
    if (reply) g_variant_unref( reply );
}

static void read_streams( struct rcap_session *s, GVariant *results )
{
    GVariantIter *iter;
    GVariant *props;
    guint32 node;
    int i = 0;

    if (!g_variant_lookup( results, "streams", "a(ua{sv})", &iter )) return;
    s->stream_count = g_variant_iter_n_children( iter );
    s->streams = calloc( s->stream_count ? s->stream_count : 1, sizeof(*s->streams) );
    while (g_variant_iter_next( iter, "(u@a{sv})", &node, &props ))
    {
        struct rcap_stream_info *info = &s->streams[i].info;
        guint32 type = SOURCE_MONITOR;

        info->node_id = node;
        g_variant_lookup( props, "position", "(ii)", &info->rect.x, &info->rect.y );
        g_variant_lookup( props, "size", "(ii)", &info->rect.width, &info->rect.height );
        g_variant_lookup( props, "source_type", "u", &type );
        info->source = type == SOURCE_WINDOW ? RCAP_SOURCE_WINDOW : RCAP_SOURCE_MONITOR;
        s->streams[i].session = s;
        g_variant_unref( props );
        i++;
    }
    s->stream_count = i;
    g_variant_iter_free( iter );
}

int rcap_portal_start( struct rcap_session *s, int *pw_fd )
{
    char token[64], key[1024], restore[512], *tokens = rcap_tokens_path();
    GVariant *results = NULL, *reply;
    GUnixFDList *fds = NULL;
    GVariantBuilder opts;
    GError *error = NULL;
    const char *handle;
    guint32 modes;
    int ret, have_token;
    gchar *address;

    if (!(address = g_dbus_address_get_for_bus_sync( G_BUS_TYPE_SESSION, NULL, NULL ))) return -ENOENT;
    s->bus = g_dbus_connection_new_for_address_sync( address, G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
                                                     G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION,
                                                     NULL, NULL, &error );
    g_free( address );
    if (!s->bus)
    {
        fprintf( stderr, "raye-capture: no session bus: %s\n", error->message );
        g_error_free( error );
        free( tokens );
        return -ENOENT;
    }
    register_app( s );

    g_variant_builder_init( &opts, G_VARIANT_TYPE_VARDICT );
    snprintf( token, sizeof(token), "rcap%u", g_random_int() );
    g_variant_builder_add( &opts, "{sv}", "handle_token", g_variant_new_string( token ) );
    g_variant_builder_add( &opts, "{sv}", "session_handle_token", g_variant_new_string( token ) );
    if ((ret = call_request( s, "CreateSession", g_variant_new( "(a{sv})", &opts ), token, &results ))) goto done;
    if (!g_variant_lookup( results, "session_handle", "&s", &handle )) { ret = -EPROTO; goto done; }
    s->session_handle = g_strdup( handle );
    g_variant_unref( results );
    results = NULL;
    s->closed_sub = g_dbus_connection_signal_subscribe( s->bus, PORTAL_BUS, "org.freedesktop.portal.Session",
                                                        "Closed", s->session_handle, NULL,
                                                        G_DBUS_SIGNAL_FLAGS_NONE, on_closed, s, NULL );

    rcap_tokens_key( s->kind, s->app_key, key, sizeof(key) );
    have_token = tokens && !rcap_tokens_get( tokens, key, restore, sizeof(restore) );
    modes = cursor_modes( s );

    g_variant_builder_init( &opts, G_VARIANT_TYPE_VARDICT );
    snprintf( token, sizeof(token), "rcap%u", g_random_int() );
    g_variant_builder_add( &opts, "{sv}", "handle_token", g_variant_new_string( token ) );
    g_variant_builder_add( &opts, "{sv}", "types", g_variant_new_uint32(
                           s->kind == RCAP_KIND_PICK ? SOURCE_MONITOR | SOURCE_WINDOW : SOURCE_MONITOR ) );
    /* one source: Plasma's chooser never enables Share with several ticked, and
       "Share full Workspace" covers every screen in one stream anyway */
    g_variant_builder_add( &opts, "{sv}", "multiple", g_variant_new_boolean( FALSE ) );
    /* Windows hands the pointer over on the side, so ask for it the same way */
    if (modes & CURSOR_METADATA)
        g_variant_builder_add( &opts, "{sv}", "cursor_mode", g_variant_new_uint32( CURSOR_METADATA ) );
    else if (modes & CURSOR_EMBEDDED)
        g_variant_builder_add( &opts, "{sv}", "cursor_mode", g_variant_new_uint32( CURSOR_EMBEDDED ) );
    g_variant_builder_add( &opts, "{sv}", "persist_mode", g_variant_new_uint32( PERSIST_FOREVER ) );
    if (have_token) g_variant_builder_add( &opts, "{sv}", "restore_token", g_variant_new_string( restore ) );
    if ((ret = call_request( s, "SelectSources", g_variant_new( "(oa{sv})", s->session_handle, &opts ),
                             token, &results ))) goto done;
    g_variant_unref( results );
    results = NULL;
    s->cursor_metadata = !!(modes & CURSOR_METADATA);

    g_variant_builder_init( &opts, G_VARIANT_TYPE_VARDICT );
    snprintf( token, sizeof(token), "rcap%u", g_random_int() );
    g_variant_builder_add( &opts, "{sv}", "handle_token", g_variant_new_string( token ) );
    ret = call_request( s, "Start", g_variant_new( "(osa{sv})", s->session_handle, "", &opts ), token, &results );
    if (ret == -EACCES && have_token) rcap_tokens_set( tokens, key, "" );
    if (ret) goto done;

    read_streams( s, results );
    if (tokens)
    {
        const char *new_token;
        if (g_variant_lookup( results, "restore_token", "&s", &new_token )) rcap_tokens_set( tokens, key, new_token );
    }

    reply = g_dbus_connection_call_with_unix_fd_list_sync( s->bus, PORTAL_BUS, PORTAL_PATH, SCREENCAST,
                                                           "OpenPipeWireRemote",
                                                           g_variant_new( "(oa{sv})", s->session_handle, NULL ),
                                                           G_VARIANT_TYPE( "(h)" ), G_DBUS_CALL_FLAGS_NONE, -1,
                                                           NULL, &fds, NULL, &error );
    if (!reply)
    {
        fprintf( stderr, "raye-capture: OpenPipeWireRemote failed: %s\n", error->message );
        g_error_free( error );
        ret = -EPIPE;
        goto done;
    }
    else
    {
        gint32 index;
        g_variant_get( reply, "(h)", &index );
        *pw_fd = fds ? g_unix_fd_list_get( fds, index, NULL ) : -1;
        g_variant_unref( reply );
        if (fds) g_object_unref( fds );
        if (*pw_fd < 0) ret = -EPIPE;
    }

done:
    if (results) g_variant_unref( results );
    free( tokens );
    return ret;
}

/* runs until the session is dropped; only the Closed signal arrives here */
void rcap_portal_run( struct rcap_session *s )
{
    while (!s->quit) g_main_context_iteration( s->context, TRUE );
}

void rcap_portal_close( struct rcap_session *s )
{
    if (!s->bus) return;
    if (s->session_handle)
    {
        g_dbus_connection_call_sync( s->bus, PORTAL_BUS, s->session_handle, "org.freedesktop.portal.Session",
                                     "Close", NULL, NULL, G_DBUS_CALL_FLAGS_NONE, 2000, NULL, NULL );
        g_free( s->session_handle );
    }
    if (s->closed_sub) g_dbus_connection_signal_unsubscribe( s->bus, s->closed_sub );
    g_object_unref( s->bus );
    s->bus = NULL;
}
