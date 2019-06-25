#ifndef VLC_PLAYER_INFO_INTERNAL_H
#define VLC_PLAYER_INFO_INTERNAL_H

#include <vlc_common.h>
#include <vlc_list.h>
#include <vlc_player_info.h>
#include "input_internal.h"

typedef struct VLC_VECTOR(struct vlc_pi_source *) vlc_pi_source_vec_t;
typedef struct VLC_VECTOR(struct vlc_pi_stream *) vlc_pi_stream_vec_t;

struct vlc_pi_input {
    vlc_pi_source_vec_t sources;
    struct vlc_list listeners;
};

struct vlc_pi_source {
    vlc_pi_stream_vec_t streams;
    input_source_t *source;
};

struct vlc_pi_stream {
    char *url;
    char *module_shortname;
    char *module_longname;
};

struct vlc_pi_track {
    vlc_es_id_t *id;
    struct vlc_pi_decoder *decoder;
    struct vlc_pi_device *device;
    struct vlc_pi_aout *aout;
    struct vlc_pi_vout *vout;
};

struct vlc_pi_decoder {

};

struct vlc_pi_device {

};

struct vlc_pi_aout {

};

struct vlc_pi_vout {

};

enum vlc_input_event_info_type {
    VLC_INPUT_EVENT_INFO_INPUT_SOURCE_ADDED,
    VLC_INPUT_EVENT_INFO_INPUT_SOURCE_DEMUX_UPDATED,
};

struct vlc_input_event_info {
    enum vlc_input_event_info_type type;
    union {
        input_source_t *source;
    };
};

void
vlc_pi_input_init(struct vlc_pi_input *input);

void
vlc_pi_input_destroy(struct vlc_pi_input *input);

void
vlc_pi_input_reset(struct vlc_pi_input *input);

void
vlc_pi_input_handle_event(struct vlc_pi_input *input,
                          struct vlc_input_event_info *info);

#endif
