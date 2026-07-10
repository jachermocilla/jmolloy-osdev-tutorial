# Chapter 7 (The Heap) — 64-bit Port

This is JamesM's chapter 7, ported to x86-64. It builds an ordered-array index,
a hole-and-block heap with coalescing, and wires `kmalloc`/`kfree` to it.

**Read `06_paging_64/README.md` first.** Chapter 6 ended by naming three lines
that were "correct today and lies waiting to be told." This is the chapter where
one of them tells its lie.

```
07_heap_64/src/
├── boot.s, link.ld, gdt.s          ← unchanged since chapter 3
├── interrupt.s, isr.c/.h           ← unchanged since chapter 5
├── descriptor_tables.*, timer.*    ← unchanged
├── common.c/.h, monitor.c/.h       ← unchanged since chapter 6
├── ordered_array.c/.h              ← new; type_t doubles in width
├── kheap.c/.h                      ← the heap; six bugs fixed
├── paging.c/.h                     ← rewritten initialise_paging(): the direct map
└── main.c                          ← same demo, plus a virt/phys comparison
```

---

## Building and running

```bash
cd 07_heap_64/src
make
qemu-system-x86_64 -kernel kernel
```

```
a: 0x10f8b0
b: 0xffff800000080010
c: 0xffff800000080038
c - b: 0x28
v: 0xffff800000080060 -> phys 0x19f060
d: 0xffff800000080010   (reused b)
```

Read those six lines. `a` came from the placement allocator, down in the
identity-mapped low memory. `b` and `c` came from the heap, up in the canonical
higher half. `c - b` is `0x28` = 40 bytes: a 16-byte header, 8 bytes of data,
a 16-byte footer. `v` and its physical address differ — **the first time in this
tutorial that virtual is not physical**. And `d` reuses `b`'s address, proving
`free()` coalesced the two blocks back into one hole.

---

## Two things break, and they are not the ones you expect

The heap algorithm itself ports almost mechanically. `u32int` becomes `u64int`
in about forty places and you are done. What breaks is arithmetic, and an
assumption.

### Break 1: the index eats the entire heap

`type_t` is `void *`. The index is `HEAP_INDEX_SIZE` of them.

```
32-bit:  0x20000 entries × 4 bytes = 0x80000    (half the 1 MiB heap)
64-bit:  0x20000 entries × 8 bytes = 0x100000   (ALL of the 1 MiB heap)
```

In 64-bit, `create_heap` shifts `start` past the index, arrives exactly at
`end_addr`, and writes a first hole of size **zero**. Every subsequent
allocation fails in a way that is very hard to read backwards from.

The tutorial has no assertion to catch it. Add one:

```c
ASSERT(start < end_addr);
```

Then halve `HEAP_INDEX_SIZE` to `0x10000`, restoring the same byte cost and the
same 50/50 split. This is the entire fix, and the entire lesson: **when your
pointers double, so does every array of pointers.**

Related, and worth knowing where your memory went:

| | 32-bit | 64-bit |
|---|---|---|
| `sizeof(header_t)` | 12 | 16 |
| `sizeof(footer_t)` | 8 | 16 |
| **overhead per allocation** | **20 bytes** | **32 bytes** |

`footer_t` grew most, from `{u32, ptr}` at 8 bytes to `{u32, pad, ptr}` at 16.
The compiler must 8-align that pointer. Nothing in the code depends on the exact
numbers — it uses `sizeof()` throughout — but a 64-byte allocation now costs 96.

### Break 2: `next_table()` dereferences a physical address

This is the real one.

Chapter 6's page-table walk ends with this line, and a warning attached to it:

```c
// Identity map: physical address == virtual address.
return (page_table_t *)((u64int)entry->frame << 12);
```

It works because every page table came from the placement allocator, which lives
in the identity-mapped low region. But look at what happens now:

