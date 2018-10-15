/*****************************************************************************
 * playlist_new/test.c
 *****************************************************************************
 * Copyright (C) 2018 VLC authors and VideoLAN
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

#include <stdio.h>
#include "item.h"
#include "playlist.h"
#include "preparse.h"

/* the playlist lock is the one of the player */
# define vlc_playlist_Lock(p) VLC_UNUSED(p);
# define vlc_playlist_Unlock(p) VLC_UNUSED(p);

static input_item_t *
CreateDummyMedia(int num)
{
    char *url;
    char *name;

    int res = asprintf(&url, "vlc://item-%d", num);
    if (res == -1)
        return NULL;

    res = asprintf(&name, "item-%d", num);
    if (res == -1)
        return NULL;

    input_item_t *media = input_item_New(url, name);
    free(url);
    free(name);
    return media;
}

static void
CreateDummyMediaArray(input_item_t *out[], size_t count)
{
    for (size_t i = 0; i < count; ++i)
    {
        out[i] = CreateDummyMedia(i);
        assert(out[i]);
    }
}

static void
DestroyMediaArray(input_item_t *const array[], size_t count)
{
    for (size_t i = 0; i < count; ++i)
        input_item_Release(array[i]);
}

#define EXPECT_AT(index, id) \
    assert(vlc_playlist_Get(playlist, index)->media == media[id])

static void
test_append(void)
{
    vlc_playlist_t *playlist = vlc_playlist_New(NULL);
    assert(playlist);

    input_item_t *media[10];
    CreateDummyMediaArray(media, 10);

    /* append one by one */
    for (int i = 0; i < 5; ++i)
    {
        int ret = vlc_playlist_AppendOne(playlist, media[i]);
        assert(ret == VLC_SUCCESS);
    }

    /* append several at once */
    int ret = vlc_playlist_Append(playlist, &media[5], 5);
    assert(ret == VLC_SUCCESS);

    assert(vlc_playlist_Count(playlist) == 10);
    EXPECT_AT(0, 0);
    EXPECT_AT(1, 1);
    EXPECT_AT(2, 2);
    EXPECT_AT(3, 3);
    EXPECT_AT(4, 4);
    EXPECT_AT(5, 5);
    EXPECT_AT(6, 6);
    EXPECT_AT(7, 7);
    EXPECT_AT(8, 8);
    EXPECT_AT(9, 9);

    DestroyMediaArray(media, 10);
    vlc_playlist_Delete(playlist);
}

static void
test_insert(void)
{
    vlc_playlist_t *playlist = vlc_playlist_New(NULL);
    assert(playlist);

    input_item_t *media[15];
    CreateDummyMediaArray(media, 15);

    /* initial playlist with 5 items */
    int ret = vlc_playlist_Append(playlist, media, 5);
    assert(ret == VLC_SUCCESS);

    /* insert one by one */
    for (int i = 0; i < 5; ++i)
    {
        ret = vlc_playlist_InsertOne(playlist, 2, media[i + 5]);
        assert(ret == VLC_SUCCESS);
    }

    /* insert several at once */
    ret = vlc_playlist_Insert(playlist, 6, &media[10], 5);
    assert(ret == VLC_SUCCESS);

    assert(vlc_playlist_Count(playlist) == 15);

    EXPECT_AT(0, 0);
    EXPECT_AT(1, 1);

    EXPECT_AT(2, 9);
    EXPECT_AT(3, 8);
    EXPECT_AT(4, 7);
    EXPECT_AT(5, 6);

    EXPECT_AT(6, 10);
    EXPECT_AT(7, 11);
    EXPECT_AT(8, 12);
    EXPECT_AT(9, 13);
    EXPECT_AT(10, 14);

    EXPECT_AT(11, 5);
    EXPECT_AT(12, 2);
    EXPECT_AT(13, 3);
    EXPECT_AT(14, 4);

    DestroyMediaArray(media, 15);
    vlc_playlist_Delete(playlist);
}

static void
test_move(void)
{
    vlc_playlist_t *playlist = vlc_playlist_New(NULL);
    assert(playlist);

    input_item_t *media[10];
    CreateDummyMediaArray(media, 10);

    /* initial playlist with 10 items */
    int ret = vlc_playlist_Append(playlist, media, 10);
    assert(ret == VLC_SUCCESS);

    /* move slice {3, 4, 5, 6} so that its new position is 5 */
    vlc_playlist_Move(playlist, 3, 4, 5);

    EXPECT_AT(0, 0);
    EXPECT_AT(1, 1);
    EXPECT_AT(2, 2);
    EXPECT_AT(3, 7);
    EXPECT_AT(4, 8);
    EXPECT_AT(5, 3);
    EXPECT_AT(6, 4);
    EXPECT_AT(7, 5);
    EXPECT_AT(8, 6);
    EXPECT_AT(9, 9);

    /* move it back to its original position */
    vlc_playlist_Move(playlist, 5, 4, 3);

    EXPECT_AT(0, 0);
    EXPECT_AT(1, 1);
    EXPECT_AT(2, 2);
    EXPECT_AT(3, 3);
    EXPECT_AT(4, 4);
    EXPECT_AT(5, 5);
    EXPECT_AT(6, 6);
    EXPECT_AT(7, 7);
    EXPECT_AT(8, 8);
    EXPECT_AT(9, 9);

    DestroyMediaArray(media, 10);
    vlc_playlist_Delete(playlist);
}

