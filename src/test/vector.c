/*****************************************************************************
 * vector.h : Test for vlc_vector macros
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

#undef NDEBUG

#include <assert.h>

#include <vlc_common.h>
#include <vlc_vector.h>

static void test_vector(void)
{
    struct VLC_VECTOR(int) vec;
    vlc_vector_init(&vec);

    bool ok;

    ok = vlc_vector_append(&vec, 42);
    assert(ok);
    assert(vec.data[0] == 42);
    assert(vec.size == 1);

    ok = vlc_vector_append(&vec, 37);
    assert(ok);
    assert(vec.size == 2);
    assert(vec.data[0] == 42);
    assert(vec.data[1] == 37);

    ok = vlc_vector_insert(&vec, 1, 100);
    assert(ok);
    assert(vec.size == 3);
    assert(vec.data[0] == 42);
    assert(vec.data[1] == 100);
    assert(vec.data[2] == 37);
}

typedef struct input_item_t input_item_t;
struct playlist {
    struct VLC_VECTOR(input_item_t *) items;
};

/* or, with a typedef: */
typedef struct VLC_VECTOR(input_item_t *) input_item_vector_t;
struct playlist2 {
    input_item_vector_t items;
};

static struct playlist *playlist_New(void)
{
    return malloc(sizeof(struct playlist));
}

static void demo_usage(void)
{
    struct playlist *playlist = playlist_New();
    vlc_vector_init(&playlist->items);
    input_item_t *item = NULL; // input_item_New(...)
    bool ok = vlc_vector_append(&playlist->items, item);
    assert(ok);
}

int main(void) {
    test_vector();
    demo_usage();
    return 0;
}
