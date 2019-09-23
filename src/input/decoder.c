/*****************************************************************************
 * decoder.c: Functions for the management of decoders
 *****************************************************************************
 * Copyright (C) 1999-2019 VLC authors, VideoLAN and Videolabs SAS
 *
 * Authors: Christophe Massiot <massiot@via.ecp.fr>
 *          Gildas Bazin <gbazin@videolan.org>
 *          Laurent Aimar <fenrir@via.ecp.fr>
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

/*****************************************************************************
 * Preamble
 *****************************************************************************/
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#include <assert.h>
#include <stdatomic.h>

#include <vlc_common.h>
#include <vlc_block.h>
#include <vlc_vout.h>
#include <vlc_aout.h>
#include <vlc_sout.h>
#include <vlc_codec.h>
#include <vlc_spu.h>
#include <vlc_meta.h>
#include <vlc_dialog.h>
#include <vlc_modules.h>
#include <vlc_decoder.h>

#include "audio_output/aout_internal.h"
#include "stream_output/stream_output.h"
#include "../clock/clock.h"
#include "decoder.h"
#include "resource.h"

#include "../video_output/vout_internal.h"

/*
 * Possibles values set in p_priv->reload atomic
 */
enum reload
{
    RELOAD_NO_REQUEST,
    RELOAD_DECODER,     /* Reload the decoder module */
    RELOAD_DECODER_AOUT /* Stop the aout and reload the decoder module */
};

struct decoder_priv
{
    decoder_t        dec;
    input_resource_t*p_resource;
    vlc_clock_t     *p_clock;

    const struct input_decoder_callbacks *cbs;
    void *cbs_userdata;

    ssize_t          i_spu_channel;
    int64_t          i_spu_order;

    sout_instance_t         *p_sout;
    sout_packetizer_input_t *p_sout_input;

    vlc_thread_t     thread;

    /* Some decoders require already packetized data (ie. not truncated) */
    decoder_t *p_packetizer;
    bool b_packetizer;

    /* Current format in use by the output */
    es_format_t    fmt;

    /* */
    atomic_bool    b_fmt_description;
    vlc_meta_t     *p_description;
    atomic_int     reload;

    /* fifo */
    block_fifo_t *p_fifo;

    /* Lock for communication with decoder thread */
    vlc_mutex_t lock;
    vlc_cond_t  wait_request;
    vlc_cond_t  wait_acknowledge;
    vlc_cond_t  wait_fifo; /* TODO: merge with wait_acknowledge */

    /*
     * 3 threads can read/write these output variables, the DecoderThread, the
     * input thread, and the ModuleThread. The ModuleThread is either the
     * DecoderThread for synchronous modules or any thread for asynchronous
     * modules.
     *
     * Asynchronous modules are responsible for serializing/locking every
     * output calls in any thread as long as the decoder_UpdateVideoFormat() or
     * decoder_NewPicture() calls are not concurrent, cf.
     * decoder_UpdateVideoFormat() and decoder_NewPicture() notes.
     *
     * The ModuleThread is the owner of these variables, it should hold
     * the lock when writing them but doesn't have to hold it when using them.
     *
     * The DecoderThread should always hold the lock when reading/using
     * aout/vouts.
     *
     * The input thread can read these variables in order to stop outputs, when
     * both ModuleThread and DecoderThread are stopped (from DecoderDelete()).
     */
    audio_output_t *p_aout;

    vout_thread_t   *p_vout;

    /* -- Theses variables need locking on read *and* write -- */
    /* Preroll */
    vlc_tick_t i_preroll_end;

#define PREROLL_NONE    INT64_MIN // vlc_tick_t
#define PREROLL_FORCED  INT64_MAX // vlc_tick_t

    /* Pause & Rate */
    bool reset_out_state;
    vlc_tick_t pause_date;
    vlc_tick_t delay;
    float request_rate, output_rate;
    unsigned frames_countdown;
    bool paused;

    bool error;

    /* Waiting */
    bool b_waiting;
    bool b_first;
    bool b_has_data;

    /* Flushing */
    bool flushing;
    bool b_draining;
    atomic_bool drained;
    bool b_idle;

    /* CC */
#define MAX_CC_DECODERS 64 /* The es_out only creates one type of es */
    struct
    {
        bool b_supported;
        decoder_cc_desc_t desc;
        decoder_t *pp_decoder[MAX_CC_DECODERS];
        bool b_sout_created;
        sout_packetizer_input_t *p_sout_input;
    } cc;

    /* Mouse event */
    vlc_mutex_t     mouse_lock;
    vlc_mouse_event mouse_event;
    void           *mouse_opaque;
};

/* Pictures which are DECODER_BOGUS_VIDEO_DELAY or more in advance probably have
 * a bogus PTS and won't be displayed */
#define DECODER_BOGUS_VIDEO_DELAY                ((vlc_tick_t)(DEFAULT_PTS_DELAY * 30))

/* */
#define DECODER_SPU_VOUT_WAIT_DURATION   VLC_TICK_FROM_MS(200)
#define BLOCK_FLAG_CORE_PRIVATE_RELOADED (1 << BLOCK_FLAG_CORE_PRIVATE_SHIFT)

#define decoder_Notify(decoder_priv, event, ...) \
    if (decoder_priv->cbs && decoder_priv->cbs->event) \
        decoder_priv->cbs->event(&decoder_priv->dec, __VA_ARGS__, \
                                 decoder_priv->cbs_userdata);

static inline struct decoder_priv *dec_get_priv( decoder_t *p_dec )
{
    return container_of( p_dec, struct decoder_priv, dec );
}

/**
 * Load a decoder module
 */
static int LoadDecoder( decoder_t *p_dec, bool b_packetizer,
                        const es_format_t *restrict p_fmt )
{
    decoder_Init( p_dec, p_fmt );

    p_dec->b_frame_drop_allowed = true;

    /* Find a suitable decoder/packetizer module */
    if( !b_packetizer )
    {
        static const char caps[ES_CATEGORY_COUNT][16] = {
            [VIDEO_ES] = "video decoder",
            [AUDIO_ES] = "audio decoder",
            [SPU_ES] = "spu decoder",
        };
        p_dec->p_module = module_need_var( p_dec, caps[p_dec->fmt_in.i_cat],
                                           "codec" );
    }
    else
        p_dec->p_module = module_need_var( p_dec, "packetizer", "packetizer" );

    if( !p_dec->p_module )
    {
        decoder_Clean( p_dec );
        return -1;
    }
    return 0;
}

static int DecoderThread_Reload( struct decoder_priv *p_priv, bool b_packetizer,
                                 const es_format_t *restrict p_fmt, enum reload reload )
{
    /* Copy p_fmt since it can be destroyed by decoder_Clean */
    decoder_t *p_dec = &p_priv->dec;
    es_format_t fmt_in;
    if( es_format_Copy( &fmt_in, p_fmt ) != VLC_SUCCESS )
    {
        p_priv->error = true;
        return VLC_EGENERIC;
    }

    /* Restart the decoder module */
    decoder_Clean( p_dec );
    p_priv->error = false;

    if( reload == RELOAD_DECODER_AOUT )
    {
        assert( p_priv->fmt.i_cat == AUDIO_ES );
        audio_output_t *p_aout = p_priv->p_aout;
        // no need to lock, the decoder and ModuleThread are dead
        p_priv->p_aout = NULL;
        if( p_aout )
        {
            aout_DecDelete( p_aout );
            input_resource_PutAout( p_priv->p_resource, p_aout );
        }
    }

    if( LoadDecoder( p_dec, b_packetizer, &fmt_in ) )
    {
        p_priv->error = true;
        es_format_Clean( &fmt_in );
        return VLC_EGENERIC;
    }
    es_format_Clean( &fmt_in );
    return VLC_SUCCESS;
}

static void DecoderUpdateFormatLocked( struct decoder_priv *p_priv )
{
    decoder_t *p_dec = &p_priv->dec;

    vlc_mutex_assert( &p_priv->lock );

    es_format_Clean( &p_priv->fmt );
    es_format_Copy( &p_priv->fmt, &p_dec->fmt_out );

    /* Move p_description */
    if( p_dec->p_description != NULL )
    {
        if( p_priv->p_description != NULL )
            vlc_meta_Delete( p_priv->p_description );
        p_priv->p_description = p_dec->p_description;
        p_dec->p_description = NULL;
    }

    atomic_store_explicit( &p_priv->b_fmt_description, true,
                           memory_order_release );
}

static void MouseEvent( const vlc_mouse_t *newmouse, void *user_data )
{
    decoder_t *dec = user_data;
    struct decoder_priv *priv = dec_get_priv( dec );

    vlc_mutex_lock( &priv->mouse_lock );
    if( priv->mouse_event )
        priv->mouse_event( newmouse, priv->mouse_opaque);
    vlc_mutex_unlock( &priv->mouse_lock );
}

/*****************************************************************************
 * Buffers allocation callbacks for the decoders
 *****************************************************************************/
static bool aout_replaygain_changed( const audio_replay_gain_t *a,
                                     const audio_replay_gain_t *b )
{
    for( size_t i=0; i<AUDIO_REPLAY_GAIN_MAX; i++ )
    {
        if( a->pb_gain[i] != b->pb_gain[i] ||
            a->pb_peak[i] != b->pb_peak[i] ||
            a->pb_gain[i] != b->pb_gain[i] ||
            a->pb_peak[i] != b->pb_peak[i] )
            return true;
    }
    return false;
}