static void
test_remove(void)
{
    vlc_playlist_t *playlist = vlc_playlist_New(NULL);
    assert(playlist);

    input_item_t *media[10];
    CreateDummyMediaArray(media, 10);

    /* initial playlist with 10 items */
    int ret = vlc_playlist_Append(playlist, media, 10);
    assert(ret == VLC_SUCCESS);

    /* remove one by one */
    for (int i = 0; i < 3; ++i)
        vlc_playlist_RemoveOne(playlist, 2);

    /* remove several at once */
    vlc_playlist_Remove(playlist, 3, 2);

    assert(vlc_playlist_Count(playlist) == 5);
    EXPECT_AT(0, 0);
    EXPECT_AT(1, 1);
    EXPECT_AT(2, 5);
    EXPECT_AT(3, 8);
    EXPECT_AT(4, 9);

    DestroyMediaArray(media, 10);
    vlc_playlist_Delete(playlist);
}

static void
test_clear(void)
{
    vlc_playlist_t *playlist = vlc_playlist_New(NULL);
    assert(playlist);

    input_item_t *media[10];
    CreateDummyMediaArray(media, 10);

    /* initial playlist with 10 items */
    int ret = vlc_playlist_Append(playlist, media, 10);
    assert(ret == VLC_SUCCESS);

    assert(vlc_playlist_Count(playlist) == 10);
    vlc_playlist_Clear(playlist);
    assert(vlc_playlist_Count(playlist) == 0);

    DestroyMediaArray(media, 10);
    vlc_playlist_Delete(playlist);
}

static void
test_expand_item(void)
{
    vlc_playlist_t *playlist = vlc_playlist_New(NULL);
    assert(playlist);

    input_item_t *media[16];
    CreateDummyMediaArray(media, 16);

    /* initial playlist with 10 items */
    int ret = vlc_playlist_Append(playlist, media, 10);
    assert(ret == VLC_SUCCESS);

    /* create a subtree for item 8 with 4 children */
    input_item_t *item_to_expand = playlist->items.data[8]->media;
    input_item_node_t *root = input_item_node_Create(item_to_expand);
    for (int i = 0; i < 4; ++i)
    {
        input_item_node_t *node = input_item_node_AppendItem(root,
                                                             media[i + 10]);
        assert(node);
    }

    /* on the 3rd children, add 2 grand-children */
    input_item_node_t *parent = root->pp_children[2];
    for (int i = 0; i < 2; ++i)
    {
        input_item_node_t *node = input_item_node_AppendItem(parent,
                                                             media[i + 14]);
        assert(node);
    }

    bool ok = vlc_playlist_ExpandItem(playlist, 8, root);
    assert(ok);
    assert(vlc_playlist_Count(playlist) == 15);
    EXPECT_AT(7, 7);

    EXPECT_AT(8, 10);
    EXPECT_AT(9, 11);
    EXPECT_AT(10, 12);

    EXPECT_AT(11, 14);
    EXPECT_AT(12, 15);

    EXPECT_AT(13, 13);

    EXPECT_AT(14, 9);

    input_item_node_Delete(root);
    DestroyMediaArray(media, 16);
    vlc_playlist_Delete(playlist);
}

struct playlist_state
{
    size_t playlist_size;
    ssize_t current;
    bool has_prev;
    bool has_next;
};

static void
playlist_state_init(struct playlist_state *state, vlc_playlist_t *playlist)
{
    state->playlist_size = vlc_playlist_Count(playlist);
    state->current = vlc_playlist_GetCurrentIndex(playlist);
    state->has_prev = vlc_playlist_HasPrev(playlist);
    state->has_next = vlc_playlist_HasNext(playlist);
}

struct items_reset_report
{
    size_t count;
    struct playlist_state state;
};

struct items_added_report
{
    size_t index;
    size_t count;
    struct playlist_state state;
};

struct items_moved_report
{
    size_t index;
    size_t count;
    size_t target;
    struct playlist_state state;
};

struct items_removed_report
{
    size_t index;
    size_t count;
    struct playlist_state state;
};

struct playback_repeat_changed_report
{
    enum vlc_playlist_playback_repeat repeat;
};

struct playback_order_changed_report
{
    enum vlc_playlist_playback_order order;
};

struct current_index_changed_report
{
    ssize_t current;
};

struct has_prev_changed_report
{
    bool has_prev;
};

struct has_next_changed_report
{
    bool has_next;
};

struct callback_ctx
{
    struct VLC_VECTOR(struct items_reset_report)           vec_items_reset;
    struct VLC_VECTOR(struct items_added_report)           vec_items_added;
    struct VLC_VECTOR(struct items_moved_report)           vec_items_moved;
    struct VLC_VECTOR(struct items_removed_report)         vec_items_removed;
    struct VLC_VECTOR(struct playback_order_changed_report)
                                                  vec_playback_order_changed;
    struct VLC_VECTOR(struct playback_repeat_changed_report)
                                                  vec_playback_repeat_changed;
    struct VLC_VECTOR(struct current_index_changed_report)
                                                  vec_current_index_changed;
    struct VLC_VECTOR(struct has_prev_changed_report)      vec_has_prev_changed;
    struct VLC_VECTOR(struct has_next_changed_report)      vec_has_next_changed;
};

