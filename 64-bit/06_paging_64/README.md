# Chapter 6 (Paging) — 64-bit Port

This is JamesM's chapter 6, redesigned for x86-64. It builds a four-level page
table, a physical frame allocator, and a page-fault handler.

**Read `05_irq_ipt_64/README.md` first.**

This is the first chapter that is a *redesign* rather than a translation. Do not
try to port the original `paging.c` line by line — you will fight it and lose.
Read this document, then write the file.

```
06_paging_64/src/
├── boot.s                ← unchanged since chapter 3
├── link.ld, gdt.s        ← unchanged
├── interrupt.s, isr.c/.h ← unchanged since chapter 5
├── descriptor_tables.*   ← unchanged
├── timer.c/.h            ← unchanged
├── common.c/.h           ← added panic() / PANIC / ASSERT
├── monitor.c/.h          ← added monitor_write_hex64()
├── kheap.c/.h            ← placement allocator; u32int -> u64int
├── paging.h              ← rewritten: 64-bit entries, four levels
├── paging.c              ← rewritten
└── main.c                ← same demo, 64-bit pointer
```

---

## Building and running

```bash
cd 06_paging_64/src
make
qemu-system-x86_64 -kernel kernel
```

Expected output — the demo deliberately dereferences an unmapped address:

```
Hello, 64-bit paging world!
recieved interrupt: 14
Page fault! ( not-present ) at 0xa0000000
PANIC(Page fault) at paging.c:238
```

---

## Start here: paging is already on

The single most disorienting thing about this chapter is that its title is a
lie. `initialise_paging()` does not initialise paging.

Paging has been enabled since chapter 3. It *had* to be — long mode does not
exist without it. Look again at the `boot.s` you wrote back then: it built a
PML4, a PDPT, and a PD; filled the PD with 512 entries of `0x83`; loaded `CR3`;
and set `CR0.PG`. That gave you a **1 GiB identity map made of 2 MiB pages**,
and it is what your kernel has been running on for three chapters.

So what is this chapter for?

Two things. First, **granularity**: 2 MiB pages are useless for a page
allocator. You cannot hand out, protect, or fault on anything smaller than
2 MiB. We need 4 KiB pages, which means adding a fourth level (the PT) that
`boot.s` never built.

Second, **ownership**: `boot.s`'s tables are three anonymous blobs in `.bss`
with no C-side representation. We want tables we allocated ourselves, can walk,
and can modify at runtime.