```
alloc() needs more space
  → expand()
    → get_page(addr, make=1)
      → next_table(..., make=1)
        → kmalloc_ap()          ← kheap != 0, so this comes from the HEAP
          → returns 0xffff800000082000, phys 0x1a0000
        → entry->frame = 0x1a0
      → next_table returns (page_table_t *)0x1a0000   ← a PHYSICAL address
```

`0x1a0000` is not mapped. The kernel dies dereferencing it.

I verified this by deleting the fix and running it:

```
heap virt = 0xffff800000080010
heap phys = 0x19f010
recieved interrupt: 14
Page fault! ( not-present ) at 0x1a0000
PANIC(Page fault) at paging.c:267
```

That faulting address is a *physical* address being used as a *virtual* one.
Exactly the failure chapter 6 predicted, arriving exactly where it said it would.

> Note how easily this hides. The heap must cross a **2 MiB boundary** before a
> new page table is needed, because one PT covers 2 MiB. My first test allocated
> 576 KiB, the heap grew, and everything worked fine. It took a 2.3 MiB
> allocation to trip it. A bug that only fires when the heap crosses a
> particular size is exactly the kind you ship.

---

## The fix: a direct map

The 32-bit tutorial solves this with the parallel `tables[]` / `tablesPhysical[]`
arrays — keep both views of every table, forever. Chapter 6 explained why that
does not generalise to four levels.

The 64-bit answer is different, and simpler: **map all of physical memory
somewhere, permanently.** Then any physical address has a virtual address you can
compute, and `next_table`'s cast is legal for any table, from anywhere.

This is not a trick. It is what Linux does (`page_offset_base`), what the BSDs
do, and it is only possible because 64-bit gives you enough address space to
spend 16 MiB — or 64 TiB — of it on a window onto RAM. In 32-bit there simply is
not room.

`initialise_paging()` becomes three passes:

```c
// Pass 1: create every page table we will ever need, up front.
//   get_page(make=1) calls kmalloc_ap. Once the heap exists that means heap
//   memory. So all tables must exist BEFORE create_heap().
for (i = 0; i < PHYS_MEM_SIZE; i += 0x1000)
    get_page(i, 1, kernel_pml4);
for (i = KHEAP_START; i < KHEAP_START + KHEAP_INITIAL_SIZE; i += 0x1000)
    get_page(i, 1, kernel_pml4);

// Pass 2: identity-map the kernel and the placement allocations,
//   RESERVING those frames in the bitset.
i = 0;
while (i < placement_address + 0x1000) {
    alloc_frame(get_page(i, 0, kernel_pml4), 1, 1);
    i += 0x1000;
}

// Pass 3: the direct map. Map the rest of physical RAM at its own address,
//   WITHOUT reserving the frames -- they stay available to alloc_frame.
for (; i < PHYS_MEM_SIZE; i += 0x1000) {
    page_t *page = get_page(i, 0, kernel_pml4);
    page->present = 1; page->rw = 1; page->user = 0;
    page->frame = i >> 12;
}
```

Pass 3 is the whole idea. Frames handed out later by `alloc_frame` are reachable
*both* through whatever virtual address they were mapped at *and* at their
physical address. That aliasing is deliberate.

You can see the result:

```
(qemu) info mem
0000000000000000-0000000001000000  0000000001000000  -rw    ← direct map, 16 MiB
ffff800000000000-ffff800000100000  0000000000100000  -rw    ← the kernel heap
```

Two regions. Two views. The heap's data is in the second; the page tables that
describe it are reachable through the first.

### Where the heap lives

```c
#define KHEAP_START  0xFFFF800000000000UL
```

The tutorial uses `0xC0000000`, which is just "above the identity map" in a 4 GiB
world. With 256 TiB, the convention is the bottom of the upper canonical half:
bit 47 set, bits 48–63 sign-extended to match.

Use it. It costs nothing, and it exercises three things a low address would not:
the canonical-address rule, PML4 slot 256, and GCC's `movabs` for a constant that
does not fit in 32 bits.

