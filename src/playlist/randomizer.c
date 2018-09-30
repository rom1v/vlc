/*****************************************************************************
 * randomizer.c : Randomizer
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

#ifdef TEST_RANDOMIZER
# undef NDEBUG
#endif

#include <assert.h>

#include <vlc_common.h>
#include <vlc_rand.h>
#include "randomizer.h"

/* On auto-reshuffle, avoid to select the same item before at least
 * NOT_SAME_BEFORE other items have been selected (between the end of the
 * previous shuffle and the start of the new shuffle). */
#define NOT_SAME_BEFORE 1

void
randomizer_Init(struct randomizer *r)
{
    vlc_vector_init(&r->items);

    /* initialize separately instead of using vlc_lrand48() to avoid locking
     * the mutex for every random number generation */
    vlc_rand_bytes(r->xsubi, sizeof(r->xsubi));

    r->loop = false;
    r->head = 0;
    r->next = 0;
    r->history = 0;
}

void
randomizer_Destroy(struct randomizer *r)
{
    vlc_vector_clear(&r->items);
}

void
randomizer_SetLoop(struct randomizer *r, bool loop)
{
    r->loop = loop;
}

static inline ssize_t
randomizer_IndexOf(struct randomizer *r, const vlc_playlist_item_t *item)
{
    ssize_t index;
    vlc_vector_index_of(&r->items, item, &index);
    return index;
}

bool
randomizer_Count(struct randomizer *r)
{
    return r->items.size;
}

void
randomizer_Reshuffle(struct randomizer *r)
{
    /* yeah, it's that simple */
    r->head = 0;
    r->next = 0;
    r->history = 0;
}

static inline void
swap_items(struct randomizer *r, int i, int j)
{
    vlc_playlist_item_t *item = r->items.data[i];
    r->items.data[i] = r->items.data[j];
    r->items.data[j] = item;
}

static inline void
randomizer_DetermineOne_(struct randomizer *r, size_t avoid_last_n)
{
    assert(r->head < r->items.size);
    assert(r->items.size - r->head > avoid_last_n);
    size_t range_len = r->items.size - r->head - avoid_last_n;
    size_t selected = r->head + (nrand48(r->xsubi) % range_len);
    swap_items(r, r->head, selected);

    r->head++;
    if (r->head == r->history)
        r->history = (r->history + 1) % r->items.size;
}

static inline void
randomizer_DetermineOne(struct randomizer *r)
{
    randomizer_DetermineOne_(r, 0);
}

/* An autoreshuffle occurs if loop is enabled, once all item have been played.
 * In that case, we reshuffle and determine first items so that the same item
 * may not be selected before NOT_SAME_BEFORE selections. */
static void
randomizer_AutoReshuffle(struct randomizer *r)
{
    randomizer_Reshuffle(r);
    size_t avoid_last_n = NOT_SAME_BEFORE;
    if (avoid_last_n > r->items.size - 1)
        /* cannot ignore all */
        avoid_last_n = r->items.size - 1;
    while (avoid_last_n)
        randomizer_DetermineOne_(r, avoid_last_n--);
}

bool
randomizer_HasPrev(struct randomizer *r)
{
    return (r->next + r->items.size - r->history) % r->items.size > 1;
}

bool
randomizer_HasNext(struct randomizer *r)
{
    return r->loop || r->next < r->items.size;
}

vlc_playlist_item_t *
randomizer_PeekPrev(struct randomizer *r)
{
    assert(randomizer_HasPrev(r));
    size_t index = (r->next + r->items.size - 2) % r->items.size;
    return r->items.data[index];
}

vlc_playlist_item_t *
randomizer_PeekNext(struct randomizer *r)
{
    assert(randomizer_HasNext(r));

    if (r->next == r->items.size && !r->history)
    {
        assert(r->loop);
        randomizer_AutoReshuffle(r);
    }

    if (r->next == r->head)
        /* execute 1 step of the Fisher–Yates shuffle */
        randomizer_DetermineOne(r);

    return r->items.data[r->next];
}

vlc_playlist_item_t *
randomizer_Prev(struct randomizer *r)
{
    assert(randomizer_HasPrev(r));
    vlc_playlist_item_t *item = randomizer_PeekPrev(r);
    r->next = (r->next + r->items.size - 1) % r->items.size;
    return item;
}

vlc_playlist_item_t *
randomizer_Next(struct randomizer *r)
{
    assert(randomizer_HasNext(r));
    vlc_playlist_item_t *item = randomizer_PeekNext(r);
    r->next++;
    if (r->next == r->items.size && r->history)
        r->next = 0;
    return item;
}

