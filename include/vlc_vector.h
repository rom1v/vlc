/******************************************************************************
 * vlc_vector.h
 ******************************************************************************
 * Copyright © 2018 VLC authors and VideoLAN
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
#ifndef VLC_VECTOR_H
#define VLC_VECTOR_H

#include <stdbool.h>

/**
 * \defgroup vector Vector
 * \ingroup cext
 * @{
 * \file
 * This provides convenience helpers for vectors.
 */

/**
 * Vector struct body.
 *
 * A vector is a dynamic array, managed by the vlc_vector_* helpers.
 *
 * It is generic over the type of its items, so it is implemented as macros.
 *
 * To use a vector, a new type must be defined:
 *
 * \verbatim
 * struct vec_int VLC_VECTOR(int);
 * \endverbatim
 *
 * The struct may be anonymous:
 *
 * \verbatim
 * struct VLC_VECTOR(const char *) names;
 * \endverbatim
 *
 * It is convenient to define a typedef to an anonymous structure:
 *
 * \verbatim
 * typedef struct VLC_VECTOR(int) vec_int_t;
 * \endverbatim
 *
 * Vector size is accessible via `vec.size`, and items are intended to be
 * accessed directly, via `vec.data[i]`.
 *
 * Functions and macros having name ending with '_' are private.
 */
#define VLC_VECTOR(type) { \
    size_t cap; \
    size_t size; \
    type *data; \
}

/**
 * Static initializer for a vector.
 */
#define VLC_VECTOR_INITIALIZER { 0, 0, NULL }

/**
 * Initialize an empty vector.
 */
#define vlc_vector_init(pv) \
    /* cannot be implemened as do-while(0), called from vlc_vector_clear() */ \
    (void) \
    (((pv)->cap = 0, true) && \
     ((pv)->size = 0, true) && \
     ((pv)->data = NULL))

/**
 * Clear a vector, and release any associated resources.
 */
#define vlc_vector_clear(pv) \
    /* cannot be implemened as do-while(0), called from vlc_vector_resize() */ \
    (void) \
    ((free((pv)->data), true) && \
    (vlc_vector_init(pv), true))

/**
 * The minimal allocation size, in number of items.
 *
 * Private.
 */
#define VLC_VECTOR_MINCAP_ ((size_t) 10)

static inline size_t
vlc_vector_min_(size_t a, size_t b)
{
    return a < b ? a : b;
}

static inline size_t
vlc_vector_max_(size_t a, size_t b)
{
    return a > b ? a : b;
}

static inline size_t
vlc_vector_between_(size_t x, size_t min, size_t max)
{
    return vlc_vector_max_(min, vlc_vector_min_(max, x));
}

/**
 * Realloc array in *pptr.
 *
 * If reallocation failed, *pptr is kept untouched.
 *
 * Private.
 *
 * \retval true on success
 * \retval false on failure
 */
static inline bool
vlc_vector_reallocarray_(void **pptr, size_t count, size_t size)
{
    void *n = vlc_reallocarray(*pptr, count, size);
    if (n)
        /* replace ptr only on allocation success */
        *pptr = n;
    return n != NULL;
}

/**
 * Realloc the underlying array to `newsize`.
 *
 * Private.
 *
 * \param pv a pointer to the vector
 * \param newsize (size_t) the requested size
 * \retval true if no allocation failed
 * \retval false on allocation failure (the vector is left untouched)
 */
#define vlc_vector_realloc_(pv, newsize) \
    (vlc_vector_reallocarray_((void **) &(pv)->data, newsize, \
                              sizeof(*(pv)->data)) && \
    ((pv)->cap = (newsize), true) && \
    ((pv)->size = vlc_vector_min_((pv)->size, newsize), true))

/**
 * Resize the vector to `newsize` exactly.
 *
 * If `newsize` is 0, the vector is cleared.
 *
 * \param pv a pointer to the vector
 * \param newsize (size_t) the requested size
 * \retval true if no allocation failed
 * \retval false on allocation failure (the vector is left untouched)
 */
#define vlc_vector_resize(pv, newsize) \
    (((pv)->cap == (newsize)) /* nothing to do */ || \
    ((newsize) > 0 ? vlc_vector_realloc_(pv, newsize) \
                   : (vlc_vector_clear(pv), true)))

