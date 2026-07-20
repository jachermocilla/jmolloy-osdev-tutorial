// paging.h -- Defines the interface for and structures relating to paging.
//             Written for JamesM's kernel development tutorials.
//             Redesigned for x86-64.

#ifndef PAGING_H
#define PAGING_H

#include "common.h"
#include "isr.h"

// A single page-table entry. Sixty-four bits wide, not thirty-two.
//
// The same 64-bit layout is used at *every* level of the hierarchy. An entry in
// the PML4 points at a PDPT, an entry in the PDPT points at a PD, and so on;
// only an entry in the bottom-level PT actually names a page of memory. This is
// why we get away with one struct instead of four.
typedef struct page
{
    u64int present    : 1;   // Page present in memory
    u64int rw         : 1;   // Read-only if clear, readwrite if set
    u64int user       : 1;   // Supervisor level only if clear
    u64int pwt        : 1;   // Write-through caching
    u64int pcd        : 1;   // Cache disabled
    u64int accessed   : 1;   // Has the page been accessed since last refresh?
    u64int dirty      : 1;   // Has the page been written to since last refresh?
    u64int pat        : 1;   // Page Attribute Table (and PS on non-leaf levels)
    u64int global     : 1;   // Do not flush from the TLB on a CR3 reload
    u64int avail      : 3;   // Available for kernel use
    u64int frame      : 40;  // Physical frame number: the address, shifted right 12
    u64int reserved   : 11;  // Must be zero
    u64int nx         : 1;   // No-execute (only honoured if EFER.NXE is set)
} __attribute__((packed)) page_t;

// Any one of the four levels: PML4, PDPT, PD, or PT.
//
// 512 entries of 8 bytes = 4096 bytes, exactly one page. In 32-bit mode a table
// held 1024 entries of 4 bytes -- also one page. The page size did not change;
// the entries got twice as wide, so half as many fit, so it takes twice as many
// levels to cover the address space. Two levels became four.
typedef struct page_table
{
    page_t entries[512];
} page_table_t;

// A 64-bit virtual address is chopped into five fields:
//
//   63       48 47    39 38    30 29    21 20    12 11         0
//  +-----------+--------+--------+--------+--------+-----------+
//  | sign ext. |  PML4  |  PDPT  |   PD   |   PT   |  offset   |
//  +-----------+--------+--------+--------+--------+-----------+
//         16        9        9        9        9        12
//
// Nine bits per level, because 2^9 = 512 entries per table.
#define PML4_INDEX(a) (((a) >> 39) & 0x1FF)
#define PDPT_INDEX(a) (((a) >> 30) & 0x1FF)
#define PD_INDEX(a)   (((a) >> 21) & 0x1FF)
#define PT_INDEX(a)   (((a) >> 12) & 0x1FF)

/**
  Sets up the environment, page directories etc and enables paging.
**/
void initialise_paging();

/**
  Causes the specified page table to be loaded into the CR3 register.
**/
void switch_page_directory(page_table_t *new_pml4);

/**
  Retrieves a pointer to the page-table entry for the given address. If make is
  1, any missing intermediate tables are created along the way.
**/
page_t *get_page(u64int address, int make, page_table_t *pml4);

/**
  Handler for page faults.
**/
registers_t *page_fault(registers_t *regs);

/**
  Function to allocate a frame.
**/
void alloc_frame(page_t *page, int is_kernel, int is_writeable);

/**
  Function to deallocate a frame.
**/
void free_frame(page_t *page);

/**
  Make an existing kernel mapping readable (and optionally writable) from ring 3.
**/
void make_page_user(u64int address, int writeable);

/**
  Map a fresh, zeroed, user-accessible, writable page at `address`.
**/
void map_user_page(u64int address);

/**
  Clone an address space. Returns the *physical* address of a new PML4.

  Kernel mappings (the whole upper canonical half) are shared by reference, so a
  page the kernel maps after this call is visible in every address space at
  once. Everything in the lower half is walked page by page: a supervisor page
  has its frame shared, a user page is copied into a fresh frame. That split --
  share the kernel, copy the user -- is the whole of what fork() needs.
**/
u64int clone_address_space(u64int src_pml4_phys);

/**
  Build a fresh address space that shares the kernel but carries no user pages.

  Like clone_address_space it shares the whole upper canonical half by reference
  and shares lower-half supervisor frames, but where clone copies user pages this
  *skips* them. The result is a valid kernel view with an empty user region --
  exactly what exec() wants before it loads a new program into the space.
**/
u64int new_kernel_address_space(u64int src_pml4_phys);

/**
  Tear down an address space created by clone_address_space: free every user
  frame and every page table it privately owns, but touch neither the shared
  kernel frames nor the shared upper-half tables.
**/
void free_address_space(u64int pml4_phys);

/**
  Load a PML4 by physical address. Unlike switch_page_directory, this does not
  assume the table's virtual address equals its physical one -- address-space
  tables built by clone_address_space live in the direct map and are named by
  their physical address.
**/
void switch_pml4_phys(u64int pml4_phys);

// The physical address of the currently-loaded PML4.
extern u64int current_pml4_phys;

/**
  Invalidate a single page's TLB entry. Clearing a page-table entry does not
  evict the translation the CPU has already cached.
**/
static inline void invlpg(u64int addr)
{
    asm volatile("invlpg (%0)" :: "r"(addr) : "memory");
}

// The size of physical memory we manage, and hence the extent of the direct map.
#define PHYS_MEM_SIZE 0x1000000UL      // 16 MiB

#endif // PAGING_H