static int ModuleThread_UpdateAudioFormat( decoder_t *p_dec )
{
    struct decoder_priv *p_priv = dec_get_priv( p_dec );

    if( p_priv->p_aout &&
       ( !AOUT_FMTS_IDENTICAL(&p_dec->fmt_out.audio, &p_priv->fmt.audio) ||
         p_dec->fmt_out.i_codec != p_dec->fmt_out.audio.i_format ||
         p_dec->fmt_out.i_profile != p_priv->fmt.i_profile ) )
    {
        audio_output_t *p_aout = p_priv->p_aout;

        /* Parameters changed, restart the aout */
        vlc_mutex_lock( &p_priv->lock );
        p_priv->p_aout = NULL; // the DecoderThread should not use the old aout anymore
        vlc_mutex_unlock( &p_priv->lock );
        aout_DecDelete( p_aout );

        input_resource_PutAout( p_priv->p_resource, p_aout );
    }

    /* Check if only replay gain has changed */
    if( aout_replaygain_changed( &p_dec->fmt_in.audio_replay_gain,
                                 &p_priv->fmt.audio_replay_gain ) )
    {
        p_dec->fmt_out.audio_replay_gain = p_dec->fmt_in.audio_replay_gain;
        if( p_priv->p_aout )
        {
            p_priv->fmt.audio_replay_gain = p_dec->fmt_in.audio_replay_gain;
            var_TriggerCallback( p_priv->p_aout, "audio-replay-gain-mode" );
        }
    }

    if( p_priv->p_aout == NULL )
    {
        p_dec->fmt_out.audio.i_format = p_dec->fmt_out.i_codec;

        audio_sample_format_t format = p_dec->fmt_out.audio;
        aout_FormatPrepare( &format );

        const int i_force_dolby = var_InheritInteger( p_dec, "force-dolby-surround" );
        if( i_force_dolby &&
            format.i_physical_channels == (AOUT_CHAN_LEFT|AOUT_CHAN_RIGHT) )
        {
            if( i_force_dolby == 1 )
                format.i_chan_mode |= AOUT_CHANMODE_DOLBYSTEREO;
            else /* i_force_dolby == 2 */
                format.i_chan_mode &= ~AOUT_CHANMODE_DOLBYSTEREO;
        }

        audio_output_t *p_aout;

        p_aout = input_resource_GetAout( p_priv->p_resource );
        if( p_aout )
        {
            /* TODO: 3.0 HACK: we need to put i_profile inside audio_format_t
             * for 4.0 */
            if( p_dec->fmt_out.i_codec == VLC_CODEC_DTS )
                var_SetBool( p_aout, "dtshd", p_dec->fmt_out.i_profile > 0 );

            if( aout_DecNew( p_aout, &format, p_priv->p_clock,
                             &p_dec->fmt_out.audio_replay_gain ) )
            {
                input_resource_PutAout( p_priv->p_resource, p_aout );
                p_aout = NULL;
            }
        }

        vlc_mutex_lock( &p_priv->lock );
        p_priv->p_aout = p_aout;

        DecoderUpdateFormatLocked( p_priv );
        aout_FormatPrepare( &p_priv->fmt.audio );
        vlc_mutex_unlock( &p_priv->lock );

        if( p_aout == NULL )
        {
            msg_Err( p_dec, "failed to create audio output" );
            return -1;
        }

        p_dec->fmt_out.audio.i_bytes_per_frame =
            p_priv->fmt.audio.i_bytes_per_frame;
        p_dec->fmt_out.audio.i_frame_length =
            p_priv->fmt.audio.i_frame_length;

        vlc_fifo_Lock( p_priv->p_fifo );
        p_priv->reset_out_state = true;
        vlc_fifo_Unlock( p_priv->p_fifo );
    }
    return 0;
}

static int ModuleThread_UpdateVideoFormat( decoder_t *p_dec )
{
    struct decoder_priv *p_priv = dec_get_priv( p_dec );
    bool need_vout = false;
    bool need_format_update = false;

    if( p_priv->p_vout == NULL )
    {
        msg_Dbg(p_dec, "vout: none found");
        need_vout = true;
    }
    if( p_dec->fmt_out.video.i_width != p_priv->fmt.video.i_width
             || p_dec->fmt_out.video.i_height != p_priv->fmt.video.i_height )
    {
        msg_Dbg(p_dec, "vout change: decoder size");
        need_vout = true;
    }
    if( p_dec->fmt_out.video.i_visible_width != p_priv->fmt.video.i_visible_width
             || p_dec->fmt_out.video.i_visible_height != p_priv->fmt.video.i_visible_height
             || p_dec->fmt_out.video.i_x_offset != p_priv->fmt.video.i_x_offset
             || p_dec->fmt_out.video.i_y_offset != p_priv->fmt.video.i_y_offset )
    {
        msg_Dbg(p_dec, "vout change: visible size");
        need_vout = true;
    }
    if( p_dec->fmt_out.i_codec != p_priv->fmt.video.i_chroma )
    {
        msg_Dbg(p_dec, "vout change: chroma");
        need_vout = true;
    }
    if( (int64_t)p_dec->fmt_out.video.i_sar_num * p_priv->fmt.video.i_sar_den !=
             (int64_t)p_dec->fmt_out.video.i_sar_den * p_priv->fmt.video.i_sar_num )
    {
        msg_Dbg(p_dec, "vout change: SAR");
        need_vout = true;
    }
    if( p_dec->fmt_out.video.orientation != p_priv->fmt.video.orientation )
    {
        msg_Dbg(p_dec, "vout change: orientation");
        need_vout = true;
    }
    if( p_dec->fmt_out.video.multiview_mode != p_priv->fmt.video.multiview_mode )
    {
        msg_Dbg(p_dec, "vout change: multiview");
        need_vout = true;
    }

    if ( memcmp( &p_dec->fmt_out.video.mastering,
                 &p_priv->fmt.video.mastering,
                 sizeof(p_priv->fmt.video.mastering)) )
    {
        msg_Dbg(p_dec, "vout update: mastering data");
        need_format_update = true;
    }
    if ( p_dec->fmt_out.video.lighting.MaxCLL !=
         p_priv->fmt.video.lighting.MaxCLL ||
         p_dec->fmt_out.video.lighting.MaxFALL !=
         p_priv->fmt.video.lighting.MaxFALL )
    {
        msg_Dbg(p_dec, "vout update: lighting data");
        need_format_update = true;
    }

    if( need_vout )
    {
        vout_thread_t *p_vout;

        if( !p_dec->fmt_out.video.i_width ||
            !p_dec->fmt_out.video.i_height ||
            p_dec->fmt_out.video.i_width < p_dec->fmt_out.video.i_visible_width ||
            p_dec->fmt_out.video.i_height < p_dec->fmt_out.video.i_visible_height )
        {
            /* Can't create a new vout without display size */
            return -1;
        }

        video_format_t fmt = p_dec->fmt_out.video;
        fmt.i_chroma = p_dec->fmt_out.i_codec;

        if( vlc_fourcc_IsYUV( fmt.i_chroma ) )
        {
            const vlc_chroma_description_t *dsc = vlc_fourcc_GetChromaDescription( fmt.i_chroma );
            for( unsigned int i = 0; dsc && i < dsc->plane_count; i++ )
            {
                while( fmt.i_width % dsc->p[i].w.den )
                    fmt.i_width++;
                while( fmt.i_height % dsc->p[i].h.den )
                    fmt.i_height++;
            }
        }

        if( !fmt.i_visible_width || !fmt.i_visible_height )
        {
            if( p_dec->fmt_in.video.i_visible_width &&
                p_dec->fmt_in.video.i_visible_height )
            {
                fmt.i_visible_width  = p_dec->fmt_in.video.i_visible_width;
                fmt.i_visible_height = p_dec->fmt_in.video.i_visible_height;
                fmt.i_x_offset       = p_dec->fmt_in.video.i_x_offset;
                fmt.i_y_offset       = p_dec->fmt_in.video.i_y_offset;
            }
            else
            {
                fmt.i_visible_width  = fmt.i_width;
                fmt.i_visible_height = fmt.i_height;
                fmt.i_x_offset       = 0;
                fmt.i_y_offset       = 0;
            }
        }

        if( fmt.i_visible_height == 1088 &&
            var_CreateGetBool( p_dec, "hdtv-fix" ) )
        {
            fmt.i_visible_height = 1080;
            if( !(fmt.i_sar_num % 136))
            {
                fmt.i_sar_num *= 135;
                fmt.i_sar_den *= 136;
            }
            msg_Warn( p_dec, "Fixing broken HDTV stream (display_height=1088)");
        }

        if( !fmt.i_sar_num || !fmt.i_sar_den )
        {
            fmt.i_sar_num = 1;
            fmt.i_sar_den = 1;
        }

        vlc_ureduce( &fmt.i_sar_num, &fmt.i_sar_den,
                     fmt.i_sar_num, fmt.i_sar_den, 50000 );

        video_format_AdjustColorSpace( &fmt );

        vlc_mutex_lock( &p_priv->lock );

        p_vout = p_priv->p_vout;
        p_priv->p_vout = NULL; // the DecoderThread should not use the old vout anymore
        vlc_mutex_unlock( &p_priv->lock );

        unsigned dpb_size;
        switch( p_dec->fmt_in.i_codec )
        {
        case VLC_CODEC_HEVC:
        case VLC_CODEC_H264:
        case VLC_CODEC_DIRAC: /* FIXME valid ? */
            dpb_size = 18;
            break;
        case VLC_CODEC_AV1:
            dpb_size = 10;
            break;
        case VLC_CODEC_VP5:
        case VLC_CODEC_VP6:
        case VLC_CODEC_VP6F:
        case VLC_CODEC_VP8:
            dpb_size = 3;
            break;
        default:
            dpb_size = 2;
            break;
        }
        enum vlc_vout_order order;
        p_vout = input_resource_GetVout( p_priv->p_resource,
            &(vout_configuration_t) {
                .vout = p_vout, .clock = p_priv->p_clock, .fmt = &fmt,
                .dpb_size = dpb_size + p_dec->i_extra_picture_buffers + 1,
                .mouse_event = MouseEvent, .mouse_opaque = p_dec
            }, &order );
        if (p_vout)
            decoder_Notify(p_priv, on_vout_added, p_vout, order);

        vlc_mutex_lock( &p_priv->lock );
        p_priv->p_vout = p_vout;

        DecoderUpdateFormatLocked( p_priv );
        p_priv->fmt.video.i_chroma = p_dec->fmt_out.i_codec;
        vlc_mutex_unlock( &p_priv->lock );

        if( p_vout == NULL )
        {
            msg_Err( p_dec, "failed to create video output" );
            return -1;
        }

        vlc_fifo_Lock( p_priv->p_fifo );
        p_priv->reset_out_state = true;
        vlc_fifo_Unlock( p_priv->p_fifo );
    }
    else
    if ( need_format_update )
    {
        /* the format has changed but we don't need a new vout */
        vlc_mutex_lock( &p_priv->lock );
        DecoderUpdateFormatLocked( p_priv );
        vlc_mutex_unlock( &p_priv->lock );
    }
    return 0;
}

static picture_t *ModuleThread_NewVideoBuffer( decoder_t *p_dec )
{
    struct decoder_priv *p_priv = dec_get_priv( p_dec );
    assert( p_priv->p_vout );

    return vout_GetPicture( p_priv->p_vout );
}

static void DecoderThread_AbortPictures( decoder_t *p_dec, bool b_abort )
{
    struct decoder_priv *p_priv = dec_get_priv( p_dec );

    vlc_mutex_lock( &p_priv->lock ); // called in DecoderThread
    if( p_priv->p_vout != NULL )
        vout_Cancel( p_priv->p_vout, b_abort );
    vlc_mutex_unlock( &p_priv->lock );
}