#define CALLBACK_CTX_INITIALIZER \
{ \
    VLC_VECTOR_INITIALIZER, \
    VLC_VECTOR_INITIALIZER, \
    VLC_VECTOR_INITIALIZER, \
    VLC_VECTOR_INITIALIZER, \
    VLC_VECTOR_INITIALIZER, \
    VLC_VECTOR_INITIALIZER, \
    VLC_VECTOR_INITIALIZER, \
    VLC_VECTOR_INITIALIZER, \
    VLC_VECTOR_INITIALIZER, \
}

static inline void
callback_ctx_reset(struct callback_ctx *ctx)
{
    vlc_vector_clear(&ctx->vec_items_reset);
    vlc_vector_clear(&ctx->vec_items_added);
    vlc_vector_clear(&ctx->vec_items_moved);
    vlc_vector_clear(&ctx->vec_items_removed);
    vlc_vector_clear(&ctx->vec_playback_repeat_changed);
    vlc_vector_clear(&ctx->vec_playback_order_changed);
    vlc_vector_clear(&ctx->vec_current_index_changed);
    vlc_vector_clear(&ctx->vec_has_prev_changed);
    vlc_vector_clear(&ctx->vec_has_next_changed);
};

static inline void
callback_ctx_destroy(struct callback_ctx *ctx)
{
    vlc_vector_destroy(&ctx->vec_items_reset);
    vlc_vector_destroy(&ctx->vec_items_added);
    vlc_vector_destroy(&ctx->vec_items_moved);
    vlc_vector_destroy(&ctx->vec_items_removed);
    vlc_vector_destroy(&ctx->vec_playback_repeat_changed);
    vlc_vector_destroy(&ctx->vec_playback_order_changed);
    vlc_vector_destroy(&ctx->vec_current_index_changed);
    vlc_vector_destroy(&ctx->vec_has_prev_changed);
    vlc_vector_destroy(&ctx->vec_has_next_changed);
};

static void
callback_on_items_reset(vlc_playlist_t *playlist,
                        vlc_playlist_item_t *const items[], size_t count,
                        void *userdata)
{
    VLC_UNUSED(items);
    struct callback_ctx *ctx = userdata;

    struct items_reset_report report;
    report.count = count;
    playlist_state_init(&report.state, playlist);
    vlc_vector_push(&ctx->vec_items_reset, report);
}

static void
callback_on_items_added(vlc_playlist_t *playlist, size_t index,
                        vlc_playlist_item_t *const items[], size_t count,
                        void *userdata)
{
    VLC_UNUSED(items);
    struct callback_ctx *ctx = userdata;

    struct items_added_report report;
    report.index = index;
    report.count = count;
    playlist_state_init(&report.state, playlist);
    vlc_vector_push(&ctx->vec_items_added, report);
}

static void
callback_on_items_moved(vlc_playlist_t *playlist, size_t index, size_t count,
                        size_t target, void *userdata)
{
    struct callback_ctx *ctx = userdata;

    struct items_moved_report report;
    report.index = index;
    report.count = count;
    report.target = target;
    playlist_state_init(&report.state, playlist);
    vlc_vector_push(&ctx->vec_items_moved, report);
}

static void
callback_on_items_removed(vlc_playlist_t *playlist, size_t index, size_t count,
                          void *userdata)
{
    struct callback_ctx *ctx = userdata;

    struct items_removed_report report;
    report.index = index;
    report.count = count;
    playlist_state_init(&report.state, playlist);
    vlc_vector_push(&ctx->vec_items_removed, report);
}

static void
callback_on_playback_repeat_changed(vlc_playlist_t *playlist,
                                    enum vlc_playlist_playback_repeat repeat,
                                    void *userdata)
{
    VLC_UNUSED(playlist);
    struct callback_ctx *ctx = userdata;

    struct playback_repeat_changed_report report;
    report.repeat = repeat;
    vlc_vector_push(&ctx->vec_playback_repeat_changed, report);
}

static void
callback_on_playback_order_changed(vlc_playlist_t *playlist,
                                   enum vlc_playlist_playback_order order,
                                   void *userdata)
{
    VLC_UNUSED(playlist);
    struct callback_ctx *ctx = userdata;

    struct playback_order_changed_report report;
    report.order = order;
    vlc_vector_push(&ctx->vec_playback_order_changed, report);
}

static void
callback_on_current_index_changed(vlc_playlist_t *playlist, ssize_t index,
                                  void *userdata)
{
    VLC_UNUSED(playlist);
    struct callback_ctx *ctx = userdata;

    struct current_index_changed_report report;
    report.current = index;
    vlc_vector_push(&ctx->vec_current_index_changed, report);
}

static void
callback_on_has_prev_changed(vlc_playlist_t *playlist, bool has_prev,
                             void *userdata)
{
    VLC_UNUSED(playlist);
    struct callback_ctx *ctx = userdata;

    struct has_prev_changed_report report;
    report.has_prev = has_prev;
    vlc_vector_push(&ctx->vec_has_prev_changed, report);
}

static void
callback_on_has_next_changed(vlc_playlist_t *playlist, bool has_next,
                             void *userdata)
{
    VLC_UNUSED(playlist);
    struct callback_ctx *ctx = userdata;

    struct has_next_changed_report report;
    report.has_next = has_next;
    vlc_vector_push(&ctx->vec_has_next_changed, report);
}

