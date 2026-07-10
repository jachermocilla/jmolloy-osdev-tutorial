// kheap.c -- Kernel heap functions, also provides
//            a placement malloc() for use before the heap is
//            initialised.
//            Written for JamesM's kernel development tutorials.
//            Ported to x86-64.

#include "kheap.h"
#include "paging.h"

// end is defined in the linker script.
extern u64int end;
u64int placement_address = (u64int)&end;
extern page_table_t *kernel_pml4;
heap_t *kheap = 0;

u64int kmalloc_int(u64int sz, int align, u64int *phys)
{
    if (kheap != 0)
    {
        void *addr = alloc(sz, (u8int)align, kheap);
        if (phys != 0)
        {
            // The heap lives at KHEAP_START, which is NOT identity-mapped, so
            // we have to ask the page tables where it really is.
            //
            // The tutorial writes:
            //     *phys = page->frame*0x1000 + (u32int)addr&0xFFF;
            // `+` binds tighter than `&`, so that is
            //     (page->frame*0x1000 + addr) & 0xFFF
            // -- a twelve-bit number. Parenthesise the offset.
            page_t *page = get_page((u64int)addr, 0, kernel_pml4);
            *phys = ((u64int)page->frame * 0x1000) + ((u64int)addr & 0xFFF);
        }
        return (u64int)addr;
    }
    else
    {
        if (align == 1 && (placement_address & 0xFFF))
        {
            // Align the placement address;
            placement_address &= ~0xFFFUL;
            placement_address += 0x1000;
        }
        if (phys)
        {
            // Before the heap exists, everything comes from the identity-mapped
            // low region, so virtual == physical.
            *phys = placement_address;
        }
        u64int tmp = placement_address;
        placement_address += sz;
        return tmp;
    }
}

void kfree(void *p)
{
    free(p, kheap);
}

u64int kmalloc_a(u64int sz)  { return kmalloc_int(sz, 1, 0); }
u64int kmalloc_p(u64int sz, u64int *phys)  { return kmalloc_int(sz, 0, phys); }
u64int kmalloc_ap(u64int sz, u64int *phys) { return kmalloc_int(sz, 1, phys); }
u64int kmalloc(u64int sz)    { return kmalloc_int(sz, 0, 0); }

// Round up to the next page boundary.
//
// The tutorial writes `if (new_size&0xFFFFF000 != 0)` in three separate places.
// `!=` binds tighter than `&`, so `0xFFFFF000 != 0` evaluates to 1 and the test
// becomes `new_size & 1` -- "is the size odd?". It page-aligns on odd sizes and
// leaves even ones alone. One helper, written once, correctly.
static u64int page_align_up(u64int addr)
{
    if (addr & 0xFFF)
    {
        addr &= ~0xFFFUL;
        addr += 0x1000;
    }
    return addr;
}

static void expand(u64int new_size, heap_t *heap)
{
    // Sanity check.
    ASSERT(new_size > heap->end_address - heap->start_address);

    new_size = page_align_up(new_size);

    // Make sure we are not overreaching ourselves.
    ASSERT(heap->start_address + new_size <= heap->max_address);

    u64int old_size = heap->end_address - heap->start_address;

    u64int i = old_size;
    while (i < new_size)
    {
        alloc_frame(get_page(heap->start_address + i, 1, kernel_pml4),
                    (heap->supervisor) ? 1 : 0, (heap->readonly) ? 0 : 1);
        i += 0x1000;
    }
    heap->end_address = heap->start_address + new_size;
}

static u64int contract(u64int new_size, heap_t *heap)
{
    // Sanity check.
    ASSERT(new_size < heap->end_address - heap->start_address);

    // The tutorial writes `if (new_size&0x1000) { new_size &= 0x1000; ... }`,
    // which tests one bit and then throws away every other bit of the size.
    new_size = page_align_up(new_size);

    // Don't contract too far!
    if (new_size < HEAP_MIN_SIZE)
        new_size = HEAP_MIN_SIZE;

    u64int old_size = heap->end_address - heap->start_address;
    u64int i = old_size - 0x1000;
    while (new_size < i)
    {
        free_frame(get_page(heap->start_address + i, 0, kernel_pml4));
        // Removing a page-table entry does not evict the TLB. Without this the
        // CPU keeps happily using the cached translation for a freed page.
        invlpg(heap->start_address + i);
        i -= 0x1000;
    }

    heap->end_address = heap->start_address + new_size;
    return new_size;
}