static subpicture_t *ModuleThread_NewSpuBuffer( decoder_t *p_dec,
                                     const subpicture_updater_t *p_updater )
{
    struct decoder_priv *p_priv = dec_get_priv( p_dec );
    vout_thread_t *p_vout = NULL;
    subpicture_t *p_subpic;
    int i_attempts = 30;

    while( i_attempts-- )
    {
        if( p_priv->error )
            break;

        p_vout = input_resource_HoldVout( p_priv->p_resource );
        if( p_vout )
            break;

        vlc_tick_sleep( DECODER_SPU_VOUT_WAIT_DURATION );
    }

    if( !p_vout )
    {
        msg_Warn( p_dec, "no vout found, dropping subpicture" );
        if( p_priv->p_vout )
        {
            assert(p_priv->i_spu_channel != VOUT_SPU_CHANNEL_INVALID);
            decoder_Notify(p_priv, on_vout_deleted, p_priv->p_vout);

            vlc_mutex_lock( &p_priv->lock );
            vout_UnregisterSubpictureChannel(p_priv->p_vout,
                                             p_priv->i_spu_channel);
            p_priv->i_spu_channel = VOUT_SPU_CHANNEL_INVALID;

            vout_Release(p_priv->p_vout);
            p_priv->p_vout = NULL; // the DecoderThread should not use the old vout anymore
            vlc_mutex_unlock( &p_priv->lock );
        }
        return NULL;
    }

    if( p_priv->p_vout != p_vout )
    {
        if (p_priv->p_vout) /* notify the previous vout deletion unlocked */
            decoder_Notify(p_priv, on_vout_deleted, p_priv->p_vout);

        vlc_mutex_lock(&p_priv->lock);

        if (p_priv->p_vout)
        {
            /* Unregister the SPU channel of the previous vout */
            assert(p_priv->i_spu_channel != VOUT_SPU_CHANNEL_INVALID);
            vout_UnregisterSubpictureChannel(p_priv->p_vout,
                                             p_priv->i_spu_channel);
            vout_Release(p_priv->p_vout);
            p_priv->p_vout = NULL; // the DecoderThread should not use the old vout anymore
        }

        enum vlc_vout_order channel_order;
        p_priv->i_spu_channel =
            vout_RegisterSubpictureChannelInternal(p_vout, p_priv->p_clock,
                                                   &channel_order);
        p_priv->i_spu_order = 0;

        if (p_priv->i_spu_channel == VOUT_SPU_CHANNEL_INVALID)
        {
            /* The new vout doesn't support SPU, aborting... */
            vlc_mutex_unlock(&p_priv->lock);
            vout_Release(p_vout);
            return NULL;
        }

        p_priv->p_vout = p_vout;
        vlc_mutex_unlock(&p_priv->lock);

        assert(channel_order != VLC_VOUT_ORDER_NONE);
        decoder_Notify(p_priv, on_vout_added, p_vout, channel_order);
    }
    else
        vout_Release(p_vout);

    p_subpic = subpicture_New( p_updater );
    if( p_subpic )
    {
        p_subpic->i_channel = p_priv->i_spu_channel;
        p_subpic->i_order = p_priv->i_spu_order++;
        p_subpic->b_subtitle = true;
    }

    return p_subpic;
}

static int InputThread_GetInputAttachments( decoder_t *p_dec,
                                       input_attachment_t ***ppp_attachment,
                                       int *pi_attachment )
{
    struct decoder_priv *p_priv = dec_get_priv( p_dec );
    if (!p_priv->cbs || !p_priv->cbs->get_attachments)
        return VLC_ENOOBJ;

    int ret = p_priv->cbs->get_attachments(p_dec, ppp_attachment,
                                            p_priv->cbs_userdata);
    if (ret < 0)
        return VLC_EGENERIC;
    *pi_attachment = ret;
    return VLC_SUCCESS;
}

static vlc_tick_t ModuleThread_GetDisplayDate( decoder_t *p_dec,
                                       vlc_tick_t system_now, vlc_tick_t i_ts )
{
    struct decoder_priv *p_priv = dec_get_priv( p_dec );

    vlc_mutex_lock( &p_priv->lock );
    if( p_priv->b_waiting || p_priv->paused )
        i_ts = VLC_TICK_INVALID;
    float rate = p_priv->output_rate;
    vlc_mutex_unlock( &p_priv->lock );

    if( !p_priv->p_clock || i_ts == VLC_TICK_INVALID )
        return i_ts;

    return vlc_clock_ConvertToSystem( p_priv->p_clock, system_now, i_ts, rate );
}

static float ModuleThread_GetDisplayRate( decoder_t *p_dec )
{
    struct decoder_priv *p_priv = dec_get_priv( p_dec );

    if( !p_priv->p_clock )
        return 1.f;
    vlc_mutex_lock( &p_priv->lock );
    float rate = p_priv->output_rate;
    vlc_mutex_unlock( &p_priv->lock );
    return rate;
}

/*****************************************************************************
 * Public functions
 *****************************************************************************/
block_t *decoder_NewAudioBuffer( decoder_t *dec, int samples )
{
    assert( dec->fmt_out.audio.i_frame_length > 0
         && dec->fmt_out.audio.i_bytes_per_frame  > 0 );

    size_t length = samples * dec->fmt_out.audio.i_bytes_per_frame
                            / dec->fmt_out.audio.i_frame_length;
    block_t *block = block_Alloc( length );
    if( likely(block != NULL) )
    {
        block->i_nb_samples = samples;
        block->i_pts = block->i_length = 0;
    }
    return block;
}

static void RequestReload( struct decoder_priv *p_priv )
{
    /* Don't override reload if it's RELOAD_DECODER_AOUT */
    int expected = RELOAD_NO_REQUEST;
    atomic_compare_exchange_strong( &p_priv->reload, &expected, RELOAD_DECODER );
}

static void DecoderWaitUnblock( struct decoder_priv *p_priv )
{
    vlc_mutex_assert( &p_priv->lock );

    for( ;; )
    {
        if( !p_priv->b_waiting || !p_priv->b_has_data )
            break;
        vlc_cond_wait( &p_priv->wait_request, &p_priv->lock );
    }
}

static inline void DecoderUpdatePreroll( vlc_tick_t *pi_preroll, const block_t *p )
{
    if( p->i_flags & BLOCK_FLAG_PREROLL )
        *pi_preroll = PREROLL_FORCED;
    /* Check if we can use the packet for end of preroll */
    else if( (p->i_flags & BLOCK_FLAG_DISCONTINUITY) &&
             (p->i_buffer == 0 || (p->i_flags & BLOCK_FLAG_CORRUPTED)) )
        *pi_preroll = PREROLL_FORCED;
    else if( p->i_dts != VLC_TICK_INVALID )
        *pi_preroll = __MIN( *pi_preroll, p->i_dts );
    else if( p->i_pts != VLC_TICK_INVALID )
        *pi_preroll = __MIN( *pi_preroll, p->i_pts );
}

#ifdef ENABLE_SOUT
static int DecoderThread_PlaySout( struct decoder_priv *p_priv, block_t *p_sout_block )
{
    assert( !p_sout_block->p_next );

    vlc_mutex_lock( &p_priv->lock );

    if( p_priv->b_waiting )
    {
        p_priv->b_has_data = true;
        vlc_cond_signal( &p_priv->wait_acknowledge );
    }

    DecoderWaitUnblock( p_priv );

    vlc_mutex_unlock( &p_priv->lock );

    /* FIXME --VLC_TICK_INVALID inspect stream_output*/
    return sout_InputSendBuffer( p_priv->p_sout_input, p_sout_block );
}

/* This function process a block for sout
 */
static void DecoderThread_ProcessSout( struct decoder_priv *p_priv, block_t *p_block )
{
    decoder_t *p_dec = &p_priv->dec;
    block_t *p_sout_block;
    block_t **pp_block = p_block ? &p_block : NULL;

    while( ( p_sout_block =
                 p_dec->pf_packetize( p_dec, pp_block ) ) )
    {
        if( p_priv->p_sout_input == NULL )
        {
            vlc_mutex_lock( &p_priv->lock );
            DecoderUpdateFormatLocked( p_priv );

            p_priv->fmt.i_group = p_dec->fmt_in.i_group;
            p_priv->fmt.i_id = p_dec->fmt_in.i_id;
            if( p_dec->fmt_in.psz_language )
            {
                free( p_priv->fmt.psz_language );
                p_priv->fmt.psz_language =
                    strdup( p_dec->fmt_in.psz_language );
            }
            vlc_mutex_unlock( &p_priv->lock );

            p_priv->p_sout_input =
                sout_InputNew( p_priv->p_sout, &p_priv->fmt );

            if( p_priv->p_sout_input == NULL )
            {
                msg_Err( p_dec, "cannot create packetized sout output (%4.4s)",
                         (char *)&p_priv->fmt.i_codec );
                p_priv->error = true;

                if(p_block)
                    block_Release(p_block);

                block_ChainRelease(p_sout_block);
                break;
            }
        }

        while( p_sout_block )
        {
            block_t *p_next = p_sout_block->p_next;

            p_sout_block->p_next = NULL;

            if( p_priv->p_sout->b_wants_substreams && p_dec->pf_get_cc )
            {
                if( p_priv->cc.p_sout_input ||
                    !p_priv->cc.b_sout_created )
                {
                    decoder_cc_desc_t desc;
                    block_t *p_cc = p_dec->pf_get_cc( p_dec, &desc );
                    if( p_cc )
                    {
                        if(!p_priv->cc.b_sout_created)
                        {
                            es_format_t ccfmt;
                            es_format_Init(&ccfmt, SPU_ES, VLC_CODEC_CEA608);
                            ccfmt.i_group = p_priv->fmt.i_group;
                            ccfmt.subs.cc.i_reorder_depth = desc.i_reorder_depth;
                            p_priv->cc.p_sout_input = sout_InputNew( p_priv->p_sout, &ccfmt );
                            es_format_Clean(&ccfmt);
                            p_priv->cc.b_sout_created = true;
                        }

                        if( !p_priv->cc.p_sout_input ||
                            sout_InputSendBuffer( p_priv->cc.p_sout_input, p_cc ) )
                        {
                            block_Release( p_cc );
                        }
                    }
                }
            }

            if( DecoderThread_PlaySout( p_priv, p_sout_block ) == VLC_EGENERIC )
            {
                msg_Err( p_dec, "cannot continue streaming due to errors with codec %4.4s",
                                (char *)&p_priv->fmt.i_codec );

                p_priv->error = true;

                /* Cleanup */

                if( p_block )
                    block_Release( p_block );

                block_ChainRelease( p_next );
                return;
            }

            p_sout_block = p_next;
        }
    }
}
#endif

static void DecoderPlayCc( struct decoder_priv *p_priv, block_t *p_cc,
                           const decoder_cc_desc_t *p_desc )
{
    vlc_mutex_lock( &p_priv->lock );

    p_priv->cc.desc = *p_desc;

    /* Fanout data to all decoders. We do not know if es_out
       selected 608 or 708. */
    uint64_t i_bitmap = p_priv->cc.desc.i_608_channels |
                        p_priv->cc.desc.i_708_channels;

    for( int i=0; i_bitmap > 0; i_bitmap >>= 1, i++ )
    {
        decoder_t *p_ccdec = p_priv->cc.pp_decoder[i];
        struct decoder_priv *p_ccpriv = dec_get_priv( p_ccdec );
        if( !p_ccdec )
            continue;

        if( i_bitmap > 1 )
        {
            block_FifoPut( p_ccpriv->p_fifo, block_Duplicate(p_cc) );
        }
        else
        {
            block_FifoPut( p_ccpriv->p_fifo, p_cc );
            p_cc = NULL; /* was last dec */
        }
    }

    vlc_mutex_unlock( &p_priv->lock );

    if( p_cc ) /* can have bitmap set but no created decs */
        block_Release( p_cc );
}

static void PacketizerGetCc( struct decoder_priv *p_priv, decoder_t *p_dec_cc )
{
    block_t *p_cc;
    decoder_cc_desc_t desc;

    /* Do not try retreiving CC if not wanted (sout) or cannot be retreived */
    if( !p_priv->cc.b_supported )
        return;

    assert( p_dec_cc->pf_get_cc != NULL );

    p_cc = p_dec_cc->pf_get_cc( p_dec_cc, &desc );
    if( !p_cc )
        return;
    DecoderPlayCc( p_priv, p_cc, &desc );
}

