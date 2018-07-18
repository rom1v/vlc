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

void randomizer_init(randomizer *randomizer)
{
    vlc_array_init(&randomizer->items);
    randomizer->head = 0;
    randomizer->current = 0;
}

void randomizer_destroy(randomizer *randomizer)
{
    vlc_array_clear(&randomizer->items);
}

void randomizer_reshuffle(randomizer *randomizer)
{
    /* yeah, it's that simple */
    randomizer->head = 0;
    randomizer->current = 0;
}

/* XXX: move to vlc_arrays.h? */
static void array_swap(vlc_array_t *array, size_t i, size_t j)
{
    void *item = array->pp_elems[i];
    array->pp_elems[i] = array->pp_elems[j];
    array->pp_elems[j] = item;
}

void *randomizer_prev(randomizer *randomizer)
{
    if (randomizer->current == 0)
        return NULL;
    return randomizer->items.pp_elems[--randomizer->current];
}

void *randomizer_next(randomizer *randomizer)
{
    int size = vlc_array_count(&randomizer->items);
    assert(randomizer->head <= size);
    if (randomizer->head == size)
        /* no more items */
        return NULL;

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

    return randomizer->items.pp_elems[randomizer->current++];
}

bool randomizer_add(randomizer *randomizer, void *item)
{
    return vlc_array_append(&randomizer->items, item) == VLC_SUCCESS;
}

void randomizer_select(randomizer *randomizer, const void *item)
{
    ssize_t idx = vlc_array_find(&randomizer->items, item);
    assert(idx >= 0); /* item must exist */
    if (idx >= randomizer->head)
    {
        array_swap(&randomizer->items, idx, randomizer->head);
        randomizer->head++;
    } else {
        void *tmp = randomizer->items.pp_elems[idx];
        memmove(&randomizer->items.pp_elems[idx],
                &randomizer->items.pp_elems[idx + 1],
                (randomizer->head - idx - 1) * sizeof (void *));
        randomizer->items.pp_elems[randomizer->head - 1] = tmp;
    }
    randomizer->current = randomizer->head;
}

bool randomizer_remove(randomizer *randomizer, const void *item)
{
    ssize_t idx = vlc_array_find(&randomizer->items, item);
    if (idx == -1)
        return false;

    if (idx >= randomizer->head)
        /* item was not selected */
        vlc_array_swap_remove(&randomizer->items, idx);
    else
    {
        /* item was selected, keep the selected part ordered */
        memmove(&randomizer->items.pp_elems[idx],
                &randomizer->items.pp_elems[idx + 1],
                (randomizer->head - idx - 1) * sizeof (void *));
        vlc_array_swap_remove(&randomizer->items, randomizer->head);
        randomizer->head--;
    }

    vlc_array_shrink(&randomizer->items);
    return true;
}

void randomizer_clear(randomizer *randomizer)
{
    vlc_array_clear(&randomizer->items);
}

#ifdef TEST

static void test_all_items_selected_once(void)
{
    randomizer randomizer;
    randomizer_init(&randomizer);

#define SIZE 100
    char data[SIZE];

    for (int i = 0; i < SIZE; ++i)
        /* our items are pointers to some address in the data array */
        randomizer_add(&randomizer, &data[i]);

    bool selected[SIZE] = {0};
    for (int i = 0; i < SIZE; ++i)
    {
        char *item = (char *) randomizer_next(&randomizer);
        assert(item);
        int idx = (int) (item - data);
        assert(idx >= 0 && idx < SIZE);
        assert(!selected[idx]); /* never selected twice */
        selected[idx] = true;
    }

    assert(!randomizer_next(&randomizer)); /* no more items */

    for (int i = 0; i < SIZE; ++i)
        assert(selected[i]); /* all selected */

    randomizer_destroy(&randomizer);
#undef SIZE
}

int main(void)
{
    test_all_items_selected_once();
}

#endif
