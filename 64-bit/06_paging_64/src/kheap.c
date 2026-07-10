// kheap.c -- Kernel heap functions, also provides
//            a placement malloc() for use before the heap is
//            initialised.
//            Written for JamesM's kernel development tutorials.
//            Ported to x86-64.

#include "kheap.h"

// end is defined in the linker script.
extern u64int end;
u64int placement_address = (u64int)&end;

u64int kmalloc_int(u64int sz, int align, u64int *phys)
{
    // This will eventually call malloc() on the kernel heap.
    // For now, though, we just assign memory at placement_address
    // and increment it by sz. Even when we've coded our kernel
    // heap, this will be useful for use before the heap is initialised.
    //
    // NOTE: the tutorial writes `placement_address & 0xFFFFF000` here, which
    // asks "is the address above 4 KiB?" -- true for essentially every address,
    // so it page-aligns unconditionally and wastes up to 4 KiB per call. The
    // question we actually want to ask is "are the low 12 bits nonzero?".
    if (align == 1 && (placement_address & 0xFFF))
    {
        // Align the placement address;
        placement_address &= ~0xFFFUL;
        placement_address += 0x1000;
    }
    if (phys)
    {
        // Everything the placement allocator hands out lives in the
        // identity-mapped low region, so virtual == physical.
        *phys = placement_address;
    }
    u64int tmp = placement_address;
    placement_address += sz;
    return tmp;
}

u64int kmalloc_a(u64int sz)
{
    return kmalloc_int(sz, 1, 0);
}

u64int kmalloc_p(u64int sz, u64int *phys)
{
    return kmalloc_int(sz, 0, phys);
}

u64int kmalloc_ap(u64int sz, u64int *phys)
{
    return kmalloc_int(sz, 1, phys);
}

u64int kmalloc(u64int sz)
{
    return kmalloc_int(sz, 0, 0);
}