static inline size_t
vlc_vector_growsize_(size_t value)
{
    /* integer multiplication by 1.5 */
    return value + (value >> 1);
}

/* SIZE_MAX/2 to fit in ssize_t, and so that cap*1.5 does not overflow. */
#define vlc_vector_max_cap_(pv) (SIZE_MAX / 2 / sizeof(*(pv)->data))

/**
 * Increase the capacity of the vector to at least `mincap`.
 *
 * \param pv a pointer to the vector
 * \param mincap (size_t) the requested capacity
 * \retval true if no allocation failed
 * \retval false on allocation failure (the vector is left untouched)
 */
#define vlc_vector_reserve(pv, mincap) \
    /* avoid to allocate tiny arrays (< VLC_VECTOR_MINCAP_) */ \
    vlc_vector_reserve_internal_(pv, \
                                 vlc_vector_max_(mincap, VLC_VECTOR_MINCAP_))

#define vlc_vector_reserve_internal_(pv, mincap) \
    ((mincap) <= (pv)->cap /* nothing to do */ || \
    ((mincap) <= vlc_vector_max_cap_(pv) /* not too big */ && \
    vlc_vector_realloc_(pv, \
                        /* multiply by 1.5, force between [mincap, maxcap] */ \
                        vlc_vector_between_(vlc_vector_growsize_((pv)->cap), \
                                            mincap, \
                                            vlc_vector_max_cap_(pv)))))

/**
 * Resize the vector so that its capacity equals its actual size.
 *
 * \param pv a pointer to the vector
 */
#define vlc_vector_shrink_to_fit(pv) \
    (void) /* decreasing the size may not fail */ \
    vlc_vector_resize(pv, (pv)->size)

/**
 * Resize the vector down automatically.
 *
 * Shrink only when necessary (in practice when cap > (size+5)*1.5)
 *
 * \param pv a pointer to the vector
 */
#define vlc_vector_autoshrink(pv) \
    ((pv)->cap <= VLC_VECTOR_MINCAP_ /* do not shrink to tiny length */ || \
     (pv)->cap < vlc_vector_growsize_((pv)->size+5) /* no need to shrink */ || \
     (vlc_vector_resize(pv, vlc_vector_max_((pv)->size+5, VLC_VECTOR_MINCAP_))))

/**
 * Push an item at the end of the vector.
 *
 * The amortized complexity is O(1).
 *
 * \param pv a pointer to the vector
 * \param item the item to append
 * \retval true if no allocation failed
 * \retval false on allocation failure (the vector is left untouched)
 */
#define vlc_vector_push(pv, item) \
    (vlc_vector_reserve(pv, (pv)->size + 1) && \
    ((pv)->data[(pv)->size++] = (item), true))

/**
 * Insert an item at the given index.
 *
 * The items in range [index; size-1] will be moved.
 *
 * \param pv a pointer to the vector
 * \param index the index where the item is to be inserted
 * \param item the item to append
 * \retval true if no allocation failed
 * \retval false on allocation failure (the vector is left untouched)
 */
#define vlc_vector_insert(pv, index, item) \
    (vlc_vector_reserve(pv, (pv)->size + 1) && \
    (((size_t)(index) == (pv)->size) || \
        (memmove(&(pv)->data[(size_t)(index) + 1], \
                 &(pv)->data[(size_t)(index)], \
                 ((pv)->size - (size_t)(index)) * sizeof(*(pv)->data)), \
         true)) && \
    ((pv)->data[index] = (item), true) && \
    ((pv)->size++, true))

/**
 * Insert `count` items at the given index.
 *
 * The items in range [index; size-1] will be moved.
 *
 * \param pv a pointer to the vector
 * \param index the index where the items are to be inserted
 * \param items the items array to append
 * \param count the number of items in the array
 * \retval true if no allocation failed
 * \retval false on allocation failure (the vector is left untouched)
 */
#define vlc_vector_insert_all(pv, index, items, count) \
    (vlc_vector_reserve(pv, (pv)->size + (size_t)(count)) && \
    (((size_t)(index) == (pv)->size) || \
        (memmove(&(pv)->data[(size_t)(index) + (size_t)(count)], \
                 &(pv)->data[(size_t)(index)], \
                 ((pv)->size - (size_t)(index)) * sizeof(*(pv)->data)), \
         true)) && \
    (memcpy(&(pv)->data[(size_t)(index)], items, \
           (size_t)(count) * sizeof(*(pv)->data)), true) && \
    ((pv)->size += (size_t)(count), true))

