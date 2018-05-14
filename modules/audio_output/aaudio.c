/*****************************************************************************
 * aaudio.c: Android AAudio audio output module
 *****************************************************************************
 * Copyright © 2018 VLC authors and VideoLAN, VideoLabs
 *
 * Authors: Romain Vimont <rom1v@videolabs.io>
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
#include <vlc_plugin.h>
#include <vlc_aout.h>
#include <assert.h>
#include <dlfcn.h>

#include "aaudio/AAudio.h" // FIXME use angled once we compile with a newer NDK
//#include <aaudio/AAudio.h>

#define BLOCKING_TIMEOUT 5 * 1000 * 1000 * 1000 // 5s is an infinity for audio

typedef struct aout_sys_t
{
    AAudioStream *p_audio_stream;
    audio_sample_format_t fmt;
    int64_t i_frames_written;

    /* dlopen/dlsym symbols */
    void *p_so_handle;
    aaudio_result_t ( *pf_AAudio_createStreamBuilder )( AAudioStreamBuilder ** );
    const char     *( *pf_AAudio_convertResultToText )( aaudio_result_t );
    void            ( *pf_AAudioStreamBuilder_setSampleRate )( AAudioStreamBuilder *, int32_t );
    void            ( *pf_AAudioStreamBuilder_setFormat )( AAudioStreamBuilder *, aaudio_format_t );
    void            ( *pf_AAudioStreamBuilder_setChannelCount )( AAudioStreamBuilder *, int32_t );
    aaudio_result_t ( *pf_AAudioStreamBuilder_openStream )( AAudioStreamBuilder *, AAudioStream ** );
    void            ( *pf_AAudioStreamBuilder_delete )( AAudioStreamBuilder * );
    aaudio_result_t ( *pf_AAudioStream_requestStart )( AAudioStream * );
    aaudio_result_t ( *pf_AAudioStream_requestStop )( AAudioStream * );
    aaudio_result_t ( *pf_AAudioStream_requestPause )( AAudioStream * );
    aaudio_result_t ( *pf_AAudioStream_requestFlush )( AAudioStream * );
    aaudio_result_t ( *pf_AAudioStream_getTimestamp )( AAudioStream *, clockid_t, int64_t *framePosition, int64_t *timeNanoseconds);
    aaudio_result_t ( *pf_AAudioStream_write )( AAudioStream *, void *, int32_t numFrames, int64_t timeoutNanoseconds );
    aaudio_result_t ( *pf_AAudioStream_close )( AAudioStream * );
} aout_sys_t;

static inline void LogAAudioError( audio_output_t *p_aout, const char *msg, aaudio_result_t result )
{
    aout_sys_t *p_sys = p_aout->sys;

    msg_Err( p_aout, "%s: %s", msg, p_sys->pf_AAudio_convertResultToText( result ) );
}

static inline bool RequestStart( audio_output_t *p_aout )
{
    aout_sys_t *p_sys = p_aout->sys;
    aaudio_result_t result = p_sys->pf_AAudioStream_requestStart( p_sys->p_audio_stream );
    if( result == AAUDIO_OK )
        return true;

    LogAAudioError( p_aout, "Failed to start AAudio stream", result );
    return false;
}

static inline bool RequestStop( audio_output_t *p_aout )
{
    aout_sys_t *p_sys = p_aout->sys;
    aaudio_result_t result = p_sys->pf_AAudioStream_requestStop( p_sys->p_audio_stream );
    if( result == AAUDIO_OK )
        return true;

    LogAAudioError( p_aout, "Failed to stop AAudio stream", result );
    return false;
}

static inline bool RequestPause( audio_output_t *p_aout )
{
    aout_sys_t *p_sys = p_aout->sys;
    aaudio_result_t result = p_sys->pf_AAudioStream_requestPause( p_sys->p_audio_stream );
    if( result == AAUDIO_OK )
        return true;

    LogAAudioError( p_aout, "Failed to pause AAudio stream", result );
    return false;
}

static inline bool RequestFlush( audio_output_t *p_aout )
{
    aout_sys_t *p_sys = p_aout->sys;
    aaudio_result_t result = p_sys->pf_AAudioStream_requestFlush( p_sys->p_audio_stream );
    if( result == AAUDIO_OK )
        return true;

    LogAAudioError( p_aout, "Failed to flush AAudio stream", result );
    return false;
}