bool
randomizer_Add(struct randomizer *r, vlc_playlist_item_t *items[], size_t count)
{
    if (r->history)
    {
        if (!vlc_vector_insert_all(&r->items, r->history, items, count))
            return false;
        /* the insertion shifted history (and possibly next) */
        if (r->next > r->history)
            r->next += count;
        r->history += count;
        return true;
    }

    return vlc_vector_push_all(&r->items, items, count);
}

static void
randomizer_SelectIndex(struct randomizer *r, size_t index)
{
    vlc_playlist_item_t *selected = r->items.data[index];
    if (r->history && index >= r->history)
    {
        if (index > r->history)
        {
            memmove(&r->items.data[r->history + 1],
                    &r->items.data[r->history],
                    (index - r->history) * sizeof(selected));
            index = r->history;
        }
        r->history = (r->history + 1) % r->items.size;
    }

    if (index >= r->head)
    {
        r->items.data[index] = r->items.data[r->head];
        r->items.data[r->head] = selected;
        r->head++;
    }
    else if (index < r->items.size - 1)
    {
        memmove(&r->items.data[index],
                &r->items.data[index + 1],
                (r->head - index - 1) * sizeof(selected));
        r->items.data[r->head - 1] = selected;
    }

    r->next = r->head;
}

void
randomizer_Select(struct randomizer *r, const vlc_playlist_item_t *item)
{
    ssize_t index = randomizer_IndexOf(r, item);
    assert(index != -1); /* item must exist */
    randomizer_SelectIndex(r, (size_t) index);
}

static void
randomizer_RemoveAt(struct randomizer *r, size_t index)
{
    /*
     * 0          head                                history   next  size
     * |-----------|...................................|---------|-----|
     * |<--------->|                                   |<------------->|
     *    ordered                                           ordered
     */
    if (index < r->head)
    {
        /* item was selected, keep the selected part ordered */
        memmove(&r->items.data[index],
                &r->items.data[index + 1],
                (r->head - index - 1) * sizeof(*r->items.data));
        r->head--;
        index = r->head; /* the new index to remove */
    }

    if (r->history)
    {
        if (index < r->history)
        {
            size_t swap = (r->history + r->items.size - 1) % r->items.size;
            r->items.data[index] = r->items.data[swap];
            index = swap;
        }

        memmove(&r->items.data[index],
                &r->items.data[index + 1],
                (r->items.size - index - 1) * sizeof(*r->items.data));

        if (r->history > index)
            r->history--;
        else if (r->history == r->items.size)
            r->history = 0;

        if (r->next > index)
            r->next--;
        else if (r->next == r->items.size)
            r->next = 0;
    }
}

static void
randomizer_RemoveOne(struct randomizer *r, const vlc_playlist_item_t *item)
{
    ssize_t index = randomizer_IndexOf(r, item);
    assert(index >= 0); /* item must exist */
    randomizer_RemoveAt(r, index);
}

void
randomizer_Remove(struct randomizer *r, vlc_playlist_item_t *const items[],
                  size_t count)
{
    for (size_t i = 0; i < count; ++i)
        randomizer_RemoveOne(r, items[i]);

    vlc_vector_autoshrink(&r->items);
}

void
randomizer_Clear(struct randomizer *r)
{
    vlc_vector_clear(&r->items);
    r->head = 0;
    r->next = 0;
    r->history = 0;
}

#ifdef TEST_RANDOMIZER

/* fake structure to simplify tests */
struct vlc_playlist_item {
    size_t index;
};

static void
ArrayInit(vlc_playlist_item_t *array[], size_t len)
{
    for (size_t i = 0; i < len; ++i)
    {
        array[i] = malloc(sizeof(*array[i]));
        assert(array[i]);
        array[i]->index = i;
    }
}

static void
ArrayDestroy(vlc_playlist_item_t *array[], size_t len)
{
    for (size_t i = 0; i < len; ++i)
        free(array[i]);
}

static void
test_all_items_selected_exactly_once(void)
{
    struct randomizer randomizer;
    randomizer_Init(&randomizer);

    #define SIZE 100
    vlc_playlist_item_t *items[SIZE];
    ArrayInit(items, SIZE);

    bool ok = randomizer_Add(&randomizer, items, SIZE);
    assert(ok);

    bool selected[SIZE] = {0};
    for (int i = 0; i < SIZE; ++i)
    {
        assert(randomizer_HasNext(&randomizer));
        vlc_playlist_item_t *item = randomizer_Next(&randomizer);
        assert(item);
        assert(!selected[item->index]); /* never selected twice */
        selected[item->index] = true;
    }

    assert(!randomizer_HasNext(&randomizer)); /* no more items */

    for (int i = 0; i < SIZE; ++i)
        assert(selected[i]); /* all selected */

    ArrayDestroy(items, SIZE);
    randomizer_Destroy(&randomizer);
    #undef SIZE
}

