/*****************************************************************************
 * libvlc playlist test
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

#include <vlc_common.h>
#include <vlc_vector.h>
#include <vlc/libvlc.h>
#include <vlc/libvlc_media_player.h>
#include <vlc/libvlc_playlist.h>

#include <stdio.h>

#include "test.h"
#include "../lib/libvlc_internal.h"
#include "../lib/media_internal.h"

static inline libvlc_instance_t *
CreateLibvlc(void)
{
    libvlc_instance_t *libvlc =
        libvlc_new(test_defaults_nargs, test_defaults_args);
    assert(libvlc);

    // disable auto-preparsing in tests (media are dummy)
    vlc_object_t *obj = VLC_OBJECT(libvlc->p_libvlc_int);
    int ret = var_Create(obj, "auto-preparse", VLC_VAR_BOOL);
    assert(ret == VLC_SUCCESS);
    ret = var_SetBool(obj, "auto-preparse", false);
    assert(ret == VLC_SUCCESS);

    return libvlc;
}

static libvlc_media_t *
CreateDummyMedia(libvlc_instance_t *libvlc, int num)
{
    char *url;

    int res = asprintf(&url, "vlc://item-%d", num);
    if (res == -1)
        return NULL;

    libvlc_media_t *media = libvlc_media_new_path(libvlc, url);
    free(url);
    return media;
}

static void
CreateDummyMedias(libvlc_instance_t *libvlc, libvlc_media_t *out[],
                      size_t count)
{
    for (size_t i = 0; i < count; ++i)
    {
        out[i] = CreateDummyMedia(libvlc, i);
        assert(out[i]);
    }
}

static void
ReleaseMedias(libvlc_media_t *const medias[], size_t count)
{
    for (size_t i = 0; i < count; ++i)
        libvlc_media_release(medias[i]);
}

#define EXPECT_AT(index, id) \
    assert(libvlc_playlist_item_GetMedia( \
        libvlc_playlist_Get(playlist, index) \
    ) == media[id])

static void
test_append(void)
{
    libvlc_instance_t *libvlc = CreateLibvlc();
    assert(libvlc);

    libvlc_playlist_t *playlist = libvlc_playlist_New(libvlc);
    assert(playlist);

    libvlc_playlist_Lock(playlist);

    libvlc_media_t *media[10];
    CreateDummyMedias(libvlc, media, 10);

    /* append one by one */
    for (int i = 0; i < 5; ++i)
    {
        int ret = libvlc_playlist_AppendOne(playlist, media[i]);
        assert(ret == 0);
    }

    /* append several at once */
    int ret = libvlc_playlist_Append(playlist, &media[5], 5);
    assert(ret == 0);

    assert(libvlc_playlist_Count(playlist) == 10);
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

    libvlc_playlist_Unlock(playlist);

    ReleaseMedias(media, 10);
    libvlc_playlist_Delete(playlist);

    libvlc_release(libvlc);
}

static void
test_insert(void)
{
    libvlc_instance_t *libvlc = CreateLibvlc();
    assert(libvlc);

    libvlc_playlist_t *playlist = libvlc_playlist_New(libvlc);
    assert(playlist);

    libvlc_playlist_Lock(playlist);

    libvlc_media_t *media[15];
    CreateDummyMedias(libvlc, media, 15);

    /* initial playlist with 5 items */
    int ret = libvlc_playlist_Append(playlist, media, 5);
    assert(ret == 0);

    /* insert one by one */
    for (int i = 0; i < 5; ++i)
    {
        ret = libvlc_playlist_InsertOne(playlist, 2, media[i + 5]);
        assert(ret == 0);
    }

    /* insert several at once */
    ret = libvlc_playlist_Insert(playlist, 6, &media[10], 5);
    assert(ret == 0);

    assert(libvlc_playlist_Count(playlist) == 15);

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

    libvlc_playlist_Unlock(playlist);

    ReleaseMedias(media, 15);
    libvlc_playlist_Delete(playlist);

    libvlc_release(libvlc);
}

static void
test_move(void)
{
    libvlc_instance_t *libvlc = CreateLibvlc();
    assert(libvlc);

    libvlc_playlist_t *playlist = libvlc_playlist_New(libvlc);
    assert(playlist);

    libvlc_playlist_Lock(playlist);

    libvlc_media_t *media[10];
    CreateDummyMedias(libvlc, media, 10);

    /* initial playlist with 10 items */
    int ret = libvlc_playlist_Append(playlist, media, 10);
    assert(ret == 0);

    /* move slice {3, 4, 5, 6} so that its new position is 5 */
    libvlc_playlist_Move(playlist, 3, 4, 5);

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
    libvlc_playlist_Move(playlist, 5, 4, 3);

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

    libvlc_playlist_Unlock(playlist);

    ReleaseMedias(media, 10);
    libvlc_playlist_Delete(playlist);

    libvlc_release(libvlc);
}

static void
test_remove(void)
{
    libvlc_instance_t *libvlc = CreateLibvlc();
    assert(libvlc);

    libvlc_playlist_t *playlist = libvlc_playlist_New(libvlc);
    assert(playlist);

    libvlc_playlist_Lock(playlist);

    libvlc_media_t *media[10];
    CreateDummyMedias(libvlc, media, 10);

    /* initial playlist with 10 items */
    int ret = libvlc_playlist_Append(playlist, media, 10);
    assert(ret == 0);

    /* remove one by one */
    for (int i = 0; i < 3; ++i)
        libvlc_playlist_RemoveOne(playlist, 2);

    /* remove several at once */
    libvlc_playlist_Remove(playlist, 3, 2);

    assert(libvlc_playlist_Count(playlist) == 5);
    EXPECT_AT(0, 0);
    EXPECT_AT(1, 1);
    EXPECT_AT(2, 5);
    EXPECT_AT(3, 8);
    EXPECT_AT(4, 9);

    libvlc_playlist_Unlock(playlist);

    ReleaseMedias(media, 10);
    libvlc_playlist_Delete(playlist);

    libvlc_release(libvlc);
}

static void
test_clear(void)
{
    libvlc_instance_t *libvlc = CreateLibvlc();
    assert(libvlc);

    libvlc_playlist_t *playlist = libvlc_playlist_New(libvlc);
    assert(playlist);

    libvlc_playlist_Lock(playlist);

    libvlc_media_t *media[10];
    CreateDummyMedias(libvlc, media, 10);

    /* initial playlist with 10 items */
    int ret = libvlc_playlist_Append(playlist, media, 10);
    assert(ret == 0);

    assert(libvlc_playlist_Count(playlist) == 10);
    libvlc_playlist_Clear(playlist);
    assert(libvlc_playlist_Count(playlist) == 0);

    libvlc_playlist_Unlock(playlist);

    ReleaseMedias(media, 10);
    libvlc_playlist_Delete(playlist);

    libvlc_release(libvlc);
}

//static void
//test_expand_item(void)
//{
//    libvlc_instance_t *libvlc = CreateLibvlc();
//    assert(libvlc);
//
//    libvlc_playlist_t *playlist = libvlc_playlist_New(libvlc);
//    assert(playlist);
//
//    libvlc_media_t *media[16];
//    CreateDummyMedias(libvlc, media, 16);
//
//    /* initial playlist with 10 items */
//    int ret = libvlc_playlist_Append(playlist, media, 10);
//    assert(ret == 0);
//
//    /* create a subtree for item 8 with 4 children */
//    libvlc_media_t *item_to_expand =
//        libvlc_playlist_item_GetMedia(libvlc_playlist_Get(playlist, 8));
//    input_item_node_t *root = input_item_node_Create(item_to_expand);
//    for (int i = 0; i < 4; ++i)
//    {
//        input_item_node_t *node = input_item_node_AppendItem(root,
//                                                             media[i + 10]);
//        assert(node);
//    }
//
//    /* on the 3rd children, add 2 grand-children */
//    input_item_node_t *parent = root->pp_children[2];
//    for (int i = 0; i < 2; ++i)
//    {
//        input_item_node_t *node = input_item_node_AppendItem(parent,
//                                                             media[i + 14]);
//        assert(node);
//    }
//
//    playlist->current = 8;
//    playlist->has_prev = true;
//    playlist->has_next = true;
//
//    ret = libvlc_playlist_ExpandItem(playlist, 8, root);
//    assert(ret == 0);
//    assert(libvlc_playlist_Count(playlist) == 15);
//    EXPECT_AT(7, 7);
//
//    EXPECT_AT(8, 10);
//    EXPECT_AT(9, 11);
//    EXPECT_AT(10, 12);
//
//    EXPECT_AT(11, 14);
//    EXPECT_AT(12, 15);
//
//    EXPECT_AT(13, 13);
//
//    EXPECT_AT(14, 9);
//
//    /* item 8 will be replaced, the current must stay the same */
//    assert(playlist->current == 8);
//
//    input_item_node_Delete(root);
//    ReleaseMedias(media, 16);
//    libvlc_playlist_Delete(playlist);
//
//    libvlc_release(libvlc);
//}

struct playlist_state
{
    size_t playlist_size;
    ssize_t current;
    bool has_prev;
    bool has_next;
};

static void
playlist_state_init(struct playlist_state *state, libvlc_playlist_t *playlist)
{
    state->playlist_size = libvlc_playlist_Count(playlist);
    state->current = libvlc_playlist_GetCurrentIndex(playlist);
    state->has_prev = libvlc_playlist_HasPrev(playlist);
    state->has_next = libvlc_playlist_HasNext(playlist);
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
    enum libvlc_playlist_playback_repeat repeat;
};