static void ModuleThread_QueueCc( decoder_t *p_videodec, block_t *p_cc,
                                  const decoder_cc_desc_t *p_desc )
{
    struct decoder_priv *p_priv = dec_get_priv( p_videodec );

    if( unlikely( p_cc != NULL ) )
    {
        if( p_priv->cc.b_supported &&
           ( !p_priv->p_packetizer || !p_priv->p_packetizer->pf_get_cc ) )
            DecoderPlayCc( p_priv, p_cc, p_desc );
        else
            block_Release( p_cc );
    }
}

static int ModuleThread_PlayVideo( struct decoder_priv *p_priv, picture_t *p_picture )
{
    decoder_t *p_dec = &p_priv->dec;
    vout_thread_t  *p_vout = p_priv->p_vout;

    if( p_picture->date == VLC_TICK_INVALID )
        /* FIXME: VLC_TICK_INVALID -- verify video_output */
    {
        msg_Warn( p_dec, "non-dated video buffer received" );
        picture_Release( p_picture );
        return VLC_EGENERIC;
    }

    vlc_mutex_lock( &p_priv->lock );
    bool prerolled = p_priv->i_preroll_end != PREROLL_NONE;
    if( prerolled && p_priv->i_preroll_end > p_picture->date )
    {
        vlc_mutex_unlock( &p_priv->lock );
        picture_Release( p_picture );
        return VLC_SUCCESS;
    }

    p_priv->i_preroll_end = PREROLL_NONE;

    if( unlikely(prerolled) )
    {
        msg_Dbg( p_dec, "end of video preroll" );

        if( p_vout )
            vout_FlushAll( p_vout );
    }

    if( p_priv->b_waiting && !p_priv->b_first )
    {
        p_priv->b_has_data = true;
        vlc_cond_signal( &p_priv->wait_acknowledge );
    }

    DecoderWaitUnblock( p_priv );

    if( p_priv->b_waiting )
    {
        assert( p_priv->b_first );
        msg_Dbg( p_dec, "Received first picture" );
        p_priv->b_first = false;
        p_picture->b_force = true;
    }

    vlc_mutex_unlock( &p_priv->lock );

    /* FIXME: The *input* FIFO should not be locked here. This will not work
     * properly if/when pictures are queued asynchronously. */
    vlc_fifo_Lock( p_priv->p_fifo );
    if( unlikely(p_priv->paused) && likely(p_priv->frames_countdown > 0) )
        p_priv->frames_countdown--;
    vlc_fifo_Unlock( p_priv->p_fifo );

    /* */
    if( p_vout == NULL )
    {
        picture_Release( p_picture );
        return VLC_EGENERIC;
    }

    if( p_picture->b_still )
    {
        /* Ensure no earlier higher pts breaks still state */
        vout_Flush( p_vout, p_picture->date );
    }
    vout_PutPicture( p_vout, p_picture );

    return VLC_SUCCESS;
}

static void ModuleThread_UpdateStatVideo( struct decoder_priv *p_priv,
                                          bool lost )
{
    unsigned displayed = 0;
    unsigned vout_lost = 0;
    if( p_priv->p_vout != NULL )
    {
        vout_GetResetStatistic( p_priv->p_vout, &displayed, &vout_lost );
    }
    if (lost) vout_lost++;

    decoder_Notify(p_priv, on_new_video_stats, 1, vout_lost, displayed);
}

static void ModuleThread_QueueVideo( decoder_t *p_dec, picture_t *p_pic )
{
    assert( p_pic );
    struct decoder_priv *p_priv = dec_get_priv( p_dec );

    int success = ModuleThread_PlayVideo( p_priv, p_pic );

    ModuleThread_UpdateStatVideo( p_priv, success != VLC_SUCCESS );
}

static int thumbnailer_update_format( decoder_t *p_dec )
{
    p_dec->fmt_out.video.i_chroma = p_dec->fmt_out.i_codec;
    return 0;
}

static picture_t *thumbnailer_buffer_new( decoder_t *p_dec )
{
    struct decoder_priv *p_priv = dec_get_priv( p_dec );
    /* Avoid decoding more than one frame when a thumbnail was
     * already generated */
    if( !p_priv->b_first )
        return NULL;
    return picture_NewFromFormat( &p_dec->fmt_out.video );
}

static void ModuleThread_QueueThumbnail( decoder_t *p_dec, picture_t *p_pic )
{
    struct decoder_priv *p_priv = dec_get_priv( p_dec );
    if( p_priv->b_first )
    {
        decoder_Notify(p_priv, on_thumbnail_ready, p_pic);
        p_priv->b_first = false;
    }
    picture_Release( p_pic );

}

static int ModuleThread_PlayAudio( struct decoder_priv *p_priv, block_t *p_audio )
{
    decoder_t *p_dec = &p_priv->dec;

    assert( p_audio != NULL );

    if( p_audio->i_pts == VLC_TICK_INVALID ) // FIXME --VLC_TICK_INVALID verify audio_output/*
    {
        msg_Warn( p_dec, "non-dated audio buffer received" );
        block_Release( p_audio );
        return VLC_EGENERIC;
    }

    vlc_mutex_lock( &p_priv->lock );
    bool prerolled = p_priv->i_preroll_end != PREROLL_NONE;
    if( prerolled && p_priv->i_preroll_end > p_audio->i_pts )
    {
        vlc_mutex_unlock( &p_priv->lock );
        block_Release( p_audio );
        return VLC_SUCCESS;
    }

    p_priv->i_preroll_end = PREROLL_NONE;
    vlc_mutex_unlock( &p_priv->lock );

    if( unlikely(prerolled) )
    {
        msg_Dbg( p_dec, "end of audio preroll" );

        if( p_priv->p_aout )
            aout_DecFlush( p_priv->p_aout );
    }

    /* */
    /* */
    vlc_mutex_lock( &p_priv->lock );
    if( p_priv->b_waiting )
    {
        p_priv->b_has_data = true;
        vlc_cond_signal( &p_priv->wait_acknowledge );
    }

    /* */
    DecoderWaitUnblock( p_priv );
    vlc_mutex_unlock( &p_priv->lock );

    audio_output_t *p_aout = p_priv->p_aout;

    if( p_aout == NULL )
    {
        msg_Dbg( p_dec, "discarded audio buffer" );
        block_Release( p_audio );
        return VLC_EGENERIC;
    }

    int status = aout_DecPlay( p_aout, p_audio );
    if( status == AOUT_DEC_CHANGED )
    {
        /* Only reload the decoder */
        RequestReload( p_priv );
    }
    else if( status == AOUT_DEC_FAILED )
    {
        /* If we reload because the aout failed, we should release it. That
            * way, a next call to ModuleThread_UpdateAudioFormat() won't re-use the
            * previous (failing) aout but will try to create a new one. */
        atomic_store( &p_priv->reload, RELOAD_DECODER_AOUT );
    }
    return VLC_SUCCESS;
}

static void ModuleThread_UpdateStatAudio( struct decoder_priv *p_priv,
                                          bool lost )
{
    unsigned played = 0;
    unsigned aout_lost = 0;
    if( p_priv->p_aout != NULL )
    {
        aout_DecGetResetStats( p_priv->p_aout, &aout_lost, &played );
    }
    if (lost) aout_lost++;

    decoder_Notify(p_priv, on_new_audio_stats, 1, aout_lost, played);
}

static void ModuleThread_QueueAudio( decoder_t *p_dec, block_t *p_aout_buf )
{
    struct decoder_priv *p_priv = dec_get_priv( p_dec );

    int success = ModuleThread_PlayAudio( p_priv, p_aout_buf );

    ModuleThread_UpdateStatAudio( p_priv, success != VLC_SUCCESS );
}

static void ModuleThread_PlaySpu( struct decoder_priv *p_priv, subpicture_t *p_subpic )
{
    decoder_t *p_dec = &p_priv->dec;
    vout_thread_t *p_vout = p_priv->p_vout;

    /* */
    if( p_subpic->i_start == VLC_TICK_INVALID )
    {
        msg_Warn( p_dec, "non-dated spu buffer received" );
        subpicture_Delete( p_subpic );
        return;
    }

    /* */
    vlc_mutex_lock( &p_priv->lock );

    if( p_priv->b_waiting )
    {
        p_priv->b_has_data = true;
        vlc_cond_signal( &p_priv->wait_acknowledge );
    }

    DecoderWaitUnblock( p_priv );
    vlc_mutex_unlock( &p_priv->lock );

    if( p_subpic->i_start == VLC_TICK_INVALID )
    {
        subpicture_Delete( p_subpic );
        return;
    }

    vout_PutSubpicture( p_vout, p_subpic );
}

static void ModuleThread_QueueSpu( decoder_t *p_dec, subpicture_t *p_spu )
{
    assert( p_spu );
    struct decoder_priv *p_priv = dec_get_priv( p_dec );

    /* The vout must be created from a previous decoder_NewSubpicture call. */
    assert( p_priv->p_vout );

    /* Preroll does not work very well with subtitle */
    vlc_mutex_lock( &p_priv->lock );
    if( p_spu->i_start != VLC_TICK_INVALID &&
        p_spu->i_start < p_priv->i_preroll_end &&
        ( p_spu->i_stop == VLC_TICK_INVALID || p_spu->i_stop < p_priv->i_preroll_end ) )
    {
        vlc_mutex_unlock( &p_priv->lock );
        subpicture_Delete( p_spu );
    }
    else
    {
        vlc_mutex_unlock( &p_priv->lock );
        ModuleThread_PlaySpu( p_priv, p_spu );
    }
}

static void DecoderThread_ProcessInput( struct decoder_priv *p_priv, block_t *p_block );
static void DecoderThread_DecodeBlock( struct decoder_priv *p_priv, block_t *p_block )
{
    decoder_t *p_dec = &p_priv->dec;

    int ret = p_dec->pf_decode( p_dec, p_block );
    switch( ret )
    {
        case VLCDEC_SUCCESS:
            break;
        case VLCDEC_ECRITICAL:
            p_priv->error = true;
            break;
        case VLCDEC_RELOAD:
            RequestReload( p_priv );
            if( unlikely( p_block == NULL ) )
                break;
            if( !( p_block->i_flags & BLOCK_FLAG_CORE_PRIVATE_RELOADED ) )
            {
                p_block->i_flags |= BLOCK_FLAG_CORE_PRIVATE_RELOADED;
                DecoderThread_ProcessInput( p_priv, p_block );
            }
            else /* We prefer loosing this block than an infinite recursion */
                block_Release( p_block );
            break;
        default:
            vlc_assert_unreachable();
    }
}

/**
 * Decode a block
 *
 * \param p_dec the decoder object
 * \param p_block the block to decode
 */