---

## Six bugs in the original heap

None of these are 64-bit issues. All are in the 32-bit code today.

### 1–3. The same operator-precedence bug, three times

```c
if (new_size & 0xFFFFF000 != 0)     // expand()
if (start & 0xFFFFF000 != 0)        // create_heap()
if (new_size & 0x1000)              // contract()
```

`!=` binds tighter than `&`. So the first two are `new_size & (0xFFFFF000 != 0)`,
i.e. `new_size & 1` — "is the size odd?" It page-aligns odd sizes and leaves even
ones alone. The third tests a single bit and then does `new_size &= 0x1000`,
throwing away every other bit of the size.

One helper, written once, correctly:

```c
static u64int page_align_up(u64int addr)
{
    if (addr & 0xFFF) { addr &= ~0xFFFUL; addr += 0x1000; }
    return addr;
}
```

The same `& 0xFFFFF000` mistake appears twice more, in `find_smallest_hole` and
in `alloc`'s page-align branch. Fixed the same way.

### 4. `kmalloc_int`'s physical address is a 12-bit number

```c
*phys = page->frame*0x1000 + (u32int)addr&0xFFF;
```

`+` binds tighter than `&`, so this is `(page->frame*0x1000 + addr) & 0xFFF`.
Every physical address it returns is under 4096. Parenthesise the offset.

### 5. `free()` has dead code and a stale pointer

```c
if (header->size - (old_length-new_length) > 0)
```

Both operands are unsigned. The subtraction wraps rather than going negative, so
the condition is always true and the `else` branch never runs. Write
`if (header->size > old_length - new_length)`.

And in that dead `else` branch, the code searches the index for `test_header` —
a variable only assigned if the *unify-right* branch above happened to run.
Otherwise it is stale, the search finds nothing, and a defunct block stays in the
index. It wants `header`.

Fixing bug 5 is what makes bug 5b reachable, which is a nice illustration of why
dead code is dangerous rather than merely untidy.

### 6. `free()` reads outside the heap

Unify-left reads a `footer_t` immediately below `header`. If `header` is the
first block, that is memory before `heap->start_address`. Unify-right reads a
`header_t` above `footer`; if `footer` ends the heap, that is unmapped. Both
usually survive because `HEAP_MAGIC` is unlikely to appear by accident. "Usually"
is not a memory-safety strategy. Bounds-check both.

### Bonus: `ordered_array` reads one past the end

`insert_ordered_array`'s shift loop reads `array[iterator]` one slot past the
end on its final iteration; `remove_ordered_array` reads `array[i+1]` when
`i == size-1`. Both harmless, both easily avoided by walking downwards and by
stopping one earlier. And neither function ever checks `size < max_size` — a
heap under memory pressure will silently write past the index.

---

## Verifying it, properly

The tutorial's demo allocates three 8-byte blocks and frees two. That exercises
none of the interesting paths. Here is a harness that does. (Swap it into
`main.c`.)

```c
// 1. virt != phys, for the first time
u64int phys;  u64int v = kmalloc_p(64, &phys);

// 2. the direct map: same memory, two addresses
*(volatile u64int *)v = 0xFEEDFACEDEADBEEFUL;
monitor_write_hex64(*(volatile u64int *)phys);      // must match

// 3. force expand() across a 2 MiB boundary -> a new PT -> kmalloc_ap on the heap
void *big = (void *)kmalloc(0x250000);
*(volatile u64int *)((u64int)big + 0x24F000) = 0x1234;   // touch the far end

// 4. free + coalesce + contract
kfree(big);

// 5. page-aligned allocation
u64int pa = kmalloc_a(0x1000);      // pa % 0x1000 must be 0

// 6. stress: 64 allocs, free every other, realloc, free all
```

Results:

```
heap virt = 0xffff800000080010
heap phys = 0x19f010
read back via direct map = 0xfeedfacedeadbeef
big block  = 0xffff800000082020
heap grew from 0x80000
            to 0x2d1000
touched far end of big block, still alive
after kfree(big), heap size = 0x70000
kmalloc_a returned = 0xffff800000083000
  page aligned: YES
stress done, heap size = 0x70000
index entries left     = 0x3
ALL OK
```

Line by line:

- **`0x19f010` vs `0xffff800000080010`** — genuinely different addresses for the
  same byte. Everything chapter 6 got away with is now load-bearing.
- **`read back via direct map`** — writing through the heap address and reading
  through the physical address returns the same value. The alias works.
- **`0x80000 → 0x2d1000`** — the heap grew past 2 MiB, so `expand()` created a
  new page table, from the heap, and `next_table()` dereferenced it. This is the
  line that page-faults without pass 3.
- **`after kfree(big) → 0x70000`** — `contract()` gave the pages back and stopped
  at `HEAP_MIN_SIZE`. Coalescing and contraction both work.
- **`stress done, heap size = 0x70000`** — after 96 allocations and 96 frees the
  heap is back to minimum. No leak, no fragmentation blowup.

Do step 3 deliberately. Comment out pass 3 in `initialise_paging()` and watch:

```
Page fault! ( not-present ) at 0x1a0000
```

Then look up `0x1a0000` in the surviving output. It is a physical address.

---

## Things to try

1. **Delete the `ASSERT(start < end_addr)`** in `create_heap`, restore
   `HEAP_INDEX_SIZE` to `0x20000`, and try to allocate anything. Diagnose the
   resulting mess from first principles. This is the bug that would have found
   you if the assertion had not.

2. **Make the direct map a real function.** Right now `next_table()` casts
   `frame << 12` straight to a pointer, which works because the direct map is at
   virtual address 0. Introduce `#define PHYS_BASE 0xFFFF888000000000UL` and a
   `phys_to_virt()`, move the direct map there, and remove the identity map
   entirely. This is what a real kernel looks like, and it is the last step
   before the higher half.

3. **Grow physical memory.** `PHYS_MEM_SIZE` is hardcoded to 16 MiB. Read the
   real figure from the multiboot info structure `mboot_ptr` — which has been
   sitting in `RDI` since chapter 2, ignored. Watch the direct map and the frames
   bitset scale.

4. **Find the fragmentation.** Allocate 1000 blocks of random size, free a random
   half, and print `kheap->index.size`. The index is an array of *holes*. Watch
   it grow. Then work out why `HEAP_INDEX_SIZE` is a fixed 65536 and what happens
   when you exceed it. (Hint: bug bonus, above.)

5. **Break the footer.** Overwrite the four bytes just below a `kmalloc` return
   value and then `kfree` it. `ASSERT(header->magic == HEAP_MAGIC)` should catch
   you. Now overwrite the four bytes just *above* the block and free it. Does
   anything catch you? Should it?

---

## Where this leaves you

Every assumption chapter 6 flagged has now been paid for:

| Chapter 6 said | Chapter 7 did |
|---|---|
| `next_table()`'s cast assumes identity mapping | Direct map makes it true for all physical memory |
| `switch_page_directory` hands CR3 a virtual pointer | Still true — `kernel_pml4` is in the direct map |
| `kmalloc_ap`'s `*phys = placement_address` | Now branches: placement (virt==phys) or heap (`get_page` lookup) |

The one that remains is `switch_page_directory`. It survives because the PML4
comes from the placement allocator, and the direct map covers it. Move the kernel
to `0xFFFFFFFF80000000` and it will need a `virt_to_phys()` — which, once you
have done exercise 2, you will already have written.

Chapter 8 (the VFS and initrd) is a translation, not a redesign. `strcpy` and
`strcat` finally get called, so fix the bugs chapter 3 pointed at before you
start. The initrd is loaded by GRUB as a multiboot module — and `mboot_ptr`, the
argument you have been ignoring since chapter 2, is where you find out where.