static void
test_items_added_callbacks(void)
{
    vlc_playlist_t *playlist = vlc_playlist_New(NULL);
    assert(playlist);

    input_item_t *media[10];
    CreateDummyMediaArray(media, 10);

    struct vlc_playlist_callbacks cbs = {
        .on_items_added = callback_on_items_added,
        .on_current_index_changed = callback_on_current_index_changed,
        .on_has_prev_changed = callback_on_has_prev_changed,
        .on_has_next_changed = callback_on_has_next_changed,
    };

    struct callback_ctx ctx = CALLBACK_CTX_INITIALIZER;
    vlc_playlist_listener_id *listener =
            vlc_playlist_AddListener(playlist, &cbs, &ctx);
    assert(listener);

    int ret = vlc_playlist_AppendOne(playlist, media[0]);
    assert(ret == VLC_SUCCESS);

    /* the callbacks must be called with *all* values up to date */
    assert(ctx.vec_items_added.size == 1);
    assert(ctx.vec_items_added.data[0].index == 0);
    assert(ctx.vec_items_added.data[0].count == 1);
    assert(ctx.vec_items_added.data[0].state.playlist_size == 1);
    assert(ctx.vec_items_added.data[0].state.current == -1);
    assert(!ctx.vec_items_added.data[0].state.has_prev);
    assert(ctx.vec_items_added.data[0].state.has_next);

    assert(ctx.vec_current_index_changed.size == 0);

    assert(ctx.vec_has_prev_changed.size == 0);

    assert(ctx.vec_has_next_changed.size == 1);
    assert(ctx.vec_has_next_changed.data[0].has_next);

    callback_ctx_reset(&ctx);

    /* set the only item as current */
    playlist->current = 0;
    playlist->has_prev = false;
    playlist->has_next = false;

    /* insert before the current item */
    ret = vlc_playlist_Insert(playlist, 0, &media[1], 4);
    assert(ret == VLC_SUCCESS);

    assert(ctx.vec_items_added.size == 1);
    assert(ctx.vec_items_added.data[0].index == 0);
    assert(ctx.vec_items_added.data[0].count == 4);
    assert(ctx.vec_items_added.data[0].state.playlist_size == 5);
    assert(ctx.vec_items_added.data[0].state.current == 4); /* shifted */
    assert(ctx.vec_items_added.data[0].state.has_prev);
    assert(!ctx.vec_items_added.data[0].state.has_next);

    assert(ctx.vec_current_index_changed.size == 1);
    assert(ctx.vec_current_index_changed.data[0].current == 4);

    assert(ctx.vec_has_prev_changed.size == 1);
    assert(ctx.vec_has_prev_changed.data[0].has_prev);

    assert(ctx.vec_has_next_changed.size == 0);

    callback_ctx_reset(&ctx);

    /* append (after the current item) */
    ret = vlc_playlist_Append(playlist, &media[5], 5);
    assert(ret == VLC_SUCCESS);

    assert(ctx.vec_items_added.size == 1);
    assert(ctx.vec_items_added.data[0].index == 5);
    assert(ctx.vec_items_added.data[0].count == 5);
    assert(ctx.vec_items_added.data[0].state.playlist_size == 10);
    assert(ctx.vec_items_added.data[0].state.current == 4);
    assert(ctx.vec_items_added.data[0].state.has_prev);
    assert(ctx.vec_items_added.data[0].state.has_next);

    assert(ctx.vec_current_index_changed.size == 0);

    assert(ctx.vec_has_prev_changed.size == 0);

    assert(ctx.vec_has_next_changed.size == 1);
    assert(ctx.vec_has_next_changed.data[0].has_next);

    callback_ctx_destroy(&ctx);
    vlc_playlist_RemoveListener(playlist, listener);
    DestroyMediaArray(media, 10);
    vlc_playlist_Delete(playlist);
}

