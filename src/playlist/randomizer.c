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

#ifdef TEST
# undef NDEBUG
#endif

#include <assert.h>

#include <vlc_common.h>
#include <vlc_rand.h>
#include "randomizer.h"

void randomizer_Init(struct randomizer *randomizer)
{
    ARRAY_INIT(randomizer->items);
    randomizer->head = 0;
    randomizer->current = 0;
}

void randomizer_Destroy(struct randomizer *randomizer)
{
    ARRAY_RESET(randomizer->items);
}

void randomizer_Reshuffle(struct randomizer *randomizer)
{
    /* yeah, it's that simple */
    randomizer->head = 0;
    randomizer->current = 0;
}

/* XXX: move to vlc_arrays.h? */
static void array_swap(items_array_t *array, int i, int j)
{
    void *item = array->p_elems[i];
    array->p_elems[i] = array->p_elems[j];
    array->p_elems[j] = item;
}

bool randomizer_HasPrev(struct randomizer *randomizer)
{
    return randomizer->current > 0;
}

bool randomizer_HasNext(struct randomizer *randomizer)
{
    return randomizer->head < randomizer->items.i_size;
}

void *randomizer_Prev(struct randomizer *randomizer)
{
    assert(randomizer_HasPrev(randomizer));
    return randomizer->items.p_elems[--randomizer->current];
}

void *randomizer_Next(struct randomizer *randomizer)
{
    assert(randomizer_HasNext(randomizer));
    int size = randomizer->items.i_size;
    assert(randomizer->head <= size);
    assert(randomizer->current <= randomizer->head);

    if (randomizer->current == randomizer->head)
    {
        /* execute 1 step of the Fisher–Yates shuffle */
        int head = randomizer->head;
        int remaining = size - head;
        assert(remaining > 0);

        int selected = head + (vlc_lrand48() % remaining);
        assert(selected >= head && selected < size);

        array_swap(&randomizer->items, head, selected);
        randomizer->head++;
    }

    return randomizer->items.p_elems[randomizer->current++];
}

bool randomizer_Add(struct randomizer *randomizer, void *item)
{
    ARRAY_APPEND(randomizer->items, item);
    /* for now, ARRAY_APPEND() aborts on allocation failure */
    return true;
}

void randomizer_Select(struct randomizer *randomizer, const void *item)
{
    ssize_t idx;
    ARRAY_FIND(randomizer->items, item, idx);
    assert(idx >= 0); /* item must exist */
    if (idx >= randomizer->head)
    {
        array_swap(&randomizer->items, idx, randomizer->head);
        randomizer->head++;
    } else {
        void *tmp = randomizer->items.p_elems[idx];
        memmove(&randomizer->items.p_elems[idx],
                &randomizer->items.p_elems[idx + 1],
                (randomizer->head - idx - 1) * sizeof(void *));
        randomizer->items.p_elems[randomizer->head - 1] = tmp;
    }
    randomizer->current = randomizer->head;
}

bool randomizer_Remove(struct randomizer *randomizer, const void *item)
{
    ssize_t idx;
    ARRAY_FIND(randomizer->items, item, idx);
    if (idx == -1)
        return false;

    if (idx >= randomizer->head)
        /* item was not selected */
        ARRAY_SWAP_REMOVE(randomizer->items, idx);
    else
    {
        /* item was selected, keep the selected part ordered */
        memmove(&randomizer->items.p_elems[idx],
                &randomizer->items.p_elems[idx + 1],
                (randomizer->head - idx - 1) * sizeof (void *));
        ARRAY_SWAP_REMOVE(randomizer->items, randomizer->head);
        randomizer->head--;
    }

    _ARRAY_SHRINK(randomizer->items);
    return true;
}

void randomizer_Clear(struct randomizer *randomizer)
{
    ARRAY_RESET(randomizer->items);
}

#ifdef TEST

static void test_all_items_selected_exactly_once(void)
{
    struct randomizer randomizer;
    randomizer_Init(&randomizer);

    #define SIZE 100
    char data[SIZE];

    for (int i = 0; i < SIZE; ++i)
        /* our items are pointers to some address in the data array */
        randomizer_Add(&randomizer, &data[i]);

    bool selected[SIZE] = {0};
    for (int i = 0; i < SIZE; ++i)
    {
        assert(randomizer_HasNext(&randomizer));
        char *item = (char *) randomizer_Next(&randomizer);
        assert(item);
        int idx = (int) (item - data);
        assert(idx >= 0 && idx < SIZE);
        assert(!selected[idx]); /* never selected twice */
        selected[idx] = true;
    }

    assert(!randomizer_HasNext(&randomizer)); /* no more items */

    for (int i = 0; i < SIZE; ++i)
        assert(selected[i]); /* all selected */

    randomizer_Destroy(&randomizer);
    #undef SIZE
}

static void test_all_items_selected_exactly_once_with_additions_and_removals(void)
{
    // TODO
}

static void test_force_select_new_item(void)
{
    struct randomizer randomizer;
    randomizer_Init(&randomizer);

    #define SIZE 100
    char data[SIZE];

    for (int i = 0; i < SIZE; ++i)
        randomizer_Add(&randomizer, &data[i]);

    bool selected[SIZE] = {0};
    for (int i = 0; i < SIZE; ++i)
    {
        char *item;
        if (i != 50)
        {
            assert(randomizer_HasNext(&randomizer));
            item = randomizer_Next(&randomizer);
        }
        else
        {
            /* force the selection of a new item not already selected */
            item = randomizer.items.p_elems[62];
            randomizer_Select(&randomizer, item);
            /* the item should now be the last selected one */
            assert(randomizer.items.p_elems[randomizer.current - 1] == item);
        }
        assert(item);
        int idx = (int) (item - data);
        assert(idx >= 0 && idx < SIZE);
        assert(!selected[idx]); /* never selected twice */
        selected[idx] = true;
    }

    assert(!randomizer_HasNext(&randomizer)); /* no more items */

    for (int i = 0; i < SIZE; ++i)
        assert(selected[i]); /* all selected */

    randomizer_Destroy(&randomizer);
}

static void test_force_select_item_already_selected(void)
{
    struct randomizer randomizer;
    randomizer_Init(&randomizer);

    #define SIZE 100
    char data[SIZE];

    for (int i = 0; i < SIZE; ++i)
        randomizer_Add(&randomizer, &data[i]);

    bool selected[SIZE] = {0};
    /* we need an additional loop cycle, since we select the same item twice */
    for (int i = 0; i < SIZE + 1; ++i)
    {
        char *item;
        if (i != 50)
        {
            assert(randomizer_HasNext(&randomizer));
            item = randomizer_Next(&randomizer);
        }
        else
        {
            /* force the selection of an item already selected */
            item = randomizer.items.p_elems[42];
            randomizer_Select(&randomizer, item);
            /* the item should now be the last selected one */
            assert(randomizer.items.p_elems[randomizer.current - 1] == item);
        }
        assert(item);
        int idx = (int) (item - data);
        assert(idx >= 0 && idx < SIZE);

        assert((i != 50) ^ selected[idx]); /* never selected twice, except for item 50 */
        selected[idx] = true;
    }

    assert(!randomizer_HasNext(&randomizer)); /* no more items */

    for (int i = 0; i < SIZE; ++i)
        assert(selected[i]); /* all selected */

    randomizer_Destroy(&randomizer);
}

static void test_previous_item()
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