static int OpenAAudioStream( audio_output_t *p_aout, audio_sample_format_t *p_fmt )
{
    aout_sys_t *p_sys = p_aout->sys;
    aaudio_result_t result;

    AAudioStreamBuilder *p_builder;
    result = p_sys->pf_AAudio_createStreamBuilder( &p_builder );
    if( result != AAUDIO_OK )
    {
        LogAAudioError( p_aout, "Failed to create AAudio stream builder", result );
        return VLC_EGENERIC;
    }

    aaudio_format_t format;
    if( p_fmt->i_format == VLC_CODEC_S16N )
        format = AAUDIO_FORMAT_PCM_I16;
    else
    {
        if( p_fmt->i_format != VLC_CODEC_FL32 )
        {
            /* override to request conversion */
            p_fmt->i_format = VLC_CODEC_FL32;
            p_fmt->i_bytes_per_frame = 4 * p_fmt->i_channels;
        }
        format = AAUDIO_FORMAT_PCM_FLOAT;
    }

    p_sys->pf_AAudioStreamBuilder_setSampleRate( p_builder, p_fmt->i_rate );
    p_sys->pf_AAudioStreamBuilder_setFormat( p_builder, format );
    p_sys->pf_AAudioStreamBuilder_setChannelCount( p_builder, p_fmt->i_channels );

    AAudioStream *p_audio_stream;
    result = p_sys->pf_AAudioStreamBuilder_openStream( p_builder, &p_audio_stream );
    if( result != AAUDIO_OK )
    {
        LogAAudioError( p_aout, "Failed to open AAudio stream", result );
        p_sys->pf_AAudioStreamBuilder_delete( p_builder );
        return VLC_EGENERIC;
    }

    p_sys->pf_AAudioStreamBuilder_delete( p_builder );
    p_sys->p_audio_stream = p_audio_stream;
    p_sys->fmt = *p_fmt;
    return VLC_SUCCESS;
}

static void CloseAAudioStream( audio_output_t *p_aout )
{
    aout_sys_t *p_sys = p_aout->sys;

    if( p_sys->p_audio_stream )
    {
        p_sys->pf_AAudioStream_close( p_sys->p_audio_stream );
        p_sys->p_audio_stream = NULL;
    }
}

static int Start( audio_output_t *p_aout, audio_sample_format_t *fmt )
{
    int ret = OpenAAudioStream( p_aout, fmt );
    if( ret != VLC_SUCCESS )
        return ret;

    if( !RequestStart( p_aout ) )
    {
        CloseAAudioStream( p_aout );
        return VLC_EGENERIC;
    }

    return VLC_SUCCESS;
}

static void Stop( audio_output_t *p_aout )
{
    RequestStop( p_aout );
}

static bool GetFrameTimestamp( audio_output_t *p_aout, int64_t *p_frame_position, mtime_t *p_frame_time_us )
{
    aout_sys_t *p_sys = p_aout->sys;

    int64_t i_time_ns;
    aaudio_result_t result = p_sys->pf_AAudioStream_getTimestamp( p_sys->p_audio_stream, CLOCK_MONOTONIC, p_frame_position, &i_time_ns);
    if( result != AAUDIO_OK )
    {
        LogAAudioError( p_aout, "Failed to get timestamp", result );
        return false;
    }

    *p_frame_time_us = i_time_ns / 1000;
    return true;
}

static int TimeGet( audio_output_t *p_aout, mtime_t *mt_delay )
{
    aout_sys_t *p_sys = p_aout->sys;

    if( !p_sys->p_audio_stream )
        return -1;

    int64_t i_ref_position;
    mtime_t mt_ref_time_us;
    if( !GetFrameTimestamp( p_aout, &i_ref_position, &mt_ref_time_us ) )
        return -1;

    int64_t i_diff_frames = p_sys->i_frames_written - i_ref_position;
    mtime_t mt_target_time = mt_ref_time_us + i_diff_frames * CLOCK_FREQ / p_sys->fmt.i_rate;
    *mt_delay = mt_target_time - mdate();
    return 0;
}

static void Play( audio_output_t *p_aout, block_t *p_block )
{
    aout_sys_t *p_sys = p_aout->sys;
    assert( p_sys->p_audio_stream );

    aaudio_result_t result = p_sys->pf_AAudioStream_write( p_sys->p_audio_stream,
                                                           p_block->p_buffer,
                                                           p_block->i_nb_samples,
                                                           BLOCKING_TIMEOUT );
    if( result > 0 )
        p_sys->i_frames_written += result;
    else
        LogAAudioError( p_aout, "Failed to write audio block to AAudio stream", result );

    block_Release( p_block );
}

static void Pause( audio_output_t *p_aout, bool b_pause, mtime_t mt_date )
{
    ( void )mt_date;

    if( b_pause )
        RequestPause( p_aout );
    else
        RequestStart( p_aout );
}