static void
test_items_moved_callbacks(void)
{
    vlc_playlist_t *playlist = vlc_playlist_New(NULL);
    assert(playlist);

    input_item_t *media[10];
    CreateDummyMediaArray(media, 10);

    /* initial playlist with 10 items */
    int ret = vlc_playlist_Append(playlist, media, 10);
    assert(ret == VLC_SUCCESS);

    struct vlc_playlist_callbacks cbs = {
        .on_items_moved = callback_on_items_moved,
        .on_current_index_changed = callback_on_current_index_changed,
        .on_has_prev_changed = callback_on_has_prev_changed,
        .on_has_next_changed = callback_on_has_next_changed,
    };

    struct callback_ctx ctx = CALLBACK_CTX_INITIALIZER;
    vlc_playlist_listener_id *listener =
            vlc_playlist_AddListener(playlist, &cbs, &ctx);
    assert(listener);

    vlc_playlist_Move(playlist, 2, 3, 5);

    assert(ctx.vec_items_moved.size == 1);
    assert(ctx.vec_items_moved.data[0].index == 2);
    assert(ctx.vec_items_moved.data[0].count == 3);
    assert(ctx.vec_items_moved.data[0].target == 5);
    assert(ctx.vec_items_moved.data[0].state.playlist_size == 10);
    assert(ctx.vec_items_moved.data[0].state.current == -1);
    assert(!ctx.vec_items_moved.data[0].state.has_prev);
    assert(ctx.vec_items_moved.data[0].state.has_next);

    assert(ctx.vec_current_index_changed.size == 0);
    assert(ctx.vec_has_prev_changed.size == 0);
    assert(ctx.vec_has_next_changed.size == 0);

    playlist->current = 3;
    playlist->has_prev = true;
    playlist->has_next = true;

    callback_ctx_reset(&ctx);

    /* the current index belongs to the moved slice */
    vlc_playlist_Move(playlist, 1, 3, 5);

    assert(ctx.vec_items_moved.size == 1);
    assert(ctx.vec_items_moved.data[0].index == 1);
    assert(ctx.vec_items_moved.data[0].count == 3);
    assert(ctx.vec_items_moved.data[0].target == 5);
    assert(ctx.vec_items_moved.data[0].state.playlist_size == 10);
    assert(ctx.vec_items_moved.data[0].state.current == 7);
    assert(ctx.vec_items_moved.data[0].state.has_prev);
    assert(ctx.vec_items_moved.data[0].state.has_next);

    assert(ctx.vec_current_index_changed.size == 1);
    assert(ctx.vec_current_index_changed.data[0].current == 7);

    assert(ctx.vec_has_prev_changed.size == 0);
    assert(ctx.vec_has_next_changed.size == 0);

    callback_ctx_reset(&ctx);

    /* as a result of this move, the current item (7) will be at index 0 */
    vlc_playlist_Move(playlist, 0, 7, 1);

    assert(ctx.vec_items_moved.size == 1);
    assert(ctx.vec_items_moved.data[0].index == 0);
    assert(ctx.vec_items_moved.data[0].count == 7);
    assert(ctx.vec_items_moved.data[0].target == 1);
    assert(ctx.vec_items_moved.data[0].state.playlist_size == 10);
    assert(ctx.vec_items_moved.data[0].state.current == 0);
    assert(!ctx.vec_items_moved.data[0].state.has_prev);
    assert(ctx.vec_items_moved.data[0].state.has_next);

    assert(ctx.vec_current_index_changed.size == 1);
    assert(ctx.vec_current_index_changed.data[0].current == 0);

    assert(ctx.vec_has_prev_changed.size == 1);
    assert(!ctx.vec_has_prev_changed.data[0].has_prev);

    assert(ctx.vec_has_next_changed.size == 0);

    callback_ctx_destroy(&ctx);
    vlc_playlist_RemoveListener(playlist, listener);
    DestroyMediaArray(media, 10);
    vlc_playlist_Delete(playlist);
}

static void
test_items_removed_callbacks(void)
{
    vlc_playlist_t *playlist = vlc_playlist_New(NULL);
    assert(playlist);

    input_item_t *media[10];
    CreateDummyMediaArray(media, 10);

    /* initial playlist with 10 items */
    int ret = vlc_playlist_Append(playlist, media, 10);
    assert(ret == VLC_SUCCESS);

    struct vlc_playlist_callbacks cbs = {
        .on_items_removed = callback_on_items_removed,
        .on_current_index_changed = callback_on_current_index_changed,
        .on_has_prev_changed = callback_on_has_prev_changed,
        .on_has_next_changed = callback_on_has_next_changed,
    };

    struct callback_ctx ctx = CALLBACK_CTX_INITIALIZER;
    vlc_playlist_listener_id *listener =
            vlc_playlist_AddListener(playlist, &cbs, &ctx);
    assert(listener);

    vlc_playlist_RemoveOne(playlist, 4);

    assert(ctx.vec_items_removed.size == 1);
    assert(ctx.vec_items_removed.data[0].index == 4);
    assert(ctx.vec_items_removed.data[0].count == 1);
    assert(ctx.vec_items_removed.data[0].state.playlist_size == 9);
    assert(ctx.vec_items_removed.data[0].state.current == -1);
    assert(!ctx.vec_items_removed.data[0].state.has_prev);
    assert(ctx.vec_items_removed.data[0].state.has_next);

    assert(ctx.vec_current_index_changed.size == 0);
    assert(ctx.vec_has_prev_changed.size == 0);
    assert(ctx.vec_has_next_changed.size == 0);

    playlist->current = 7;
    playlist->has_prev = true;
    playlist->has_next = true;

    callback_ctx_reset(&ctx);

    /* remove items before the current */
    vlc_playlist_Remove(playlist, 2, 4);

    assert(ctx.vec_items_removed.size == 1);
    assert(ctx.vec_items_removed.data[0].index == 2);
    assert(ctx.vec_items_removed.data[0].count == 4);
    assert(ctx.vec_items_removed.data[0].state.playlist_size == 5);
    assert(ctx.vec_items_removed.data[0].state.current == 3); /* shifted */
    assert(ctx.vec_items_removed.data[0].state.has_prev);
    assert(ctx.vec_items_removed.data[0].state.has_next);

    assert(ctx.vec_current_index_changed.size == 1);
    assert(ctx.vec_current_index_changed.data[0].current == 3);

    assert(ctx.vec_has_prev_changed.size == 0);

    assert(ctx.vec_has_next_changed.size == 0);

    callback_ctx_reset(&ctx);

    /* remove the remaining items (without Clear) */
    vlc_playlist_Remove(playlist, 0, 5);

    assert(ctx.vec_items_removed.size == 1);
    assert(ctx.vec_items_removed.data[0].index == 0);
    assert(ctx.vec_items_removed.data[0].count == 5);
    assert(ctx.vec_items_removed.data[0].state.playlist_size == 0);
    assert(ctx.vec_items_removed.data[0].state.current == -1);
    assert(!ctx.vec_items_removed.data[0].state.has_prev);
    assert(!ctx.vec_items_removed.data[0].state.has_next);

    assert(ctx.vec_current_index_changed.size == 1);
    assert(ctx.vec_current_index_changed.data[0].current == -1);

    assert(ctx.vec_has_prev_changed.size == 1);
    assert(!ctx.vec_has_prev_changed.data[0].has_prev);

    assert(ctx.vec_has_next_changed.size == 1);
    assert(!ctx.vec_has_next_changed.data[0].has_next);

    callback_ctx_destroy(&ctx);
    vlc_playlist_RemoveListener(playlist, listener);
    DestroyMediaArray(media, 10);
    vlc_playlist_Delete(playlist);
}

