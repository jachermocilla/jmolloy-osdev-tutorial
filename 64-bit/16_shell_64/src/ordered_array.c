// ordered_array.c -- Implementation for creating, inserting and deleting
//                    from ordered arrays.
//                    Written for JamesM's kernel development tutorials.
//                    Ported to x86-64.

#include "ordered_array.h"
#include "kheap.h"

s8int standard_lessthan_predicate(type_t a, type_t b)
{
    return (a < b) ? 1 : 0;
}

ordered_array_t create_ordered_array(u64int max_size, lessthan_predicate_t less_than)
{
    ordered_array_t to_ret;
    to_ret.array = (void *)kmalloc(max_size * sizeof(type_t));
    memset((u8int *)to_ret.array, 0, max_size * sizeof(type_t));
    to_ret.size = 0;
    to_ret.max_size = max_size;
    to_ret.less_than = less_than;
    return to_ret;
}

ordered_array_t place_ordered_array(void *addr, u64int max_size, lessthan_predicate_t less_than)
{
    ordered_array_t to_ret;
    to_ret.array = (type_t *)addr;
    memset((u8int *)to_ret.array, 0, max_size * sizeof(type_t));
    to_ret.size = 0;
    to_ret.max_size = max_size;
    to_ret.less_than = less_than;
    return to_ret;
}

void destroy_ordered_array(ordered_array_t *array)
{
//    kfree(array->array);
}

void insert_ordered_array(type_t item, ordered_array_t *array)
{
    ASSERT(array->less_than);
    ASSERT(array->size < array->max_size);   // the tutorial never checks this

    u64int iterator = 0;
    while (iterator < array->size && array->less_than(array->array[iterator], item))
        iterator++;

    if (iterator == array->size)     // just add at the end of the array.
    {
        array->array[array->size++] = item;
        return;
    }

    // Shift everything from `iterator` up one slot, then drop the item in.
    //
    // The tutorial reads array->array[iterator] one slot past the end on its
    // final iteration. Harmless, but it reads uninitialised memory. Walk
    // downwards instead and the question does not arise.
    for (u64int i = array->size; i > iterator; i--)
        array->array[i] = array->array[i-1];
    array->array[iterator] = item;
    array->size++;
}

type_t lookup_ordered_array(u64int i, ordered_array_t *array)
{
    ASSERT(i < array->size);
    return array->array[i];
}

void remove_ordered_array(u64int i, ordered_array_t *array)
{
    // The tutorial's loop runs while (i < size) and reads array[i+1], so on the
    // last iteration it reads one element past the end. Stop one earlier.
    while (i < array->size - 1)
    {
        array->array[i] = array->array[i+1];
        i++;
    }
    array->size--;
}
