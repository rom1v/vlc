#ifndef VLC_PLAYLIST_NEW_H_
#define VLC_PLAYLIST_NEW_H_

# ifdef __cplusplus
extern "C" {
# endif

#include <vlc_common.h>

/**
 * \defgroup playlist VLC playlist
 * \ingroup playlist
 * VLC playlist controls
 * @{
 */

typedef struct input_item_t input_item_t;

/* opaque types */
typedef struct vlc_playlist vlc_playlist_t;
typedef struct vlc_playlist_item vlc_playlist_item_t;
typedef struct vlc_playlist_listener_id vlc_playlist_listener_id;

enum vlc_playlist_playback_mode
{
    VLC_PLAYLIST_PLAYBACK_NORMAL,
    VLC_PLAYLIST_PLAYBACK_LOOP_ON_ALL,
    VLC_PLAYLIST_PLAYBACK_LOOP_ON_CURRENT,
    VLC_PLAYLIST_PLAYBACK_STOP_AFTER_CURRENT,
};

enum vlc_playlist_playback_order
{
    VLC_PLAYLIST_PLAYBACK_ORDER_NORMAL,
    VLC_PLAYLIST_PLAYBACK_ORDER_RANDOM,
};

struct vlc_playlist_callbacks
{
    //void (*on_listener_added)(vlc_playlist_t *, void *userdata);
    void (*on_items_cleared)(vlc_playlist_t *, void *userdata);
    void (*on_item_added)(vlc_playlist_t *, int index, vlc_playlist_item_t *, void *userdata);
    void (*on_item_removed)(vlc_playlist_t *, int index, vlc_playlist_item_t *, void *userdata);
    void (*on_playback_mode_changed)(vlc_playlist_t *, enum vlc_playlist_playback_mode, void *userdata);
    void (*on_playback_order_changed)(vlc_playlist_t *, enum vlc_playlist_playback_order, void *userdata);
    void (*on_current_item_changed)(vlc_playlist_t *, int index, vlc_playlist_item_t *, void *userdata);
    void (*on_has_next_changed)(vlc_playlist_t *, bool has_next, void *userdata);
    void (*on_has_prev_changed)(vlc_playlist_t *, bool has_prev, void *userdata);
};

VLC_API input_item_t *vlc_playlist_item_GetInputItem(vlc_playlist_item_t *);

VLC_API vlc_playlist_t *vlc_playlist_New(vlc_object_t *parent);
VLC_API void vlc_playlist_Delete(vlc_playlist_t *);

VLC_API void vlc_playlist_Lock(vlc_playlist_t *);
VLC_API void vlc_playlist_Unlock(vlc_playlist_t *);

VLC_API vlc_playlist_listener_id *
vlc_playlist_AddListener(vlc_playlist_t *,
                         const struct vlc_playlist_callbacks *,
                         void *userdata);

VLC_API void
vlc_playlist_RemoveListener(vlc_playlist_t *, vlc_playlist_listener_id *);

/**
 * Default implementation for listener_added(), which calls item_added() for
 * every existing item.
 */
//VLC_API void vlc_playlist_listener_added_default(vlc_playlist_t *, void *userdata);

VLC_API void vlc_playlist_setPlaybackMode(vlc_playlist_t *,
                                          enum vlc_playlist_playback_mode);
VLC_API void vlc_playlist_setPlaybackOrder(vlc_playlist_t *,
                                           enum vlc_playlist_playback_order);

VLC_API void vlc_playlist_Clear(vlc_playlist_t *);
VLC_API bool vlc_playlist_Append(vlc_playlist_t *, input_item_t *);
VLC_API bool vlc_playlist_Insert(vlc_playlist_t *, int index, input_item_t *);
VLC_API void vlc_playlist_RemoveAt(vlc_playlist_t *, int index);
VLC_API bool vlc_playlist_Remove(vlc_playlist_t *, input_item_t *);
VLC_API int vlc_playlist_Find(vlc_playlist_t *, input_item_t *);
VLC_API int vlc_playlist_Count(vlc_playlist_t *);
VLC_API vlc_playlist_item_t *vlc_playlist_Get(vlc_playlist_t *, int index);
VLC_API int vlc_playlist_GetCurrentIndex(vlc_playlist_t *);

VLC_API bool vlc_playlist_HasNext(vlc_playlist_t *);
VLC_API bool vlc_playlist_HasPrev(vlc_playlist_t *);
VLC_API void vlc_playlist_Next(vlc_playlist_t *);
VLC_API void vlc_playlist_Prev(vlc_playlist_t *);
VLC_API void vlc_playlist_GoTo(vlc_playlist_t *, int index);

VLC_API vlc_player_t *vlc_playlist_GetPlayer(vlc_playlist_t *);

//VLC_API void vlc_playlist_RequestPlay(struct vlc_playlist *);
//VLC_API void vlc_playlist_RequestStop(struct vlc_playlist *);
//
//VLC_API void vlc_playlist_RequestPause(struct vlc_playlist *);
//VLC_API void vlc_playlist_RequestResume(struct vlc_playlist *);

/** @} */
# ifdef __cplusplus
}
# endif

#endif
