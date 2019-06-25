#ifndef VLC_PLAYER_INFO_H
#define VLC_PLAYER_INFO_H

#include <vlc_common.h>
#include <vlc_vector.h>

typedef struct input_source input_source_t;

// "pi" stands for "player info" to avoid excessively long names

// everything must be called with player locked

struct vlc_pi_input;
struct vlc_pi_source;
struct vlc_pi_stream;

struct vlc_pi_track;
struct vlc_pi_decoder;
struct vlc_pi_decoder_device;
struct vlc_pi_aout;
struct vlc_pi_vout;

typedef struct vlc_pi_input_listener_id vlc_pi_input_listener_id;

struct vlc_pi_input_callbacks {
    void
    (*on_reset)(struct vlc_pi_input *pi_input, void *userdata);

    void
    (*on_source_added)(struct vlc_pi_input *pi_input, size_t source_index,
                       struct vlc_pi_source *pi_source, void *userdata);

    void
    (*on_source_demux_updated)(struct vlc_pi_input *pi_input,
                               size_t source_index,
                               struct vlc_pi_source *pi_source, void *userdata);
};

VLC_API
vlc_pi_input_listener_id *
vlc_pi_input_AddListener(struct vlc_pi_input *pi_input,
                         const struct vlc_pi_input_callbacks *cbs,
                         void *userdata);

VLC_API
void
vlc_pi_input_RemoveListener(struct vlc_pi_input *pi_input,
                            vlc_pi_input_listener_id *listener);

VLC_API
size_t
vlc_pi_input_GetSourcesCount(struct vlc_pi_input *pi_input);

VLC_API
struct vlc_pi_source *
vlc_pi_input_GetSource(struct vlc_pi_input *pi_input, size_t index);

VLC_API
size_t
vlc_pi_source_GetStreamsCount(struct vlc_pi_source *pi_source);

VLC_API
struct vlc_pi_stream *
vlc_pi_source_GetStream(struct vlc_pi_source *pi_source, size_t index);

VLC_API
const char *
vlc_pi_stream_GetModuleShortName(struct vlc_pi_stream *pi_stream);

VLC_API
const char *
vlc_pi_stream_GetModuleLongName(struct vlc_pi_stream *pi_stream);

VLC_API
const char *
vlc_pi_stream_GetUrl(struct vlc_pi_stream *pi_stream);

#endif