static void
test_items_reset_callbacks(void)
{
    vlc_playlist_t *playlist = vlc_playlist_New(NULL);
    assert(playlist);

    input_item_t *media[10];
    CreateDummyMediaArray(media, 10);

    /* initial playlist with 10 items */
    int ret = vlc_playlist_Append(playlist, media, 10);
    assert(ret == VLC_SUCCESS);

    struct vlc_playlist_callbacks cbs = {
        .on_items_reset = callback_on_items_reset,
        .on_current_index_changed = callback_on_current_index_changed,
        .on_has_prev_changed = callback_on_has_prev_changed,
        .on_has_next_changed = callback_on_has_next_changed,
    };

    struct callback_ctx ctx = CALLBACK_CTX_INITIALIZER;
    vlc_playlist_listener_id *listener =
            vlc_playlist_AddListener(playlist, &cbs, &ctx);
    assert(listener);

    callback_ctx_reset(&ctx);

    playlist->current = 9; /* last item */
    playlist->has_prev = true;
    playlist->has_next = false;

    vlc_playlist_Clear(playlist);

    assert(ctx.vec_items_reset.size == 1);
    assert(ctx.vec_items_reset.data[0].count == 0);
    assert(ctx.vec_items_reset.data[0].state.playlist_size == 0);
    assert(ctx.vec_items_reset.data[0].state.current == -1);
    assert(!ctx.vec_items_reset.data[0].state.has_prev);
    assert(!ctx.vec_items_reset.data[0].state.has_next);

    assert(ctx.vec_current_index_changed.size == 1);
    assert(ctx.vec_current_index_changed.data[0].current == -1);

    assert(ctx.vec_has_prev_changed.size == 1);
    assert(!ctx.vec_has_prev_changed.data[0].has_prev);

    assert(ctx.vec_has_next_changed.size == 0);

    callback_ctx_destroy(&ctx);
    vlc_playlist_RemoveListener(playlist, listener);
    DestroyMediaArray(media, 10);
    vlc_playlist_Delete(playlist);
}

static void
test_playback_repeat_changed_callbacks(void)
{
    vlc_playlist_t *playlist = vlc_playlist_New(NULL);
    assert(playlist);

    playlist->repeat = VLC_PLAYLIST_PLAYBACK_REPEAT_NONE;

    struct vlc_playlist_callbacks cbs = {
        .on_playback_repeat_changed = callback_on_playback_repeat_changed,
    };

    struct callback_ctx ctx = CALLBACK_CTX_INITIALIZER;
    vlc_playlist_listener_id *listener =
            vlc_playlist_AddListener(playlist, &cbs, &ctx);
    assert(listener);

    vlc_playlist_SetPlaybackRepeat(playlist, VLC_PLAYLIST_PLAYBACK_REPEAT_ALL);

    assert(vlc_playlist_GetPlaybackRepeat(playlist) ==
                                            VLC_PLAYLIST_PLAYBACK_REPEAT_ALL);

    assert(ctx.vec_playback_repeat_changed.size == 1);
    assert(ctx.vec_playback_repeat_changed.data[0].repeat ==
                                            VLC_PLAYLIST_PLAYBACK_REPEAT_ALL);

    callback_ctx_destroy(&ctx);
    vlc_playlist_RemoveListener(playlist, listener);
    vlc_playlist_Delete(playlist);
}

static void
test_playback_order_changed_callbacks(void)
{
    vlc_playlist_t *playlist = vlc_playlist_New(NULL);
    assert(playlist);

    playlist->order = VLC_PLAYLIST_PLAYBACK_ORDER_NORMAL;

    struct vlc_playlist_callbacks cbs = {
        .on_playback_order_changed = callback_on_playback_order_changed,
    };

    struct callback_ctx ctx = CALLBACK_CTX_INITIALIZER;
    vlc_playlist_listener_id *listener =
            vlc_playlist_AddListener(playlist, &cbs, &ctx);
    assert(listener);

    vlc_playlist_SetPlaybackOrder(playlist, VLC_PLAYLIST_PLAYBACK_ORDER_RANDOM);

    assert(vlc_playlist_GetPlaybackOrder(playlist) ==
                                            VLC_PLAYLIST_PLAYBACK_ORDER_RANDOM);

    assert(ctx.vec_playback_order_changed.size == 1);
    assert(ctx.vec_playback_order_changed.data[0].order ==
                                            VLC_PLAYLIST_PLAYBACK_ORDER_RANDOM);

    callback_ctx_destroy(&ctx);
    vlc_playlist_RemoveListener(playlist, listener);
    vlc_playlist_Delete(playlist);
}

