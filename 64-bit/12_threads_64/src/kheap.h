// kheap.h -- Interface for kernel heap functions, also provides
//            a placement malloc() for use before the heap is
//            initialised.
//            Written for JamesM's kernel development tutorials.
//            Ported to x86-64.

#ifndef KHEAP_H
#define KHEAP_H

#include "common.h"
#include "ordered_array.h"

// The kernel heap lives in the canonical higher half.
//
// The 32-bit tutorial puts it at 0xC0000000, which is simply "somewhere above
// the identity map" in a 4 GiB address space. With 256 TiB to play with, the
// conventional home for kernel-only mappings is the bottom of the upper
// canonical range: bit 47 set, and bits 48-63 sign-extended to match.
//
// This is the first address in this tutorial where virtual != physical, and
// the first that does not fit in 32 bits. GCC emits a `movabs` for it.
#define KHEAP_START         0xFFFF800000000000UL
#define KHEAP_INITIAL_SIZE  0x100000        // 1 MiB
#define KHEAP_MAX           (KHEAP_START + 0x1000000UL)

// The index is an array of `type_t`, which is `void *`.
//
// In 32-bit that is 4 bytes, so 0x20000 entries cost 0x80000 -- half the
// initial heap, leaving half for data. In 64-bit a pointer is 8 bytes, so the
// same 0x20000 entries would cost 0x100000: the *entire* initial heap. The
// first hole would have size zero and the heap would be born full.
//
// Halve the entry count to keep the same byte cost and the same 50/50 split.
#define HEAP_INDEX_SIZE   0x10000
#define HEAP_MAGIC        0x123890AB
#define HEAP_MIN_SIZE     0x70000

/**
   Size information for a hole/block.

   sizeof(header_t) is 16 in 64-bit (was 12), and sizeof(footer_t) is 16
   (was 8), because `size` widened and `header` is now an 8-byte pointer that
   the compiler must align to 8. Per-allocation overhead therefore goes from
   20 bytes to 32. Nothing depends on the exact numbers -- the code uses
   sizeof() throughout -- but it is worth knowing where your heap went.
**/
typedef struct
{
    u32int magic;    // Magic number, used for error checking and identification.
    u8int  is_hole;  // 1 if this is a hole. 0 if this is a block.
    u64int size;     // size of the block, including the end footer.
} header_t;

typedef struct
{
    u32int magic;     // Magic number, same as in header_t.
    header_t *header; // Pointer to the block header.
} footer_t;

typedef struct
{
    ordered_array_t index;
    u64int start_address; // The start of our allocated space.
    u64int end_address;   // The end of our allocated space. May be expanded up to max_address.
    u64int max_address;   // The maximum address the heap can be expanded to.
    u8int supervisor;     // Should extra pages requested by us be mapped as supervisor-only?
    u8int readonly;       // Should extra pages requested by us be mapped as read-only?
} heap_t;

heap_t *create_heap(u64int start, u64int end, u64int max, u8int supervisor, u8int readonly);
void *alloc(u64int size, u8int page_align, heap_t *heap);
void free(void *p, heap_t *heap);

u64int kmalloc_int(u64int sz, int align, u64int *phys);
u64int kmalloc_a(u64int sz);
u64int kmalloc_p(u64int sz, u64int *phys);
u64int kmalloc_ap(u64int sz, u64int *phys);
u64int kmalloc(u64int sz);
void kfree(void *p);

#endif // KHEAP_H
