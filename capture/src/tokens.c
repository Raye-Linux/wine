/*
 * Restore tokens, so a program is asked once, like Windows asks once.
 *
 * Copyright 2026 Raye Linux
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#include "private.h"

char *rcap_tokens_path( void )
{
    const char *state = getenv( "XDG_STATE_HOME" ), *home = getenv( "HOME" );
    char *path;

    if (state && state[0] == '/')
    {
        if (asprintf( &path, "%s/raye/capture/restore-tokens", state ) < 0) return NULL;
    }
    else if (home)
    {
        if (asprintf( &path, "%s/.local/state/raye/capture/restore-tokens", home ) < 0) return NULL;
    }
    else return NULL;
    return path;
}

void rcap_tokens_key( enum rcap_kind kind, const char *app_key, char *key, size_t size )
{
    size_t len;
    char *p;

    snprintf( key, size, "%s:%s", kind == RCAP_KIND_PICK ? "pick" : "screens", app_key ? app_key : "" );
    len = strlen( key );
    for (p = key; *p; p++)
    {
        if (*p == '\t' || *p == '\n' || *p == '\r') *p = ' ';
        else *p = tolower( (unsigned char)*p );
    }
    while (len && key[len - 1] == ' ') key[--len] = 0;
}

static int make_parent( const char *path )
{
    char *dir = strdup( path ), *p;

    if (!dir) return -ENOMEM;
    for (p = dir + 1; *p; p++)
    {
        if (*p != '/') continue;
        *p = 0;
        if (mkdir( dir, 0700 ) && errno != EEXIST) { free( dir ); return -errno; }
        *p = '/';
    }
    free( dir );
    return 0;
}

int rcap_tokens_get( const char *path, const char *key, char *token, size_t size )
{
    size_t keylen = strlen( key );
    char line[4096];
    int ret = -ENOENT;
    FILE *f;

    if (!(f = fopen( path, "r" ))) return -errno;
    while (fgets( line, sizeof(line), f ))
    {
        char *end;
        if (strncmp( line, key, keylen ) || line[keylen] != '\t') continue;
        if ((end = strchr( line, '\n' ))) *end = 0;
        if (strlen( line + keylen + 1 ) >= size) { ret = -ENOSPC; break; }
        strcpy( token, line + keylen + 1 );
        ret = token[0] ? 0 : -ENOENT;
        break;
    }
    fclose( f );
    return ret;
}

/* rewrites the file under a lock: two programs may save at the same time */
int rcap_tokens_set( const char *path, const char *key, const char *token )
{
    size_t keylen = strlen( key );
    char line[4096], *tmp, *lock;
    FILE *in, *out;
    int fd, lockfd, ret = 0;

    if ((ret = make_parent( path ))) return ret;
    if (asprintf( &lock, "%s.lock", path ) < 0) return -ENOMEM;
    lockfd = open( lock, O_RDWR | O_CREAT | O_CLOEXEC, 0600 );
    free( lock );
    if (lockfd < 0) return -errno;
    flock( lockfd, LOCK_EX );

    if (asprintf( &tmp, "%s.XXXXXX", path ) < 0) { close( lockfd ); return -ENOMEM; }
    if ((fd = mkstemp( tmp )) < 0 || !(out = fdopen( fd, "w" )))
    {
        ret = -errno;
        if (fd >= 0) close( fd );
        goto done;
    }
    if ((in = fopen( path, "r" )))
    {
        while (fgets( line, sizeof(line), in ))
            if (strncmp( line, key, keylen ) || line[keylen] != '\t') fputs( line, out );
        fclose( in );
    }
    if (token && token[0]) fprintf( out, "%s\t%s\n", key, token );
    if (fflush( out ) || fsync( fd )) ret = -errno;
    fclose( out );
    if (!ret && rename( tmp, path )) ret = -errno;
    if (ret) unlink( tmp );

done:
    free( tmp );
    close( lockfd );
    return ret;
}
