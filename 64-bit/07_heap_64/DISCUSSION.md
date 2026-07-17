# Chapter 7: Dynamic Memory and the Kernel Heap

In the previous chapter, the kernel took control of virtual memory. It could build page tables, allocate physical frames, map new addresses, and catch the faults raised by addresses it had not mapped. One capability was still missing, and every operating system depends on it: the ability to hand out a few dozen bytes and get them back again.

Until now, allocation has meant advancing a pointer. Whenever the kernel needed memory, it took the next unused address and moved the marker along. That is a **placement allocator**, or a bump allocator, and it is exactly right for early initialization, where memory is taken once and never returned.

A growing kernel breaks it quickly. Process descriptors, page tables, device structures, file system metadata, and network buffers appear and disappear continuously, and an allocator that can only move forward will eventually run out of address no matter how little memory is genuinely in use.

This chapter builds the **kernel heap**: a region of memory and an allocator that can both give and take back. Freed space is recycled, adjacent free space is rejoined, and the region itself grows when demand requires it. The kernel moves from managing pages to managing the objects that live inside them.

---

# From Pages to Objects

Paging and dynamic allocation work at different scales. The paging subsystem deals in 4 KiB pages, which is the smallest unit the hardware can map or protect. Kernel objects are nothing like that size — a list node, a timer, a file descriptor, a few dozen bytes each — and giving every one of them a page would waste almost all of it.

```text
Paging hands out pages:

+----------------------------------------+
|               4 KiB page               |
+----------------------------------------+

The heap divides them into objects:

+--------+------+-----------+-----+------+
| object |object|  object   |free | obj  |
+--------+------+-----------+-----+------+
```

The two subsystems answer to each other. Paging supplies the pages; the heap subdivides them and keeps the books.

---

# Why the Placement Allocator Is Not Enough

Every allocation so far has been a pointer moving in one direction.

```text
+----------------------------------------+
| Used | Used | Used | Free..............|
+----------------------------------------+
                     ^
                placement_address
```

Nothing is searched, nothing is recorded, and allocation costs a single addition. The price for that simplicity is total: nothing can ever be given back. A subsystem that takes a kilobyte and finishes with it a moment later has removed that kilobyte from the machine for good, and the allocator eventually runs off the end of memory while most of what it handed out sits unused.

The memory has not disappeared. The allocator simply has no way to find it again, because it never wrote down where anything went. Everything in this chapter follows from deciding to keep records.

---

# Blocks and Holes

The heap is one contiguous region of memory that the allocator carves into a sequence of **blocks**, each either allocated or free. Free blocks are called **holes**, and the whole design is a story about them.

```text
Heap

+--------+--------+--------+--------+--------+
| block  |  hole  | block  | block  |  hole  |
+--------+--------+--------+--------+--------+
```

At birth the heap is a single hole spanning everything. Allocation carves a block out of a hole; freeing turns a block back into a hole. The blocks and holes always tile the region exactly, with no gaps between them — a property the allocator leans on later, when it wants to find a block's neighbours.

Unlike the stack, which gives back memory in the reverse of the order it took it, the heap must cope with any order at all. The kernel allocates A, then B, then frees A while B lives on, and the hole left behind sits in the middle of the region rather than at the end.

---

# The Shape of a Block

The allocator cannot hand a caller a bare pointer and forget about it. When that pointer comes back to `kfree`, the allocator must know how large the block was and where it ends, and the caller will not tell it. So every block carries its own bookkeeping, one structure in front of the data and another behind it.

```text
      header                                            footer
        |                                                  |
        v                                                  v
+----------------------+------------------------+----------------------+
| magic | is_hole |size|       user data        | magic |  header ptr  |
+----------------------+------------------------+----------------------+
|<---- 16 bytes ------>|<------ size bytes ---->|<---- 16 bytes ------>|
                       ^
                       |
              the pointer kmalloc returns

|<------------------ header->size, the whole block ------------------->|
```

The header records how big the block is and whether it is a hole. The footer holds a pointer back to its own header. The caller sees neither: `alloc` returns the address just past the header, and `free` subtracts the same amount to find its way back.

The footer looks redundant until you ask a block about its left-hand neighbour. Given a header, walking *right* is easy — add `size` and you are standing on the next header. Walking *left* is impossible, because the previous block's size is recorded at its start, and you have no idea where its start is. The footer solves this by putting a back-pointer at the *end* of every block: the sixteen bytes immediately below any header belong to the previous block's footer, and that footer names its header. One extra field buys a doubly-linked chain through memory without a linked list.

