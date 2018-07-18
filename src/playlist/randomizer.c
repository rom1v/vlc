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
    vlc_vector_init(&randomizer->items);
    randomizer->head = 0;
    randomizer->current = 0;
}

void randomizer_Destroy(struct randomizer *randomizer)
{
    vlc_vector_clear(&randomizer->items);
}

void randomizer_Reshuffle(struct randomizer *randomizer)
{
    /* yeah, it's that simple */
    randomizer->head = 0;
    randomizer->current = 0;
}

/* XXX: move to vlc_vector? */
static void swap_items(struct randomizer *randomizer, int i, int j)
{
    void *item = randomizer->items.data[i];
    randomizer->items.data[i] = randomizer->items.data[j];
    randomizer->items.data[j] = item;
}

bool randomizer_HasPrev(struct randomizer *randomizer)
{
    return randomizer->current > 0;
}

bool randomizer_HasNext(struct randomizer *randomizer)
{
    return randomizer->head < randomizer->items.size;
}

void *randomizer_Prev(struct randomizer *randomizer)
{
    assert(randomizer_HasPrev(randomizer));
    return randomizer->items.data[--randomizer->current];
}

void *randomizer_Next(struct randomizer *randomizer)
{
    assert(randomizer_HasNext(randomizer));
    size_t size = randomizer->items.size;
    assert(randomizer->head <= size);
    assert(randomizer->current <= randomizer->head);

    if (randomizer->current == randomizer->head)
    {
        /* execute 1 step of the Fisher–Yates shuffle */
        size_t head = randomizer->head;
        size_t remaining = size - head;
        assert(remaining > 0);

        size_t selected = head + (vlc_lrand48() % remaining);
        assert(selected >= head && selected < size);

        swap_items(randomizer, head, selected);
        randomizer->head++;
    }

    return randomizer->items.data[randomizer->current++];
}

bool randomizer_Add(struct randomizer *randomizer, void *item)
{
    return vlc_vector_push(&randomizer->items, item);
}

void randomizer_Select(struct randomizer *randomizer, const void *item)
{
    ssize_t idx_;
    vlc_vector_index_of(&randomizer->items, item, &idx_);
    assert(idx_ >= 0); /* item must exist */
    size_t idx = (size_t) idx_;
    if (idx >= randomizer->head)
    {
        swap_items(randomizer, idx, randomizer->head);
        randomizer->head++;
    } else if (idx < randomizer->items.size - 1) {
        void *tmp = randomizer->items.data[idx];
        memmove(&randomizer->items.data[idx],
                &randomizer->items.data[idx + 1],
                (randomizer->head - idx - 1) * sizeof(void *));
        randomizer->items.data[randomizer->head - 1] = tmp;
    }
    randomizer->current = randomizer->head;
}

bool randomizer_Remove(struct randomizer *randomizer, const void *item)
{
    ssize_t idx_;
    vlc_vector_index_of(&randomizer->items, item, &idx_);
    if (idx_ == -1)
        return false;

    size_t idx = (size_t) idx_;
    if (idx >= randomizer->head)
        /* item was not selected */
        vlc_vector_swap_remove(&randomizer->items, idx);
    else
    {
        /* item was selected, keep the selected part ordered */
        memmove(&randomizer->items.data[idx],
                &randomizer->items.data[idx + 1],
                (randomizer->head - idx - 1) * sizeof (void *));
        vlc_vector_swap_remove(&randomizer->items, randomizer->head);
        randomizer->head--;
    }

    vlc_vector_autoshrink(&randomizer->items);
    return true;
}

void randomizer_Clear(struct randomizer *randomizer)
{
    vlc_vector_clear(&randomizer->items);
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
            item = randomizer.items.data[62];
            randomizer_Select(&randomizer, item);
            /* the item should now be the last selected one */
            assert(randomizer.items.data[randomizer.current - 1] == item);
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
            item = randomizer.items.data[42];
            randomizer_Select(&randomizer, item);
            /* the item should now be the last selected one */
            assert(randomizer.items.data[randomizer.current - 1] == item);
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