static void
test_index_of(void)
{
    vlc_playlist_t *playlist = vlc_playlist_New(NULL);
    assert(playlist);

    input_item_t *media[10];
    CreateDummyMediaArray(media, 10);

    /* initial playlist with 9 items (1 is not added) */
    int ret = vlc_playlist_Append(playlist, media, 9);
    assert(ret == VLC_SUCCESS);

    assert(vlc_playlist_IndexOfMedia(playlist, media[4]) == 4);
    /* only items 0 to 8 were added */
    assert(vlc_playlist_IndexOfMedia(playlist, media[9]) == -1);

    vlc_playlist_item_t *item = vlc_playlist_Get(playlist, 4);
    assert(vlc_playlist_IndexOf(playlist, item) == 4);

    vlc_playlist_item_Hold(item);
    vlc_playlist_RemoveOne(playlist, 4);
    assert(vlc_playlist_IndexOf(playlist, item) == -1);
    vlc_playlist_item_Release(item);

    DestroyMediaArray(media, 10);
    vlc_playlist_Delete(playlist);
}

static void
test_prev(void)
{
    vlc_playlist_t *playlist = vlc_playlist_New(NULL);
    assert(playlist);

    input_item_t *media[4];
    CreateDummyMediaArray(media, 4);

    /* initial playlist with 3 items */
    int ret = vlc_playlist_Append(playlist, media, 3);
    assert(ret == VLC_SUCCESS);

    struct vlc_playlist_callbacks cbs = {
        .on_current_index_changed = callback_on_current_index_changed,
        .on_has_prev_changed = callback_on_has_prev_changed,
        .on_has_next_changed = callback_on_has_next_changed,
    };

    struct callback_ctx ctx = CALLBACK_CTX_INITIALIZER;
    vlc_playlist_listener_id *listener =
            vlc_playlist_AddListener(playlist, &cbs, &ctx);
    assert(listener);

    playlist->current = 2; /* last item */
    playlist->has_prev = true;
    playlist->has_next = false;

    /* go to the previous item (at index 1) */
    assert(vlc_playlist_HasPrev(playlist));
    ret = vlc_playlist_Prev(playlist);
    assert(ret == VLC_SUCCESS);

    assert(playlist->current == 1);
    assert(playlist->has_prev);
    assert(playlist->has_next);

    assert(ctx.vec_current_index_changed.size == 1);
    assert(ctx.vec_current_index_changed.data[0].current == 1);

    assert(ctx.vec_has_prev_changed.size == 0);

    assert(ctx.vec_has_next_changed.size == 1);
    assert(ctx.vec_has_next_changed.data[0].has_next);

    callback_ctx_reset(&ctx);

    /* go to the previous item (at index 0) */
    assert(vlc_playlist_HasPrev(playlist));
    ret = vlc_playlist_Prev(playlist);
    assert(ret == VLC_SUCCESS);

    assert(playlist->current == 0);
    assert(!playlist->has_prev);
    assert(playlist->has_next);

    assert(ctx.vec_current_index_changed.size == 1);
    assert(ctx.vec_current_index_changed.data[0].current == 0);

    assert(ctx.vec_has_prev_changed.size == 1);
    assert(!ctx.vec_has_prev_changed.data[0].has_prev);

    assert(ctx.vec_has_next_changed.size == 0);

    /* no more previous item */
    assert(!vlc_playlist_HasPrev(playlist));

    /* returns an error, but does not crash */
    assert(vlc_playlist_Prev(playlist) == VLC_EGENERIC);

    callback_ctx_destroy(&ctx);
    vlc_playlist_RemoveListener(playlist, listener);
    DestroyMediaArray(media, 4);
    vlc_playlist_Delete(playlist);
}

static void
test_next(void)
{
    vlc_playlist_t *playlist = vlc_playlist_New(NULL);
    assert(playlist);

    input_item_t *media[3];
    CreateDummyMediaArray(media, 3);

    /* initial playlist with 3 items */
    int ret = vlc_playlist_Append(playlist, media, 3);
    assert(ret == VLC_SUCCESS);

    struct vlc_playlist_callbacks cbs = {
        .on_current_index_changed = callback_on_current_index_changed,
        .on_has_prev_changed = callback_on_has_prev_changed,
        .on_has_next_changed = callback_on_has_next_changed,
    };

    struct callback_ctx ctx = CALLBACK_CTX_INITIALIZER;
    vlc_playlist_listener_id *listener =
            vlc_playlist_AddListener(playlist, &cbs, &ctx);
    assert(listener);

    playlist->current = 0; /* first item */
    playlist->has_prev = false;
    playlist->has_next = true;

    /* go to the next item (at index 1) */
    assert(vlc_playlist_HasNext(playlist));
    ret = vlc_playlist_Next(playlist);
    assert(ret == VLC_SUCCESS);

    assert(playlist->current == 1);
    assert(playlist->has_prev);
    assert(playlist->has_next);

    assert(ctx.vec_current_index_changed.size == 1);
    assert(ctx.vec_current_index_changed.data[0].current == 1);

    assert(ctx.vec_has_prev_changed.size == 1);
    assert(ctx.vec_has_prev_changed.data[0].has_prev);

    assert(ctx.vec_has_next_changed.size == 0);

    callback_ctx_reset(&ctx);

    /* go to the next item (at index 2) */
    assert(vlc_playlist_HasNext(playlist));
    ret = vlc_playlist_Next(playlist);
    assert(ret == VLC_SUCCESS);

    assert(playlist->current == 2);
    assert(playlist->has_prev);
    assert(!playlist->has_next);

    assert(ctx.vec_current_index_changed.size == 1);
    assert(ctx.vec_current_index_changed.data[0].current == 2);

    assert(ctx.vec_has_prev_changed.size == 0);

    assert(ctx.vec_has_next_changed.size == 1);
    assert(!ctx.vec_has_next_changed.data[0].has_next);

    /* no more next item */
    assert(!vlc_playlist_HasNext(playlist));

    /* returns an error, but does not crash */
    assert(vlc_playlist_Next(playlist) == VLC_EGENERIC);

    callback_ctx_destroy(&ctx);
    vlc_playlist_RemoveListener(playlist, listener);
    DestroyMediaArray(media, 3);
    vlc_playlist_Delete(playlist);
}

