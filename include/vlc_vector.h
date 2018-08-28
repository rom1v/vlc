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

#define VLC_VECTOR(type) { \
    size_t cap; \
    size_t size; \
    type *data; \
}

#define VLC_VECTOR_INITIALIZER { \
    .cap = 0; \
    .size = 0; \
    .data = NULL; \
}

#define vlc_vector_init(pv) \
    do { \
        (pv)->cap = 0; \
        (pv)->size = 0; \
        (pv)->data = NULL; \
    } while (0)

#define VLC_VECTOR_MINCAP ((size_t) 10)

static inline size_t
vlc_vector_min(size_t a, size_t b)
{
    return a < b ? a : b;
}

static inline size_t
vlc_vector_max(size_t a, size_t b)
{
    return a > b ? a : b;
}

static inline size_t
vlc_vector_between(size_t x, size_t min, size_t max)
{
    return vlc_vector_max(min, vlc_vector_min(max, x));
}

/**
 * \retval true if the allocation succeeded
 * \retval false if the allocation failed
 */
#define vlc_vector_realloc_(pv, newsize) \
    (((pv)->cap = (newsize), true) && \
     ((pv)->size = vlc_vector_min((pv)->size, newsize), true) && \
     ((pv)->data = vlc_reallocarray((pv)->data, newsize, sizeof(*(pv)->data))))

//#define vlc_vector_size(pv) (pv)->size
//#define vlc_vector_is_empty(pv) ((pv)->size == 0)
//#define vlc_vector_get(pv, index) ((pv)->data[index])

static inline size_t
vlc_vector_growsize_(size_t value)
{
    /* integer multiplication by 1.5 */
    return value + (value >> 1);
}

/* SIZE_MAX/2 to guarantee that multiplication by 1.5 does not overflow. */
#define vlc_vector_maxcap_(pv) (SIZE_MAX / 2 / sizeof(*(pv)->data))

#define vlc_vector_reserve(pv, mincap) \
    /* avoid to allocate tiny arrays (< VLC_VECTOR_MINCAP) */ \
    vlc_vector_reserve_internal(pv, vlc_vector_max(mincap, VLC_VECTOR_MINCAP))

#define vlc_vector_reserve_internal(pv, mincap) \
    (mincap <= (pv)->cap ? true /* nothing to do */ : \
    (mincap > vlc_vector_maxcap_(pv) ? false /* too big */ : \
    vlc_vector_realloc_(pv, \
                        /* multiply by 1.5, force between [mincap, maxcap] */ \
                        vlc_vector_between(vlc_vector_growsize_((pv)->cap), \
                                           mincap, \
                                           vlc_vector_maxcap_(pv)))))

#define vlc_vector_append(pv, item) \
    (vlc_vector_reserve(pv, (pv)->size + 1) && \
    ((pv)->data[(pv)->size++] = (item), true))

#define vlc_vector_insert(pv, index, item) \
    (vlc_vector_reserve(pv, (pv)->size + 1) && \
    ((index == (pv)->size) ? true : \
        (memmove(&(pv)->data[(index)+1], &(pv)->data[index], \
                ((pv)->size - (index)) * sizeof(*(pv)->data)), true)) && \
    ((pv)->data[index] = (item), true) && \
    ((pv)->size++, true))

#endif