static void DecoderThread_ProcessInput( struct decoder_priv *p_priv, block_t *p_block )
{
    decoder_t *p_dec = &p_priv->dec;

    if( p_priv->error )
        goto error;

    /* Here, the atomic doesn't prevent to miss a reload request.
     * DecoderThread_ProcessInput() can still be called after the decoder module or the
     * audio output requested a reload. This will only result in a drop of an
     * input block or an output buffer. */
    enum reload reload;
    if( ( reload = atomic_exchange( &p_priv->reload, RELOAD_NO_REQUEST ) ) )
    {
        msg_Warn( p_dec, "Reloading the decoder module%s",
                  reload == RELOAD_DECODER_AOUT ? " and the audio output" : "" );

        if( DecoderThread_Reload( p_priv, false, &p_dec->fmt_in, reload ) != VLC_SUCCESS )
            goto error;
    }

    bool packetize = p_priv->p_packetizer != NULL;
    if( p_block )
    {
        if( p_block->i_buffer <= 0 )
            goto error;

        vlc_mutex_lock( &p_priv->lock );
        DecoderUpdatePreroll( &p_priv->i_preroll_end, p_block );
        vlc_mutex_unlock( &p_priv->lock );
        if( unlikely( p_block->i_flags & BLOCK_FLAG_CORE_PRIVATE_RELOADED ) )
        {
            /* This block has already been packetized */
            packetize = false;
        }
    }

#ifdef ENABLE_SOUT
    if( p_priv->p_sout != NULL )
    {
        DecoderThread_ProcessSout( p_priv, p_block );
        return;
    }
#endif
    if( packetize )
    {
        block_t *p_packetized_block;
        block_t **pp_block = p_block ? &p_block : NULL;
        decoder_t *p_packetizer = p_priv->p_packetizer;

        while( (p_packetized_block =
                p_packetizer->pf_packetize( p_packetizer, pp_block ) ) )
        {
            if( !es_format_IsSimilar( &p_dec->fmt_in, &p_packetizer->fmt_out ) )
            {
                msg_Dbg( p_dec, "restarting module due to input format change");

                /* Drain the decoder module */
                DecoderThread_DecodeBlock( p_priv, NULL );

                if( DecoderThread_Reload( p_priv, false, &p_packetizer->fmt_out,
                                          RELOAD_DECODER ) != VLC_SUCCESS )
                {
                    block_ChainRelease( p_packetized_block );
                    return;
                }
            }

            if( p_packetizer->pf_get_cc )
                PacketizerGetCc( p_priv, p_packetizer );

            while( p_packetized_block )
            {
                block_t *p_next = p_packetized_block->p_next;
                p_packetized_block->p_next = NULL;

                DecoderThread_DecodeBlock( p_priv, p_packetized_block );
                if( p_priv->error )
                {
                    block_ChainRelease( p_next );
                    return;
                }

                p_packetized_block = p_next;
            }
        }
        /* Drain the decoder after the packetizer is drained */
        if( !pp_block )
            DecoderThread_DecodeBlock( p_priv, NULL );
    }
    else
        DecoderThread_DecodeBlock( p_priv, p_block );
    return;

error:
    if( p_block )
        block_Release( p_block );
}

static void DecoderThread_Flush( struct decoder_priv *p_priv )
{
    decoder_t *p_dec = &p_priv->dec;
    decoder_t *p_packetizer = p_priv->p_packetizer;

    if( p_priv->error )
        return;

    if( p_packetizer != NULL && p_packetizer->pf_flush != NULL )
        p_packetizer->pf_flush( p_packetizer );

    if ( p_dec->pf_flush != NULL )
        p_dec->pf_flush( p_dec );

    /* flush CC sub decoders */
    if( p_priv->cc.b_supported )
    {
        for( int i=0; i<MAX_CC_DECODERS; i++ )
        {
            decoder_t *p_subdec = p_priv->cc.pp_decoder[i];
            if( p_subdec && p_subdec->pf_flush )
                p_subdec->pf_flush( p_subdec );
        }
    }

    vlc_mutex_lock( &p_priv->lock );
#ifdef ENABLE_SOUT
    if ( p_priv->p_sout_input != NULL )
    {
        sout_InputFlush( p_priv->p_sout_input );
    }
#endif
    if( p_dec->fmt_out.i_cat == AUDIO_ES )
    {
        if( p_priv->p_aout )
            aout_DecFlush( p_priv->p_aout );
    }
    else if( p_dec->fmt_out.i_cat == VIDEO_ES )
    {
        if( p_priv->p_vout )
            vout_FlushAll( p_priv->p_vout );
    }
    else if( p_dec->fmt_out.i_cat == SPU_ES )
    {
        if( p_priv->p_vout )
        {
            assert( p_priv->i_spu_channel != VOUT_SPU_CHANNEL_INVALID );
            vout_FlushSubpictureChannel( p_priv->p_vout, p_priv->i_spu_channel );
        }
    }

    p_priv->i_preroll_end = PREROLL_NONE;
    vlc_mutex_unlock( &p_priv->lock );
}

static void DecoderThread_ChangePause( struct decoder_priv *p_priv, bool paused, vlc_tick_t date )
{
    decoder_t *p_dec = &p_priv->dec;

    msg_Dbg( p_dec, "toggling %s", paused ? "resume" : "pause" );
    switch( p_dec->fmt_out.i_cat )
    {
        case VIDEO_ES:
            vlc_mutex_lock( &p_priv->lock );
            if( p_priv->p_vout != NULL )
                vout_ChangePause( p_priv->p_vout, paused, date );
            vlc_mutex_unlock( &p_priv->lock );
            break;
        case AUDIO_ES:
            vlc_mutex_lock( &p_priv->lock );
            if( p_priv->p_aout != NULL )
                aout_DecChangePause( p_priv->p_aout, paused, date );
            vlc_mutex_unlock( &p_priv->lock );
            break;
        case SPU_ES:
            break;
        default:
            vlc_assert_unreachable();
    }
}

static void DecoderThread_ChangeRate( struct decoder_priv *p_priv, float rate )
{
    decoder_t *p_dec = &p_priv->dec;

    msg_Dbg( p_dec, "changing rate: %f", rate );
    vlc_mutex_lock( &p_priv->lock );
    switch( p_dec->fmt_out.i_cat )
    {
        case VIDEO_ES:
            if( p_priv->p_vout != NULL )
                vout_ChangeRate( p_priv->p_vout, rate );
            break;
        case AUDIO_ES:
            if( p_priv->p_aout != NULL )
                aout_DecChangeRate( p_priv->p_aout, rate );
            break;
        case SPU_ES:
            if( p_priv->p_vout != NULL )
            {
                assert(p_priv->i_spu_channel != VOUT_SPU_CHANNEL_INVALID);
                vout_ChangeSpuRate(p_priv->p_vout, p_priv->i_spu_channel,
                                   rate );
            }
            break;
        default:
            vlc_assert_unreachable();
    }
    p_priv->output_rate = rate;
    vlc_mutex_unlock( &p_priv->lock );
}

static void DecoderThread_ChangeDelay( struct decoder_priv *p_priv, vlc_tick_t delay )
{
    decoder_t *p_dec = &p_priv->dec;

    msg_Dbg( p_dec, "changing delay: %"PRId64, delay );

    switch( p_dec->fmt_out.i_cat )
    {
        case VIDEO_ES:
            vlc_mutex_lock( &p_priv->lock );
            if( p_priv->p_vout != NULL )
                vout_ChangeDelay( p_priv->p_vout, delay );
            vlc_mutex_unlock( &p_priv->lock );
            break;
        case AUDIO_ES:
            vlc_mutex_lock( &p_priv->lock );
            if( p_priv->p_aout != NULL )
                aout_DecChangeDelay( p_priv->p_aout, delay );
            vlc_mutex_unlock( &p_priv->lock );
            break;
        case SPU_ES:
            vlc_mutex_lock( &p_priv->lock );
            if( p_priv->p_vout != NULL )
            {
                assert(p_priv->i_spu_channel != VOUT_SPU_CHANNEL_INVALID);
                vout_ChangeSpuDelay(p_priv->p_vout, p_priv->i_spu_channel,
                                    delay);
            }
            vlc_mutex_unlock( &p_priv->lock );
            break;
        default:
            vlc_assert_unreachable();
    }
}

/**
 * The decoding main loop
 *
 * \param p_dec the decoder
 */
static void *DecoderThread( void *p_data )
{
    struct decoder_priv *p_priv = (struct decoder_priv *)p_data;
    float rate = 1.f;
    vlc_tick_t delay = 0;
    bool paused = false;

    /* The decoder's main loop */
    vlc_fifo_Lock( p_priv->p_fifo );
    vlc_fifo_CleanupPush( p_priv->p_fifo );

    for( ;; )
    {
        if( p_priv->flushing )
        {   /* Flush before/regardless of pause. We do not want to resume just
             * for the sake of flushing (glitches could otherwise happen). */
            int canc = vlc_savecancel();

            vlc_fifo_Unlock( p_priv->p_fifo );

            /* Flush the decoder (and the output) */
            DecoderThread_Flush( p_priv );

            vlc_fifo_Lock( p_priv->p_fifo );
            vlc_restorecancel( canc );

            /* Reset flushing after DecoderThread_ProcessInput in case input_DecoderFlush
             * is called again. This will avoid a second useless flush (but
             * harmless). */
            p_priv->flushing = false;

            continue;
        }

        /* Reset the original pause/rate state when a new aout/vout is created:
         * this will trigger the DecoderThread_ChangePause/DecoderThread_ChangeRate code path
         * if needed. */
        if( p_priv->reset_out_state )
        {
            rate = 1.f;
            paused = false;
            delay = 0;
            p_priv->reset_out_state = false;
        }

        if( paused != p_priv->paused )
        {   /* Update playing/paused status of the output */
            int canc = vlc_savecancel();
            vlc_tick_t date = p_priv->pause_date;

            paused = p_priv->paused;
            vlc_fifo_Unlock( p_priv->p_fifo );

            DecoderThread_ChangePause( p_priv, paused, date );

            vlc_restorecancel( canc );
            vlc_fifo_Lock( p_priv->p_fifo );
            continue;
        }

        if( rate != p_priv->request_rate )
        {
            int canc = vlc_savecancel();

            rate = p_priv->request_rate;
            vlc_fifo_Unlock( p_priv->p_fifo );

            DecoderThread_ChangeRate( p_priv, rate );

            vlc_restorecancel( canc );
            vlc_fifo_Lock( p_priv->p_fifo );
        }

        if( delay != p_priv->delay )
        {
            int canc = vlc_savecancel();

            delay = p_priv->delay;
            vlc_fifo_Unlock( p_priv->p_fifo );

            DecoderThread_ChangeDelay( p_priv, delay );

            vlc_restorecancel( canc );
            vlc_fifo_Lock( p_priv->p_fifo );
        }

        if( p_priv->paused && p_priv->frames_countdown == 0 )
        {   /* Wait for resumption from pause */
            p_priv->b_idle = true;
            vlc_cond_signal( &p_priv->wait_acknowledge );
            vlc_fifo_Wait( p_priv->p_fifo );
            p_priv->b_idle = false;
            continue;
        }

        vlc_cond_signal( &p_priv->wait_fifo );
        vlc_testcancel(); /* forced expedited cancellation in case of stop */

        block_t *p_block = vlc_fifo_DequeueUnlocked( p_priv->p_fifo );
        if( p_block == NULL )
        {
            if( likely(!p_priv->b_draining) )
            {   /* Wait for a block to decode (or a request to drain) */
                p_priv->b_idle = true;
                vlc_cond_signal( &p_priv->wait_acknowledge );
                vlc_fifo_Wait( p_priv->p_fifo );
                p_priv->b_idle = false;
                continue;
            }
            /* We have emptied the FIFO and there is a pending request to
             * drain. Pass p_block = NULL to decoder just once. */
        }

        vlc_fifo_Unlock( p_priv->p_fifo );

        int canc = vlc_savecancel();
        DecoderThread_ProcessInput( p_priv, p_block );

        if( p_block == NULL && p_priv->dec.fmt_out.i_cat == AUDIO_ES )
        {   /* Draining: the decoder is drained and all decoded buffers are
             * queued to the output at this point. Now drain the output. */
            if( p_priv->p_aout != NULL )
                aout_DecDrain( p_priv->p_aout );
        }
        vlc_restorecancel( canc );

        /* TODO? Wait for draining instead of polling. */
        vlc_mutex_lock( &p_priv->lock );
        vlc_fifo_Lock( p_priv->p_fifo );
        if( p_priv->b_draining && (p_block == NULL) )
        {
            p_priv->b_draining = false;
            p_priv->drained = true;
        }
        vlc_cond_signal( &p_priv->wait_acknowledge );
        vlc_mutex_unlock( &p_priv->lock );
    }
    vlc_cleanup_pop();
    vlc_assert_unreachable();
}

