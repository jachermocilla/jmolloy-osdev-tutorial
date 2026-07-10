// paging.c -- Defines the interface for and structures relating to paging.
//             Written for JamesM's kernel development tutorials.
//             Redesigned for x86-64.

#include "paging.h"
#include "kheap.h"
#include "monitor.h"
#include "common.h"

// The kernel's page tables, and the set currently loaded into CR3.
page_table_t *kernel_pml4 = 0;
page_table_t *current_pml4 = 0;

// A bitset of frames -- used or free.
//
// The tutorial declares this `u32int *`. We use u64int so each word of the
// bitset holds 64 bits, which is what the machine wants anyway.
u64int *frames;
u64int nframes;

// Defined in kheap.c
extern u64int placement_address;

// Macros used in the bitset algorithms.
#define BITS_PER_WORD (8 * sizeof(u64int))
#define INDEX_FROM_BIT(a)  ((a) / BITS_PER_WORD)
#define OFFSET_FROM_BIT(a) ((a) % BITS_PER_WORD)

// Static function to set a bit in the frames bitset
static void set_frame(u64int frame_addr)
{
    u64int frame = frame_addr / 0x1000;
    u64int idx = INDEX_FROM_BIT(frame);
    u64int off = OFFSET_FROM_BIT(frame);
    frames[idx] |= (0x1UL << off);
}

// Static function to clear a bit in the frames bitset
static void clear_frame(u64int frame_addr)
{
    u64int frame = frame_addr / 0x1000;
    u64int idx = INDEX_FROM_BIT(frame);
    u64int off = OFFSET_FROM_BIT(frame);
    frames[idx] &= ~(0x1UL << off);
}

// Static function to find the first free frame.
static u64int first_frame()
{
    for (u64int i = 0; i < INDEX_FROM_BIT(nframes); i++)
    {
        if (frames[i] != 0xFFFFFFFFFFFFFFFFUL)   // nothing free here, skip
        {
            for (u64int j = 0; j < BITS_PER_WORD; j++)
            {
                if (!(frames[i] & (0x1UL << j)))
                {
                    return i * BITS_PER_WORD + j;
                }
            }
        }
    }
    // The tutorial's version simply falls off the end here, returning garbage.
    // alloc_frame() checks for this sentinel.
    return (u64int)-1;
}

// Function to allocate a frame.
void alloc_frame(page_t *page, int is_kernel, int is_writeable)
{
    // The tutorial tests `page->frame != 0`, which cannot distinguish "already
    // mapped to frame 0" from "not mapped". `present` is the right question.
    if (page->present)
    {
        return;
    }

    u64int idx = first_frame();
    if (idx == (u64int)-1)
    {
        PANIC("No free frames!");
    }
    set_frame(idx * 0x1000);
    page->present = 1;
    page->rw      = is_writeable ? 1 : 0;
    page->user    = is_kernel ? 0 : 1;
    page->frame   = idx;
}

// Function to deallocate a frame.
void free_frame(page_t *page)
{
    if (!page->present)
    {
        return;
    }
    // The tutorial passes the frame *index* to clear_frame(), which then divides
    // by 0x1000 again. Multiply back up to an address first.
    clear_frame(page->frame * 0x1000);
    page->present = 0;
    page->frame   = 0;
}

void switch_page_directory(page_table_t *pml4)
{
    current_pml4 = pml4;
    // CR3 wants a *physical* address. Every table we allocate comes from the
    // placement allocator, which lives inside the identity-mapped low region,
    // so the virtual address we hold is already the physical one. The moment
    // that stops being true -- when you move the kernel to the higher half --
    // this line needs a virt_to_phys().
    asm volatile("mov %0, %%cr3" :: "r"(pml4) : "memory");
}

// Follow one entry down to the next level, optionally creating the table.
//
// This little helper is the whole reason the 64-bit version is *simpler* than
// the 32-bit one. The tutorial's page_directory_t carries two parallel arrays,
// `tables[]` (virtual pointers) and `tablesPhysical[]` (what the CPU reads),
// because it needs both views of the same table. With four levels that would
// mean four pairs of arrays. Instead we store only what the hardware stores --
// the physical frame number -- and rely on the identity map to dereference it.
static page_table_t *next_table(page_t *entry, int make)
{
    if (!entry->present)
    {
        if (!make)
        {
            return 0;
        }

        u64int phys;
        page_table_t *table = (page_table_t *)kmalloc_ap(sizeof(page_table_t), &phys);
        memset((u8int *)table, 0, sizeof(page_table_t));

        // Permissions are ANDed together down the walk, so an intermediate
        // entry must be at least as permissive as the leaf below it. Leave the
        // real access decision to the PT entry, in alloc_frame().
        entry->present = 1;
        entry->rw      = 1;
        entry->user    = 1;
        entry->frame   = phys >> 12;
    }

    // Identity map: physical address == virtual address.
    return (page_table_t *)((u64int)entry->frame << 12);
}

