/*****************************************************************************
 * vlc_arrays.h : Arrays and data structures handling
 *****************************************************************************
 * Copyright (C) 1999-2004 VLC authors and VideoLAN
 * $Id$
 *
 * Authors: Samuel Hocevar <sam@zoy.org>
 *          Clément Stenac <zorglub@videolan.org>
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

#ifndef VLC_ARRAYS_H_
#define VLC_ARRAYS_H_

#include <vlc_common.h>
#include <assert.h>

/**
 * \file
 * This file defines functions, structures and macros for handling arrays in vlc
 */

/* realloc() that never fails *if* downsizing */
static inline void *realloc_down( void *ptr, size_t size )
{
    void *ret = realloc( ptr, size );
    return ret ? ret : ptr;
}

/**
 * This wrapper around realloc() will free the input pointer when
 * realloc() returns NULL. The use case ptr = realloc(ptr, newsize) will
 * cause a memory leak when ptr pointed to a heap allocation before,
 * leaving the buffer allocated but unreferenced. vlc_realloc() is a
 * drop-in replacement for that use case (and only that use case).
 */
static inline void *realloc_or_free( void *p, size_t sz )
{
    void *n = realloc(p,sz);
    if( !n )
        free(p);
    return n;
}

#define TAB_INIT( count, tab )                  \
  do {                                          \
    (count) = 0;                                \
    (tab) = NULL;                               \
  } while(0)

#define TAB_CLEAN( count, tab )                 \
  do {                                          \
    free( tab );                                \
    (count)= 0;                                 \
    (tab)= NULL;                                \
  } while(0)

#define TAB_APPEND_CAST( cast, count, tab, p )             \
  do {                                          \
    if( (count) > 0 )                           \
        (tab) = cast realloc( tab, sizeof( *(tab) ) * ( (count) + 1 ) ); \
    else                                        \
        (tab) = cast malloc( sizeof( *(tab) ) );    \
    if( !(tab) ) abort();                       \
    (tab)[count] = (p);                         \
    (count)++;                                  \
  } while(0)

#define TAB_APPEND( count, tab, p )             \
    TAB_APPEND_CAST( , count, tab, p )

#define TAB_FIND( count, tab, p, idx )          \
  do {                                          \
    for( (idx) = 0; (idx) < (count); (idx)++ )  \
        if( (tab)[(idx)] == (p) )               \
            break;                              \
    if( (idx) >= (count) )                      \
        (idx) = -1;                             \
  } while(0)


#define TAB_ERASE( count, tab, index )      \
  do {                                      \
        if( (count) > 1 )                   \
            memmove( (tab) + (index),       \
                     (tab) + (index) + 1,   \
                     ((count) - (index) - 1 ) * sizeof( *(tab) ) );\
        (count)--;                          \
        if( (count) == 0 )                  \
        {                                   \
            free( tab );                    \
            (tab) = NULL;                   \
        }                                   \
  } while(0)

#define TAB_REMOVE( count, tab, p )             \
  do {                                          \
        int i_index;                            \
        TAB_FIND( count, tab, p, i_index );     \
        if( i_index >= 0 )                      \
            TAB_ERASE( count, tab, i_index );   \
  } while(0)

#define TAB_INSERT_CAST( cast, count, tab, p, index ) do { \
    if( (count) > 0 )                           \
        (tab) = cast realloc( tab, sizeof( *(tab) ) * ( (count) + 1 ) ); \
    else                                        \
        (tab) = cast malloc( sizeof( *(tab) ) );       \
    if( !(tab) ) abort();                       \
    if( (count) - (index) > 0 )                 \
        memmove( (tab) + (index) + 1,           \
                 (tab) + (index),               \
                 ((count) - (index)) * sizeof( *(tab) ) );\
    (tab)[(index)] = (p);                       \
    (count)++;                                  \
} while(0)

#define TAB_INSERT( count, tab, p, index )      \
    TAB_INSERT_CAST( , count, tab, p, index )

/**
 * Binary search in a sorted array. The key must be comparable by < and >
 * \param entries array of entries
 * \param count number of entries
 * \param elem key to check within an entry (like .id, or ->i_id)
 * \param zetype type of the key
 * \param key value of the key
 * \param answer index of answer within the array. -1 if not found
 */
#define BSEARCH( entries, count, elem, zetype, key, answer ) \
   do {  \
    int low = 0, high = count - 1;   \
    answer = -1; \
    while( low <= high ) {\
        int mid = ((unsigned int)low + (unsigned int)high) >> 1;\
        zetype mid_val = entries[mid] elem;\
        if( mid_val < key ) \
            low = mid + 1; \
        else if ( mid_val > key ) \
            high = mid -1;  \
        else    \
        {   \
            answer = mid;  break;   \
        }\
    } \
 } while(0)