/**
 * Remove a slice of items, without shrinking the array.
 *
 * If you have no good reason to use the _noshrink() version, use
 * vlc_vector_remove_slice() instead.
 *
 * The items in range [index+count; size-1] will be moved.
 *
 * \param pv a pointer to the vector
 * \param index the index of the first item to remove
 * \param count the number of items to remove
 */
#define vlc_vector_remove_slice_noshrink(pv, index, count) \
    do { \
        if ((size_t)(index) + (size_t)(count) < (pv)->size) \
            memmove(&(pv)->data[(size_t)(index)], \
                    &(pv)->data[(size_t)(index) + (size_t)(count)], \
                    ((pv)->size - (size_t)(index) - (size_t)(count)) \
                        * sizeof(*(pv)->data)); \
        (pv)->size -= (size_t)(count); \
    } while (0)

/**
 * Remove a slice of items.
 *
 * The items in range [index+count; size-1] will be moved.
 *
 * \param pv a pointer to the vector
 * \param index the index of the first item to remove
 * \param count the number of items to remove
 */
#define vlc_vector_remove_slice(pv, index, count) \
    do { \
        vlc_vector_remove_slice_noshrink(pv, index, count); \
        vlc_vector_autoshrink(pv); \
    } while (0)

/**
 * Remove an item, without shrinking the array.
 *
 * If you have no good reason to use the _noshrink() version, use
 * vlc_vector_remove() instead.
 *
 * The items in range [index+1; size-1] will be moved.
 *
 * \param pv a pointer to the vector
 * \param index the index of item to remove
 */
#define vlc_vector_remove_noshrink(pv, index) \
    vlc_vector_remove_slice_noshrink(pv, index, 1)

/**
 * Remove an item.
 *
 * The items in range [index+1; size-1] will be moved.
 *
 * \param pv a pointer to the vector
 * \param index the index of item to remove
 */
#define vlc_vector_remove(pv, index) \
    do { \
        vlc_vector_remove_noshrink(pv, index); \
        vlc_vector_autoshrink(pv); \
    } while (0)

/**
 * Remove an item.
 *
 * The removed item is replaced by the last item of the vector.
 *
 * This does not preserve ordering, but is O(1). This is useful when the order
 * of items is not meaningful.
 *
 * \param pv a pointer to the vector
 * \param index the index of item to remove
 */
#define vlc_vector_swap_remove(pv, index) \
    (pv)->data[index] = (pv)->data[--(pv)->size]

/**
 * Return the index of an item.
 *
 * Iterate over all items to find a given item.
 *
 * Use only for vectors of primitive types or pointers.
 *
 * The result is written to `*(pidx)`:
 *  - the index of the item if it is found;
 *  - -1 if it is not found.
 *
 * \param pv a pointer to the vector
 * \param item the item to find (compared with ==)
 * \param a pointer to the result (ssize_t *) [OUT]
 */
#define vlc_vector_index_of(pv, item, pidx) \
    do { \
        size_t vlc_vector_find_idx_; \
        for (vlc_vector_find_idx_ = 0; \
             vlc_vector_find_idx_ < (pv)->size; \
             ++vlc_vector_find_idx_) \
            if ((pv)->data[vlc_vector_find_idx_] == (item)) \
                break; \
        *(pidx) = vlc_vector_find_idx_ == (pv)->size ? -1 : \
            (ssize_t) vlc_vector_find_idx_; \
    } while (0)

/**
 * For-each loop.
 *
 * Use only for vectors of primitive types or pointers (every struct would be
 * copied for a vector of structs).
 *
 * \param item the iteration variable [OUT]
 * \param pv a pointer to the vector [OUT]
 */
#define vlc_vector_foreach(item, pv) \
    for (size_t vlc_vector_idx_##item = 0; \
         vlc_vector_idx_##item < (pv)->size && \
             ((item) = (pv)->data[vlc_vector_idx_##item], true); \
         ++vlc_vector_idx_##item)

/** @} */

#endif