static void Flush( audio_output_t *p_aout, bool b_wait )
{
    if( b_wait )
    {
        mtime_t delay;
        if( !TimeGet( p_aout, &delay ) )
            msleep( delay );
    }
    else
        // XXX AAudio only supports flushing in paused state
        RequestFlush( p_aout );
}

static int Open( vlc_object_t *obj )
{
    // TODO force failure on Android < 8.1, where multiple restarts may cause segfault
    // (unless we leak the AAudioStream on Close)
    // <https://github.com/google/oboe/issues/40>
    // The problem should be fixed in AOSP by:
    // <https://android.googlesource.com/platform/frameworks/av/+/8a8a9e5d91c8cc110b9916982f4c5242efca33e3%5E%21/>

    audio_output_t *p_aout = ( audio_output_t * )obj;

    aout_sys_t *p_sys = calloc( 1, sizeof( *p_sys ) );
    if( unlikely( !p_sys ) )
        return VLC_ENOMEM;

    p_sys->p_so_handle = dlopen( "libaaudio.so", RTLD_NOW );
    if( !p_sys->p_so_handle )
    {
        msg_Err( p_aout, "Failed to load libaaudio");
        goto error;
    }

#define AAUDIO_DLSYM( target, name )                         \
    do {                                                     \
        void *sym = dlsym( p_sys->p_so_handle, name );       \
        if( unlikely( !sym ) )                               \
        {                                                    \
            msg_Err( p_aout, "Failed to load symbol "name ); \
            goto error;                                      \
        }                                                    \
        *(void **) &target = sym;                            \
    } while( 0 )

    AAUDIO_DLSYM( p_sys->pf_AAudio_createStreamBuilder, "AAudio_createStreamBuilder" );
    AAUDIO_DLSYM( p_sys->pf_AAudio_convertResultToText, "AAudio_convertResultToText" );
    AAUDIO_DLSYM( p_sys->pf_AAudioStreamBuilder_setSampleRate, "AAudioStreamBuilder_setSampleRate" );
    AAUDIO_DLSYM( p_sys->pf_AAudioStreamBuilder_setChannelCount, "AAudioStreamBuilder_setChannelCount" );
    AAUDIO_DLSYM( p_sys->pf_AAudioStreamBuilder_setFormat, "AAudioStreamBuilder_setFormat" );
    AAUDIO_DLSYM( p_sys->pf_AAudioStreamBuilder_openStream, "AAudioStreamBuilder_openStream" );
    AAUDIO_DLSYM( p_sys->pf_AAudioStreamBuilder_delete, "AAudioStreamBuilder_delete" );
    AAUDIO_DLSYM( p_sys->pf_AAudioStream_requestStart, "AAudioStream_requestStart" );
    AAUDIO_DLSYM( p_sys->pf_AAudioStream_requestStop, "AAudioStream_requestStop" );
    AAUDIO_DLSYM( p_sys->pf_AAudioStream_requestPause, "AAudioStream_requestPause" );
    AAUDIO_DLSYM( p_sys->pf_AAudioStream_requestFlush, "AAudioStream_requestFlush" );
    AAUDIO_DLSYM( p_sys->pf_AAudioStream_getTimestamp, "AAudioStream_getTimestamp" );
    AAUDIO_DLSYM( p_sys->pf_AAudioStream_write, "AAudioStream_write" );
    AAUDIO_DLSYM( p_sys->pf_AAudioStream_close, "AAudioStream_close" );
#undef AAUDIO_DLSYM

    p_aout->sys = p_sys;
    p_aout->start = Start;
    p_aout->stop = Stop;
    p_aout->time_get = TimeGet;
    p_aout->play = Play;
    p_aout->pause = Pause;
    p_aout->flush = Flush;
    return VLC_SUCCESS;

error:
    if( p_sys->p_so_handle )
        dlclose( p_sys->p_so_handle );
    free( p_sys );
    return VLC_EGENERIC;
}

static void Close( vlc_object_t *obj )
{
    audio_output_t *p_aout = ( audio_output_t * )obj;
    aout_sys_t *p_sys = p_aout->sys;

    CloseAAudioStream( p_aout );
    dlclose( p_sys->p_so_handle );
    free( p_sys );
}

vlc_module_begin ()
    set_shortname( "AAudio" )
    set_description( "Android AAudio audio output" )
    set_capability( "audio output", 190 )
    set_category( CAT_AUDIO )
    set_subcategory( SUBCAT_AUDIO_AOUT )
    add_shortcut( "aaudio" )
    set_callbacks( Open, Close )
vlc_module_end ()