static s64int find_smallest_hole(u64int size, u8int page_align, heap_t *heap)
{
    u64int iterator = 0;
    while (iterator < heap->index.size)
    {
        header_t *header = (header_t *)lookup_ordered_array(iterator, &heap->index);
        if (page_align > 0)
        {
            // Page-align the starting point of this header.
            u64int location = (u64int)header;
            s64int offset = 0;
            // Again: & 0xFFF, not & 0xFFFFF000.
            if ((location + sizeof(header_t)) & 0xFFF)
                offset = 0x1000 - (location + sizeof(header_t)) % 0x1000;
            s64int hole_size = (s64int)header->size - offset;
            if (hole_size >= (s64int)size)
                break;
        }
        else if (header->size >= size)
            break;
        iterator++;
    }

    if (iterator == heap->index.size)
        return -1;              // We got to the end and didn't find anything.
    else
        return (s64int)iterator;
}

static s8int header_t_less_than(void *a, void *b)
{
    return (((header_t *)a)->size < ((header_t *)b)->size) ? 1 : 0;
}

heap_t *create_heap(u64int start, u64int end_addr, u64int max, u8int supervisor, u8int readonly)
{
    heap_t *heap = (heap_t *)kmalloc(sizeof(heap_t));

    // All our assumptions are made on startAddress and endAddress being page-aligned.
    ASSERT(start % 0x1000 == 0);
    ASSERT(end_addr % 0x1000 == 0);

    // Initialise the index.
    heap->index = place_ordered_array((void *)start, HEAP_INDEX_SIZE, &header_t_less_than);

    // Shift the start address forward to resemble where we can start putting data.
    start += sizeof(type_t) * HEAP_INDEX_SIZE;
    start = page_align_up(start);

    heap->start_address = start;
    heap->end_address = end_addr;
    heap->max_address = max;
    heap->supervisor = supervisor;
    heap->readonly = readonly;

    // The index must not have eaten the entire heap. In 64-bit it very nearly
    // does; see the note on HEAP_INDEX_SIZE in kheap.h.
    ASSERT(start < end_addr);

    // We start off with one large hole in the index.
    header_t *hole = (header_t *)start;
    hole->size = end_addr - start;
    hole->magic = HEAP_MAGIC;
    hole->is_hole = 1;
    insert_ordered_array((void *)hole, &heap->index);

    return heap;
}

void *alloc(u64int size, u8int page_align, heap_t *heap)
{
    // Make sure we take the size of header/footer into account.
    u64int new_size = size + sizeof(header_t) + sizeof(footer_t);
    s64int iterator = find_smallest_hole(new_size, page_align, heap);

    if (iterator == -1)     // If we didn't find a suitable hole
    {
        u64int old_length = heap->end_address - heap->start_address;
        u64int old_end_address = heap->end_address;

        // We need to allocate some more space.
        expand(old_length + new_size, heap);
        u64int new_length = heap->end_address - heap->start_address;

        // Find the endmost header. (Not endmost in size, but in location).
        u64int it = 0;
        s64int idx = -1;
        u64int value = 0x0;
        while (it < heap->index.size)
        {
            u64int tmp = (u64int)lookup_ordered_array(it, &heap->index);
            if (tmp > value)
            {
                value = tmp;
                idx = (s64int)it;
            }
            it++;
        }

        // If we didn't find ANY headers, we need to add one.
        if (idx == -1)
        {
            header_t *header = (header_t *)old_end_address;
            header->magic = HEAP_MAGIC;
            header->size = new_length - old_length;
            header->is_hole = 1;
            footer_t *footer = (footer_t *)(old_end_address + header->size - sizeof(footer_t));
            footer->magic = HEAP_MAGIC;
            footer->header = header;
            insert_ordered_array((void *)header, &heap->index);
        }
        else
        {
            // The last header needs adjusting.
            header_t *header = lookup_ordered_array((u64int)idx, &heap->index);
            header->size += new_length - old_length;
            footer_t *footer = (footer_t *)((u64int)header + header->size - sizeof(footer_t));
            footer->header = header;
            footer->magic = HEAP_MAGIC;
        }
        // We now have enough space. Recurse, and call the function again.
        return alloc(size, page_align, heap);
    }

    header_t *orig_hole_header = (header_t *)lookup_ordered_array((u64int)iterator, &heap->index);
    u64int orig_hole_pos = (u64int)orig_hole_header;
    u64int orig_hole_size = orig_hole_header->size;

    // Should we split the hole we found into two parts?
    if (orig_hole_size - new_size < sizeof(header_t) + sizeof(footer_t))
    {
        size += orig_hole_size - new_size;
        new_size = orig_hole_size;
    }

    // If we need to page-align the data, do it now and make a new hole in front.
    // Once more: & 0xFFF.
    if (page_align && ((orig_hole_pos + sizeof(header_t)) & 0xFFF))
    {
        u64int new_location   = orig_hole_pos + 0x1000 - (orig_hole_pos & 0xFFF) - sizeof(header_t);
        header_t *hole_header = (header_t *)orig_hole_pos;
        hole_header->size     = 0x1000 - (orig_hole_pos & 0xFFF) - sizeof(header_t);
        hole_header->magic    = HEAP_MAGIC;
        hole_header->is_hole  = 1;
        footer_t *hole_footer = (footer_t *)(new_location - sizeof(footer_t));
        hole_footer->magic    = HEAP_MAGIC;
        hole_footer->header   = hole_header;
        orig_hole_pos         = new_location;
        orig_hole_size        = orig_hole_size - hole_header->size;
    }
    else
    {
        // Else we don't need this hole any more, delete it from the index.
        remove_ordered_array((u64int)iterator, &heap->index);
    }

    // Overwrite the original header...
    header_t *block_header = (header_t *)orig_hole_pos;
    block_header->magic    = HEAP_MAGIC;
    block_header->is_hole  = 0;
    block_header->size     = new_size;
    // ...And the footer
    footer_t *block_footer = (footer_t *)(orig_hole_pos + sizeof(header_t) + size);
    block_footer->magic    = HEAP_MAGIC;
    block_footer->header   = block_header;

    // We may need to write a new hole after the allocated block.
    // orig_hole_size and new_size are unsigned, so `a - b > 0` is `a != b`.
    // Say what we mean.
    if (orig_hole_size > new_size)
    {
        header_t *hole_header = (header_t *)(orig_hole_pos + sizeof(header_t) + size + sizeof(footer_t));
        hole_header->magic    = HEAP_MAGIC;
        hole_header->is_hole  = 1;
        hole_header->size     = orig_hole_size - new_size;
        footer_t *hole_footer = (footer_t *)((u64int)hole_header + orig_hole_size - new_size - sizeof(footer_t));
        if ((u64int)hole_footer < heap->end_address)
        {
            hole_footer->magic = HEAP_MAGIC;
            hole_footer->header = hole_header;
        }
        insert_ordered_array((void *)hole_header, &heap->index);
    }

    return (void *)((u64int)block_header + sizeof(header_t));
}

