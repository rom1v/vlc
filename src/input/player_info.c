#include "player_info.h"

#include "demux.h"

struct vlc_pi_input_listener_id
{
    const struct vlc_pi_input_callbacks *cbs;
    void *userdata;
    struct vlc_list node; /**< node of vlc_pi_input.listeners */
};

#define vlc_pi_input_listener_foreach(listener, pi_input) \
    vlc_list_foreach(listener, &(pi_input)->listeners, node)

#define vlc_pi_input_NotifyListener(pi_input, listener, event, ...) \
do { \
    if (listener->cbs->event) \
        listener->cbs->event(pi_input, __VA_ARGS__, listener->userdata); \
} while (0)

#define vlc_pi_input_Notify(pi_input, event, ...) \
do { \
    vlc_pi_input_listener_id *listener; \
    vlc_pi_input_listener_foreach(listener, pi_input) \
        vlc_pi_input_NotifyListener(pi_input, listener, event, __VA_ARGS__); \
} while(0)

// ##__VA_ARGS__ is forbidden in VLC, so we need a specific macro for 0 varargs
#define vlc_pi_input_NotifyListener0(pi_input, listener, event) \
do { \
    if (listener->cbs->event) \
        listener->cbs->event(pi_input, listener->userdata); \
} while (0)

#define vlc_pi_input_Notify0(pi_input, event) \
do { \
    vlc_pi_input_listener_id *listener; \
    vlc_pi_input_listener_foreach(listener, pi_input) \
        vlc_pi_input_NotifyListener0(pi_input, listener, event); \
} while(0)

static struct vlc_pi_stream *
vlc_pi_stream_new(const char *url, const char *module_shortname,
                  const char *module_longname)
{
    struct vlc_pi_stream *pi_stream = malloc(sizeof(*pi_stream));
    if (!pi_stream)
        return NULL;

    assert(url);
    pi_stream->url = strdup(url);
    if (!pi_stream->url)
    {
        free(pi_stream);
        return NULL;
    }

    if (module_shortname) {
        pi_stream->module_shortname = strdup(module_shortname);
        if (!pi_stream->module_shortname)
        {
            free(pi_stream->url);
            free(pi_stream);
            return NULL;
        }

        // either no module names, either both short and long names
        assert(module_longname);
        pi_stream->module_longname = strdup(module_longname);
        if (!pi_stream->module_longname)
        {
            free(pi_stream->module_shortname);
            free(pi_stream->url);
            free(pi_stream);
            return NULL;
        }
    }
    else
    {
        pi_stream->module_shortname = NULL;
        pi_stream->module_longname = NULL;
    }

    return pi_stream;
}

static void
vlc_pi_stream_delete(struct vlc_pi_stream *pi_stream)
{
    free(pi_stream->module_shortname);
    free(pi_stream->module_longname);
    free(pi_stream->url);
    free(pi_stream);
}

static struct vlc_pi_source *
vlc_pi_source_new(input_source_t *source)
{
    struct vlc_pi_source *pi_source = malloc(sizeof(*pi_source));
    if (!pi_source)
        return NULL;

    vlc_vector_init(&pi_source->streams);
    pi_source->source = source;
    return pi_source;
}

static void
vlc_pi_source_delete(struct vlc_pi_source *pi_source)
{
    for (size_t i = 0; i < pi_source->streams.size; ++i)
        vlc_pi_stream_delete(pi_source->streams.data[i]);
    vlc_vector_destroy(&pi_source->streams);

    free(pi_source);
}

void
vlc_pi_input_init(struct vlc_pi_input *pi_input)
{
    vlc_vector_init(&pi_input->sources);
    vlc_list_init(&pi_input->listeners);
}

void
vlc_pi_input_destroy(struct vlc_pi_input *pi_input)
{
    assert(vlc_list_is_empty(&pi_input->listeners));

    for (size_t i = 0; i < pi_input->sources.size; ++i)
        vlc_pi_source_delete(pi_input->sources.data[i]);
    vlc_vector_destroy(&pi_input->sources);
}

void
vlc_pi_input_reset(struct vlc_pi_input *pi_input)
{
    for (size_t i = 0; i < pi_input->sources.size; ++i)
        vlc_pi_source_delete(pi_input->sources.data[i]);
    vlc_vector_clear(&pi_input->sources);

    vlc_pi_input_Notify0(pi_input, on_reset);
}

vlc_pi_input_listener_id *
vlc_pi_input_AddListener(struct vlc_pi_input *pi_input,
                         const struct vlc_pi_input_callbacks *cbs,
                         void *userdata)
{
    vlc_pi_input_listener_id *listener = malloc(sizeof(*listener));
    if (!listener)
        return NULL;

    listener->cbs = cbs;
    listener->userdata = userdata;
    vlc_list_append(&listener->node, &pi_input->listeners);

    // notify initial state
    vlc_pi_input_NotifyListener0(pi_input, listener, on_reset);

    return listener;
}

