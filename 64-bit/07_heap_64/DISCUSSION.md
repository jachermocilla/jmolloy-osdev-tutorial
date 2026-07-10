This chapter should tell the story of **why an operating system needs dynamic memory**. The previous chapter gave the kernel control over virtual memory, but memory allocation is still primitive. The kernel can map pages, yet it has no convenient way to request small amounts of memory for its own data structures. This chapter fills that gap by introducing the kernel heap.

I would structure it like this.

---

# Chapter 7: Dynamic Memory and the Kernel Heap

In the previous chapter, the kernel learned how to manage virtual memory. It could create page tables, allocate physical frames, establish new mappings, and recover from page faults. Although this represented a major milestone, the kernel still lacked one capability that every modern operating system depends upon: dynamic memory allocation.

Throughout the previous chapters, memory allocation followed a simple strategy. Whenever the kernel needed memory, it advanced a pointer to the next unused location. This approach, known as a **placement allocator** or **bump allocator**, works well during early initialization because memory is allocated only once and never released.

As the kernel grows, however, this strategy quickly becomes inadequate. Operating system components create and destroy objects continuously. Process descriptors, page tables, device structures, file system metadata, network buffers, and countless other data structures appear and disappear throughout the lifetime of the system. If memory can only be allocated and never reclaimed, the kernel eventually exhausts available memory regardless of how much is actually in use.

This chapter introduces the **kernel heap**, a dynamic memory allocator that allows memory to be both allocated and released. Instead of treating memory as a one-way sequence of allocations, the kernel begins recycling unused regions, reducing fragmentation, and expanding its address space when additional memory is required.

The kernel has now progressed from managing pages to managing objects that reside within those pages.

---

# From Pages to Objects

Paging and dynamic memory allocation solve different problems.

The paging subsystem manages memory at the granularity of entire pages. On most x86-64 systems, a page occupies 4 KiB.

Applications and kernel subsystems, however, rarely require memory in page-sized units.

Consider a linked list node, a process descriptor, or a timer object. These structures often occupy only a few dozen or a few hundred bytes.

Allocating an entire page for every object would waste enormous amounts of memory.

Conceptually, the relationship looks like this.

```text
Physical Memory
        |
        v
+----------------------+
|      4 KiB Page      |
+----------------------+
| Object | Object |    |
| Object |         |    |
+----------------------+
```

The paging subsystem provides pages.

The heap divides those pages into smaller allocations suitable for ordinary kernel objects.

The two systems therefore complement one another.

---

# Why the Placement Allocator Is Not Enough

During system initialization, the placement allocator performed exactly the task it was designed for.

Whenever the kernel requested memory, the allocator returned the next unused address.

```text
Memory

+----------------------------------------+
| Used | Used | Used | Free..............|
+----------------------------------------+
                       ^
                  Next Allocation
```

The algorithm is remarkably simple.

Memory is never searched.

No bookkeeping is required.

Allocation takes constant time.

Unfortunately, this simplicity comes at a cost.

Nothing can ever be released.

If a subsystem allocates one kilobyte and later no longer requires it, that memory remains permanently unavailable.

Eventually, the allocator reaches the end of available memory.

The problem is not that memory has disappeared.

Rather, the allocator has no mechanism for reusing it.

A dynamic operating system requires an allocator capable of both allocation and deallocation.

---

# What Is a Heap?

A heap is simply a region of memory managed by an allocator.

Unlike the stack, which grows and shrinks in a strict last-in, first-out order, heap allocations may occur in any order and may be released independently.

Conceptually, the heap evolves over time.

```text
Initially

+--------------------------------------+
|              One Free Hole           |
+--------------------------------------+


After Several Allocations

+------+--------+------+--------------+
|Used  | Used   |Used  | Free         |
+------+--------+------+--------------+


After Freeing One Block

+------+--------+------+--------------+
|Used  | Hole   |Used  | Free         |
+------+--------+------+--------------+
```

Notice that unused memory gradually becomes scattered throughout the heap.

Managing these free regions efficiently becomes one of the allocator's primary responsibilities.

---

# Blocks and Holes

The allocator views the heap as a sequence of blocks.

Each block exists in one of two states.

It is either

* allocated, or
* free.

Free blocks are traditionally called **holes** because they represent empty space that may satisfy future allocation requests.

```text
Heap

+------+--------+------+--------+
|Used  | Hole   |Used  | Hole   |
+------+--------+------+--------+
```

Whenever a new allocation is requested, the allocator searches for a hole large enough to satisfy the request.

When memory is released, the corresponding block becomes a new hole.

Over time, the heap alternates continuously between these two states.

---

# Finding Free Memory Efficiently

Searching every hole in the heap whenever memory is requested would quickly become expensive.

As the operating system grows, hundreds or even thousands of holes may exist.

The allocator therefore maintains an index containing every free block.

Rather than storing holes in arbitrary order, the index is sorted by size.

```text
Free Hole Index

Smallest
   |
   v

+--------+
| 32 B   |
+--------+
| 64 B   |
+--------+
|128 B   |
+--------+
|512 B   |
+--------+
```