/************************************************************************
 * Dynamic arrays with progressive allocation
 ************************************************************************/

/* Internal functions */
#define _ARRAY_ALLOC(array, newsize) {                                      \
    (array).i_alloc = newsize;                                              \
    (array).p_elems = realloc( (array).p_elems, (array).i_alloc *           \
                               sizeof(*(array).p_elems) );                  \
    if( !(array).p_elems ) abort();                                         \
}

#define _ARRAY_GROW1(array) {                                               \
    if( (array).i_alloc < 10 )                                              \
        _ARRAY_ALLOC(array, 10 )                                            \
    else if( (array).i_alloc == (array).i_size )                            \
        _ARRAY_ALLOC(array, (int)((array).i_alloc * 1.5) )                    \
}

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

/* API */
#define DECL_ARRAY(type) struct {                                           \
    int i_alloc;                                                            \
    int i_size;                                                             \
    type *p_elems;                                                          \
}

#define TYPEDEF_ARRAY(type, name) typedef DECL_ARRAY(type) name;

#define ARRAY_INIT(array)                                                   \
  do {                                                                      \
    (array).i_alloc = 0;                                                    \
    (array).i_size = 0;                                                     \
    (array).p_elems = NULL;                                                 \
  } while(0)

#define ARRAY_RESET(array)                                                  \
  do {                                                                      \
    (array).i_alloc = 0;                                                    \
    (array).i_size = 0;                                                     \
    free( (array).p_elems ); (array).p_elems = NULL;                        \
  } while(0)

#define ARRAY_APPEND(array, elem)                                           \
  do {                                                                      \
    _ARRAY_GROW1(array);                                                    \
    (array).p_elems[(array).i_size] = elem;                                 \
    (array).i_size++;                                                       \
  } while(0)

#define ARRAY_INSERT(array,elem,pos)                                        \
  do {                                                                      \
    _ARRAY_GROW1(array);                                                    \
    if( (array).i_size - pos ) {                                            \
        memmove( (array).p_elems + pos + 1, (array).p_elems + pos,          \
                 ((array).i_size-pos) * sizeof(*(array).p_elems) );         \
    }                                                                       \
    (array).p_elems[pos] = elem;                                            \
    (array).i_size++;                                                       \
  } while(0)

#define _ARRAY_SHRINK(array) {                                              \
    if( (array).i_size > 10 && (array).i_size < (int)((array).i_alloc / 1.5) ) {  \
        _ARRAY_ALLOC(array, (array).i_size + 5);                            \
    }                                                                       \
}

#define ARRAY_FIND(array, p, idx)                                           \
  TAB_FIND((array).i_size, (array).p_elems, p, idx)

#define ARRAY_REMOVE(array,pos)                                             \
  do {                                                                      \
    if( (array).i_size - (pos) - 1 )                                        \
    {                                                                       \
        memmove( (array).p_elems + pos, (array).p_elems + pos + 1,          \
                 ( (array).i_size - pos - 1 ) *sizeof(*(array).p_elems) );  \
    }                                                                       \
    (array).i_size--;                                                       \
    _ARRAY_SHRINK(array);                                                   \
  } while(0)

#define ARRAY_VAL(array, pos) array.p_elems[pos]

#define ARRAY_BSEARCH(array, elem, zetype, key, answer) \
    BSEARCH( (array).p_elems, (array).i_size, elem, zetype, key, answer)

