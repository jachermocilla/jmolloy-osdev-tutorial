// ordered_array.h -- Interface for creating, inserting and deleting
//                    from ordered arrays.
//                    Written for JamesM's kernel development tutorials.
//                    Ported to x86-64.

#ifndef ORDERED_ARRAY_H
#define ORDERED_ARRAY_H

#include "common.h"

/**
   This array is insertion sorted - it always remains in a sorted state (between calls).
   It can store anything that can be cast to a void* -- so a u64int, or any pointer.

   Note: `type_t` is now eight bytes, not four. Every array of them is twice the
   size it used to be. See KHEAP_INITIAL_SIZE in kheap.h.
**/
typedef void* type_t;

/**
   A predicate should return nonzero if the first argument is less than the second. Else
   it should return zero.
**/
typedef s8int (*lessthan_predicate_t)(type_t,type_t);

typedef struct
{
    type_t *array;
    u64int size;
    u64int max_size;
    lessthan_predicate_t less_than;
} ordered_array_t;

s8int standard_lessthan_predicate(type_t a, type_t b);

ordered_array_t create_ordered_array(u64int max_size, lessthan_predicate_t less_than);
ordered_array_t place_ordered_array(void *addr, u64int max_size, lessthan_predicate_t less_than);

void destroy_ordered_array(ordered_array_t *array);
void insert_ordered_array(type_t item, ordered_array_t *array);
type_t lookup_ordered_array(u64int i, ordered_array_t *array);
void remove_ordered_array(u64int i, ordered_array_t *array);

#endif // ORDERED_ARRAY_H