static const struct decoder_owner_ops dec_video_ops =
{
    .video = {
        .format_update = ModuleThread_UpdateVideoFormat,
        .buffer_new = ModuleThread_NewVideoBuffer,
        .abort_pictures = DecoderThread_AbortPictures,
        .queue = ModuleThread_QueueVideo,
        .queue_cc = ModuleThread_QueueCc,
        .get_display_date = ModuleThread_GetDisplayDate,
        .get_display_rate = ModuleThread_GetDisplayRate,
    },
    .get_attachments = InputThread_GetInputAttachments,
};
static const struct decoder_owner_ops dec_thumbnailer_ops =
{
    .video = {
        .format_update = thumbnailer_update_format,
        .buffer_new = thumbnailer_buffer_new,
        .queue = ModuleThread_QueueThumbnail,
    },
    .get_attachments = InputThread_GetInputAttachments,
};
static const struct decoder_owner_ops dec_audio_ops =
{
    .audio = {
        .format_update = ModuleThread_UpdateAudioFormat,
        .queue = ModuleThread_QueueAudio,
    },
    .get_attachments = InputThread_GetInputAttachments,
};
static const struct decoder_owner_ops dec_spu_ops =
{
    .spu = {
        .buffer_new = ModuleThread_NewSpuBuffer,
        .queue = ModuleThread_QueueSpu,
    },
    .get_attachments = InputThread_GetInputAttachments,
};

/**
 * Create a decoder object
 *
 * \param p_input the input thread
 * \param p_es the es descriptor
 * \param b_packetizer instead of a decoder
 * \return the decoder object
 */
static struct decoder_priv * CreateDecoder( vlc_object_t *p_parent,
                                  const es_format_t *fmt, vlc_clock_t *p_clock,
                                  input_resource_t *p_resource,
                                  sout_instance_t *p_sout, bool b_thumbnailing,
                                  const struct input_decoder_callbacks *cbs,
                                  void *cbs_userdata )
{
    decoder_t *p_dec;
    struct decoder_priv *p_priv;
    static_assert(offsetof(struct decoder_priv, dec) == 0,
                  "the decoder must be first in the priv structure");

    p_priv = vlc_custom_create( p_parent, sizeof( *p_priv ), "decoder" );
    if( p_priv == NULL )
        return NULL;
    p_dec = &p_priv->dec;

    p_priv->p_clock = p_clock;
    p_priv->i_preroll_end = PREROLL_NONE;
    p_priv->p_resource = p_resource;
    p_priv->cbs = cbs;
    p_priv->cbs_userdata = cbs_userdata;
    p_priv->p_aout = NULL;
    p_priv->p_vout = NULL;
    p_priv->i_spu_channel = VOUT_SPU_CHANNEL_INVALID;
    p_priv->i_spu_order = 0;
    p_priv->p_sout = p_sout;
    p_priv->p_sout_input = NULL;
    p_priv->p_packetizer = NULL;

    atomic_init( &p_priv->b_fmt_description, false );
    p_priv->p_description = NULL;

    p_priv->reset_out_state = false;
    p_priv->delay = 0;
    p_priv->output_rate = p_priv->request_rate = 1.f;
    p_priv->paused = false;
    p_priv->pause_date = VLC_TICK_INVALID;
    p_priv->frames_countdown = 0;

    p_priv->b_waiting = false;
    p_priv->b_first = true;
    p_priv->b_has_data = false;

    p_priv->error = false;

    p_priv->flushing = false;
    p_priv->b_draining = false;
    p_priv->drained = false;
    atomic_init( &p_priv->reload, RELOAD_NO_REQUEST );
    p_priv->b_idle = false;

    p_priv->mouse_event = NULL;
    p_priv->mouse_opaque = NULL;

    es_format_Init( &p_priv->fmt, fmt->i_cat, 0 );

    /* decoder fifo */
    p_priv->p_fifo = block_FifoNew();
    if( unlikely(p_priv->p_fifo == NULL) )
    {
        vlc_object_delete(p_dec);
        return NULL;
    }

    vlc_mutex_init( &p_priv->lock );
    vlc_mutex_init( &p_priv->mouse_lock );
    vlc_cond_init( &p_priv->wait_request );
    vlc_cond_init( &p_priv->wait_acknowledge );
    vlc_cond_init( &p_priv->wait_fifo );

    /* Load a packetizer module if the input is not already packetized */
    if( p_sout == NULL && !fmt->b_packetized )
    {
        p_priv->p_packetizer =
            vlc_custom_create( p_parent, sizeof( decoder_t ), "packetizer" );
        if( p_priv->p_packetizer )
        {
            if( LoadDecoder( p_priv->p_packetizer, true, fmt ) )
            {
                vlc_object_delete(p_priv->p_packetizer);
                p_priv->p_packetizer = NULL;
            }
            else
            {
                p_priv->p_packetizer->fmt_out.b_packetized = true;
                fmt = &p_priv->p_packetizer->fmt_out;
            }
        }
    }

    switch( fmt->i_cat )
    {
        case VIDEO_ES:
            if( !b_thumbnailing )
                p_dec->owner_ops = &dec_video_ops;
            else
                p_dec->owner_ops = &dec_thumbnailer_ops;
            break;
        case AUDIO_ES:
            p_dec->owner_ops = &dec_audio_ops;
            break;
        case SPU_ES:
            p_dec->owner_ops = &dec_spu_ops;
            break;
        default:
            msg_Err( p_dec, "unknown ES format" );
            return p_priv;
    }

    /* Find a suitable decoder/packetizer module */
    if( LoadDecoder( p_dec, p_sout != NULL, fmt ) )
        return p_priv;

    assert( p_dec->fmt_in.i_cat == p_dec->fmt_out.i_cat && fmt->i_cat == p_dec->fmt_in.i_cat);

    /* Copy ourself the input replay gain */
    if( fmt->i_cat == AUDIO_ES )
    {
        for( unsigned i = 0; i < AUDIO_REPLAY_GAIN_MAX; i++ )
        {
            if( !p_dec->fmt_out.audio_replay_gain.pb_peak[i] )
            {
                p_dec->fmt_out.audio_replay_gain.pb_peak[i] = fmt->audio_replay_gain.pb_peak[i];
                p_dec->fmt_out.audio_replay_gain.pf_peak[i] = fmt->audio_replay_gain.pf_peak[i];
            }
            if( !p_dec->fmt_out.audio_replay_gain.pb_gain[i] )
            {
                p_dec->fmt_out.audio_replay_gain.pb_gain[i] = fmt->audio_replay_gain.pb_gain[i];
                p_dec->fmt_out.audio_replay_gain.pf_gain[i] = fmt->audio_replay_gain.pf_gain[i];
            }
        }
    }

    /* */
    p_priv->cc.b_supported = ( p_sout == NULL );

    p_priv->cc.desc.i_608_channels = 0;
    p_priv->cc.desc.i_708_channels = 0;
    for( unsigned i = 0; i < MAX_CC_DECODERS; i++ )
        p_priv->cc.pp_decoder[i] = NULL;
    p_priv->cc.p_sout_input = NULL;
    p_priv->cc.b_sout_created = false;
    return p_priv;
}

/**
 * Destroys a decoder object
 *
 * \param p_dec the decoder object
 * \return nothing
 */
static void DeleteDecoder( decoder_t * p_dec )
{
    struct decoder_priv *p_priv = dec_get_priv( p_dec );

    msg_Dbg( p_dec, "killing decoder fourcc `%4.4s'",
             (char*)&p_dec->fmt_in.i_codec );

    const enum es_format_category_e i_cat =p_dec->fmt_in.i_cat;
    decoder_Clean( p_dec );

    /* Free all packets still in the decoder fifo. */
    block_FifoRelease( p_priv->p_fifo );

    /* Cleanup */
#ifdef ENABLE_SOUT
    if( p_priv->p_sout_input )
    {
        sout_InputDelete( p_priv->p_sout_input );
        if( p_priv->cc.p_sout_input )
            sout_InputDelete( p_priv->cc.p_sout_input );
    }
#endif

    switch( i_cat )
    {
        case AUDIO_ES:
            if( p_priv->p_aout )
            {
                /* TODO: REVISIT gap-less audio */
                aout_DecDelete( p_priv->p_aout );
                input_resource_PutAout( p_priv->p_resource, p_priv->p_aout );
            }
            break;
        case VIDEO_ES: {
            vout_thread_t *vout = p_priv->p_vout;

            if (vout != NULL)
            {
                /* Reset the cancel state that was set before joining the decoder
                 * thread */
                vout_Cancel(vout, false);
                decoder_Notify(p_priv, on_vout_deleted, vout);
                input_resource_PutVout(p_priv->p_resource, vout);
            }
            break;
        }
        case SPU_ES:
        {
            if( p_priv->p_vout )
            {
                assert( p_priv->i_spu_channel != VOUT_SPU_CHANNEL_INVALID );
                decoder_Notify(p_priv, on_vout_deleted, p_priv->p_vout);

                vout_UnregisterSubpictureChannel( p_priv->p_vout,
                                                  p_priv->i_spu_channel );
                vout_Release(p_priv->p_vout);
            }
            break;
        }
        case DATA_ES:
        case UNKNOWN_ES:
            break;
        default:
            vlc_assert_unreachable();
    }

    es_format_Clean( &p_priv->fmt );

    if( p_priv->p_description )
        vlc_meta_Delete( p_priv->p_description );

    decoder_Destroy( p_priv->p_packetizer );

    vlc_cond_destroy( &p_priv->wait_fifo );
    vlc_cond_destroy( &p_priv->wait_acknowledge );
    vlc_cond_destroy( &p_priv->wait_request );
    vlc_mutex_destroy( &p_priv->lock );
    vlc_mutex_destroy( &p_priv->mouse_lock );

    decoder_Destroy( &p_priv->dec );
}