void
vlc_pi_input_RemoveListener(struct vlc_pi_input *pi_input,
                            vlc_pi_input_listener_id *listener)
{
    (void) pi_input;
    vlc_list_remove(&listener->node);
    free(listener);
}

static void
debug_print_pi_input(struct vlc_pi_input *pi_input)
{
    printf("[PI INPUT]\n");
    for (size_t i = 0; i < pi_input->sources.size; ++i)
    {
        struct vlc_pi_source *pi_source = pi_input->sources.data[i];
        printf("  [PI SOURCE %zd]\n", i);
        for (size_t j = 0; j < pi_source->streams.size; ++j)
        {
            struct vlc_pi_stream *pi_stream = pi_source->streams.data[j];
            printf("    [PI STREAM %zd]: %s (%s): %s\n", j,
                   pi_stream->module_longname, pi_stream->module_shortname,
                   pi_stream->url);
        }
    }
}

static ssize_t
vlc_pi_source_find(struct vlc_pi_input *pi_input, input_source_t *source)
{
    for (size_t i = 0; i < pi_input->sources.size; ++i)
    {
        struct vlc_pi_source *pi_source = pi_input->sources.data[i];
        if (pi_source->source == source)
            return i;
    }
    return -1;
}

static bool
vlc_pi_source_reset_streams(struct vlc_pi_source *pi_source,
                                 demux_t *demux)
{
    vlc_vector_clear(&pi_source->streams);

    assert(demux);
    while (demux) {
        const char *module_shortname;
        const char *module_longname;
        if (demux->p_next)
        {
            // demux filter
            module_shortname = demux_GetModuleName(demux, false);
            module_longname = demux_GetModuleName(demux, true);
            assert(module_shortname);
            assert(module_longname);
        }
        else
        {
            // access stream, no module
            module_shortname = NULL;
            module_longname = NULL;
        }

        struct vlc_pi_stream *pi_stream =
            vlc_pi_stream_new(demux->psz_url, module_shortname, module_longname);
        if (!pi_stream || !vlc_vector_push(&pi_source->streams, pi_stream))
        {
            vlc_vector_foreach(pi_stream, &pi_source->streams)
                vlc_pi_stream_delete(pi_stream);
            vlc_vector_clear(&pi_source->streams);
            return false;
        }

        demux = demux->p_next;
    }

    return true;
}

static void
handle_info_input_source_added(struct vlc_pi_input *pi_input,
                               input_source_t *source)
{
    struct vlc_pi_source *pi_source = vlc_pi_source_new(source);
    if (!pi_source)
        return;

    if (!vlc_pi_source_reset_streams(pi_source, source->p_demux))
    {
        vlc_pi_source_delete(pi_source);
        return;
    }

    size_t index = pi_input->sources.size;
    if (!vlc_vector_push(&pi_input->sources, pi_source))
        return;

    vlc_pi_input_Notify(pi_input, on_source_added, index, pi_source);
}

static void
handle_info_input_source_demux_updated(struct vlc_pi_input *pi_input,
                                       input_source_t *source)
{
    ssize_t source_index = vlc_pi_source_find(pi_input, source);
    assert(source_index >= 0);
    struct vlc_pi_source *pi_source = pi_input->sources.data[source_index];
    if (!vlc_pi_source_reset_streams(pi_source, source->p_demux))
        return;

    vlc_pi_input_Notify(pi_input, on_source_demux_updated, source_index,
                        pi_source);
}

void
vlc_pi_input_handle_event(struct vlc_pi_input *input,
                          struct vlc_input_event_info *info)
{
    switch (info->type) {
        case VLC_INPUT_EVENT_INFO_INPUT_SOURCE_ADDED:
            handle_info_input_source_added(input, info->source);
            //debug_print_pi_input(input);
            break;
        case VLC_INPUT_EVENT_INFO_INPUT_SOURCE_DEMUX_UPDATED:
            handle_info_input_source_demux_updated(input, info->source);
            //debug_print_pi_input(input);
        default:
            // do nothing
            break;
    }
}

size_t
vlc_pi_input_GetSourcesCount(struct vlc_pi_input *pi_input)
{
    return pi_input->sources.size;
}

struct vlc_pi_source *
vlc_pi_input_GetSource(struct vlc_pi_input *pi_input, size_t index)
{
    return pi_input->sources.data[index];
}

size_t
vlc_pi_source_GetStreamsCount(struct vlc_pi_source *pi_source)
{
    return pi_source->streams.size;
}

struct vlc_pi_stream *
vlc_pi_source_GetStream(struct vlc_pi_source *pi_source, size_t index)
{
    return pi_source->streams.data[index];
}

const char *
vlc_pi_stream_GetModuleShortName(struct vlc_pi_stream *pi_stream)
{
    return pi_stream->module_shortname;
}

const char *
vlc_pi_stream_GetModuleLongName(struct vlc_pi_stream *pi_stream)
{
    return pi_stream->module_longname;
}

const char *
vlc_pi_stream_GetUrl(struct vlc_pi_stream *pi_stream)
{
    return pi_stream->url;
}