The `magic` field in both structures is how the allocator decides those bytes really are a footer, rather than the middle of somebody's array that happens to be there. It is a sanity check, not a proof — and in a kernel, an assertion on it is the difference between catching a corrupted heap and following a wild pointer.

Two numbers are worth remembering: in this port both structures are sixteen bytes, so every allocation costs thirty-two bytes of overhead on top of what the caller asked for. A request for eight bytes occupies forty.

---

# The Ordered Array: an Index of Holes

Finding memory means finding a hole big enough, and the honest way to do that is to walk the heap from the start, header by header, skipping over allocated blocks and measuring free ones. That works, and it costs a pass over the entire heap on every single allocation.

The allocator therefore keeps a separate **index**: an array of pointers to every hole in the heap, sorted by the size of the hole they point at.

```text
Index (sorted by hole size)          Heap
+---+--------+                       +---+---------+---+--------------+
| 0 |  ptr --+---------------------> | H | 32 B    | F | block        |
+---+--------+                       +---+---------+---+--------------+
| 1 |  ptr --+-------------+
+---+--------+             |         +---+---------------+---+--------+
| 2 |  ptr --+---------+   +-------> | H |    128 B      | F | block  |
+---+--------+         |             +---+---------------+---+--------+
| 3 |  ptr --+---+     |
+---+--------+   |     |             +---+---------------------------+
  size = 4       |     +-----------> | H |          512 B            |
                 |                   +---+---------------------------+
                 |
                 +-----------------> ... a 4 KiB hole somewhere else

sizes:  32 -> 128 -> 512 -> 4096      (ascending; address order irrelevant)
```

The array stores nothing but addresses. The holes themselves stay where they are, described by the headers already sitting in the heap, and the index is a table of contents rather than a copy.

Sorting is the point. Because the array is ordered by size, the first entry large enough to satisfy a request is also the *smallest* entry large enough, so a scan from the front that stops at the first fit performs a best fit. Choosing the tightest hole leaves the bigger ones intact for the bigger requests that will follow, which is the cheapest defence against fragmentation the allocator has.

The array keeps itself sorted by insertion rather than by sorting: an insert walks forward to the first entry larger than the newcomer, shifts the tail up one slot, and drops it in.

```text
insert a 64-byte hole into [32, 128, 512, 4096]

  step 1: find the slot        32 | 128 | 512 | 4096
                                    ^ first one bigger than 64

  step 2: shift the tail up    32 |     | 128 | 512 | 4096

  step 3: drop it in           32 | 64  | 128 | 512 | 4096
```

That makes insertion and removal linear in the number of holes, and lookup by position free. For a kernel with a few hundred holes it is a good trade, and it is worth being clear that it is a trade: real allocators replace this with size-class free lists or a balanced tree precisely because the shifting does not scale. The ordered array is the simplest structure that makes the point.

Two details in the implementation matter later. The array is generic — it stores anything pointer-sized and takes a comparison function, and this heap hands it one that compares `header->size` — which is why the same file reappears in chapter 9 sorting something else entirely. And the array is a fixed size, chosen when the heap is created and never grown; a heap that fragments into more holes than the index has slots has nowhere to record them, so an assertion catches it rather than letting the array run off its end.

There is a chicken-and-egg problem in the index, and its solution is worth noticing. The index describes the heap's free space, so it cannot live in the heap's free space. Instead it is *placed*: `create_heap` reserves the first stretch of the region for the array, points the index at that fixed address, and only then declares everything after it to be the first hole. The `heap_t` structure itself comes from the placement allocator, because at the moment it is built there is no heap to allocate it from.

```text
KHEAP_START
    |
    v
+---------------------------+---+-------------------------------------+
|      index array          | H |     one hole, the entire heap       |
+---------------------------+---+-------------------------------------+
|<--- HEAP_INDEX_SIZE  ---->|
|      pointers             |
```

---

# Allocating: Find, Split, Record

An allocation walks the index for the smallest adequate hole, and the size it looks for is the caller's request plus the thirty-two bytes of header and footer.

Once found, the hole is almost always too big, and handing over the whole thing would waste the difference. So the allocator **splits** it: the front becomes the block, and the remainder becomes a smaller hole.