static void
test_goto(void)
{
    vlc_playlist_t *playlist = vlc_playlist_New(NULL);
    assert(playlist);

    input_item_t *media[10];
    CreateDummyMediaArray(media, 10);

    /* initial playlist with 10 items */
    int ret = vlc_playlist_Append(playlist, media, 10);
    assert(ret == VLC_SUCCESS);

    struct vlc_playlist_callbacks cbs = {
        .on_current_index_changed = callback_on_current_index_changed,
        .on_has_prev_changed = callback_on_has_prev_changed,
        .on_has_next_changed = callback_on_has_next_changed,
    };

    struct callback_ctx ctx = CALLBACK_CTX_INITIALIZER;
    vlc_playlist_listener_id *listener =
            vlc_playlist_AddListener(playlist, &cbs, &ctx);
    assert(listener);

    /* go to an item in the middle */
    ret = vlc_playlist_GoTo(playlist, 4);
    assert(ret == VLC_SUCCESS);

    assert(playlist->current == 4);
    assert(playlist->has_prev);
    assert(playlist->has_next);

    assert(ctx.vec_current_index_changed.size == 1);
    assert(ctx.vec_current_index_changed.data[0].current == 4);

    assert(ctx.vec_has_prev_changed.size == 1);
    assert(ctx.vec_has_prev_changed.data[0].has_prev);

    assert(ctx.vec_has_next_changed.size == 0);

    callback_ctx_reset(&ctx);

    /* go to the same item */
    ret = vlc_playlist_GoTo(playlist, 4);
    assert(ret == VLC_SUCCESS);

    assert(playlist->current == 4);
    assert(playlist->has_prev);
    assert(playlist->has_next);

    assert(ctx.vec_current_index_changed.size == 0);
    assert(ctx.vec_has_prev_changed.size == 0);
    assert(ctx.vec_has_next_changed.size == 0);

    callback_ctx_reset(&ctx);

    /* go to the first item */
    ret = vlc_playlist_GoTo(playlist, 0);
    assert(ret == VLC_SUCCESS);

    assert(playlist->current == 0);
    assert(!playlist->has_prev);
    assert(playlist->has_next);

    assert(ctx.vec_current_index_changed.size == 1);
    assert(ctx.vec_current_index_changed.data[0].current == 0);

    assert(ctx.vec_has_prev_changed.size == 1);
    assert(!ctx.vec_has_prev_changed.data[0].has_prev);

    assert(ctx.vec_has_next_changed.size == 0);

    callback_ctx_reset(&ctx);

    /* go to the last item */
    ret = vlc_playlist_GoTo(playlist, 9);
    assert(ret == VLC_SUCCESS);

    assert(playlist->current == 9);
    assert(playlist->has_prev);
    assert(!playlist->has_next);

    assert(ctx.vec_current_index_changed.size == 1);
    assert(ctx.vec_current_index_changed.data[0].current == 9);

    assert(ctx.vec_has_prev_changed.size == 1);
    assert(ctx.vec_has_prev_changed.data[0].has_prev);

    assert(ctx.vec_has_next_changed.size == 1);
    assert(!ctx.vec_has_next_changed.data[0].has_next);

    callback_ctx_reset(&ctx);

    /* deselect current */
    ret = vlc_playlist_GoTo(playlist, -1);
    assert(ret == VLC_SUCCESS);

    assert(playlist->current == -1);
    assert(!playlist->has_prev);
    assert(playlist->has_next);

    assert(ctx.vec_current_index_changed.size == 1);
    assert(ctx.vec_current_index_changed.data[0].current == -1);

    assert(ctx.vec_has_prev_changed.size == 1);
    assert(!ctx.vec_has_prev_changed.data[0].has_prev);

    assert(ctx.vec_has_next_changed.size == 1);
    assert(ctx.vec_has_next_changed.data[0].has_next);

    callback_ctx_destroy(&ctx);
    vlc_playlist_RemoveListener(playlist, listener);
    DestroyMediaArray(media, 10);
    vlc_playlist_Delete(playlist);
}

#undef EXPECT_AT

int main(void)
{
    test_append();
    test_insert();
    test_move();
    test_remove();
    test_clear();
    test_expand_item();
    test_items_added_callbacks();
    test_items_moved_callbacks();
    test_items_removed_callbacks();
    test_items_reset_callbacks();
    test_playback_repeat_changed_callbacks();
    test_playback_order_changed_callbacks();
    test_index_of();
    test_prev();
    test_next();
    test_goto();
    return 0;
}