This organization allows the allocator to locate the smallest hole capable of satisfying a request.

Choosing the smallest suitable hole helps reduce wasted space and slows the growth of fragmentation.

Although more sophisticated data structures exist, an ordered array provides an excellent balance between simplicity and performance for an educational operating system.

---

# Splitting Large Blocks

Suppose the allocator finds a free hole of 512 bytes, but the caller requests only 128 bytes.

Using the entire hole would waste most of the available memory.

Instead, the allocator divides the hole into two parts.

```text
Before

+-----------------------------+
|         512-byte Hole       |
+-----------------------------+


After

+--------+--------------------+
|128 Used|     384-byte Hole  |
+--------+--------------------+
```

The requested portion becomes an allocated block.

The remaining space returns to the free-hole index for future use.

Splitting blocks allows the heap to make efficient use of large free regions.

---

# Fragmentation and Coalescing

Over time, allocations and deallocations gradually fragment the heap.

Although a large amount of free memory may exist, it becomes scattered across many small holes.

```text
+----+----+----+----+----+
|Used|Hole|Used|Hole|Used|
+----+----+----+----+----+
```

Eventually, no individual hole is large enough to satisfy a new request.

The allocator therefore attempts to merge adjacent holes whenever possible.

This process is known as **coalescing**.

```text
Before

+----+------+------+
|Used| Hole | Hole |
+----+------+------+


After

+----+-------------+
|Used| Larger Hole |
+----+-------------+
```

By combining neighboring free blocks, the allocator recreates larger regions of available memory and delays fragmentation.

Coalescing is one of the defining characteristics of a practical heap allocator.

---

# Growing and Shrinking the Heap

Unlike the placement allocator, the heap is not limited to a fixed amount of memory.

If no suitable hole exists, the allocator requests additional pages from the paging subsystem.

Conceptually,

```text
Heap Full
     |
     v
Allocate New Pages
     |
     v
Expand Heap
```

Similarly, if a large free region develops near the end of the heap, those pages can be returned to the paging subsystem.

```text
Large Free Region
        |
        v
Release Pages
        |
        v
Shrink Heap
```

The heap therefore grows and shrinks according to demand.

This illustrates another important relationship between the heap and paging.

The heap manages individual allocations.

Paging manages the pages that contain those allocations.

---

# Metadata: Remembering Every Allocation

The allocator cannot simply hand memory to the caller.

It must also remember the size and status of every block.

To accomplish this, each allocation contains small bookkeeping structures surrounding the user data.

Conceptually,

```text
+--------+----------------+--------+
| Header |   User Data    | Footer |
+--------+----------------+--------+
```

The header records information such as the block's size and whether it is currently allocated.

The footer duplicates enough information to locate neighboring blocks efficiently.

These structures are invisible to the caller but are essential for operations such as splitting and coalescing.

Many production allocators use similar techniques, although their metadata is often considerably more sophisticated.

---

# Virtual Memory and the Heap

The introduction of the heap marks the first point at which virtual and physical memory begin to diverge in meaningful ways.

Earlier chapters relied almost entirely on identity mapping.

The heap, however, allocates memory using virtual addresses while the paging subsystem continues managing physical frames.

Conceptually,

```text
Heap Allocation
       |
Virtual Address
       |
       v
Page Tables
       |
       v
Physical Frame
```

Most kernel code never needs to know where a page resides physically.

Occasionally, however, the operating system must interact directly with hardware, such as DMA-capable devices, that require physical addresses.

For this reason, the allocator is capable of returning both the virtual address used by software and the corresponding physical address when necessary.

This is one of the first practical demonstrations that virtual and physical memory are distinct concepts rather than interchangeable addresses.

---

# The Heap Depends on Paging

An important architectural relationship emerges in this chapter.

The heap could not exist without the paging subsystem introduced previously.

Whenever the heap expands, it requests additional pages.

Whenever it contracts, those pages are released.

The interaction is straightforward.

```text
Kernel Requests Memory
          |
          v
Heap
          |
Needs More Space?
          |
         Yes
          |
          v
Paging System
          |
Allocate Pages
          |
          v
Heap Continues
```

The heap therefore builds directly upon the services provided by virtual memory.

This layered design is characteristic of operating systems.

Each subsystem relies upon the abstractions established by earlier components.

---

# Looking Ahead

The kernel heap completes one of the final pieces of the operating system's memory-management infrastructure. The kernel can now allocate and release memory dynamically, reuse freed regions, grow when demand increases, and cooperate with the paging subsystem to obtain additional pages when necessary.

Future chapters will build upon this foundation. Process management, file systems, device drivers, networking, and many other subsystems all depend upon reliable dynamic memory allocation. Almost every kernel object created from this point onward will reside somewhere within the heap.

By combining paging with a dynamic heap, the operating system has taken an important step toward becoming a fully general-purpose kernel. Memory is no longer a fixed resource established during boot. It has become a flexible service that the kernel can allocate, organize, reclaim, and reshape as the needs of the system evolve.