```text
Before                     the hole the index led us to
+---+-------------------------------------------------+---+
| H |                     480 bytes                   | F |
+---+-------------------------------------------------+---+
|<-------------------- size = 512 --------------------->|
  ^ the index entry points here

After   alloc(128)
+---+-----------+---+---+-------------------------+---+
| H | 128 bytes | F | H |        288 bytes        | F |
+---+-----------+---+---+-------------------------+---+
|<-- size=160 ---->|<-------- size = 352 --------->|
  block, is_hole = 0    new hole, is_hole = 1
  removed from index    inserted into index
```

The caller asked for 128 and the block costs 160, so the 512-byte hole gives up 160 and the 352 that remain become a hole in their own right — header, 288 usable bytes, footer.

The arithmetic on the remainder is the only subtle part. A leftover has to be big enough to hold a header and a footer of its own, or it cannot exist as a hole at all; when the remainder would be smaller than that, the allocator hands the caller the entire original hole instead and records the block as slightly larger than requested. The waste is bounded by thirty-two bytes and buys the guarantee that every block in the heap is a well-formed block.

When no hole is large enough, the allocator grows the heap, extends the last block to cover the new ground, and calls itself again — the second attempt is guaranteed to find what it needs.

---

# Freeing: Coalesce, Left and Right

Freeing begins by subtracting the header size from the caller's pointer, checking the magic numbers, and setting `is_hole`. Stopping there would work and would slowly destroy the heap: allocate a hundred blocks, free them all, and you have a hundred adjacent holes and not one of them large enough for a big request. The free memory is all there and it is useless, and that is **fragmentation**.

```text
+----+----+----+----+----+
|hole|hole|hole|hole|hole|      5 × 64 B free, and alloc(200) fails
+----+----+----+----+----+
```

The cure is to notice that neighbouring holes are the same hole wearing two headers. Whenever a block is freed, the allocator looks both ways and merges what it finds. This is **coalescing**, and it is what separates a real allocator from a toy.

Looking right is a matter of adding `size` to reach the next header and asking whether it is a hole; if it is, its size is absorbed and it ceases to exist. Looking left uses the footer trick from earlier: the sixteen bytes below this header are the previous block's footer, and its back-pointer leads to that block's header. If *that* is a hole, then this newly freed block is absorbed into it, and the surviving header is the left neighbour's.

```text
free(B), with holes on both sides

Before
+---+------+---+---+------+---+---+------+---+
| H | hole | F | H |  B   | F | H | hole | F |
+---+------+---+---+------+---+---+------+---+
  A (a hole)     B (being freed)   C (a hole)
       <---------- ^ ---------->
     footer leads left      header found right

After
+---+---------------------------------------+
| H |            one hole, A+B+C            | F |
+---+---------------------------------------+
  A's header survives, C's footer survives
```

Both merges are size arithmetic on headers, and both have to be paid for in the index. A hole that gets absorbed must be removed from the index, because its header is no longer a header; the surviving hole must be inserted, or, if it was already there, left alone. Absorbing the left neighbour means the merged hole is *already* indexed — under the wrong size, which the sorted array tolerates only because that entry is removed and reinserted the next time it changes hands.

Here the index's sortedness turns against it. To remove the right-hand neighbour, the allocator must find the entry that points at it, and it cannot binary-search for it: the array is ordered by *size*, and what the allocator has is an *address*. So the merge does a linear scan of the index looking for a matching pointer. This is the price of choosing the index's order for the benefit of `alloc`, and it is paid by `free`.

The last thing `free` checks is whether the resulting hole runs to the very end of the heap. If it does, the pages underneath it are no longer holding anything, and the heap can give them back.

---

# Growing and Shrinking

The heap is not a fixed region. When no hole is large enough, it asks the paging subsystem for more pages, maps them at the addresses immediately following its own end, and pushes its end marker forward. When a large hole forms at the end, the pages beneath it are freed and the marker retreats — down to a floor, since a heap that shrinks to nothing would only have to grow again on the next allocation.

```text
alloc() finds no hole                     free() leaves a hole at the end
        |                                          |
        v                                          v
   expand(new_size)                          contract(new_size)
        |                                          |
        v                                          v
  get_page + alloc_frame                    free_frame + invlpg
  for each new page                         for each dead page
        |                                          |
        v                                          v
  end_address moves up                      end_address moves down
```

