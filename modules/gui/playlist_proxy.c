#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "playlist_proxy.h"
#include <assert.h>
#include <stdlib.h>
#include <vlc_playlist_new.h>
#include <vlc_vector.h>

int
vlc_playlist_RequestInsert(vlc_playlist_t *playlist, size_t index,
                           input_item_t *const media[], size_t count)
{
    size_t size = vlc_playlist_Count(playlist);
    if (index > size)
        index = size;

    return vlc_playlist_Insert(playlist, index, media, count);
}

static ssize_t
FindRealIndex(vlc_playlist_t *playlist, vlc_playlist_item_t *item,
              ssize_t index_hint)
{
    if (index_hint != -1 && (size_t) index_hint < vlc_playlist_Count(playlist))
    {
        if (item == vlc_playlist_Get(playlist, index_hint))
            /* we are lucky */
            return index_hint;
    }

    /* we are unlucky, we need to find the item */
    return vlc_playlist_IndexOf(playlist, item);
}

struct size_vector VLC_VECTOR(size_t);

static void
FindIndices(vlc_playlist_t *playlist, vlc_playlist_item_t *const items[],
            size_t count, ssize_t index_hint, struct size_vector *out)
{
    for (size_t i = 0; i < count; ++i)
    {
        ssize_t real_index = FindRealIndex(playlist, items[i], index_hint + i);
        if (real_index != -1)
        {
            int ok = vlc_vector_push(out, real_index);
            assert(ok); /* cannot fail, space had been reserved */
            VLC_UNUSED(ok);
        }
    }
}

static void
RemoveBySlices(vlc_playlist_t *playlist, size_t sorted_indices[], size_t count)
{
    assert(count > 0);
    size_t last_index = sorted_indices[count - 1];
    size_t slice_size = 1;
    /* size_t is unsigned, take care not to compare for non-negativity */
    for (size_t i = count - 1; i != 0; --i)
    {
        size_t index = sorted_indices[i - 1];
        if (index == last_index - 1)
            slice_size++;
        else
        {
            /* the previous slice is complete */
            vlc_playlist_Remove(playlist, last_index, slice_size);
            slice_size = 1;
        }
        last_index = index;
    }
    /* remove the last slice */
    vlc_playlist_Remove(playlist, last_index, slice_size);
}

static int
cmp_size(const void *lhs, const void *rhs)
{
    size_t a = *(size_t *) lhs;
    size_t b = *(size_t *) rhs;
    if (a < b)
        return -1;
    if (a == b)
        return 0;
    return 1;
}

int
vlc_playlist_RequestRemove(vlc_playlist_t *playlist, ssize_t index_hint,
                           vlc_playlist_item_t *const items[], size_t count)
{
    struct size_vector vector = VLC_VECTOR_INITIALIZER;
    if (!vlc_vector_reserve(&vector, count))
        return VLC_ENOMEM;

    FindIndices(playlist, items, count, index_hint, &vector);

    if (vector.size > 0)
    {
        /* sort so that removing an item does not shift the other indices */
        qsort(vector.data, vector.size, sizeof(vector.data[0]), cmp_size);

        RemoveBySlices(playlist, vector.data, vector.size);
    }

    vlc_vector_clear(&vector);
    return VLC_SUCCESS;
}

#ifdef TEST

#include <assert.h>

int main(void)
{
    return 0;
}
#endif