/* */
static void DecoderUnsupportedCodec( decoder_t *p_dec, const es_format_t *fmt, bool b_decoding )
{
    if (fmt->i_codec != VLC_CODEC_UNKNOWN && fmt->i_codec) {
        const char *desc = vlc_fourcc_GetDescription(fmt->i_cat, fmt->i_codec);
        if (!desc || !*desc)
            desc = N_("No description for this codec");
        msg_Err( p_dec, "Codec `%4.4s' (%s) is not supported.", (char*)&fmt->i_codec, desc );
        vlc_dialog_display_error( p_dec, _("Codec not supported"),
            _("VLC could not decode the format \"%4.4s\" (%s)"),
            (char*)&fmt->i_codec, desc );
    } else if( b_decoding ){
        msg_Err( p_dec, "could not identify codec" );
        vlc_dialog_display_error( p_dec, _("Unidentified codec"),
            _("VLC could not identify the audio or video codec" ) );
    }
}

/* TODO: pass p_sout through p_resource? -- Courmisch */
static decoder_t *decoder_New( vlc_object_t *p_parent, const es_format_t *fmt,
                               vlc_clock_t *p_clock, input_resource_t *p_resource,
                               sout_instance_t *p_sout, bool thumbnailing,
                               const struct input_decoder_callbacks *cbs,
                               void *userdata)
{
    const char *psz_type = p_sout ? N_("packetizer") : N_("decoder");
    int i_priority;

    /* Create the decoder configuration structure */
    struct decoder_priv *p_priv = CreateDecoder( p_parent, fmt, p_clock, p_resource, p_sout,
                           thumbnailing, cbs, userdata );
    if( p_priv == NULL )
    {
        msg_Err( p_parent, "could not create %s", psz_type );
        vlc_dialog_display_error( p_parent, _("Streaming / Transcoding failed"),
            _("VLC could not open the %s module."), vlc_gettext( psz_type ) );
        return NULL;
    }

    decoder_t *p_dec = &p_priv->dec;
    if( !p_dec->p_module )
    {
        DecoderUnsupportedCodec( p_dec, fmt, !p_sout );

        DeleteDecoder( p_dec );
        return NULL;
    }

    assert( p_dec->fmt_in.i_cat != UNKNOWN_ES );

    if( p_dec->fmt_in.i_cat == AUDIO_ES )
        i_priority = VLC_THREAD_PRIORITY_AUDIO;
    else
        i_priority = VLC_THREAD_PRIORITY_VIDEO;

#ifdef ENABLE_SOUT
    /* Do not delay sout creation for SPU or DATA. */
    if( p_sout && fmt->b_packetized &&
        (fmt->i_cat != VIDEO_ES && fmt->i_cat != AUDIO_ES) )
    {
        p_priv->p_sout_input = sout_InputNew( p_priv->p_sout, fmt );
        if( p_priv->p_sout_input == NULL )
        {
            msg_Err( p_dec, "cannot create sout input (%4.4s)",
                     (char *)&fmt->i_codec );
            p_priv->error = true;
        }
    }
#endif

    /* Spawn the decoder thread */
    if( vlc_clone( &p_priv->thread, DecoderThread, p_priv, i_priority ) )
    {
        msg_Err( p_dec, "cannot spawn decoder thread" );
        DeleteDecoder( p_dec );
        return NULL;
    }

    return p_dec;
}


/**
 * Spawns a new decoder thread from the input thread
 *
 * \param p_input the input thread
 * \param p_es the es descriptor
 * \return the spawned decoder object
 */
decoder_t *input_DecoderNew( vlc_object_t *parent, es_format_t *fmt,
                             vlc_clock_t *p_clock, input_resource_t *resource,
                             sout_instance_t *p_sout, bool thumbnailing,
                             const struct input_decoder_callbacks *cbs,
                             void *cbs_userdata)
{
    return decoder_New( parent, fmt, p_clock, resource, p_sout, thumbnailing,
                        cbs, cbs_userdata );
}

/**
 * Spawn a decoder thread outside of the input thread.
 */
decoder_t *input_DecoderCreate( vlc_object_t *p_parent, const es_format_t *fmt,
                                input_resource_t *p_resource )
{
    return decoder_New( p_parent, fmt, NULL, p_resource, NULL, false, NULL,
                        NULL );
}


/**
 * Kills a decoder thread and waits until it's finished
 *
 * \param p_input the input thread
 * \param p_es the es descriptor
 * \return nothing
 */
void input_DecoderDelete( decoder_t *p_dec )
{
    struct decoder_priv *p_priv = dec_get_priv( p_dec );

    vlc_cancel( p_priv->thread );

    vlc_fifo_Lock( p_priv->p_fifo );
    p_priv->flushing = true;
    vlc_fifo_Unlock( p_priv->p_fifo );

    /* Make sure we aren't waiting/decoding anymore */
    vlc_mutex_lock( &p_priv->lock );
    p_priv->b_waiting = false;
    vlc_cond_signal( &p_priv->wait_request );

    /* If the video output is paused or slow, or if the picture pool size was
     * under-estimated (e.g. greedy video filter, buggy decoder...), the
     * the picture pool may be empty, and the decoder thread or any decoder
     * module worker threads may be stuck waiting for free picture buffers.
     *
     * This unblocks the thread, allowing the decoder module to join all its
     * worker threads (if any) and the decoder thread to terminate. */
    if( p_dec->fmt_in.i_cat == VIDEO_ES && p_priv->p_vout != NULL )
        vout_Cancel( p_priv->p_vout, true );
    vlc_mutex_unlock( &p_priv->lock );

    vlc_join( p_priv->thread, NULL );

    /* */
    if( p_priv->cc.b_supported )
    {
        for( int i = 0; i < MAX_CC_DECODERS; i++ )
            input_DecoderSetCcState( p_dec, VLC_CODEC_CEA608, i, false );
    }

    /* Delete decoder */
    DeleteDecoder( p_dec );
}

/**
 * Put a block_t in the decoder's fifo.
 * Thread-safe w.r.t. the decoder. May be a cancellation point.
 *
 * \param p_dec the decoder object
 * \param p_block the data block
 */
void input_DecoderDecode( decoder_t *p_dec, block_t *p_block, bool b_do_pace )
{
    struct decoder_priv *p_priv = dec_get_priv( p_dec );

    vlc_fifo_Lock( p_priv->p_fifo );
    if( !b_do_pace )
    {
        /* FIXME: ideally we would check the time amount of data
         * in the FIFO instead of its size. */
        /* 400 MiB, i.e. ~ 50mb/s for 60s */
        if( vlc_fifo_GetBytes( p_priv->p_fifo ) > 400*1024*1024 )
        {
            msg_Warn( p_dec, "decoder/packetizer fifo full (data not "
                      "consumed quickly enough), resetting fifo!" );
            block_ChainRelease( vlc_fifo_DequeueAllUnlocked( p_priv->p_fifo ) );
            p_block->i_flags |= BLOCK_FLAG_DISCONTINUITY;
        }
    }
    else
    if( !p_priv->b_waiting )
    {   /* The FIFO is not consumed when waiting, so pacing would deadlock VLC.
         * Locking is not necessary as b_waiting is only read, not written by
         * the decoder thread. */
        while( vlc_fifo_GetCount( p_priv->p_fifo ) >= 10 )
            vlc_fifo_WaitCond( p_priv->p_fifo, &p_priv->wait_fifo );
    }

    vlc_fifo_QueueUnlocked( p_priv->p_fifo, p_block );
    vlc_fifo_Unlock( p_priv->p_fifo );
}

bool input_DecoderIsEmpty( decoder_t * p_dec )
{
    struct decoder_priv *p_priv = dec_get_priv( p_dec );

    assert( !p_priv->b_waiting );

    vlc_fifo_Lock( p_priv->p_fifo );
    if( !vlc_fifo_IsEmpty( p_priv->p_fifo ) || p_priv->b_draining )
    {
        vlc_fifo_Unlock( p_priv->p_fifo );
        return false;
    }
    vlc_fifo_Unlock( p_priv->p_fifo );

    bool b_empty;

    vlc_mutex_lock( &p_priv->lock );
#ifdef ENABLE_SOUT
    if( p_priv->p_sout_input != NULL )
        b_empty = sout_InputIsEmpty( p_priv->p_sout_input );
    else
#endif
    if( p_priv->fmt.i_cat == VIDEO_ES && p_priv->p_vout != NULL )
        b_empty = vout_IsEmpty( p_priv->p_vout );
    else if( p_priv->fmt.i_cat == AUDIO_ES )
        b_empty = !p_priv->b_draining || p_priv->drained;
    else
        b_empty = true; /* TODO subtitles support */
    vlc_mutex_unlock( &p_priv->lock );

    return b_empty;
}

/**
 * Signals that there are no further blocks to decode, and requests that the
 * decoder drain all pending buffers. This is used to ensure that all
 * intermediate buffers empty and no samples get lost at the end of the stream.
 *
 * @note The function does not actually wait for draining. It just signals that
 * draining should be performed once the decoder has emptied FIFO.
 */
void input_DecoderDrain( decoder_t *p_dec )
{
    struct decoder_priv *p_priv = dec_get_priv( p_dec );

    vlc_fifo_Lock( p_priv->p_fifo );
    p_priv->b_draining = true;
    vlc_fifo_Signal( p_priv->p_fifo );
    vlc_fifo_Unlock( p_priv->p_fifo );
}

/**
 * Requests that the decoder immediately discard all pending buffers.
 * This is useful when seeking or when deselecting a stream.
 */
void input_DecoderFlush( decoder_t *p_dec )
{
    struct decoder_priv *p_priv = dec_get_priv( p_dec );

    vlc_fifo_Lock( p_priv->p_fifo );

    /* Empty the fifo */
    block_ChainRelease( vlc_fifo_DequeueAllUnlocked( p_priv->p_fifo ) );

    /* Don't need to wait for the DecoderThread to flush. Indeed, if called a
     * second time, this function will clear the FIFO again before anything was
     * dequeued by DecoderThread and there is no need to flush a second time in
     * a row. */
    p_priv->flushing = true;

    /* Flush video/spu decoder when paused: increment frames_countdown in order
     * to display one frame/subtitle */
    if( p_priv->paused
     && ( p_priv->fmt.i_cat == VIDEO_ES || p_priv->fmt.i_cat == SPU_ES )
     && p_priv->frames_countdown == 0 )
        p_priv->frames_countdown++;

    vlc_fifo_Signal( p_priv->p_fifo );

    vlc_fifo_Unlock( p_priv->p_fifo );
}

void input_DecoderGetCcDesc( decoder_t *p_dec, decoder_cc_desc_t *p_desc )
{
    struct decoder_priv *p_priv = dec_get_priv( p_dec );

    vlc_mutex_lock( &p_priv->lock );
    *p_desc = p_priv->cc.desc;
    vlc_mutex_unlock( &p_priv->lock );
}

