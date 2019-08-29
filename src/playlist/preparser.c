/*****************************************************************************
 * preparser.c
 *****************************************************************************
 * Copyright © 2017-2017 VLC authors and VideoLAN
 * $Id$
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <vlc_common.h>

#include "misc/background_worker.h"
#include "input/input_interface.h"
#include "input/input_internal.h"
#include "preparser.h"
#include "fetcher.h"

struct playlist_preparser_t
{
    vlc_object_t* owner;
    playlist_fetcher_t* fetcher;
    struct background_worker* worker;
    atomic_bool deactivated;
};

struct input_preparser_req
{
    input_item_t *item;
    atomic_uint rc;
};

struct input_preparser_task
{
    struct input_preparser_req *req;
    input_thread_t *input;
};

static struct input_preparser_req *
input_preparser_req_new(input_item_t *item)
{
    struct input_preparser_req *req = malloc(sizeof(*req));
    if (!req)
        return NULL;

    req->item = input_item_Hold(item);
    atomic_init(&req->rc, 1);
    return req;
}

static void
input_preparser_req_Hold(struct input_preparser_req *req)
{
    atomic_fetch_add(&req->rc, 1);
}

static void
input_preparser_req_Release(struct input_preparser_req *req)
{
    if (atomic_fetch_sub(&req->rc, 1) != 1)
        return;

    input_item_Release(req->item);
    free(req);
}

static int InputEvent( vlc_object_t* obj, const char* varname,
    vlc_value_t old, vlc_value_t cur, void* worker )
{
    VLC_UNUSED( obj ); VLC_UNUSED( varname ); VLC_UNUSED( old );

    if( cur.i_int == INPUT_EVENT_DEAD )
        background_worker_RequestProbe( worker );

    return VLC_SUCCESS;
}

static int PreparserOpenInput( void* preparser_, void* req_, void** out )
{
    playlist_preparser_t* preparser = preparser_;
    struct input_preparser_req *req = req_;

    struct input_preparser_task *task = malloc(sizeof(*task));
    if (!task)
        goto error;

    input_thread_t *input = input_CreatePreparser(preparser->owner, req->item);
    if (!input)
        goto error;

    task->req = req;
    task->input = input;

    var_AddCallback( input, "intf-event", InputEvent, preparser->worker );
    if( input_Start( input ) )
    {
        var_DelCallback( input, "intf-event", InputEvent, preparser->worker );
        input_Close( input );
        goto error;
    }

    *out = task;

    return VLC_SUCCESS;

error:
    free(task);
    input_item_SignalPreparseEnded( req->item, ITEM_PREPARSE_FAILED );
    return VLC_EGENERIC;
}

static int PreparserProbeInput( void* preparser_, void* task_ )
{
    struct input_preparser_task *task = task_;
    int state = input_GetState( task->input );
    return state == END_S || state == ERROR_S;
    VLC_UNUSED( preparser_ );
}

static void PreparserCloseInput( void* preparser_, void* task_ )
{
    struct input_preparser_task *task = task_;
    playlist_preparser_t* preparser = preparser_;
    input_thread_t* input = task->input;
    input_item_t* item = input_priv(input)->p_item;

    var_DelCallback( input, "intf-event", InputEvent, preparser->worker );

    int status;
    switch( input_GetState( input ) )
    {
        case END_S:
            status = ITEM_PREPARSE_DONE;
            break;
        case ERROR_S:
            status = ITEM_PREPARSE_FAILED;
            break;
        default:
            status = ITEM_PREPARSE_TIMEOUT;
    }

    input_Stop( input );
    input_Close( input );

    free(task);

    if( preparser->fetcher )
    {
        if( !playlist_fetcher_Push( preparser->fetcher, item, 0, status ) )
            return;
    }

    input_item_SetPreparsed( item, true );
    input_item_SignalPreparseEnded( item, status );
}

static void ReqRelease(void *req) { input_preparser_req_Release(req); }
static void ReqHold(void* req) { input_preparser_req_Hold(req); }

playlist_preparser_t* playlist_preparser_New( vlc_object_t *parent )
{
    playlist_preparser_t* preparser = malloc( sizeof *preparser );

    struct background_worker_config conf = {
        .default_timeout = var_InheritInteger( parent, "preparse-timeout" ),
        .pf_start = PreparserOpenInput,
        .pf_probe = PreparserProbeInput,
        .pf_stop = PreparserCloseInput,
        .pf_release = ReqRelease,
        .pf_hold = ReqHold };


    if( likely( preparser ) )
        preparser->worker = background_worker_New( preparser, &conf );

    if( unlikely( !preparser || !preparser->worker ) )
    {
        free( preparser );
        return NULL;
    }

    preparser->owner = parent;
    preparser->fetcher = playlist_fetcher_New( parent );
    atomic_init( &preparser->deactivated, false );

    if( unlikely( !preparser->fetcher ) )
        msg_Warn( parent, "unable to create art fetcher" );

    return preparser;
}

void playlist_preparser_Push( playlist_preparser_t *preparser,
    input_item_t *item, input_item_meta_request_option_t i_options,
    int timeout, void *id )
{
    if( atomic_load( &preparser->deactivated ) )
        return;

    vlc_mutex_lock( &item->lock );
    int i_type = item->i_type;
    int b_net = item->b_net;
    vlc_mutex_unlock( &item->lock );

    switch( i_type )
    {
        case ITEM_TYPE_NODE:
        case ITEM_TYPE_FILE:
        case ITEM_TYPE_DIRECTORY:
        case ITEM_TYPE_PLAYLIST:
            if( !b_net || i_options & META_REQUEST_OPTION_SCOPE_NETWORK )
                break;
        default:
            input_item_SignalPreparseEnded( item, ITEM_PREPARSE_SKIPPED );
            return;
    }

    struct input_preparser_req *req = input_preparser_req_new(item);
    if (!req)
    {
        input_item_SignalPreparseEnded( item, ITEM_PREPARSE_FAILED );
        return;
    }

    if( background_worker_Push( preparser->worker, req, id, timeout ) )
        input_item_SignalPreparseEnded( item, ITEM_PREPARSE_FAILED );

    input_preparser_req_Release(req);
}

void playlist_preparser_fetcher_Push( playlist_preparser_t *preparser,
    input_item_t *item, input_item_meta_request_option_t options )
{
    if( preparser->fetcher )
        playlist_fetcher_Push( preparser->fetcher, item, options, -1 );
}

void playlist_preparser_Cancel( playlist_preparser_t *preparser, void *id )
{
    background_worker_Cancel( preparser->worker, id );
}

void playlist_preparser_Deactivate( playlist_preparser_t* preparser )
{
    atomic_store( &preparser->deactivated, true );
    background_worker_Cancel( preparser->worker, NULL );
}

void playlist_preparser_Delete( playlist_preparser_t *preparser )
{
    background_worker_Delete( preparser->worker );

    if( preparser->fetcher )
        playlist_fetcher_Delete( preparser->fetcher );

    free( preparser );
}