struct playback_order_changed_report
{
    enum libvlc_playlist_playback_order order;
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
callback_on_items_reset(libvlc_playlist_t *playlist,
                        libvlc_playlist_item_t *const items[], size_t count,
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
callback_on_items_added(libvlc_playlist_t *playlist, size_t index,
                        libvlc_playlist_item_t *const items[], size_t count,
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
callback_on_items_moved(libvlc_playlist_t *playlist, size_t index, size_t count,
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
callback_on_items_removed(libvlc_playlist_t *playlist, size_t index, size_t count,
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
callback_on_playback_repeat_changed(libvlc_playlist_t *playlist,
                                    enum libvlc_playlist_playback_repeat repeat,
                                    void *userdata)
{
    VLC_UNUSED(playlist);
    struct callback_ctx *ctx = userdata;

    struct playback_repeat_changed_report report;
    report.repeat = repeat;
    vlc_vector_push(&ctx->vec_playback_repeat_changed, report);
}

static void
callback_on_playback_order_changed(libvlc_playlist_t *playlist,
                                   enum libvlc_playlist_playback_order order,
                                   void *userdata)
{
    VLC_UNUSED(playlist);
    struct callback_ctx *ctx = userdata;

    struct playback_order_changed_report report;
    report.order = order;
    vlc_vector_push(&ctx->vec_playback_order_changed, report);
}

static void
callback_on_current_index_changed(libvlc_playlist_t *playlist, ssize_t index,
                                  void *userdata)
{
    VLC_UNUSED(playlist);
    struct callback_ctx *ctx = userdata;

    struct current_index_changed_report report;
    report.current = index;
    vlc_vector_push(&ctx->vec_current_index_changed, report);
}

static void
callback_on_has_prev_changed(libvlc_playlist_t *playlist, bool has_prev,
                             void *userdata)
{
    VLC_UNUSED(playlist);
    struct callback_ctx *ctx = userdata;

    struct has_prev_changed_report report;
    report.has_prev = has_prev;
    vlc_vector_push(&ctx->vec_has_prev_changed, report);
}

static void
callback_on_has_next_changed(libvlc_playlist_t *playlist, bool has_next,
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
    libvlc_instance_t *libvlc = CreateLibvlc();
    assert(libvlc);

    libvlc_playlist_t *playlist = libvlc_playlist_New(libvlc);
    assert(playlist);

    libvlc_playlist_Lock(playlist);

    libvlc_media_t *media[10];
    CreateDummyMedias(libvlc, media, 10);

    struct libvlc_playlist_callbacks cbs = {
        .on_items_added = callback_on_items_added,
        .on_current_index_changed = callback_on_current_index_changed,
        .on_has_prev_changed = callback_on_has_prev_changed,
        .on_has_next_changed = callback_on_has_next_changed,
    };

    struct callback_ctx ctx = CALLBACK_CTX_INITIALIZER;
    libvlc_playlist_listener_id *listener =
            libvlc_playlist_AddListener(playlist, &cbs, &ctx, false);
    assert(listener);

    int ret = libvlc_playlist_AppendOne(playlist, media[0]);
    assert(ret == 0);

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

    /* set the only item as current */
    ret = libvlc_playlist_GoTo(playlist, 0);
    assert(ret == 0);

    callback_ctx_reset(&ctx);

    /* insert before the current item */
    ret = libvlc_playlist_Insert(playlist, 0, &media[1], 4);
    assert(ret == 0);

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
    ret = libvlc_playlist_Append(playlist, &media[5], 5);
    assert(ret == 0);

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

    libvlc_playlist_Unlock(playlist);

    callback_ctx_destroy(&ctx);
    libvlc_playlist_RemoveListener(playlist, listener);
    ReleaseMedias(media, 10);
    libvlc_playlist_Delete(playlist);

    libvlc_release(libvlc);
}

static void
test_items_moved_callbacks(void)
{
    libvlc_instance_t *libvlc = CreateLibvlc();
    assert(libvlc);

    libvlc_playlist_t *playlist = libvlc_playlist_New(libvlc);
    assert(playlist);

    libvlc_playlist_Lock(playlist);

    libvlc_media_t *media[10];
    CreateDummyMedias(libvlc, media, 10);

    /* initial playlist with 10 items */
    int ret = libvlc_playlist_Append(playlist, media, 10);
    assert(ret == 0);

    struct libvlc_playlist_callbacks cbs = {
        .on_items_moved = callback_on_items_moved,
        .on_current_index_changed = callback_on_current_index_changed,
        .on_has_prev_changed = callback_on_has_prev_changed,
        .on_has_next_changed = callback_on_has_next_changed,
    };

    struct callback_ctx ctx = CALLBACK_CTX_INITIALIZER;
    libvlc_playlist_listener_id *listener =
            libvlc_playlist_AddListener(playlist, &cbs, &ctx, false);
    assert(listener);

    libvlc_playlist_Move(playlist, 2, 3, 5);

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

    ret = libvlc_playlist_GoTo(playlist, 3);
    assert(ret == 0);

    callback_ctx_reset(&ctx);

    /* the current index belongs to the moved slice */
    libvlc_playlist_Move(playlist, 1, 3, 5);

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
    libvlc_playlist_Move(playlist, 0, 7, 1);

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

    ret = libvlc_playlist_GoTo(playlist, 5);
    assert(ret == 0);

    callback_ctx_reset(&ctx);

    libvlc_playlist_Move(playlist, 6, 2, 3);

    assert(ctx.vec_items_moved.size == 1);
    assert(ctx.vec_items_moved.data[0].index == 6);
    assert(ctx.vec_items_moved.data[0].count == 2);
    assert(ctx.vec_items_moved.data[0].target == 3);
    assert(ctx.vec_items_moved.data[0].state.playlist_size == 10);
    assert(ctx.vec_items_moved.data[0].state.current == 7);
    assert(ctx.vec_items_moved.data[0].state.has_prev);
    assert(ctx.vec_items_moved.data[0].state.has_next);

    assert(ctx.vec_current_index_changed.size == 1);
    assert(ctx.vec_current_index_changed.data[0].current == 7);

    assert(ctx.vec_has_prev_changed.size == 0);

    assert(ctx.vec_has_next_changed.size == 0);

    libvlc_playlist_Unlock(playlist);

    callback_ctx_destroy(&ctx);
    libvlc_playlist_RemoveListener(playlist, listener);
    ReleaseMedias(media, 10);
    libvlc_playlist_Delete(playlist);

    libvlc_release(libvlc);
}

static void
test_items_removed_callbacks(void)
{
    libvlc_instance_t *libvlc = CreateLibvlc();
    assert(libvlc);

    libvlc_playlist_t *playlist = libvlc_playlist_New(libvlc);
    assert(playlist);

    libvlc_playlist_Lock(playlist);

    libvlc_media_t *media[10];
    CreateDummyMedias(libvlc, media, 10);

    /* initial playlist with 10 items */
    int ret = libvlc_playlist_Append(playlist, media, 10);
    assert(ret == 0);

    struct libvlc_playlist_callbacks cbs = {
        .on_items_removed = callback_on_items_removed,
        .on_current_index_changed = callback_on_current_index_changed,
        .on_has_prev_changed = callback_on_has_prev_changed,
        .on_has_next_changed = callback_on_has_next_changed,
    };

    struct callback_ctx ctx = CALLBACK_CTX_INITIALIZER;
    libvlc_playlist_listener_id *listener =
            libvlc_playlist_AddListener(playlist, &cbs, &ctx, false);
    assert(listener);

    libvlc_playlist_RemoveOne(playlist, 4);

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

    ret = libvlc_playlist_GoTo(playlist, 7);
    assert(ret == 0);

    callback_ctx_reset(&ctx);

    /* remove items before the current */
    libvlc_playlist_Remove(playlist, 2, 4);

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
    libvlc_playlist_Remove(playlist, 0, 5);

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

    libvlc_playlist_Unlock(playlist);

    callback_ctx_destroy(&ctx);
    libvlc_playlist_RemoveListener(playlist, listener);
    ReleaseMedias(media, 10);
    libvlc_playlist_Delete(playlist);

    libvlc_release(libvlc);
}

static void
test_items_reset_callbacks(void)
{
    libvlc_instance_t *libvlc = CreateLibvlc();
    assert(libvlc);

    libvlc_playlist_t *playlist = libvlc_playlist_New(libvlc);
    assert(playlist);

    libvlc_playlist_Lock(playlist);

    libvlc_media_t *media[10];
    CreateDummyMedias(libvlc, media, 10);

    /* initial playlist with 10 items */
    int ret = libvlc_playlist_Append(playlist, media, 10);
    assert(ret == 0);

    struct libvlc_playlist_callbacks cbs = {
        .on_items_reset = callback_on_items_reset,
        .on_current_index_changed = callback_on_current_index_changed,
        .on_has_prev_changed = callback_on_has_prev_changed,
        .on_has_next_changed = callback_on_has_next_changed,
    };

    struct callback_ctx ctx = CALLBACK_CTX_INITIALIZER;
    libvlc_playlist_listener_id *listener =
            libvlc_playlist_AddListener(playlist, &cbs, &ctx, false);
    assert(listener);

    ret = libvlc_playlist_GoTo(playlist, 9); /* last item */
    assert(ret == 0);

    callback_ctx_reset(&ctx);

    libvlc_playlist_Clear(playlist);

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

    libvlc_playlist_Unlock(playlist);

    callback_ctx_destroy(&ctx);
    libvlc_playlist_RemoveListener(playlist, listener);
    ReleaseMedias(media, 10);
    libvlc_playlist_Delete(playlist);

    libvlc_release(libvlc);
}

static void
test_playback_repeat_changed_callbacks(void)
{
    libvlc_instance_t *libvlc = CreateLibvlc();
    assert(libvlc);

    libvlc_playlist_t *playlist = libvlc_playlist_New(libvlc);
    assert(playlist);

    libvlc_playlist_Lock(playlist);

    libvlc_playlist_SetPlaybackRepeat(playlist,
                                      LIBVLC_PLAYLIST_PLAYBACK_REPEAT_NONE);

    struct libvlc_playlist_callbacks cbs = {
        .on_playback_repeat_changed = callback_on_playback_repeat_changed,
    };

    struct callback_ctx ctx = CALLBACK_CTX_INITIALIZER;
    libvlc_playlist_listener_id *listener =
            libvlc_playlist_AddListener(playlist, &cbs, &ctx, false);
    assert(listener);

    libvlc_playlist_SetPlaybackRepeat(playlist, LIBVLC_PLAYLIST_PLAYBACK_REPEAT_ALL);

    assert(libvlc_playlist_GetPlaybackRepeat(playlist) ==
                                            LIBVLC_PLAYLIST_PLAYBACK_REPEAT_ALL);

    assert(ctx.vec_playback_repeat_changed.size == 1);
    assert(ctx.vec_playback_repeat_changed.data[0].repeat ==
                                            LIBVLC_PLAYLIST_PLAYBACK_REPEAT_ALL);

    libvlc_playlist_Unlock(playlist);

    callback_ctx_destroy(&ctx);
    libvlc_playlist_RemoveListener(playlist, listener);
    libvlc_playlist_Delete(playlist);

    libvlc_release(libvlc);
}

static void
test_playback_order_changed_callbacks(void)
{
    libvlc_instance_t *libvlc = CreateLibvlc();
    assert(libvlc);

    libvlc_playlist_t *playlist = libvlc_playlist_New(libvlc);
    assert(playlist);

    libvlc_playlist_Lock(playlist);

    libvlc_playlist_SetPlaybackOrder(playlist,
                                     LIBVLC_PLAYLIST_PLAYBACK_ORDER_NORMAL);

    struct libvlc_playlist_callbacks cbs = {
        .on_playback_order_changed = callback_on_playback_order_changed,
    };

    struct callback_ctx ctx = CALLBACK_CTX_INITIALIZER;
    libvlc_playlist_listener_id *listener =
            libvlc_playlist_AddListener(playlist, &cbs, &ctx, false);
    assert(listener);

    libvlc_playlist_SetPlaybackOrder(playlist, LIBVLC_PLAYLIST_PLAYBACK_ORDER_RANDOM);

    assert(libvlc_playlist_GetPlaybackOrder(playlist) ==
                                            LIBVLC_PLAYLIST_PLAYBACK_ORDER_RANDOM);

    assert(ctx.vec_playback_order_changed.size == 1);
    assert(ctx.vec_playback_order_changed.data[0].order ==
                                            LIBVLC_PLAYLIST_PLAYBACK_ORDER_RANDOM);

    libvlc_playlist_Unlock(playlist);

    callback_ctx_destroy(&ctx);
    libvlc_playlist_RemoveListener(playlist, listener);
    libvlc_playlist_Delete(playlist);

    libvlc_release(libvlc);
}

static void
test_callbacks_on_add_listener(void)
{
    libvlc_instance_t *libvlc = CreateLibvlc();
    assert(libvlc);

    libvlc_playlist_t *playlist = libvlc_playlist_New(libvlc);
    assert(playlist);

    libvlc_playlist_Lock(playlist);

    libvlc_media_t *media[10];
    CreateDummyMedias(libvlc, media, 10);

    /* initial playlist with 10 items */
    int ret = libvlc_playlist_Append(playlist, media, 10);
    assert(ret == 0);

    libvlc_playlist_SetPlaybackRepeat(playlist, LIBVLC_PLAYLIST_PLAYBACK_REPEAT_ALL);
    libvlc_playlist_SetPlaybackOrder(playlist, LIBVLC_PLAYLIST_PLAYBACK_ORDER_NORMAL);

    ret = libvlc_playlist_GoTo(playlist, 5);
    assert(ret == 0);

    struct libvlc_playlist_callbacks cbs = {
        .on_items_reset = callback_on_items_reset,
        .on_playback_repeat_changed = callback_on_playback_repeat_changed,
        .on_playback_order_changed = callback_on_playback_order_changed,
        .on_current_index_changed = callback_on_current_index_changed,
        .on_has_prev_changed = callback_on_has_prev_changed,
        .on_has_next_changed = callback_on_has_next_changed,
    };

    struct callback_ctx ctx = CALLBACK_CTX_INITIALIZER;
    libvlc_playlist_listener_id *listener =
            libvlc_playlist_AddListener(playlist, &cbs, &ctx, true);
    assert(listener);

    assert(ctx.vec_items_reset.size == 1);
    assert(ctx.vec_items_reset.data[0].count == 10);

    assert(ctx.vec_playback_repeat_changed.size == 1);
    assert(ctx.vec_playback_repeat_changed.data[0].repeat ==
                                            LIBVLC_PLAYLIST_PLAYBACK_REPEAT_ALL);

    assert(ctx.vec_playback_order_changed.size == 1);
    assert(ctx.vec_playback_order_changed.data[0].order ==
                                            LIBVLC_PLAYLIST_PLAYBACK_ORDER_NORMAL);

    assert(ctx.vec_current_index_changed.size == 1);
    assert(ctx.vec_current_index_changed.data[0].current == 5);

    assert(ctx.vec_has_prev_changed.size == 1);
    assert(ctx.vec_has_prev_changed.data[0].has_prev);

    assert(ctx.vec_has_next_changed.size == 1);
    assert(ctx.vec_has_next_changed.data[0].has_next);

    libvlc_playlist_Unlock(playlist);

    callback_ctx_destroy(&ctx);
    libvlc_playlist_RemoveListener(playlist, listener);
    ReleaseMedias(media, 10);
    libvlc_playlist_Delete(playlist);

    libvlc_release(libvlc);
}

static void
test_index_of(void)
{
    libvlc_instance_t *libvlc = CreateLibvlc();
    assert(libvlc);

    libvlc_playlist_t *playlist = libvlc_playlist_New(libvlc);
    assert(playlist);

    libvlc_playlist_Lock(playlist);

    libvlc_media_t *media[10];
    CreateDummyMedias(libvlc, media, 10);

    /* initial playlist with 9 items (1 is not added) */
    int ret = libvlc_playlist_Append(playlist, media, 9);
    assert(ret == 0);

    assert(libvlc_playlist_IndexOfMedia(playlist, media[4]) == 4);
    /* only items 0 to 8 were added */
    assert(libvlc_playlist_IndexOfMedia(playlist, media[9]) == -1);

    libvlc_playlist_item_t *item = libvlc_playlist_Get(playlist, 4);
    assert(libvlc_playlist_IndexOf(playlist, item) == 4);

    libvlc_playlist_item_Hold(item);
    libvlc_playlist_RemoveOne(playlist, 4);
    assert(libvlc_playlist_IndexOf(playlist, item) == -1);
    libvlc_playlist_item_Release(item);

    libvlc_playlist_Unlock(playlist);

    ReleaseMedias(media, 10);
    libvlc_playlist_Delete(playlist);

    libvlc_release(libvlc);
}

static void
test_prev(void)
{
    libvlc_instance_t *libvlc = CreateLibvlc();
    assert(libvlc);

    libvlc_playlist_t *playlist = libvlc_playlist_New(libvlc);
    assert(playlist);

    libvlc_playlist_Lock(playlist);

    libvlc_media_t *media[4];
    CreateDummyMedias(libvlc, media, 4);

    /* initial playlist with 3 items */
    int ret = libvlc_playlist_Append(playlist, media, 3);
    assert(ret == 0);

    struct libvlc_playlist_callbacks cbs = {
        .on_current_index_changed = callback_on_current_index_changed,
        .on_has_prev_changed = callback_on_has_prev_changed,
        .on_has_next_changed = callback_on_has_next_changed,
    };

    ret = libvlc_playlist_GoTo(playlist, 2);
    assert(ret == 0);

    struct callback_ctx ctx = CALLBACK_CTX_INITIALIZER;
    libvlc_playlist_listener_id *listener =
            libvlc_playlist_AddListener(playlist, &cbs, &ctx, false);
    assert(listener);

    /* go to the previous item (at index 1) */
    assert(libvlc_playlist_HasPrev(playlist));
    ret = libvlc_playlist_Prev(playlist);
    assert(ret == 0);

    assert(libvlc_playlist_GetCurrentIndex(playlist) == 1);
    assert(libvlc_playlist_HasPrev(playlist));
    assert(libvlc_playlist_HasNext(playlist));

    assert(ctx.vec_current_index_changed.size == 1);
    assert(ctx.vec_current_index_changed.data[0].current == 1);

    assert(ctx.vec_has_prev_changed.size == 0);

    assert(ctx.vec_has_next_changed.size == 1);
    assert(ctx.vec_has_next_changed.data[0].has_next);

    callback_ctx_reset(&ctx);

    /* go to the previous item (at index 0) */
    assert(libvlc_playlist_HasPrev(playlist));
    ret = libvlc_playlist_Prev(playlist);
    assert(ret == 0);

    assert(libvlc_playlist_GetCurrentIndex(playlist) == 0);
    assert(!libvlc_playlist_HasPrev(playlist));
    assert(libvlc_playlist_HasNext(playlist));

    assert(ctx.vec_current_index_changed.size == 1);
    assert(ctx.vec_current_index_changed.data[0].current == 0);

    assert(ctx.vec_has_prev_changed.size == 1);
    assert(!ctx.vec_has_prev_changed.data[0].has_prev);

    assert(ctx.vec_has_next_changed.size == 0);

    /* no more previous item */
    assert(!libvlc_playlist_HasPrev(playlist));

    /* returns an error, but does not crash */
    assert(libvlc_playlist_Prev(playlist) != 0);

    libvlc_playlist_Unlock(playlist);

    callback_ctx_destroy(&ctx);
    libvlc_playlist_RemoveListener(playlist, listener);
    ReleaseMedias(media, 4);
    libvlc_playlist_Delete(playlist);

    libvlc_release(libvlc);
}

static void
test_next(void)
{
    libvlc_instance_t *libvlc = CreateLibvlc();
    assert(libvlc);

    libvlc_playlist_t *playlist = libvlc_playlist_New(libvlc);
    assert(playlist);

    libvlc_playlist_Lock(playlist);

    libvlc_media_t *media[3];
    CreateDummyMedias(libvlc, media, 3);

    /* initial playlist with 3 items */
    int ret = libvlc_playlist_Append(playlist, media, 3);
    assert(ret == 0);

    struct libvlc_playlist_callbacks cbs = {
        .on_current_index_changed = callback_on_current_index_changed,
        .on_has_prev_changed = callback_on_has_prev_changed,
        .on_has_next_changed = callback_on_has_next_changed,
    };

    ret = libvlc_playlist_GoTo(playlist, 0); /* first item */
    assert(ret == 0);

    struct callback_ctx ctx = CALLBACK_CTX_INITIALIZER;
    libvlc_playlist_listener_id *listener =
            libvlc_playlist_AddListener(playlist, &cbs, &ctx, false);
    assert(listener);

    /* go to the next item (at index 1) */
    assert(libvlc_playlist_HasNext(playlist));
    ret = libvlc_playlist_Next(playlist);
    assert(ret == 0);

    assert(libvlc_playlist_GetCurrentIndex(playlist) == 1);
    assert(libvlc_playlist_HasPrev(playlist));
    assert(libvlc_playlist_HasNext(playlist));

    assert(ctx.vec_current_index_changed.size == 1);
    assert(ctx.vec_current_index_changed.data[0].current == 1);

    assert(ctx.vec_has_prev_changed.size == 1);
    assert(ctx.vec_has_prev_changed.data[0].has_prev);

    assert(ctx.vec_has_next_changed.size == 0);

    callback_ctx_reset(&ctx);

    /* go to the next item (at index 2) */
    assert(libvlc_playlist_HasNext(playlist));
    ret = libvlc_playlist_Next(playlist);
    assert(ret == 0);

    assert(libvlc_playlist_GetCurrentIndex(playlist) == 2);
    assert(libvlc_playlist_HasPrev(playlist));
    assert(!libvlc_playlist_HasNext(playlist));

    assert(ctx.vec_current_index_changed.size == 1);
    assert(ctx.vec_current_index_changed.data[0].current == 2);

    assert(ctx.vec_has_prev_changed.size == 0);

    assert(ctx.vec_has_next_changed.size == 1);
    assert(!ctx.vec_has_next_changed.data[0].has_next);

    /* no more next item */
    assert(!libvlc_playlist_HasNext(playlist));

    /* returns an error, but does not crash */
    assert(libvlc_playlist_Next(playlist) != 0);

    libvlc_playlist_Unlock(playlist);

    callback_ctx_destroy(&ctx);
    libvlc_playlist_RemoveListener(playlist, listener);
    ReleaseMedias(media, 3);
    libvlc_playlist_Delete(playlist);

    libvlc_release(libvlc);
}

static void
test_goto(void)
{
    libvlc_instance_t *libvlc = CreateLibvlc();
    assert(libvlc);

    libvlc_playlist_t *playlist = libvlc_playlist_New(libvlc);
    assert(playlist);

    libvlc_playlist_Lock(playlist);

    libvlc_media_t *media[10];
    CreateDummyMedias(libvlc, media, 10);

    /* initial playlist with 10 items */
    int ret = libvlc_playlist_Append(playlist, media, 10);
    assert(ret == 0);

    struct libvlc_playlist_callbacks cbs = {
        .on_current_index_changed = callback_on_current_index_changed,
        .on_has_prev_changed = callback_on_has_prev_changed,
        .on_has_next_changed = callback_on_has_next_changed,
    };

    struct callback_ctx ctx = CALLBACK_CTX_INITIALIZER;
    libvlc_playlist_listener_id *listener =
            libvlc_playlist_AddListener(playlist, &cbs, &ctx, false);
    assert(listener);

    /* go to an item in the middle */
    ret = libvlc_playlist_GoTo(playlist, 4);
    assert(ret == 0);

    assert(libvlc_playlist_GetCurrentIndex(playlist) == 4);
    assert(libvlc_playlist_HasPrev(playlist));
    assert(libvlc_playlist_HasNext(playlist));

    assert(ctx.vec_current_index_changed.size == 1);
    assert(ctx.vec_current_index_changed.data[0].current == 4);

    assert(ctx.vec_has_prev_changed.size == 1);
    assert(ctx.vec_has_prev_changed.data[0].has_prev);

    assert(ctx.vec_has_next_changed.size == 0);

    callback_ctx_reset(&ctx);

    /* go to the same item */
    ret = libvlc_playlist_GoTo(playlist, 4);
    assert(ret == 0);

    assert(libvlc_playlist_GetCurrentIndex(playlist) == 4);
    assert(libvlc_playlist_HasPrev(playlist));
    assert(libvlc_playlist_HasNext(playlist));

    assert(ctx.vec_current_index_changed.size == 0);
    assert(ctx.vec_has_prev_changed.size == 0);
    assert(ctx.vec_has_next_changed.size == 0);

    callback_ctx_reset(&ctx);

    /* go to the first item */
    ret = libvlc_playlist_GoTo(playlist, 0);
    assert(ret == 0);

    assert(libvlc_playlist_GetCurrentIndex(playlist) == 0);
    assert(!libvlc_playlist_HasPrev(playlist));
    assert(libvlc_playlist_HasNext(playlist));

    assert(ctx.vec_current_index_changed.size == 1);
    assert(ctx.vec_current_index_changed.data[0].current == 0);

    assert(ctx.vec_has_prev_changed.size == 1);
    assert(!ctx.vec_has_prev_changed.data[0].has_prev);

    assert(ctx.vec_has_next_changed.size == 0);

    callback_ctx_reset(&ctx);

    /* go to the last item */
    ret = libvlc_playlist_GoTo(playlist, 9);
    assert(ret == 0);

    assert(libvlc_playlist_GetCurrentIndex(playlist) == 9);
    assert(libvlc_playlist_HasPrev(playlist));
    assert(!libvlc_playlist_HasNext(playlist));

    assert(ctx.vec_current_index_changed.size == 1);
    assert(ctx.vec_current_index_changed.data[0].current == 9);

    assert(ctx.vec_has_prev_changed.size == 1);
    assert(ctx.vec_has_prev_changed.data[0].has_prev);

    assert(ctx.vec_has_next_changed.size == 1);
    assert(!ctx.vec_has_next_changed.data[0].has_next);

    callback_ctx_reset(&ctx);

    /* deselect current */
    ret = libvlc_playlist_GoTo(playlist, -1);
    assert(ret == 0);

    assert(libvlc_playlist_GetCurrentIndex(playlist) == -1);
    assert(!libvlc_playlist_HasPrev(playlist));
    assert(libvlc_playlist_HasNext(playlist));

    assert(ctx.vec_current_index_changed.size == 1);
    assert(ctx.vec_current_index_changed.data[0].current == -1);

    assert(ctx.vec_has_prev_changed.size == 1);
    assert(!ctx.vec_has_prev_changed.data[0].has_prev);

    assert(ctx.vec_has_next_changed.size == 1);
    assert(ctx.vec_has_next_changed.data[0].has_next);

    libvlc_playlist_Unlock(playlist);

    callback_ctx_destroy(&ctx);
    libvlc_playlist_RemoveListener(playlist, listener);
    ReleaseMedias(media, 10);
    libvlc_playlist_Delete(playlist);

    libvlc_release(libvlc);
}

static void
test_request_insert(void)
{
    libvlc_instance_t *libvlc = CreateLibvlc();
    assert(libvlc);

    libvlc_playlist_t *playlist = libvlc_playlist_New(libvlc);
    assert(playlist);

    libvlc_playlist_Lock(playlist);

    libvlc_media_t *media[5];
    CreateDummyMedias(libvlc, media, 5);

    /* initial playlist with 3 items */
    int ret = libvlc_playlist_Append(playlist, media, 3);
    assert(ret == 0);

    struct libvlc_playlist_callbacks cbs = {
        .on_items_added = callback_on_items_added,
    };

    struct callback_ctx ctx = CALLBACK_CTX_INITIALIZER;
    libvlc_playlist_listener_id *listener =
            libvlc_playlist_AddListener(playlist, &cbs, &ctx, false);
    assert(listener);

    /* insert 5 items at index 10 (out-of-bounds) */
    ret = libvlc_playlist_RequestInsert(playlist, 10, &media[3], 2);
    assert(ret == 0);

    assert(libvlc_playlist_Count(playlist) == 5);

    EXPECT_AT(0, 0);
    EXPECT_AT(1, 1);
    EXPECT_AT(2, 2);
    EXPECT_AT(3, 3);
    EXPECT_AT(4, 4);

    assert(ctx.vec_items_added.size == 1);
    assert(ctx.vec_items_added.data[0].index == 3); /* index was changed */
    assert(ctx.vec_items_added.data[0].count == 2);
    assert(ctx.vec_items_added.data[0].state.playlist_size == 5);

    libvlc_playlist_Unlock(playlist);

    callback_ctx_destroy(&ctx);
    libvlc_playlist_RemoveListener(playlist, listener);
    ReleaseMedias(media, 5);
    libvlc_playlist_Delete(playlist);

    libvlc_release(libvlc);
}

static void
test_request_remove_with_matching_hint(void)
{
    libvlc_instance_t *libvlc = CreateLibvlc();
    assert(libvlc);

    libvlc_playlist_t *playlist = libvlc_playlist_New(libvlc);
    assert(playlist);

    libvlc_playlist_Lock(playlist);

    libvlc_media_t *media[10];
    CreateDummyMedias(libvlc, media, 10);

    /* initial playlist with 10 items */
    int ret = libvlc_playlist_Append(playlist, media, 10);
    assert(ret == 0);

    struct libvlc_playlist_callbacks cbs = {
        .on_items_removed = callback_on_items_removed,
    };

    struct callback_ctx ctx = CALLBACK_CTX_INITIALIZER;
    libvlc_playlist_listener_id *listener =
            libvlc_playlist_AddListener(playlist, &cbs, &ctx, false);
    assert(listener);

    libvlc_playlist_item_t *items_to_remove[] = {
        libvlc_playlist_Get(playlist, 3),
        libvlc_playlist_Get(playlist, 4),
        libvlc_playlist_Get(playlist, 5),
        libvlc_playlist_Get(playlist, 6),
    };

    ret = libvlc_playlist_RequestRemove(playlist, items_to_remove, 4, 3);
    assert(ret == 0);

    assert(libvlc_playlist_Count(playlist) == 6);

    EXPECT_AT(0, 0);
    EXPECT_AT(1, 1);
    EXPECT_AT(2, 2);
    EXPECT_AT(3, 7);
    EXPECT_AT(4, 8);
    EXPECT_AT(5, 9);

    assert(ctx.vec_items_removed.size == 1);
    assert(ctx.vec_items_removed.data[0].index == 3);
    assert(ctx.vec_items_removed.data[0].count == 4);
    assert(ctx.vec_items_removed.data[0].state.playlist_size == 6);

    libvlc_playlist_Unlock(playlist);

    callback_ctx_destroy(&ctx);
    libvlc_playlist_RemoveListener(playlist, listener);
    ReleaseMedias(media, 10);
    libvlc_playlist_Delete(playlist);

    libvlc_release(libvlc);
}

static void
test_request_remove_without_hint(void)
{
    libvlc_instance_t *libvlc = CreateLibvlc();
    assert(libvlc);

    libvlc_playlist_t *playlist = libvlc_playlist_New(libvlc);
    assert(playlist);

    libvlc_playlist_Lock(playlist);

    libvlc_media_t *media[10];
    CreateDummyMedias(libvlc, media, 10);

    /* initial playlist with 10 items */
    int ret = libvlc_playlist_Append(playlist, media, 10);
    assert(ret == 0);

    struct libvlc_playlist_callbacks cbs = {
        .on_items_removed = callback_on_items_removed,
    };

    struct callback_ctx ctx = CALLBACK_CTX_INITIALIZER;
    libvlc_playlist_listener_id *listener =
            libvlc_playlist_AddListener(playlist, &cbs, &ctx, false);
    assert(listener);

    libvlc_playlist_item_t *items_to_remove[] = {
        libvlc_playlist_Get(playlist, 3),
        libvlc_playlist_Get(playlist, 4),
        libvlc_playlist_Get(playlist, 5),
        libvlc_playlist_Get(playlist, 6),
    };

    ret = libvlc_playlist_RequestRemove(playlist, items_to_remove, 4, -1);
    assert(ret == 0);

    assert(libvlc_playlist_Count(playlist) == 6);

    EXPECT_AT(0, 0);
    EXPECT_AT(1, 1);
    EXPECT_AT(2, 2);
    EXPECT_AT(3, 7);
    EXPECT_AT(4, 8);
    EXPECT_AT(5, 9);

    assert(ctx.vec_items_removed.size == 1);
    assert(ctx.vec_items_removed.data[0].index == 3);
    assert(ctx.vec_items_removed.data[0].count == 4);
    assert(ctx.vec_items_removed.data[0].state.playlist_size == 6);

    libvlc_playlist_Unlock(playlist);

    callback_ctx_destroy(&ctx);
    libvlc_playlist_RemoveListener(playlist, listener);
    ReleaseMedias(media, 10);
    libvlc_playlist_Delete(playlist);

    libvlc_release(libvlc);
}

static void
test_request_remove_adapt(void)
{
    libvlc_instance_t *libvlc = CreateLibvlc();
    assert(libvlc);

    libvlc_playlist_t *playlist = libvlc_playlist_New(libvlc);
    assert(playlist);

    libvlc_playlist_Lock(playlist);

    libvlc_media_t *media[11];
    CreateDummyMedias(libvlc, media, 11);

    /* initial playlist with 11 items */
    int ret = libvlc_playlist_Append(playlist, media, 11);
    assert(ret == 0);

    /* remove the last one so that it does not exist in the playlist */
    libvlc_playlist_item_t *dummy = libvlc_playlist_Get(playlist, 10);
    libvlc_playlist_item_Hold(dummy);
    assert(dummy);
    libvlc_playlist_RemoveOne(playlist, 10);

    struct libvlc_playlist_callbacks cbs = {
        .on_items_removed = callback_on_items_removed,
    };

    struct callback_ctx ctx = CALLBACK_CTX_INITIALIZER;
    libvlc_playlist_listener_id *listener =
            libvlc_playlist_AddListener(playlist, &cbs, &ctx, false);
    assert(listener);

    /* remove items in a wrong order at wrong position, as if the playlist had
     * been sorted/shuffled before the request were applied */
    libvlc_playlist_item_t *items_to_remove[] = {
        libvlc_playlist_Get(playlist, 3),
        libvlc_playlist_Get(playlist, 2),
        libvlc_playlist_Get(playlist, 6),
        libvlc_playlist_Get(playlist, 9),
        libvlc_playlist_Get(playlist, 1),
        dummy, /* inexistant */
        libvlc_playlist_Get(playlist, 8),
    };

    ret = libvlc_playlist_RequestRemove(playlist, items_to_remove, 7, 3);
    assert(ret == 0);

    libvlc_playlist_item_Release(dummy);

    assert(libvlc_playlist_Count(playlist) == 4);

    EXPECT_AT(0, 0);
    EXPECT_AT(1, 4);
    EXPECT_AT(2, 5);
    EXPECT_AT(3, 7);

    /* it should notify 3 different slices removed, in descending order for
     * optimization: {8,9}, {6}, {1,2,3}. */

    assert(ctx.vec_items_removed.size == 3);

    assert(ctx.vec_items_removed.data[0].index == 8);
    assert(ctx.vec_items_removed.data[0].count == 2);
    assert(ctx.vec_items_removed.data[0].state.playlist_size == 8);

    assert(ctx.vec_items_removed.data[1].index == 6);
    assert(ctx.vec_items_removed.data[1].count == 1);
    assert(ctx.vec_items_removed.data[1].state.playlist_size == 7);

    assert(ctx.vec_items_removed.data[2].index == 1);
    assert(ctx.vec_items_removed.data[2].count == 3);
    assert(ctx.vec_items_removed.data[2].state.playlist_size == 4);

    libvlc_playlist_Unlock(playlist);

    callback_ctx_destroy(&ctx);
    libvlc_playlist_RemoveListener(playlist, listener);
    ReleaseMedias(media, 11);
    libvlc_playlist_Delete(playlist);

    libvlc_release(libvlc);
}

static void
test_request_move_with_matching_hint(void)
{
    libvlc_instance_t *libvlc = CreateLibvlc();
    assert(libvlc);

    libvlc_playlist_t *playlist = libvlc_playlist_New(libvlc);
    assert(playlist);

    libvlc_playlist_Lock(playlist);

    libvlc_media_t *media[10];
    CreateDummyMedias(libvlc, media, 10);

    /* initial playlist with 10 items */
    int ret = libvlc_playlist_Append(playlist, media, 10);
    assert(ret == 0);

    struct libvlc_playlist_callbacks cbs = {
        .on_items_moved = callback_on_items_moved,
    };

    struct callback_ctx ctx = CALLBACK_CTX_INITIALIZER;
    libvlc_playlist_listener_id *listener =
            libvlc_playlist_AddListener(playlist, &cbs, &ctx, false);
    assert(listener);

    libvlc_playlist_item_t *items_to_move[] = {
        libvlc_playlist_Get(playlist, 5),
        libvlc_playlist_Get(playlist, 6),
        libvlc_playlist_Get(playlist, 7),
        libvlc_playlist_Get(playlist, 8),
    };

    ret = libvlc_playlist_RequestMove(playlist, items_to_move, 4, 2, 5);
    assert(ret == 0);

    assert(libvlc_playlist_Count(playlist) == 10);

    EXPECT_AT(0, 0);
    EXPECT_AT(1, 1);
    EXPECT_AT(2, 5);
    EXPECT_AT(3, 6);
    EXPECT_AT(4, 7);
    EXPECT_AT(5, 8);
    EXPECT_AT(6, 2);
    EXPECT_AT(7, 3);
    EXPECT_AT(8, 4);
    EXPECT_AT(9, 9);

    assert(ctx.vec_items_moved.size == 1);
    assert(ctx.vec_items_moved.data[0].index == 5);
    assert(ctx.vec_items_moved.data[0].count == 4);
    assert(ctx.vec_items_moved.data[0].state.playlist_size == 10);

    libvlc_playlist_Unlock(playlist);

    callback_ctx_destroy(&ctx);
    libvlc_playlist_RemoveListener(playlist, listener);
    ReleaseMedias(media, 10);
    libvlc_playlist_Delete(playlist);

    libvlc_release(libvlc);
}

static void
test_request_move_without_hint(void)
{
    libvlc_instance_t *libvlc = CreateLibvlc();
    assert(libvlc);

    libvlc_playlist_t *playlist = libvlc_playlist_New(libvlc);
    assert(playlist);

    libvlc_playlist_Lock(playlist);

    libvlc_media_t *media[10];
    CreateDummyMedias(libvlc, media, 10);

    /* initial playlist with 10 items */
    int ret = libvlc_playlist_Append(playlist, media, 10);
    assert(ret == 0);

    struct libvlc_playlist_callbacks cbs = {
        .on_items_moved = callback_on_items_moved,
    };

    struct callback_ctx ctx = CALLBACK_CTX_INITIALIZER;
    libvlc_playlist_listener_id *listener =
            libvlc_playlist_AddListener(playlist, &cbs, &ctx, false);
    assert(listener);

    libvlc_playlist_item_t *items_to_move[] = {
        libvlc_playlist_Get(playlist, 5),
        libvlc_playlist_Get(playlist, 6),
        libvlc_playlist_Get(playlist, 7),
        libvlc_playlist_Get(playlist, 8),
    };

    ret = libvlc_playlist_RequestMove(playlist, items_to_move, 4, 2, -1);
    assert(ret == 0);

    assert(libvlc_playlist_Count(playlist) == 10);

    EXPECT_AT(0, 0);
    EXPECT_AT(1, 1);
    EXPECT_AT(2, 5);
    EXPECT_AT(3, 6);
    EXPECT_AT(4, 7);
    EXPECT_AT(5, 8);
    EXPECT_AT(6, 2);
    EXPECT_AT(7, 3);
    EXPECT_AT(8, 4);
    EXPECT_AT(9, 9);

    assert(ctx.vec_items_moved.size == 1);
    assert(ctx.vec_items_moved.data[0].index == 5);
    assert(ctx.vec_items_moved.data[0].count == 4);
    assert(ctx.vec_items_moved.data[0].state.playlist_size == 10);

    libvlc_playlist_item_t *item = libvlc_playlist_Get(playlist, 3);
    /* move it to index 42 (out of bounds) */
    libvlc_playlist_RequestMove(playlist, &item, 1, 42, -1);

    EXPECT_AT(0, 0);
    EXPECT_AT(1, 1);
    EXPECT_AT(2, 5);
    EXPECT_AT(3, 7);
    EXPECT_AT(4, 8);
    EXPECT_AT(5, 2);
    EXPECT_AT(6, 3);
    EXPECT_AT(7, 4);
    EXPECT_AT(8, 9);
    EXPECT_AT(9, 6);

    libvlc_playlist_Unlock(playlist);

    callback_ctx_destroy(&ctx);
    libvlc_playlist_RemoveListener(playlist, listener);
    ReleaseMedias(media, 10);
    libvlc_playlist_Delete(playlist);

    libvlc_release(libvlc);
}

static void
test_request_move_adapt(void)
{
    libvlc_instance_t *libvlc = CreateLibvlc();
    assert(libvlc);

    libvlc_playlist_t *playlist = libvlc_playlist_New(libvlc);
    assert(playlist);

    libvlc_playlist_Lock(playlist);

    libvlc_media_t *media[16];
    CreateDummyMedias(libvlc, media, 16);

    /* initial playlist with 16 items */
    int ret = libvlc_playlist_Append(playlist, media, 16);
    assert(ret == 0);

    /* remove the last one so that it does not exist in the playlist */
    libvlc_playlist_item_t *dummy = libvlc_playlist_Get(playlist, 15);
    libvlc_playlist_item_Hold(dummy);
    assert(dummy);
    libvlc_playlist_RemoveOne(playlist, 15);

    struct libvlc_playlist_callbacks cbs = {
        .on_items_moved = callback_on_items_moved,
    };

    struct callback_ctx ctx = CALLBACK_CTX_INITIALIZER;
    libvlc_playlist_listener_id *listener =
            libvlc_playlist_AddListener(playlist, &cbs, &ctx, false);
    assert(listener);

    /* move items in a wrong order at wrong position, as if the playlist had
     * been sorted/shuffled before the request were applied */
    libvlc_playlist_item_t *items_to_move[] = {
        libvlc_playlist_Get(playlist, 7),
        libvlc_playlist_Get(playlist, 8),
        libvlc_playlist_Get(playlist, 5),
        libvlc_playlist_Get(playlist, 12),
        dummy, /* inexistant */
        libvlc_playlist_Get(playlist, 3),
        libvlc_playlist_Get(playlist, 13),
        libvlc_playlist_Get(playlist, 14),
        libvlc_playlist_Get(playlist, 1),
    };

    libvlc_playlist_RequestMove(playlist, items_to_move, 9, 3, 2);

    libvlc_playlist_item_Release(dummy);

    assert(libvlc_playlist_Count(playlist) == 15);

    EXPECT_AT(0, 0);
    EXPECT_AT(1, 2);
    EXPECT_AT(2, 4);

    EXPECT_AT(3, 7);
    EXPECT_AT(4, 8);
    EXPECT_AT(5, 5);
    EXPECT_AT(6, 12);
    EXPECT_AT(7, 3);
    EXPECT_AT(8, 13);
    EXPECT_AT(9, 14);
    EXPECT_AT(10, 1);

    EXPECT_AT(11, 6);
    EXPECT_AT(12, 9);
    EXPECT_AT(13, 10);
    EXPECT_AT(14, 11);

    /* there are 6 slices to move: 7-8, 5, 12, 3, 13-14, 1 */
    assert(ctx.vec_items_moved.size == 6);

    struct VLC_VECTOR(int) vec = VLC_VECTOR_INITIALIZER;
    for (int i = 0; i < 15; ++i)
        vlc_vector_push(&vec, i * 10);

    struct items_moved_report report;
    vlc_vector_foreach(report, &ctx.vec_items_moved)
        /* apply the changes as reported by the callbacks */
        vlc_vector_move_slice(&vec, report.index, report.count, report.target);

    /* the vector items must have been moved the same way as the playlist */
    assert(vec.size == 15);
    assert(vec.data[0] == 0);
    assert(vec.data[1] == 20);
    assert(vec.data[2] == 40);
    assert(vec.data[3] == 70);
    assert(vec.data[4] == 80);
    assert(vec.data[5] == 50);
    assert(vec.data[6] == 120);
    assert(vec.data[7] == 30);
    assert(vec.data[8] == 130);
    assert(vec.data[9] == 140);
    assert(vec.data[10] == 10);
    assert(vec.data[11] == 60);
    assert(vec.data[12] == 90);
    assert(vec.data[13] == 100);
    assert(vec.data[14] == 110);

    vlc_vector_destroy(&vec);

    libvlc_playlist_Unlock(playlist);

    callback_ctx_destroy(&ctx);
    libvlc_playlist_RemoveListener(playlist, listener);
    ReleaseMedias(media, 16);
    libvlc_playlist_Delete(playlist);

    libvlc_release(libvlc);
}

static void
test_request_goto_with_matching_hint(void)
{
    libvlc_instance_t *libvlc = CreateLibvlc();
    assert(libvlc);

    libvlc_playlist_t *playlist = libvlc_playlist_New(libvlc);
    assert(playlist);

    libvlc_playlist_Lock(playlist);

    libvlc_media_t *media[10];
    CreateDummyMedias(libvlc, media, 10);

    /* initial playlist with 10 items */
    int ret = libvlc_playlist_Append(playlist, media, 10);
    assert(ret == 0);

    struct libvlc_playlist_callbacks cbs = {
        .on_current_index_changed = callback_on_current_index_changed,
        .on_has_prev_changed = callback_on_has_prev_changed,
        .on_has_next_changed = callback_on_has_next_changed,
    };

    struct callback_ctx ctx = CALLBACK_CTX_INITIALIZER;
    libvlc_playlist_listener_id *listener =
            libvlc_playlist_AddListener(playlist, &cbs, &ctx, false);
    assert(listener);

    /* go to an item in the middle, with incorrect index_hint */
    libvlc_playlist_item_t *item = libvlc_playlist_Get(playlist, 4);
    ret = libvlc_playlist_RequestGoTo(playlist, item, 4);
    assert(ret == 0);

    assert(libvlc_playlist_GetCurrentIndex(playlist) == 4);
    assert(libvlc_playlist_HasPrev(playlist));
    assert(libvlc_playlist_HasNext(playlist));

    assert(ctx.vec_current_index_changed.size == 1);
    assert(ctx.vec_current_index_changed.data[0].current == 4);

    assert(ctx.vec_has_prev_changed.size == 1);
    assert(ctx.vec_has_prev_changed.data[0].has_prev);

    assert(ctx.vec_has_next_changed.size == 0);

    libvlc_playlist_Unlock(playlist);

    callback_ctx_destroy(&ctx);
    libvlc_playlist_RemoveListener(playlist, listener);
    ReleaseMedias(media, 10);
    libvlc_playlist_Delete(playlist);

    libvlc_release(libvlc);
}

static void
test_request_goto_without_hint(void)
{
    libvlc_instance_t *libvlc = CreateLibvlc();
    assert(libvlc);

    libvlc_playlist_t *playlist = libvlc_playlist_New(libvlc);
    assert(playlist);

    libvlc_playlist_Lock(playlist);

    libvlc_media_t *media[10];
    CreateDummyMedias(libvlc, media, 10);

    /* initial playlist with 10 items */
    int ret = libvlc_playlist_Append(playlist, media, 10);
    assert(ret == 0);

    struct libvlc_playlist_callbacks cbs = {
        .on_current_index_changed = callback_on_current_index_changed,
        .on_has_prev_changed = callback_on_has_prev_changed,
        .on_has_next_changed = callback_on_has_next_changed,
    };

    struct callback_ctx ctx = CALLBACK_CTX_INITIALIZER;
    libvlc_playlist_listener_id *listener =
            libvlc_playlist_AddListener(playlist, &cbs, &ctx, false);
    assert(listener);

    /* go to an item in the middle, with incorrect index_hint */
    libvlc_playlist_item_t *item = libvlc_playlist_Get(playlist, 4);
    ret = libvlc_playlist_RequestGoTo(playlist, item, -1); /* no hint */
    assert(ret == 0);

    assert(libvlc_playlist_GetCurrentIndex(playlist) == 4);
    assert(libvlc_playlist_HasPrev(playlist));
    assert(libvlc_playlist_HasNext(playlist));

    assert(ctx.vec_current_index_changed.size == 1);
    assert(ctx.vec_current_index_changed.data[0].current == 4);

    assert(ctx.vec_has_prev_changed.size == 1);
    assert(ctx.vec_has_prev_changed.data[0].has_prev);

    assert(ctx.vec_has_next_changed.size == 0);

    libvlc_playlist_Unlock(playlist);

    callback_ctx_destroy(&ctx);
    libvlc_playlist_RemoveListener(playlist, listener);
    ReleaseMedias(media, 10);
    libvlc_playlist_Delete(playlist);

    libvlc_release(libvlc);
}

static void
test_request_goto_adapt(void)
{
    libvlc_instance_t *libvlc = CreateLibvlc();
    assert(libvlc);

    libvlc_playlist_t *playlist = libvlc_playlist_New(libvlc);
    assert(playlist);

    libvlc_playlist_Lock(playlist);

    libvlc_media_t *media[10];
    CreateDummyMedias(libvlc, media, 10);

    /* initial playlist with 10 items */
    int ret = libvlc_playlist_Append(playlist, media, 10);
    assert(ret == 0);

    struct libvlc_playlist_callbacks cbs = {
        .on_current_index_changed = callback_on_current_index_changed,
        .on_has_prev_changed = callback_on_has_prev_changed,
        .on_has_next_changed = callback_on_has_next_changed,
    };

    struct callback_ctx ctx = CALLBACK_CTX_INITIALIZER;
    libvlc_playlist_listener_id *listener =
            libvlc_playlist_AddListener(playlist, &cbs, &ctx, false);
    assert(listener);

    /* go to an item in the middle, with incorrect index_hint */
    libvlc_playlist_item_t *item = libvlc_playlist_Get(playlist, 4);
    ret = libvlc_playlist_RequestGoTo(playlist, item, 7); /* wrong index hint */
    assert(ret == 0);

    assert(libvlc_playlist_GetCurrentIndex(playlist) == 4);
    assert(libvlc_playlist_HasPrev(playlist));
    assert(libvlc_playlist_HasNext(playlist));

    assert(ctx.vec_current_index_changed.size == 1);
    assert(ctx.vec_current_index_changed.data[0].current == 4);

    assert(ctx.vec_has_prev_changed.size == 1);
    assert(ctx.vec_has_prev_changed.data[0].has_prev);

    assert(ctx.vec_has_next_changed.size == 0);

    libvlc_playlist_Unlock(playlist);

    callback_ctx_destroy(&ctx);
    libvlc_playlist_RemoveListener(playlist, listener);
    ReleaseMedias(media, 10);
    libvlc_playlist_Delete(playlist);

    libvlc_release(libvlc);
}

/* this only tests that the randomizer is correctly managed by the playlist,
 * for further tests on randomization properties, see randomizer tests. */
static void
test_random(void)
{
    libvlc_instance_t *libvlc = CreateLibvlc();
    assert(libvlc);

    libvlc_playlist_t *playlist = libvlc_playlist_New(libvlc);
    assert(playlist);

    libvlc_playlist_Lock(playlist);

    libvlc_media_t *media[6];
    CreateDummyMedias(libvlc, media, 6);

    /* initial playlist with 5 items (1 is not added immediately) */
    int ret = libvlc_playlist_Append(playlist, media, 5);
    assert(ret == 0);

    struct libvlc_playlist_callbacks cbs = {
        .on_current_index_changed = callback_on_current_index_changed,
        .on_has_prev_changed = callback_on_has_prev_changed,
        .on_has_next_changed = callback_on_has_next_changed,
    };

    struct callback_ctx ctx = CALLBACK_CTX_INITIALIZER;
    libvlc_playlist_listener_id *listener =
            libvlc_playlist_AddListener(playlist, &cbs, &ctx, false);
    assert(listener);

    assert(!libvlc_playlist_HasPrev(playlist));
    assert(libvlc_playlist_HasNext(playlist));

    for (int i = 0; i < 3; ++i)
    {
        assert(libvlc_playlist_HasNext(playlist));
        ret = libvlc_playlist_Next(playlist);
        assert(ret == 0);
    }

    assert(libvlc_playlist_HasPrev(playlist));
    libvlc_playlist_SetPlaybackOrder(playlist, LIBVLC_PLAYLIST_PLAYBACK_ORDER_RANDOM);

    /* in random order, previous uses the history of randomly selected items */
    assert(!libvlc_playlist_HasPrev(playlist));

    bool selected[5] = {};
    for (int i = 0; i < 5; ++i)
    {
        assert(libvlc_playlist_HasNext(playlist));
        ret = libvlc_playlist_Next(playlist);
        assert(ret == 0);
        ssize_t index = libvlc_playlist_GetCurrentIndex(playlist);
        assert(index != -1);
        assert(!selected[index]); /* not selected twice */
        selected[index] = true;
    }

    assert(!libvlc_playlist_HasNext(playlist));

    /* add a new item, it must be taken into account */
    ret = libvlc_playlist_AppendOne(playlist, media[5]);
    assert(ret == 0);
    assert(libvlc_playlist_HasNext(playlist));

    ret = libvlc_playlist_Next(playlist);
    assert(ret == 0);

    assert(libvlc_playlist_GetCurrentIndex(playlist) == 5);
    assert(!libvlc_playlist_HasNext(playlist));

    libvlc_playlist_RemoveOne(playlist, 5);

    /* enable repeat */
    libvlc_playlist_SetPlaybackRepeat(playlist, LIBVLC_PLAYLIST_PLAYBACK_REPEAT_ALL);

    /* now there are more items */
    assert(libvlc_playlist_HasNext(playlist));

    /* once again */
    memset(selected, 0, sizeof(selected));
    for (int i = 0; i < 5; ++i)
    {
        assert(libvlc_playlist_HasNext(playlist));
        ret = libvlc_playlist_Next(playlist);
        assert(ret == 0);
        ssize_t index = libvlc_playlist_GetCurrentIndex(playlist);
        assert(index != -1);
        assert(!selected[index]); /* not selected twice */
        selected[index] = true;
    }

    /* there are always more items */
    assert(libvlc_playlist_HasNext(playlist));

    /* move to the middle of the random array */
    for (int i = 0; i < 3; ++i)
    {
        assert(libvlc_playlist_HasNext(playlist));
        ret = libvlc_playlist_Next(playlist);
        assert(ret == 0);
    }

    memset(selected, 0, sizeof(selected));
    int actual[5]; /* store the selected items (by their index) */

    ssize_t current = libvlc_playlist_GetCurrentIndex(playlist);
    assert(current != -1);
    actual[4] = current;

    for (int i = 3; i >= 0; --i)
    {
        assert(libvlc_playlist_HasPrev(playlist));
        ret = libvlc_playlist_Prev(playlist);
        assert(ret == 0);
        ssize_t index = libvlc_playlist_GetCurrentIndex(playlist);
        assert(index != -1);
        actual[i] = index;
        assert(!selected[index]); /* not selected twice */
        selected[index] = true;
    }

    /* no more previous, the history may only contain each item once */
    assert(!libvlc_playlist_HasPrev(playlist));

    /* we should get the same items in the reverse order going forward */
    for (int i = 1; i < 5; ++i)
    {
        assert(libvlc_playlist_HasNext(playlist));
        ret = libvlc_playlist_Next(playlist);
        assert(ret == 0);
        ssize_t index = libvlc_playlist_GetCurrentIndex(playlist);
        assert(index != -1);
        assert(index == actual[i]);
    }

    /* there are always more items */
    assert(libvlc_playlist_HasNext(playlist));

    libvlc_playlist_Unlock(playlist);

    callback_ctx_destroy(&ctx);
    libvlc_playlist_RemoveListener(playlist, listener);
    ReleaseMedias(media, 6);
    libvlc_playlist_Delete(playlist);

    libvlc_release(libvlc);
}

static void
test_shuffle(void)
{
    libvlc_instance_t *libvlc = CreateLibvlc();
    assert(libvlc);

    libvlc_playlist_t *playlist = libvlc_playlist_New(libvlc);
    assert(playlist);

    libvlc_playlist_Lock(playlist);

    libvlc_media_t *media[10];
    CreateDummyMedias(libvlc, media, 10);

    /* initial playlist with 10 items */
    int ret = libvlc_playlist_Append(playlist, media, 10);
    assert(ret == 0);

    struct libvlc_playlist_callbacks cbs = {
        .on_items_reset = callback_on_items_reset,
        .on_current_index_changed = callback_on_current_index_changed,
        .on_has_prev_changed = callback_on_has_prev_changed,
        .on_has_next_changed = callback_on_has_next_changed,
    };

    ret = libvlc_playlist_GoTo(playlist, 4);
    assert(ret == 0);

    struct callback_ctx ctx = CALLBACK_CTX_INITIALIZER;
    libvlc_playlist_listener_id *listener =
            libvlc_playlist_AddListener(playlist, &cbs, &ctx, false);
    assert(listener);

    ret = libvlc_playlist_Shuffle(playlist);
    assert(ret == 0);

    ssize_t index = libvlc_playlist_IndexOfMedia(playlist, media[4]);
    assert(index != -1);
    assert(index == libvlc_playlist_GetCurrentIndex(playlist));

    assert(ctx.vec_items_reset.size == 1);
    assert(ctx.vec_items_reset.data[0].count == 10);
    assert(ctx.vec_items_reset.data[0].state.playlist_size == 10);
    assert(ctx.vec_items_reset.data[0].state.current == index);
    assert(ctx.vec_items_reset.data[0].state.has_prev == (index > 0));
    assert(ctx.vec_items_reset.data[0].state.has_next == (index < 9));

    if (index == 4)
        assert(ctx.vec_current_index_changed.size == 0);
    else
    {
        assert(ctx.vec_current_index_changed.size == 1);
        assert(ctx.vec_current_index_changed.data[0].current == index);
    }

    if (index == 0)
    {
        assert(!libvlc_playlist_HasPrev(playlist));
        assert(ctx.vec_has_prev_changed.size == 1);
        assert(!ctx.vec_has_prev_changed.data[0].has_prev);
    }
    else
    {
        assert(libvlc_playlist_HasPrev(playlist));
        assert(ctx.vec_has_prev_changed.size == 0);
    }

    if (index == 9)
    {
        assert(!libvlc_playlist_HasNext(playlist));
        assert(ctx.vec_has_next_changed.size == 1);
        assert(!ctx.vec_has_next_changed.data[0].has_next);
    }
    else
    {
        assert(libvlc_playlist_HasNext(playlist));
        assert(ctx.vec_has_next_changed.size == 0);
    }

    libvlc_playlist_Unlock(playlist);

    callback_ctx_destroy(&ctx);
    libvlc_playlist_RemoveListener(playlist, listener);
    ReleaseMedias(media, 10);
    libvlc_playlist_Delete(playlist);

    libvlc_release(libvlc);
}

static void
test_sort(void)
{
    libvlc_instance_t *libvlc = CreateLibvlc();
    assert(libvlc);

    libvlc_playlist_t *playlist = libvlc_playlist_New(libvlc);
    assert(playlist);

    libvlc_playlist_Lock(playlist);

    libvlc_media_t *media[10];

    media[0] = CreateDummyMedia(libvlc, 4);
    media[0]->p_input_item->i_duration = 42;

    media[1] = CreateDummyMedia(libvlc, 1);
    media[1]->p_input_item->i_duration = 5;

    media[2] = CreateDummyMedia(libvlc, 6);
    media[2]->p_input_item->i_duration = 100;

    media[3] = CreateDummyMedia(libvlc, 2);
    media[3]->p_input_item->i_duration = 1;

    media[4] = CreateDummyMedia(libvlc, 1);
    media[4]->p_input_item->i_duration = 8;

    media[5] = CreateDummyMedia(libvlc, 4);
    media[5]->p_input_item->i_duration = 23;

    media[6] = CreateDummyMedia(libvlc, 3);
    media[6]->p_input_item->i_duration = 60;

    media[7] = CreateDummyMedia(libvlc, 3);
    media[7]->p_input_item->i_duration = 40;

    media[8] = CreateDummyMedia(libvlc, 0);
    media[8]->p_input_item->i_duration = 42;

    media[9] = CreateDummyMedia(libvlc, 5);
    media[9]->p_input_item->i_duration = 42;

    /* initial playlist with 10 items */
    int ret = libvlc_playlist_Append(playlist, media, 10);
    assert(ret == 0);

    struct libvlc_playlist_callbacks cbs = {
        .on_items_reset = callback_on_items_reset,
        .on_current_index_changed = callback_on_current_index_changed,
        .on_has_prev_changed = callback_on_has_prev_changed,
        .on_has_next_changed = callback_on_has_next_changed,
    };

    ret = libvlc_playlist_GoTo(playlist, 0);
    assert(ret == 0);

    struct callback_ctx ctx = CALLBACK_CTX_INITIALIZER;
    libvlc_playlist_listener_id *listener =
            libvlc_playlist_AddListener(playlist, &cbs, &ctx, false);
    assert(listener);

    struct libvlc_playlist_sort_criterion criteria1[] = {
        { LIBVLC_PLAYLIST_SORT_KEY_TITLE, LIBVLC_PLAYLIST_SORT_ORDER_ASCENDING },
        { LIBVLC_PLAYLIST_SORT_KEY_DURATION, LIBVLC_PLAYLIST_SORT_ORDER_ASCENDING },
    };
    libvlc_playlist_Sort(playlist, criteria1, 2);

    EXPECT_AT(0, 8);
    EXPECT_AT(1, 1);
    EXPECT_AT(2, 4);
    EXPECT_AT(3, 3);
    EXPECT_AT(4, 7);
    EXPECT_AT(5, 6);
    EXPECT_AT(6, 5);
    EXPECT_AT(7, 0);
    EXPECT_AT(8, 9);
    EXPECT_AT(9, 2);

    ssize_t index = libvlc_playlist_IndexOfMedia(playlist, media[0]);
    assert(index == 7);
    assert(libvlc_playlist_GetCurrentIndex(playlist) == 7);

    assert(ctx.vec_items_reset.size == 1);
    assert(ctx.vec_items_reset.data[0].count == 10);
    assert(ctx.vec_items_reset.data[0].state.playlist_size == 10);
    assert(ctx.vec_items_reset.data[0].state.current == 7);
    assert(ctx.vec_items_reset.data[0].state.has_prev);
    assert(ctx.vec_items_reset.data[0].state.has_next);

    assert(ctx.vec_current_index_changed.size == 1);
    assert(ctx.vec_current_index_changed.data[0].current == 7);

    assert(ctx.vec_has_prev_changed.size == 1);
    assert(ctx.vec_has_prev_changed.data[0].has_prev);

    assert(ctx.vec_has_next_changed.size == 0);

    callback_ctx_reset(&ctx);

    struct libvlc_playlist_sort_criterion criteria2[] = {
        { LIBVLC_PLAYLIST_SORT_KEY_DURATION, LIBVLC_PLAYLIST_SORT_ORDER_DESCENDING },
        { LIBVLC_PLAYLIST_SORT_KEY_TITLE, LIBVLC_PLAYLIST_SORT_ORDER_ASCENDING },
    };

    libvlc_playlist_Sort(playlist, criteria2, 2);

    EXPECT_AT(0, 2);
    EXPECT_AT(1, 6);
    EXPECT_AT(2, 8);
    EXPECT_AT(3, 0);
    EXPECT_AT(4, 9);
    EXPECT_AT(5, 7);
    EXPECT_AT(6, 5);
    EXPECT_AT(7, 4);
    EXPECT_AT(8, 1);
    EXPECT_AT(9, 3);

    assert(ctx.vec_items_reset.size == 1);
    assert(ctx.vec_items_reset.data[0].count == 10);
    assert(ctx.vec_items_reset.data[0].state.playlist_size == 10);

    libvlc_playlist_Unlock(playlist);

    callback_ctx_destroy(&ctx);
    libvlc_playlist_RemoveListener(playlist, listener);
    ReleaseMedias(media, 10);
    libvlc_playlist_Delete(playlist);

    libvlc_release(libvlc);
}

#undef EXPECT_AT

int main(void)
{
    test_init();

    test_append();
    test_insert();
    test_move();
    test_remove();
    test_clear();
    //test_expand_item();
    test_items_added_callbacks();
    test_items_moved_callbacks();
    test_items_removed_callbacks();
    test_items_reset_callbacks();
    test_playback_repeat_changed_callbacks();
    test_playback_order_changed_callbacks();
    test_callbacks_on_add_listener();
    test_index_of();
    test_prev();
    test_next();
    test_goto();
    test_request_insert();
    test_request_remove_with_matching_hint();
    test_request_remove_without_hint();
    test_request_remove_adapt();
    test_request_move_with_matching_hint();
    test_request_move_without_hint();
    test_request_move_adapt();
    test_request_goto_with_matching_hint();
    test_request_goto_without_hint();
    test_request_goto_adapt();
    test_random();
    test_shuffle();
    test_sort();
    return 0;
}