static bool input_DecoderHasCCChanFlag( struct decoder_priv *p_priv,
                                        vlc_fourcc_t codec, int i_channel )
{
    int i_max_channels;
    uint64_t i_bitmap;
    if( codec == VLC_CODEC_CEA608 )
    {
        i_max_channels = 4;
        i_bitmap = p_priv->cc.desc.i_608_channels;
    }
    else if( codec == VLC_CODEC_CEA708 )
    {
        i_max_channels = 64;
        i_bitmap = p_priv->cc.desc.i_708_channels;
    }
    else return false;

    return ( i_channel >= 0 && i_channel < i_max_channels &&
             ( i_bitmap & ((uint64_t)1 << i_channel) ) );
}

int input_DecoderSetCcState( decoder_t *p_dec, vlc_fourcc_t codec,
                             int i_channel, bool b_decode )
{
    struct decoder_priv *p_priv = dec_get_priv( p_dec );

    //msg_Warn( p_dec, "input_DecoderSetCcState: %d @%x", b_decode, i_channel );

    if( !input_DecoderHasCCChanFlag( p_priv, codec, i_channel ) )
        return VLC_EGENERIC;

    if( b_decode )
    {
        decoder_t *p_cc;
        es_format_t fmt;

        es_format_Init( &fmt, SPU_ES, codec );
        fmt.subs.cc.i_channel = i_channel;
        fmt.subs.cc.i_reorder_depth = p_priv->cc.desc.i_reorder_depth;
        p_cc = input_DecoderNew( VLC_OBJECT(p_dec), &fmt, p_priv->p_clock,
                                 p_priv->p_resource, p_priv->p_sout, false,
                                 NULL, NULL );
        if( !p_cc )
        {
            msg_Err( p_dec, "could not create decoder" );
            vlc_dialog_display_error( p_dec,
                _("Streaming / Transcoding failed"), "%s",
                _("VLC could not open the decoder module.") );
            return VLC_EGENERIC;
        }
        else if( !p_cc->p_module )
        {
            DecoderUnsupportedCodec( p_dec, &fmt, true );
            input_DecoderDelete(p_cc);
            return VLC_EGENERIC;
        }
        struct decoder_priv *p_ccpriv = dec_get_priv( p_cc );
        p_ccpriv->p_clock = p_priv->p_clock;

        vlc_mutex_lock( &p_priv->lock );
        p_priv->cc.pp_decoder[i_channel] = p_cc;
        vlc_mutex_unlock( &p_priv->lock );
    }
    else
    {
        decoder_t *p_cc;

        vlc_mutex_lock( &p_priv->lock );
        p_cc = p_priv->cc.pp_decoder[i_channel];
        p_priv->cc.pp_decoder[i_channel] = NULL;
        vlc_mutex_unlock( &p_priv->lock );

        if( p_cc )
            input_DecoderDelete(p_cc);
    }
    return VLC_SUCCESS;
}

int input_DecoderGetCcState( decoder_t *p_dec, vlc_fourcc_t codec,
                             int i_channel, bool *pb_decode )
{
    struct decoder_priv *p_priv = dec_get_priv( p_dec );

    if( !input_DecoderHasCCChanFlag( p_priv, codec, i_channel ) )
        return VLC_EGENERIC;

    vlc_mutex_lock( &p_priv->lock );
    *pb_decode = p_priv->cc.pp_decoder[i_channel] != NULL;
    vlc_mutex_unlock( &p_priv->lock );
    return VLC_SUCCESS;
}

void input_DecoderChangePause( decoder_t *p_dec, bool b_paused, vlc_tick_t i_date )
{
    struct decoder_priv *p_priv = dec_get_priv( p_dec );

    /* Normally, p_priv->b_paused != b_paused here. But if a track is added
     * while the input is paused (e.g. add sub file), then b_paused is
     * (incorrectly) false. FIXME: This is a bug in the decoder priv. */
    vlc_fifo_Lock( p_priv->p_fifo );
    p_priv->paused = b_paused;
    p_priv->pause_date = i_date;
    p_priv->frames_countdown = 0;
    vlc_fifo_Signal( p_priv->p_fifo );
    vlc_fifo_Unlock( p_priv->p_fifo );
}

void input_DecoderChangeRate( decoder_t *dec, float rate )
{
    struct decoder_priv *priv = dec_get_priv( dec );

    vlc_fifo_Lock( priv->p_fifo );
    priv->request_rate = rate;
    vlc_fifo_Unlock( priv->p_fifo );
}

void input_DecoderChangeDelay( decoder_t *dec, vlc_tick_t delay )
{
    struct decoder_priv *priv = dec_get_priv( dec );

    vlc_fifo_Lock( priv->p_fifo );
    priv->delay = delay;
    vlc_fifo_Unlock( priv->p_fifo );
}

void input_DecoderStartWait( decoder_t *p_dec )
{
    struct decoder_priv *p_priv = dec_get_priv( p_dec );

    assert( !p_priv->b_waiting );

    vlc_mutex_lock( &p_priv->lock );
    p_priv->b_first = true;
    p_priv->b_has_data = false;
    p_priv->b_waiting = true;
    vlc_cond_signal( &p_priv->wait_request );
    vlc_mutex_unlock( &p_priv->lock );
}

void input_DecoderStopWait( decoder_t *p_dec )
{
    struct decoder_priv *p_priv = dec_get_priv( p_dec );

    assert( p_priv->b_waiting );

    vlc_mutex_lock( &p_priv->lock );
    p_priv->b_waiting = false;
    vlc_cond_signal( &p_priv->wait_request );
    vlc_mutex_unlock( &p_priv->lock );
}

void input_DecoderWait( decoder_t *p_dec )
{
    struct decoder_priv *p_priv = dec_get_priv( p_dec );

    assert( p_priv->b_waiting );

    vlc_mutex_lock( &p_priv->lock );
    while( !p_priv->b_has_data )
    {
        /* Don't need to lock p_priv->paused since it's only modified by the
         * priv */
        if( p_priv->paused )
            break;
        vlc_fifo_Lock( p_priv->p_fifo );
        if( p_priv->b_idle && vlc_fifo_IsEmpty( p_priv->p_fifo ) )
        {
            msg_Err( p_dec, "buffer deadlock prevented" );
            vlc_fifo_Unlock( p_priv->p_fifo );
            break;
        }
        vlc_fifo_Unlock( p_priv->p_fifo );
        vlc_cond_wait( &p_priv->wait_acknowledge, &p_priv->lock );
    }
    vlc_mutex_unlock( &p_priv->lock );
}

void input_DecoderFrameNext( decoder_t *p_dec, vlc_tick_t *pi_duration )
{
    struct decoder_priv *p_priv = dec_get_priv( p_dec );

    assert( p_priv->paused );
    *pi_duration = 0;

    vlc_fifo_Lock( p_priv->p_fifo );
    p_priv->frames_countdown++;
    vlc_fifo_Signal( p_priv->p_fifo );
    vlc_fifo_Unlock( p_priv->p_fifo );

    vlc_mutex_lock( &p_priv->lock );
    if( p_priv->fmt.i_cat == VIDEO_ES )
    {
        if( p_priv->p_vout )
            vout_NextPicture( p_priv->p_vout, pi_duration );
    }
    vlc_mutex_unlock( &p_priv->lock );
}

bool input_DecoderHasFormatChanged( decoder_t *p_dec, es_format_t *p_fmt, vlc_meta_t **pp_meta )
{
    struct decoder_priv *p_priv = dec_get_priv( p_dec );

    if( !atomic_exchange_explicit( &p_priv->b_fmt_description, false,
                                   memory_order_acquire ) )
        return false;

    vlc_mutex_lock( &p_priv->lock );
    if( p_fmt != NULL )
        es_format_Copy( p_fmt, &p_priv->fmt );

    if( pp_meta )
    {
        *pp_meta = NULL;
        if( p_priv->p_description )
        {
            *pp_meta = vlc_meta_New();
            if( *pp_meta )
                vlc_meta_Merge( *pp_meta, p_priv->p_description );
        }
    }
    vlc_mutex_unlock( &p_priv->lock );
    return true;
}

size_t input_DecoderGetFifoSize( decoder_t *p_dec )
{
    struct decoder_priv *p_priv = dec_get_priv( p_dec );

    return block_FifoSize( p_priv->p_fifo );
}

void input_DecoderSetVoutMouseEvent( decoder_t *dec, vlc_mouse_event mouse_event,
                                    void *user_data )
{
    struct decoder_priv *priv = dec_get_priv( dec );
    assert( dec->fmt_in.i_cat == VIDEO_ES );

    vlc_mutex_lock( &priv->mouse_lock );

    priv->mouse_event = mouse_event;
    priv->mouse_opaque = user_data;

    vlc_mutex_unlock( &priv->mouse_lock );
}

int input_DecoderAddVoutOverlay( decoder_t *dec, subpicture_t *sub,
                                 size_t *channel )
{
    struct decoder_priv *priv = dec_get_priv( dec );
    assert( dec->fmt_in.i_cat == VIDEO_ES );
    assert( sub && channel );

    vlc_mutex_lock( &priv->lock );

    if( !priv->p_vout )
    {
        vlc_mutex_unlock( &priv->lock );
        return VLC_EGENERIC;
    }
    ssize_t channel_id =
        vout_RegisterSubpictureChannel( priv->p_vout );
    if (channel_id == -1)
    {
        vlc_mutex_unlock( &priv->lock );
        return VLC_EGENERIC;
    }
    sub->i_start = sub->i_stop = vlc_tick_now();
    sub->i_channel = *channel = channel_id;
    sub->i_order = 0;
    sub->b_ephemer = true;
    vout_PutSubpicture( priv->p_vout, sub );

    vlc_mutex_unlock( &priv->lock );
    return VLC_SUCCESS;
}

int input_DecoderDelVoutOverlay( decoder_t *dec, size_t channel )
{
    struct decoder_priv *priv = dec_get_priv( dec );
    assert( dec->fmt_in.i_cat == VIDEO_ES );

    vlc_mutex_lock( &priv->lock );

    if( !priv->p_vout )
    {
        vlc_mutex_unlock( &priv->lock );
        return VLC_EGENERIC;
    }
    vout_UnregisterSubpictureChannel( priv->p_vout, channel );

    vlc_mutex_unlock( &priv->lock );
    return VLC_SUCCESS;
}

int input_DecoderSetSpuHighlight( decoder_t *dec,
                                  const vlc_spu_highlight_t *spu_hl )
{
    struct decoder_priv *p_priv = dec_get_priv( dec );
    assert( dec->fmt_in.i_cat == SPU_ES );

#ifdef ENABLE_SOUT
    if( p_priv->p_sout_input )
        sout_InputControl( p_priv->p_sout_input, SOUT_INPUT_SET_SPU_HIGHLIGHT, spu_hl );
#endif

    vlc_mutex_lock( &p_priv->lock );
    if( !p_priv->p_vout )
    {
        vlc_mutex_unlock( &p_priv->lock );
        return VLC_EGENERIC;
    }

    vout_SetSpuHighlight( p_priv->p_vout, spu_hl );

    vlc_mutex_unlock( &p_priv->lock );
    return VLC_SUCCESS;
}