#define FOREACH_ARRAY( item, array ) { \
    int fe_idx; \
    for( fe_idx = 0 ; fe_idx < (array).i_size ; fe_idx++ ) \
    { \
        item = (array).p_elems[fe_idx];

#define FOREACH_END() } }


/************************************************************************
 * Dynamic arrays with progressive allocation (Preferred API)
 ************************************************************************/
typedef struct vlc_array_t
{
    size_t i_capacity;
    size_t i_count;
    void ** pp_elems;
} vlc_array_t;

#define VLC_ARRAY_MAX_LENGTH (SIZE_MAX / sizeof(void *))
#define VLC_ARRAY_MIN_ALLOC 10

static inline void vlc_array_init( vlc_array_t * p_array )
{
    p_array->i_capacity = 0;
    p_array->i_count = 0;
    p_array->pp_elems = NULL;
}

static inline void vlc_array_clear( vlc_array_t * p_array )
{
    free( p_array->pp_elems );
    vlc_array_init( p_array );
}

/* Read */
static inline size_t vlc_array_count( vlc_array_t * p_array )
{
    return p_array->i_count;
}

#ifndef __cplusplus
# define vlc_array_get(ar, idx) \
    _Generic((ar), \
        const vlc_array_t *: ((ar)->pp_elems[idx]), \
        vlc_array_t *: ((ar)->pp_elems[idx]))
#else
static inline void *vlc_array_get( vlc_array_t *ar, size_t idx )
{
    return ar->pp_elems[idx];
}

static inline const void *vlc_array_get( const vlc_array_t *ar,
                                         size_t idx )
{
    return ar->pp_elems[idx];
}
#endif

static inline ssize_t vlc_array_find( const vlc_array_t *ar,
                                      const void *elem )
{
    for( size_t i = 0; i < ar->i_count; i++ )
    {
        if( ar->pp_elems[i] == elem )
            return i;
    }
    return -1;
}

static inline int vlc_array_resize_(vlc_array_t *array, size_t target)
{
    void **pp = (void **) vlc_reallocarray(array->pp_elems,
                                           target, sizeof(void *));
    if (unlikely(!pp))
        return -1;

    array->pp_elems = pp;
    array->i_capacity = target;
    return 0;
}

static inline size_t vlc_array_mul_by_growth_factor_(size_t value)
{
    /* integer multiplication by 1.5 */
    return value + (value >> 1);
}

static inline int vlc_array_reserve(vlc_array_t *array, size_t min_capacity)
{
    if (min_capacity <= array->i_capacity)
        return 0; /* nothing to do */

    if (min_capacity > VLC_ARRAY_MAX_LENGTH)
        return -1;

    if (min_capacity < VLC_ARRAY_MIN_ALLOC)
        min_capacity = VLC_ARRAY_MIN_ALLOC; /* do not allocate tiny arrays */

    /* multiply by 1.5 first (this may not overflow due to the way
     * VLC_ARRAY_MAX_LENGTH is defined, unless sizeof(void *) == 1, which will
     * never happen) */
    size_t new_capacity = vlc_array_mul_by_growth_factor_(array->i_capacity);
    /* if it's still not sufficient */
    if (new_capacity < min_capacity)
        new_capacity = min_capacity;
    else if (new_capacity > VLC_ARRAY_MAX_LENGTH)
        /* the capacity must never exceed VLC_ARRAY_MAX_LENGTH */
        new_capacity = VLC_ARRAY_MAX_LENGTH;

    return vlc_array_resize_(array, new_capacity);
}

static inline int vlc_array_shrink_to_(vlc_array_t *array,
                                       size_t target)
{
    if (array->i_capacity == target)
        return 0;

    if (target == 0)
    {
        vlc_array_clear(array);
        return 0;
    }

    return vlc_array_resize_(array, target);
}

static inline int vlc_array_shrink(vlc_array_t *array)
{
    if (array->i_capacity <= VLC_ARRAY_MIN_ALLOC)
        return 0; /* do not shrink to tiny length */

    size_t grown_count = vlc_array_mul_by_growth_factor_(array->i_count);
    if (array->i_capacity - 5 < grown_count)
        return 0; /* no need to shrink */

    size_t target = array->i_count < VLC_ARRAY_MIN_ALLOC - 3
                    ? VLC_ARRAY_MIN_ALLOC
                    : array->i_count + 5;
    if (target >= array->i_capacity)
        return 0; /* do not grow */

    return vlc_array_shrink_to_(array, target);
}

static inline int vlc_array_shrink_to_fit(vlc_array_t *array)
{
    return vlc_array_shrink_to_(array, array->i_count);
}

/* Write */
static inline int vlc_array_insert( vlc_array_t *ar, void *elem, int idx )
{
    if (unlikely(vlc_array_reserve(ar, ar->i_count + 1)))
        return VLC_ENOMEM;

    void **pp = ar->pp_elems;
    size_t tail = ar->i_count - idx;
    if( tail > 0 )
        memmove( pp + idx + 1, pp + idx, sizeof( void * ) * tail );

    ar->pp_elems[idx] = elem;
    ar->i_count++;
    return VLC_SUCCESS;
}

static inline void vlc_array_insert_or_abort( vlc_array_t *ar, void *elem, int idx )
{
    if( vlc_array_insert( ar, elem, idx ) )
        abort();
}

static inline int vlc_array_append( vlc_array_t *ar, void *elem )
{
    if (unlikely(vlc_array_reserve(ar, ar->i_count + 1)))
        return VLC_ENOMEM;

    ar->pp_elems[ar->i_count++] = elem;
    return VLC_SUCCESS;
}

static inline void vlc_array_append_or_abort( vlc_array_t *ar, void *elem )
{
    if( vlc_array_append( ar, elem ) != 0 )
        abort();
}

static inline void vlc_array_remove( vlc_array_t *ar, size_t idx )
{
    void **pp = ar->pp_elems;
    size_t tail = ar->i_count - idx - 1;

    if( tail > 0 )
        memmove( pp + idx, pp + idx + 1, sizeof( void * ) * tail );

    ar->i_count--;
    vlc_array_shrink(ar);
}

static inline void vlc_array_swap_remove(vlc_array_t *array, size_t idx)
{
    array->pp_elems[idx] = array->pp_elems[array->i_count - 1];
    array->i_count--;
}

/************************************************************************
 * Dictionaries
 ************************************************************************/

/* This function is not intended to be crypto-secure, we only want it to be
 * fast and not suck too much. This one is pretty fast and did 0 collisions
 * in wenglish's dictionary.
 */
static inline uint64_t DictHash( const char *psz_string, int hashsize )
{
    uint64_t i_hash = 0;
    if( psz_string )
    {
        while( *psz_string )
        {
            i_hash += *psz_string++;
            i_hash += i_hash << 10;
            i_hash ^= i_hash >> 8;
        }
    }
    return i_hash % hashsize;
}

typedef struct vlc_dictionary_entry_t
{
    char *   psz_key;
    void *   p_value;
    struct vlc_dictionary_entry_t * p_next;
} vlc_dictionary_entry_t;

typedef struct vlc_dictionary_t
{
    int i_size;
    vlc_dictionary_entry_t ** p_entries;
} vlc_dictionary_t;

static void * const kVLCDictionaryNotFound = NULL;

static inline void vlc_dictionary_init( vlc_dictionary_t * p_dict, int i_size )
{
    p_dict->p_entries = NULL;

    if( i_size > 0 )
    {
        p_dict->p_entries = (vlc_dictionary_entry_t **)calloc( i_size, sizeof(*p_dict->p_entries) );
        if( !p_dict->p_entries )
            i_size = 0;
    }
    p_dict->i_size = i_size;
}

static inline void vlc_dictionary_clear( vlc_dictionary_t * p_dict,
                                         void ( * pf_free )( void * p_data, void * p_obj ),
                                         void * p_obj )
{
    if( p_dict->p_entries )
    {
        for( int i = 0; i < p_dict->i_size; i++ )
        {
            vlc_dictionary_entry_t * p_current, * p_next;
            p_current = p_dict->p_entries[i];
            while( p_current )
            {
                p_next = p_current->p_next;
                if( pf_free != NULL )
                    ( * pf_free )( p_current->p_value, p_obj );
                free( p_current->psz_key );
                free( p_current );
                p_current = p_next;
            }
        }
        free( p_dict->p_entries );
        p_dict->p_entries = NULL;
    }
    p_dict->i_size = 0;
}

static inline int
vlc_dictionary_has_key( const vlc_dictionary_t * p_dict, const char * psz_key )
{
    if( !p_dict->p_entries )
        return 0;

    int i_pos = DictHash( psz_key, p_dict->i_size );
    const vlc_dictionary_entry_t * p_entry = p_dict->p_entries[i_pos];
    for( ; p_entry != NULL; p_entry = p_entry->p_next )
    {
        if( !strcmp( psz_key, p_entry->psz_key ) )
            break;
    }
    return p_entry != NULL;
}

static inline void *
vlc_dictionary_value_for_key( const vlc_dictionary_t * p_dict, const char * psz_key )
{
    if( !p_dict->p_entries )
        return kVLCDictionaryNotFound;

    int i_pos = DictHash( psz_key, p_dict->i_size );
    vlc_dictionary_entry_t * p_entry = p_dict->p_entries[i_pos];

    if( !p_entry )
        return kVLCDictionaryNotFound;

    /* Make sure we return the right item. (Hash collision) */
    do {
        if( !strcmp( psz_key, p_entry->psz_key ) )
            return p_entry->p_value;
        p_entry = p_entry->p_next;
    } while( p_entry );

    return kVLCDictionaryNotFound;
}

static inline int
vlc_dictionary_keys_count( const vlc_dictionary_t * p_dict )
{
    vlc_dictionary_entry_t * p_entry;
    int i, count = 0;

    if( !p_dict->p_entries )
        return 0;

    for( i = 0; i < p_dict->i_size; i++ )
    {
        for( p_entry = p_dict->p_entries[i]; p_entry; p_entry = p_entry->p_next ) count++;
    }
    return count;
}

static inline bool
vlc_dictionary_is_empty( const vlc_dictionary_t * p_dict )
{
    if( p_dict->p_entries )
        for( int i = 0; i < p_dict->i_size; i++ )
            if( p_dict->p_entries[i] )
                return false;
    return true;
}

static inline char **
vlc_dictionary_all_keys( const vlc_dictionary_t * p_dict )
{
    vlc_dictionary_entry_t * p_entry;
    char ** ppsz_ret;
    int i, count = vlc_dictionary_keys_count( p_dict );

    ppsz_ret = (char**)malloc(sizeof(char *) * (count + 1));
    if( unlikely(!ppsz_ret) )
        return NULL;

    count = 0;
    for( i = 0; i < p_dict->i_size; i++ )
    {
        for( p_entry = p_dict->p_entries[i]; p_entry; p_entry = p_entry->p_next )
            ppsz_ret[count++] = strdup( p_entry->psz_key );
    }
    ppsz_ret[count] = NULL;
    return ppsz_ret;
}

static inline void
vlc_dictionary_insert_impl_( vlc_dictionary_t * p_dict, const char * psz_key,
                             void * p_value, bool rebuild )
{
    if( !p_dict->p_entries )
        vlc_dictionary_init( p_dict, 1 );

    int i_pos = DictHash( psz_key, p_dict->i_size );
    vlc_dictionary_entry_t * p_entry;

    p_entry = (vlc_dictionary_entry_t *)malloc(sizeof(*p_entry));
    p_entry->psz_key = strdup( psz_key );
    p_entry->p_value = p_value;
    p_entry->p_next = p_dict->p_entries[i_pos];
    p_dict->p_entries[i_pos] = p_entry;
    if( rebuild )
    {
        /* Count how many items there was */
        int count;
        for( count = 1; p_entry->p_next; count++ )
            p_entry = p_entry->p_next;
        if( count > 3 ) /* XXX: this need tuning */
        {
            /* Here it starts to be not good, rebuild a bigger dictionary */
            struct vlc_dictionary_t new_dict;
            int i_new_size = ( (p_dict->i_size+2) * 3) / 2; /* XXX: this need tuning */
            int i;
            vlc_dictionary_init( &new_dict, i_new_size );
            for( i = 0; i < p_dict->i_size; i++ )
            {
                p_entry = p_dict->p_entries[i];
                while( p_entry )
                {
                    vlc_dictionary_insert_impl_( &new_dict, p_entry->psz_key,
                                             p_entry->p_value,
                                             false /* To avoid multiple rebuild loop */);
                    p_entry = p_entry->p_next;
                }
            }

            vlc_dictionary_clear( p_dict, NULL, NULL );
            p_dict->i_size = new_dict.i_size;
            p_dict->p_entries = new_dict.p_entries;
        }
    }
}

static inline void
vlc_dictionary_insert( vlc_dictionary_t * p_dict, const char * psz_key, void * p_value )
{
    vlc_dictionary_insert_impl_( p_dict, psz_key, p_value, true );
}

static inline void
vlc_dictionary_remove_value_for_key( const vlc_dictionary_t * p_dict, const char * psz_key,
                                     void ( * pf_free )( void * p_data, void * p_obj ),
                                     void * p_obj )
{
    if( !p_dict->p_entries )
        return;

    int i_pos = DictHash( psz_key, p_dict->i_size );
    vlc_dictionary_entry_t * p_entry = p_dict->p_entries[i_pos];
    vlc_dictionary_entry_t * p_prev;

    if( !p_entry )
        return; /* Not found, nothing to do */

    /* Hash collision */
    p_prev = NULL;
    do {
        if( !strcmp( psz_key, p_entry->psz_key ) )
        {
            if( pf_free != NULL )
                ( * pf_free )( p_entry->p_value, p_obj );
            if( !p_prev )
                p_dict->p_entries[i_pos] = p_entry->p_next;
            else
                p_prev->p_next = p_entry->p_next;
            free( p_entry->psz_key );
            free( p_entry );
            return;
        }
        p_prev = p_entry;
        p_entry = p_entry->p_next;
    } while( p_entry );

    /* No key was found */
}

#ifdef __cplusplus
// C++ helpers
template <typename T>
void vlc_delete_all( T &container )
{
    typename T::iterator it = container.begin();
    while ( it != container.end() )
    {
        delete *it;
        ++it;
    }
    container.clear();
}

#endif

#endif
