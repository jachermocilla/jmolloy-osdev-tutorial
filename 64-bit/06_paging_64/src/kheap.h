// kheap.h -- Interface for kernel heap functions, also provides
//            a placement malloc() for use before the heap is
//            initialised.
//            Written for JamesM's kernel development tutorials.
//            Ported to x86-64.

#ifndef KHEAP_H
#define KHEAP_H

#include "common.h"

/**
   Allocate a chunk of memory, sz in size. If align == 1,
   the chunk must be page-aligned. If phys != 0, the physical
   location of the allocated chunk will be stored into phys.

   This is the internal version of kmalloc. More user-friendly
   parameter representations are available in kmalloc, kmalloc_a,
   kmalloc_ap, kmalloc_p.
**/
u64int kmalloc_int(u64int sz, int align, u64int *phys);

/**
   Allocate a chunk of memory, sz in size. The chunk must be
   page aligned.
**/
u64int kmalloc_a(u64int sz);

/**
   Allocate a chunk of memory, sz in size. The physical address
   is returned in phys. Phys MUST be a valid pointer to u64int!
**/
u64int kmalloc_p(u64int sz, u64int *phys);

/**
   Allocate a chunk of memory, sz in size. The physical address
   is returned in phys. It must be page-aligned.
**/
u64int kmalloc_ap(u64int sz, u64int *phys);

/**
   General allocation function.
**/
u64int kmalloc(u64int sz);

#endif // KHEAP_H