page_t *get_page(u64int address, int make, page_table_t *pml4)
{
    page_table_t *pdpt = next_table(&pml4->entries[PML4_INDEX(address)], make);
    if (!pdpt) return 0;

    page_table_t *pd = next_table(&pdpt->entries[PDPT_INDEX(address)], make);
    if (!pd) return 0;

    page_table_t *pt = next_table(&pd->entries[PD_INDEX(address)], make);
    if (!pt) return 0;

    return &pt->entries[PT_INDEX(address)];
}

void initialise_paging()
{
    // The size of physical memory. For the moment we assume it is 16MB big.
    u64int mem_end_page = 0x1000000;

    nframes = mem_end_page / 0x1000;

    // nframes is a count of *bits*. kmalloc takes a count of *bytes*.
    //
    // The tutorial writes kmalloc(INDEX_FROM_BIT(nframes)), which is
    // nframes/32 -- the number of 32-bit *words* -- and so under-allocates the
    // bitset by a factor of four. It survives only because the frames beyond
    // the first thousand are never touched in this chapter.
    frames = (u64int *)kmalloc(nframes / 8);
    memset((u8int *)frames, 0, nframes / 8);

    // Let's make a page directory.
    kernel_pml4 = (page_table_t *)kmalloc_a(sizeof(page_table_t));
    memset((u8int *)kernel_pml4, 0, sizeof(page_table_t));
    current_pml4 = kernel_pml4;

    // We need to identity map (phys addr = virt addr) from 0x0 to the end of
    // used memory, so we can access this transparently, as if paging wasn't
    // enabled. Inside the loop body we actually change placement_address by
    // calling kmalloc(). A while loop causes this to be computed on-the-fly
    // rather than once at the start.
    //
    // The extra page of slack covers the last table allocated on the final
    // iteration, which pushes placement_address past `i` after the test.
    u64int i = 0;
    while (i < placement_address + 0x1000)
    {
        // Supervisor-only, writeable. (The tutorial passes is_kernel=0 here,
        // which sets the user bit and leaves the whole kernel readable from
        // ring 3 -- the opposite of what its comment claims.)
        alloc_frame(get_page(i, 1, kernel_pml4), 1, 1);
        i += 0x1000;
    }

    // Before we enable paging, we must register our page fault handler.
    register_interrupt_handler(14, page_fault);

    // Now, enable paging!
    //
    // Except: paging is *already* enabled. It has been since boot.s turned on
    // CR0.PG to reach long mode in chapter 3. All we are doing here is taking
    // ownership -- swapping boot.s's coarse 2 MiB identity map for our own
    // 4 KiB one, which we can actually manipulate at page granularity.
    switch_page_directory(kernel_pml4);
}

void page_fault(registers_t *regs)
{
    // A page fault has occurred.
    // The faulting address is stored in the CR2 register.
    u64int faulting_address;
    asm volatile("mov %%cr2, %0" : "=r" (faulting_address));

    // The error code gives us details of what happened.
    int present  =   regs->err_code & 0x1;   // Page present?
    int rw       =   regs->err_code & 0x2;   // Write operation?
    int us       =   regs->err_code & 0x4;   // Processor was in user-mode?
    int reserved =   regs->err_code & 0x8;   // Overwritten CPU-reserved bits?
    int id       =   regs->err_code & 0x10;  // Caused by an instruction fetch?

    // Output an error message.
    monitor_write("Page fault! ( ");
    if (!present) { monitor_write("not-present "); }
    if (rw)       { monitor_write("write "); }
    if (us)       { monitor_write("user-mode "); }
    if (reserved) { monitor_write("reserved "); }
    if (id)       { monitor_write("instruction-fetch "); }
    monitor_write(") at ");
    monitor_write_hex64(faulting_address);
    monitor_write("\n");
    PANIC("Page fault");
}