static void
test_all_items_selected_exactly_once_per_cycle(void)
{
    struct randomizer randomizer;
    randomizer_Init(&randomizer);
    randomizer_SetLoop(&randomizer, true);

    #define SIZE 100
    vlc_playlist_item_t *items[SIZE];
    ArrayInit(items, SIZE);

    bool ok = randomizer_Add(&randomizer, items, SIZE);
    assert(ok);

    for (int cycle = 0; cycle < 4; ++cycle)
    {
        bool selected[SIZE] = {0};
        for (int i = 0; i < SIZE; ++i)
        {
            assert(randomizer_HasNext(&randomizer));
            vlc_playlist_item_t *item = randomizer_Next(&randomizer);
            assert(item);
            assert(!selected[item->index]); /* never selected twice */
            selected[item->index] = true;
        }

        assert(randomizer_HasNext(&randomizer)); /* still has items in loop */

        for (int i = 0; i < SIZE; ++i)
            assert(selected[i]); /* all selected */
    }

    ArrayDestroy(items, SIZE);
    randomizer_Destroy(&randomizer);
    #undef SIZE
}

static void
test_all_items_selected_exactly_once_with_additions_and_removals(void)
{
    // TODO
}

static void
test_force_select_new_item(void)
{
    struct randomizer randomizer;
    randomizer_Init(&randomizer);

    #define SIZE 100
    vlc_playlist_item_t *items[SIZE];
    ArrayInit(items, SIZE);

    bool ok = randomizer_Add(&randomizer, items, SIZE);
    assert(ok);

    bool selected[SIZE] = {0};
    for (int i = 0; i < SIZE; ++i)
    {
        vlc_playlist_item_t *item;
        if (i != 50)
        {
            assert(randomizer_HasNext(&randomizer));
            item = randomizer_Next(&randomizer);
        }
        else
        {
            /* force the selection of a new item not already selected */
            item = randomizer.items.data[62];
            randomizer_Select(&randomizer, item);
            /* the item should now be the last selected one */
            assert(randomizer.items.data[randomizer.next - 1] == item);
        }
        assert(item);
        assert(!selected[item->index]); /* never selected twice */
        selected[item->index] = true;
    }

    assert(!randomizer_HasNext(&randomizer)); /* no more items */

    for (int i = 0; i < SIZE; ++i)
        assert(selected[i]); /* all selected */

    ArrayDestroy(items, SIZE);
    randomizer_Destroy(&randomizer);
}

static void
test_force_select_item_already_selected(void)
{
    struct randomizer randomizer;
    randomizer_Init(&randomizer);

    #define SIZE 100
    vlc_playlist_item_t *items[SIZE];
    ArrayInit(items, SIZE);

    bool ok = randomizer_Add(&randomizer, items, SIZE);
    assert(ok);

    bool selected[SIZE] = {0};
    /* we need an additional loop cycle, since we select the same item twice */
    for (int i = 0; i < SIZE + 1; ++i)
    {
        vlc_playlist_item_t *item;
        if (i != 50)
        {
            assert(randomizer_HasNext(&randomizer));
            item = randomizer_Next(&randomizer);
        }
        else
        {
            /* force the selection of an item already selected */
            item = randomizer.items.data[42];
            randomizer_Select(&randomizer, item);
            /* the item should now be the last selected one */
            assert(randomizer.items.data[randomizer.next - 1] == item);
        }
        assert(item);
        assert((i != 50) ^ selected[item->index]); /* never selected twice, except for item 50 */
        selected[item->index] = true;
    }

    assert(!randomizer_HasNext(&randomizer)); /* no more items */

    for (int i = 0; i < SIZE; ++i)
        assert(selected[i]); /* all selected */

    ArrayDestroy(items, SIZE);
    randomizer_Destroy(&randomizer);
}

static void
test_previous_item()
{
    // TODO
}

int main(void)
{
    test_all_items_selected_exactly_once();
    test_all_items_selected_exactly_once_per_cycle();
    test_all_items_selected_exactly_once_with_additions_and_removals();
    test_force_select_new_item();
    test_force_select_item_already_selected();
    test_previous_item();
}

#endif