So `initialise_paging()` builds a new, finer, 4 KiB-granular identity map, and
then swaps it in by writing our `CR3`. You can watch the handover happen — the
`CR3` value changes from `0x105000` (`boot.s`'s `pml4` symbol in `.bss`) to
whatever the placement allocator handed us:

```
$ nm kernel64.elf | grep pml4
0000000000105000 b pml4          <- boot.s's

(qemu) info registers
CR3=000000000010e000               <- ours
```

Everything else in this chapter follows from that.

---

## Four levels, not two

A 32-bit page directory has 1024 entries of 4 bytes = one 4 KiB page. Ten bits
of index, ten bits of index, twelve bits of offset: 32 bits, two levels, done.

On x86-64 the entries are **8 bytes** wide. A page still holds 4096 bytes, so a
table now holds only **512** entries — nine bits of index. The page didn't
change size; the entries did, so half as many fit, so you need twice as many
levels to cover a comparable address space.

```
 63       48 47    39 38    30 29    21 20    12 11         0
+-----------+--------+--------+--------+--------+-----------+
| sign ext. |  PML4  |  PDPT  |   PD   |   PT   |  offset   |
+-----------+--------+--------+--------+--------+-----------+
       16        9        9        9        9        12
```

Four indexes of nine bits, plus a twelve-bit offset, is 48 bits. The top sixteen
bits must be a sign-extension of bit 47 — the hardware enforces this, and an
address that violates it is called *non-canonical* and faults immediately. So
x86-64 gives you a 48-bit address space (256 TiB) with a hole in the middle, not
a 64-bit one.

```c
#define PML4_INDEX(a) (((a) >> 39) & 0x1FF)
#define PDPT_INDEX(a) (((a) >> 30) & 0x1FF)
#define PD_INDEX(a)   (((a) >> 21) & 0x1FF)
#define PT_INDEX(a)   (((a) >> 12) & 0x1FF)
```

Crucially, **all four levels have the same 64-bit entry format**. A PML4 entry
points at a PDPT, a PDPT entry points at a PD, a PD entry points at a PT, and
only a PT entry names a page of actual memory. But the bits mean the same
things. So we need exactly one struct:

```c
typedef struct page_table { page_t entries[512]; } page_table_t;
```

...used at all four levels. That's a simplification the 32-bit tutorial doesn't
get to enjoy, because a 32-bit `page_directory_entry` and `page_table_entry`
genuinely differ.

---

## The parallel-arrays problem, and why it disappears

Here is the 32-bit tutorial's central data structure:

```c
typedef struct page_directory
{
    page_table_t *tables[1024];       // virtual pointers, for us
    u32int tablesPhysical[1024];      // physical addresses, for the CPU
    u32int physicalAddr;
} page_directory_t;
```

Two parallel arrays holding the same 1024 tables, twice. Why? Because the MMU
reads physical addresses out of the table, but C code walking the table needs
virtual ones. In the 32-bit tutorial these happen to be equal (everything is
identity-mapped) — but the author kept both because chapter 7 moves the heap
somewhere they *aren't* equal.

Now try to generalise that to four levels. You'd need a parallel array at every
level: `pml4_virt[512]` / `pml4_phys[512]`, and a `pdpt_virt`/`pdpt_phys` per
PML4 entry, and so on. It metastasises. **This is the part that does not port.**

The fix is to stop storing anything twice. Store only what the hardware stores —
the physical frame number, in the entry itself — and convert to a virtual
pointer at the moment of use:

```c
static page_table_t *next_table(page_t *entry, int make)
{
    if (!entry->present)
    {
        if (!make) return 0;
        u64int phys;
        page_table_t *table = (page_table_t *)kmalloc_ap(sizeof(page_table_t), &phys);
        memset((u8int *)table, 0, sizeof(page_table_t));
        entry->present = 1;
        entry->rw      = 1;
        entry->user    = 1;
        entry->frame   = phys >> 12;
    }
    // Identity map: physical address == virtual address.
    return (page_table_t *)((u64int)entry->frame << 12);
}
```

And then the four-level walk is four lines:

```c
page_t *get_page(u64int address, int make, page_table_t *pml4)
{
    page_table_t *pdpt = next_table(&pml4->entries[PML4_INDEX(address)], make);
    if (!pdpt) return 0;
    page_table_t *pd   = next_table(&pdpt->entries[PDPT_INDEX(address)], make);
    if (!pd) return 0;
    page_table_t *pt   = next_table(&pd->entries[PD_INDEX(address)], make);
    if (!pt) return 0;
    return &pt->entries[PT_INDEX(address)];
}
```

Every page table we ever allocate comes from the placement allocator, which
lives inside the identity-mapped low region. So `phys == virt` and that final
cast is legitimate.

**Be honest with yourself about this.** It is a load-bearing assumption, not a
fact about x86-64. The one line that will break first is in
`switch_page_directory`:

```c
asm volatile("mov %0, %%cr3" :: "r"(pml4) : "memory");
```

`CR3` demands a *physical* address, and we hand it a *virtual* pointer. That is
correct today and wrong the moment the kernel moves to the higher half at
`0xFFFFFFFF80000000`. When that day comes you'll need a real `virt_to_phys()`,
and the idiom to reach for is **recursive mapping**: point PML4 entry 510 at the
PML4 itself, and every page table in the system becomes addressable at a
computable virtual address. Look it up before you need it.

---

## The permission bits, and a comment that lies

The identity-map loop in the tutorial reads:

```c
// Kernel code is readable but not writeable from userspace.
alloc_frame(get_page(i, 1, kernel_directory), 0, 1);
```

Check the signature: `alloc_frame(page, is_kernel, is_writeable)`. The second
argument is `0`, so `is_kernel` is false, so:

```c
page->user = (is_kernel == 1) ? 0 : 1;   // user = 1
```

The kernel's entire identity map is marked **user-accessible**. The comment says
the opposite of what the code does. Once you reach ring 3 in chapter 10, any
user process can read every byte of kernel memory.

This port passes `is_kernel = 1`. You can see the difference in QEMU's monitor:

```
(qemu) info mem
0000000000000000-0000000000113000 0000000000113000 -rw     <- ours
0000000000000000-0000000000113000 0000000000113000 urw     <- the tutorial's
```

Note that `next_table()` still sets `user = 1` on the *intermediate* entries.
That is correct and not a contradiction: the CPU **ANDs** the permission bits
along the entire walk. An intermediate entry must be at least as permissive as
the leaf beneath it, and the leaf makes the real decision.

---

## Bugs fixed from the original

These are all present in the 32-bit code and none of them are 64-bit issues.
They are worth fixing because you are about to build a heap on top of this.

**The frames bitset is four times too small.**

```c
frames = (u32int *)kmalloc(INDEX_FROM_BIT(nframes));   // tutorial
```

`nframes` is a count of *bits*. `INDEX_FROM_BIT(nframes)` is `nframes/32` — a
count of 32-bit *words*. `kmalloc` wants *bytes*. For 16 MiB of RAM you need
`4096/8 = 512` bytes and it allocates 128. The kernel survives only because the
frames past the first thousand are never touched in this chapter. Correct is
`kmalloc(nframes / 8)`.

**`first_frame()` has no return on the failure path.** It falls off the end of a
non-`void` function, returning whatever happens to be in `RAX`. `alloc_frame`
then tests `if (idx == (u32int)-1)`, a condition that can never be true. Ours
returns `(u64int)-1` explicitly.

**`free_frame()` divides by 4096 twice.** It passes `page->frame` — already a
frame *index* — to `clear_frame()`, which promptly divides it by `0x1000` again.
Ours passes `page->frame * 0x1000`.

**`alloc_frame()` asks the wrong question.** `if (page->frame != 0) return;`
cannot distinguish "already mapped to physical frame 0" from "not mapped at
all." Test `page->present`.

**`kmalloc_int`'s alignment check is inverted.**

```c
if (align == 1 && (placement_address & 0xFFFFF000))   // tutorial
```

This asks "is the address greater than 4 KiB?" — true for every address the
allocator will ever see — so it page-aligns unconditionally and burns up to
4 KiB on every single call. The question you meant to ask is "are the low twelve
bits nonzero?": `placement_address & 0xFFF`.

---

## Verifying it, properly

The demo in `main.c` proves one thing: an unmapped read faults. That is a low
bar. Here is a harness that actually exercises the design. (Swap it into
`main.c` temporarily.)

```c
monitor_write("placement_address = "); monitor_write_hex64(placement_address);

// 1. The last mapped page is readable.
volatile u64int *ok = (u64int *)(placement_address - 0x1000);
monitor_write_hex64(*ok);

// 2. Past the map: the PT exists (it covers 0-2MiB) but the entry is absent.
page_t *e = get_page(placement_address + 0x2000, 0, kernel_pml4);
monitor_write_hex64((u64int)e);  monitor_write_dec(e->present);

// 3. A different PML4 slot entirely: no intermediate table -> NULL.
monitor_write_hex64((u64int)get_page(0x8000000000UL, 0, kernel_pml4));

// 4. Demand-map a fresh page at 4 MiB, write to it, read it back.
page_t *p = get_page(0x400000, 1, kernel_pml4);
alloc_frame(p, 1, 1);
volatile u64int *fresh = (u64int *)0x400000;
*fresh = 0xDEADBEEFCAFEBABEUL;

// 5. Hand the frame back, flush the TLB entry, touch it again.
free_frame(p);
asm volatile("invlpg (%0)" :: "r"(0x400000UL) : "memory");
monitor_write_hex64(*fresh);        // -> page fault
```

Output:

```
placement_address = 0x113000
read last mapped page = 0x3
get_page(just past the map): entry=0x1128a8 present=0
get_page(0x8000000000, make=0) = 0x0
wrote+read 0x400000 = 0xdeadbeefcafebabe
  backed by frame 0x114
freed; touching it again...
recieved interrupt: 14
Page fault! ( not-present ) at 0x400000
PANIC(Page fault) at paging.c:238
```

Read that carefully; every line is telling you something.

- **`read last mapped page = 0x3`** — we just read a page-table entry out of our
  own page tables. `0x3` is `present | rw`. The tables are ordinary memory, and
  the identity map means we can see them.

- **`entry=0x1128a8 present=0`** — this is `get_page`'s contract, and it is not
  "is this address mapped?". A `page_t *` came back because the PT covering the
  first 2 MiB already exists; the *entry* within it is simply absent. Confusing
  these two is a classic bug.

- **`= 0x0`** for `0x8000000000` — a genuine NULL, because that address lands in
  PML4 slot 1, whose PDPT was never created and `make` was 0.

- **`backed by frame 0x114`** — 276 decimal. The identity map consumed frames
  0 through 275, so `first_frame()` handed us the next one. The allocator works.

- **`invlpg`** — this is the step people forget. Removing an entry from a page
  table does *not* remove it from the TLB. Without the `invlpg`, the CPU happily
  keeps using the cached translation and step 5 reads back `0xdeadbeef...`,
  making a freed page look mapped. `switch_page_directory` gets away without it
  only because reloading `CR3` flushes the whole TLB.

---

## Things to try

1. **Delete the `invlpg`.** Watch the freed page keep working. Now you have felt
   a stale-TLB bug in a controlled setting, which is far better than meeting one
   in chapter 9.

2. **Reinstate the bitset bug.** Change `kmalloc(nframes / 8)` back to
   `kmalloc(nframes / 32)`, then raise `mem_end_page` to 64 MiB and allocate
   several thousand frames. Watch the allocator start handing out frames it has
   already given away.

3. **Find your own kernel in its own page tables.** `get_page((u64int)&main, 0,
   kernel_pml4)` and print the frame number. Confirm `frame << 12` equals
   `&main & ~0xFFF`.

4. **Make a page read-only.** Map a page, write to it, then clear `p->rw`,
   `invlpg`, and write again. You should get `Page fault! ( write )` with
   `present` *not* printed — the page is there, you just aren't allowed.

5. **Turn on NX.** Set `EFER.NXE` (bit 11 of MSR `0xC0000080`) in `boot.s`, then
   set `p->nx = 1` on a page and try to execute from it. The error code will
   have bit 4 set and you'll see `instruction-fetch`. This bit is inert until
   `NXE` is on — which is a nice trap to have sprung deliberately once.

6. **Break canonicality.** Dereference `0x0000800000000000`, the first
   non-canonical address. You will *not* get a page fault. Work out which
   exception you get instead, and why.

---

## Next

Chapter 7 (the heap) is mostly a translation. `ordered_array` stores `void *`,
which is now 8 bytes; the heap's `header_t`/`footer_t` grow; and `KHEAP_START`
should move somewhere with room to breathe now that you have 256 TiB.

But note what chapter 7 assumes: that `KHEAP_START` is mapped in
`kernel_pml4` before the heap is used. In the 32-bit tutorial the heap lives at
`0xC0000000`, comfortably above the identity map, and `initialise_paging()`
pre-maps it. You'll do the same, and it will be the first time your kernel
touches memory where `virt != phys`. Every place this port leaned on the
identity map — `next_table()`'s final cast, `switch_page_directory`,
`kmalloc_ap`'s `*phys = placement_address` — is a place to re-read before you
start.