Both directions round to whole pages, because pages are the only unit paging deals in. Contraction has one obligation that expansion does not: removing a page-table entry does not remove the cached translation, so each freed page must be flushed from the Translation Lookaside Buffer explicitly. Skip it and the processor cheerfully keeps using a page the heap no longer owns.

The layering here is worth naming, because it is the shape of the whole kernel. The heap manages allocations; paging manages the pages the allocations live in; the heap asks paging for pages and never touches a frame itself. Each subsystem is a customer of the one below it.

---

# Wiring Up kmalloc and kfree

None of this changes how the rest of the kernel asks for memory. `kmalloc` has been there since chapter 6, and the kernel keeps calling it exactly as before — the same function now answers in two entirely different ways depending on when it is called.

```text
kmalloc(sz)      kmalloc_a(sz)     kmalloc_p(sz,&phys)   kmalloc_ap(sz,&phys)
   align=0          align=1            align=0                align=1
   phys=0           phys=0             phys=&phys             phys=&phys
      \                |                   |                     /
       \               |                   |                    /
        +--------------+---------+---------+-------------------+
                                 |
                                 v
                         kmalloc_int(sz, align, phys)
                                 |
                          is kheap set yet?
                    no  /                    \  yes
                       v                      v
        +-----------------------+   +-----------------------------+
        | placement allocator   |   | alloc(sz, align, kheap)     |
        | take placement_address|   | find hole, split, return    |
        | advance it by sz      |   | ask page tables for phys    |
        | virt == phys          |   | virt != phys                |
        +-----------------------+   +-----------------------------+
```

The switch is one variable. `kheap` starts at zero, so every early call bumps the placement pointer, exactly as it always did; `initialise_paging` and `create_heap` both run on that. The moment `kheap` is assigned, every subsequent call routes into the heap, and the caller never learns that anything changed.

The four entry points differ only in two flags. `align` forces the returned address onto a page boundary, which is what the page-table code needs and what makes `alloc` do its strangest work — splitting a hole in front of the block, so that the *data* lands on the boundary and the header sits in the gap before it. `phys` asks for the physical address alongside the virtual one, and it is where the two modes finally part company: in placement mode virtual and physical are the same number, and the answer is free; in heap mode the allocator has to walk the page tables and ask.

`kfree` is the shorter story. It hands the pointer to `free` along with the heap, and `free` does everything described above.

There is an asymmetry here that the code does not announce. Memory handed out before the heap existed cannot be freed — those bytes have no header, no footer, and no magic number, and `kfree` would read the sixteen bytes in front of them and find whatever was there. Placement allocations are permanent by construction. In practice this is fine, because what they hold — the frame bitmap, the kernel's page tables, the heap's own structure — is what the kernel intends to keep forever.

---

# Where Virtual Stops Meaning Physical

The heap lives high in the address space, in the canonical upper half, and nothing identity-maps it. This is the first allocation in the tutorial whose virtual address is not also its physical address, and the numbers say so plainly: a pointer from the placement allocator looks like `0x10f8b0`, and one from the heap looks like `0xffff800000080010`.

```text
kmalloc()  ->  0xffff800000080010    the virtual address the kernel uses
                        |
                        v
                 page tables
                        |
                        v
               0x19f010              the physical address the RAM knows
```

Kernel code almost never cares. The pointer works, the data is there, and where the bytes physically sit is the page tables' business. The exception is hardware: a device doing direct memory access reads physical addresses off the bus and knows nothing of page tables, so a driver handing it a buffer must hand it a physical address. That is what `kmalloc_p` is for, and it is the first place in this kernel where the distinction between the two kinds of address stops being pedagogy and starts being a bug you can hit.

It is also, quietly, the moment chapter 6's warning comes due. The page-table walk was written on the assumption that a table's physical address could be dereferenced directly, which was true for as long as every table came from the placement allocator. Page tables now come from the heap.

---

# Looking Ahead

The kernel's memory-management infrastructure is complete in outline. It can map pages, allocate frames, hand out arbitrary small objects, take them back, rejoin the ground they leave behind, and grow when the demand does.

Nearly every object the kernel creates from here lives in the heap. Process descriptors, file system nodes, device structures, and buffers all begin with a `kmalloc`, and every subsystem in the chapters ahead is a customer of the allocator built here.

Memory has stopped being a layout fixed at boot. It has become a service — one the kernel allocates, organizes, reclaims, and reshapes as the system's needs change.