void free(void *p, heap_t *heap)
{
    // Exit gracefully for null pointers.
    if (p == 0)
        return;

    header_t *header = (header_t *)((u64int)p - sizeof(header_t));
    footer_t *footer = (footer_t *)((u64int)header + header->size - sizeof(footer_t));

    ASSERT(header->magic == HEAP_MAGIC);
    ASSERT(footer->magic == HEAP_MAGIC);

    header->is_hole = 1;

    // Do we want to add this header into the 'free holes' index?
    char do_add = 1;

    // Unify left. If the thing immediately to the left of us is a footer...
    footer_t *test_footer = (footer_t *)((u64int)header - sizeof(footer_t));
    if ((u64int)test_footer >= heap->start_address &&
        test_footer->magic == HEAP_MAGIC &&
        test_footer->header->is_hole == 1)
    {
        u64int cache_size = header->size;
        header = test_footer->header;
        footer->header = header;
        header->size += cache_size;
        do_add = 0;
    }

    // Unify right. If the thing immediately to the right of us is a header...
    header_t *test_header = (header_t *)((u64int)footer + sizeof(footer_t));
    if ((u64int)test_header < heap->end_address &&
        test_header->magic == HEAP_MAGIC &&
        test_header->is_hole)
    {
        header->size += test_header->size;
        test_footer = (footer_t *)((u64int)test_header + test_header->size - sizeof(footer_t));
        footer = test_footer;

        // Find and remove this header from the index.
        u64int iterator = 0;
        while ((iterator < heap->index.size) &&
               (lookup_ordered_array(iterator, &heap->index) != (void *)test_header))
            iterator++;

        ASSERT(iterator < heap->index.size);
        remove_ordered_array(iterator, &heap->index);
    }

    // If the footer location is the end address, we can contract.
    if ((u64int)footer + sizeof(footer_t) == heap->end_address)
    {
        u64int old_length = heap->end_address - heap->start_address;
        u64int new_length = contract((u64int)header - heap->start_address, heap);

        // The tutorial writes `if (header->size - (old_length-new_length) > 0)`.
        // Both operands are unsigned, so the subtraction wraps rather than going
        // negative and the test is always true -- the `else` branch is dead code.
        if (header->size > old_length - new_length)
        {
            // We will still exist, so resize us.
            header->size -= old_length - new_length;
            footer = (footer_t *)((u64int)header + header->size - sizeof(footer_t));
            footer->magic = HEAP_MAGIC;
            footer->header = header;
        }
        else
        {
            // We will no longer exist. Remove us from the index.
            //
            // The tutorial searches the index for `test_header`, which is only
            // assigned if the unify-right branch above ran. Otherwise it is a
            // stale pointer and the search finds nothing. We want `header`.
            u64int iterator = 0;
            while ((iterator < heap->index.size) &&
                   (lookup_ordered_array(iterator, &heap->index) != (void *)header))
                iterator++;
            if (iterator < heap->index.size)
                remove_ordered_array(iterator, &heap->index);
            do_add = 0;
        }
    }

    if (do_add == 1)
        insert_ordered_array((void *)header, &heap->index);
}
