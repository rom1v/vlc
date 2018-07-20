/*****************************************************************************
 * arrays.c : Test for VLC arrays
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
#include <vlc_arrays.h>

static void test_array_insert_remove(void)
{
    DECL_ARRAY(int) array;
    ARRAY_INIT(array);

    ARRAY_APPEND(array, 42);
    assert(array.i_size == 1);
    assert(ARRAY_VAL(array, 0) == 42);

    ARRAY_REMOVE(array, 0);
    assert(array.i_size == 0);

    ARRAY_APPEND(array, 43);
    ARRAY_APPEND(array, 44);
    ARRAY_APPEND(array, 45);
    ARRAY_REMOVE(array, 1);
    assert(array.i_size == 2);
    assert(ARRAY_VAL(array, 0) == 43);
    assert(ARRAY_VAL(array, 1) == 45);

    ARRAY_INSERT(array, 100, 1);
    assert(array.i_size == 3);
    assert(ARRAY_VAL(array, 0) == 43);
    assert(ARRAY_VAL(array, 1) == 100);
    assert(ARRAY_VAL(array, 2) == 45);

    ARRAY_RESET(array);
}

static void test_array_foreach(void)
{
    DECL_ARRAY(int) array;
    ARRAY_INIT(array);

    for (int i = 0; i < 10; ++i)
        ARRAY_APPEND(array, i);

    int count = 0;
    int item;
    FOREACH_ARRAY(item, array)
        assert(item == count);
        count++;
    FOREACH_END()
    assert(count == 10);

    ARRAY_RESET(array);
}

static void test_array_find(void)
{
    DECL_ARRAY(int) array;
    ARRAY_INIT(array);

    ARRAY_APPEND(array, 17);
    ARRAY_APPEND(array, 52);
    ARRAY_APPEND(array, 26);
    ARRAY_APPEND(array, 13);
    ARRAY_APPEND(array, 40);
    ARRAY_APPEND(array, 20);
    ARRAY_APPEND(array, 10);
    ARRAY_APPEND(array, 5);

    int index;

    ARRAY_FIND(array, 17, index);
    assert(index == 0);

    ARRAY_FIND(array, 52, index);
    assert(index == 1);

    ARRAY_FIND(array, 26, index);
    assert(index == 2);

    ARRAY_FIND(array, 13, index);
    assert(index == 3);

    ARRAY_FIND(array, 10, index);
    assert(index == 6);

    ARRAY_FIND(array, 5, index);
    assert(index == 7);

    ARRAY_FIND(array, 14, index);
    assert(index == -1);

    ARRAY_RESET(array);
}

static void test_array_bsearch(void)
{
    struct item {
        int value;
    };

    DECL_ARRAY(struct item) array;
    ARRAY_INIT(array);

    ARRAY_APPEND(array, (struct item) { 1 });
    ARRAY_APPEND(array, (struct item) { 2 });
    ARRAY_APPEND(array, (struct item) { 3 });
    ARRAY_APPEND(array, (struct item) { 5 });
    ARRAY_APPEND(array, (struct item) { 8 });
    ARRAY_APPEND(array, (struct item) { 13 });
    ARRAY_APPEND(array, (struct item) { 21 });

    int index;

    ARRAY_BSEARCH(array, .value, int, 1, index);
    assert(index == 0);

    ARRAY_BSEARCH(array, .value, int, 2, index);
    assert(index == 1);

    ARRAY_BSEARCH(array, .value, int, 3, index);
    assert(index == 2);

    ARRAY_BSEARCH(array, .value, int, 8, index);
    assert(index == 4);

    ARRAY_BSEARCH(array, .value, int, 21, index);
    assert(index == 6);

    ARRAY_BSEARCH(array, .value, int, 4, index);
    assert(index == -1);

    ARRAY_RESET(array);
}

#define ASSERT_SUCCESS(expr) \
    do { \
        int assert_success_result = (expr); \
        assert(assert_success_result == VLC_SUCCESS); \
    } while (0);

static void test_vlc_array_insert_remove(void)
{
    vlc_array_t array;
    vlc_array_init(&array);

    char data[32];

    ASSERT_SUCCESS(vlc_array_append(&array, &data[0]));
    assert(vlc_array_count(&array) == 1);
    assert(vlc_array_get(&array, 0) == &data[0]);

    vlc_array_remove(&array, 0);
    assert(vlc_array_count(&array) == 0);

    ASSERT_SUCCESS(vlc_array_append(&array, &data[1]));
    ASSERT_SUCCESS(vlc_array_append(&array, &data[2]));
    ASSERT_SUCCESS(vlc_array_append(&array, &data[3]));
    vlc_array_remove(&array, 1);
    assert(vlc_array_count(&array) == 2);
    assert(vlc_array_get(&array, 0) == &data[1]);
    assert(vlc_array_get(&array, 1) == &data[3]);

    vlc_array_insert(&array, &data[4], 1);
    assert(vlc_array_count(&array) == 3);
    assert(vlc_array_get(&array, 0) == &data[1]);
    assert(vlc_array_get(&array, 1) == &data[4]);
    assert(vlc_array_get(&array, 2) == &data[3]);

    vlc_array_clear(&array);
}

static void test_vlc_array_find(void)
{
    vlc_array_t array;
    vlc_array_init(&array);

    char data[10];

    for (int i = 0; i < 10; ++i)
        ASSERT_SUCCESS(vlc_array_append(&array, &data[i]));

    ssize_t idx;

    idx = vlc_array_find(&array, &data[0]);
    assert(idx == 0);

    idx = vlc_array_find(&array, &data[1]);
    assert(idx == 1);

    idx = vlc_array_find(&array, &data[4]);
    assert(idx == 4);

    idx = vlc_array_find(&array, &data[9]);
    assert(idx == 9);

    idx = vlc_array_find(&array, "some other pointer");
    assert(idx == -1);

    vlc_array_clear(&array);
}

int main(void)
{
    test_array_insert_remove();
    test_array_foreach();
    test_array_find();
    test_array_bsearch();

    test_vlc_array_insert_remove();
    test_vlc_array_find();
}
