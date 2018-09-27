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

void
randomizer_Init(struct randomizer *randomizer)
{
    vlc_vector_init(&randomizer->items);

    /* initialize separately instead of using vlc_lrand48() to avoid locking
     * the mutex for every random number generation */
    vlc_rand_bytes(randomizer->xsubi, sizeof(randomizer->xsubi));

    randomizer->head = 0;
    randomizer->next = 0;
}

void
randomizer_Destroy(struct randomizer *randomizer)
{
    vlc_vector_clear(&randomizer->items);
}

static inline ssize_t
randomizer_IndexOf(struct randomizer *randomizer, const vlc_playlist_item_t *item)
{
    ssize_t index;
    vlc_vector_index_of(&randomizer->items, item, &index);
    return index;
}

bool
randomizer_Count(struct randomizer *randomizer)
{
    return randomizer->items.size;
}

void
randomizer_Reshuffle(struct randomizer *randomizer)
{
    /* yeah, it's that simple */
    randomizer->head = 0;
    randomizer->next = 0;
}

static inline void
swap_items(struct randomizer *randomizer, int i, int j)
{
    vlc_playlist_item_t *item = randomizer->items.data[i];
    randomizer->items.data[i] = randomizer->items.data[j];
    randomizer->items.data[j] = item;
}

bool
randomizer_HasPrev(struct randomizer *randomizer)
{
    return randomizer->next > 0;
}

bool
randomizer_HasNext(struct randomizer *randomizer)
{
    return randomizer->head < randomizer->items.size;
}

vlc_playlist_item_t *
randomizer_PeekPrev(struct randomizer *randomizer)
{
    assert(randomizer_HasPrev(randomizer));
    return randomizer->items.data[randomizer->next - 1];
}

vlc_playlist_item_t *
randomizer_PeekNext(struct randomizer *randomizer)
{
    assert(randomizer_HasNext(randomizer));
    size_t size = randomizer->items.size;
    assert(randomizer->head <= size);
    assert(randomizer->next <= randomizer->head);

    if (randomizer->next == randomizer->head)
    {
        /* execute 1 step of the Fisher–Yates shuffle */
        size_t head = randomizer->head;
        size_t remaining = size - head;
        assert(remaining > 0);

        size_t selected = head + (nrand48(randomizer->xsubi) % remaining);
        assert(selected >= head && selected < size);

        swap_items(randomizer, head, selected);
        randomizer->head++;
    }

    return randomizer->items.data[randomizer->next];
}

vlc_playlist_item_t *
randomizer_Prev(struct randomizer *randomizer)
{
    assert(randomizer_HasPrev(randomizer));
    vlc_playlist_item_t *item = randomizer_PeekPrev(randomizer);
    randomizer->next--;
    return item;
}

vlc_playlist_item_t *
randomizer_Next(struct randomizer *randomizer)
{
    assert(randomizer_HasNext(randomizer));
    vlc_playlist_item_t *item = randomizer_PeekNext(randomizer);
    randomizer->next++;
    return item;
}

bool
randomizer_Add(struct randomizer *randomizer, vlc_playlist_item_t *items[],
               size_t count)
{
    return vlc_vector_push_all(&randomizer->items, items, count);
}

static void
randomizer_SelectIndex(struct randomizer *randomizer, size_t index)
{
    if (index >= randomizer->head)
    {
        swap_items(randomizer, index, randomizer->head);
        randomizer->head++;
    } else if (index < randomizer->items.size - 1) {
        vlc_playlist_item_t *tmp = randomizer->items.data[index];
        memmove(&randomizer->items.data[index],
                &randomizer->items.data[index + 1],
                (randomizer->head - index - 1) * sizeof(tmp));
        randomizer->items.data[randomizer->head - 1] = tmp;
    }
    randomizer->next = randomizer->head;
}

void
randomizer_Select(struct randomizer *randomizer,
                  const vlc_playlist_item_t *item)
{
    ssize_t index = randomizer_IndexOf(randomizer, item);
    assert(index >= 0); /* item must exist */
    randomizer_SelectIndex(randomizer, (size_t) index);
}

vlc_playlist_item_t *
randomizer_SelectRandom(struct randomizer *randomizer, size_t avoid_last_n)
{
    size_t size = randomizer->items.size;
    size_t next = randomizer->next;
    assert(size > 0);
    if (avoid_last_n > next) {
        avoid_last_n = next;
        if (next == size)
            /* we cannot avoid _all_ the items */
            avoid_last_n--;
    }
    assert(avoid_last_n < next);

    /*
     * 0                        next  head                size
     * |------------|____________|-----|...................|
     *               <---------->
     *               avoid_last_n
     */

    size_t selected = nrand48(randomizer->xsubi) % size - avoid_last_n;
    if (selected >= next - avoid_last_n)
        selected += avoid_last_n;

    vlc_playlist_item_t *item = randomizer->items.data[selected];
    randomizer_SelectIndex(randomizer, selected);
    return item;
}


static void
randomizer_RemoveAt(struct randomizer *randomizer, size_t index)
{
    if (index >= randomizer->head)
        /* item was not selected */
        vlc_vector_swap_remove(&randomizer->items, index);
    else
    {
        /* item was selected, keep the selected part ordered */
        memmove(&randomizer->items.data[index],
                &randomizer->items.data[index + 1],
                (randomizer->head - index - 1) * sizeof (void *));
        vlc_vector_swap_remove(&randomizer->items, randomizer->head);
        randomizer->head--;
    }
}

static void
randomizer_RemoveOne(struct randomizer *randomizer,
                     const vlc_playlist_item_t *item)
{
    ssize_t index = randomizer_IndexOf(randomizer, item);
    assert(index >= 0); /* item must exist */
    randomizer_RemoveAt(randomizer, index);
}

void
randomizer_Remove(struct randomizer *randomizer,
                  vlc_playlist_item_t *const items[], size_t count)
{
    for (size_t i = 0; i < count; ++i)
        randomizer_RemoveOne(randomizer, items[i]);

    vlc_vector_autoshrink(&randomizer->items);
}

void
randomizer_Clear(struct randomizer *randomizer)
{
    vlc_vector_clear(&randomizer->items);
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
    test_all_items_selected_exactly_once_with_additions_and_removals();
    test_force_select_new_item();
    test_force_select_item_already_selected();
    test_previous_item();
}

#endif
