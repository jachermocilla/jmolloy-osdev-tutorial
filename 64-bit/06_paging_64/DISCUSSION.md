# Chapter 6: Taking Control of Virtual Memory

In Chapter 3, the kernel entered 64-bit long mode by enabling paging. At that time, however, paging was little more than a bootstrap mechanism. The assembly code constructed a simple identity map using large 2 MiB pages, allowing the processor to execute 64-bit code without requiring the kernel to understand virtual memory.

That arrangement was sufficient for booting the system, but it was never intended to be permanent.

The page tables were created entirely in assembly, their structure was fixed, and the kernel had no ability to create new mappings, reclaim memory, or respond intelligently to page faults. In effect, paging existed, but the operating system did not control it.

This chapter changes that.

Instead of relying on page tables created during boot, the kernel takes ownership of virtual memory. It learns how to construct page tables dynamically, allocate physical memory frames, establish new virtual mappings, and respond when software accesses unmapped memory.

This marks an important transition in the development of the operating system. Memory management is no longer something configured once during startup. It becomes a dynamic subsystem that the kernel actively manages throughout the lifetime of the system.

---

# Why the Bootstrap Page Tables Are Not Enough

The page tables created during boot solved exactly one problem.

They allowed the processor to enter long mode.

Beyond that, they offered very little flexibility.

Every address in the first gigabyte of memory was mapped directly to the same physical address.

```text
Virtual Address
        |
        v
Physical Address
```

This identity mapping was ideal for initialization because existing code required no modification.

Unfortunately, it prevents the operating system from using virtual memory for its intended purpose.

The kernel cannot allocate new pages.

It cannot create separate address spaces for different processes.

It cannot protect one region of memory from another.

Most importantly, it cannot recover gracefully when software accesses invalid memory.

To accomplish these tasks, paging must become a service managed by the operating system rather than a static structure built during boot.

---

# Virtual Memory Is an Illusion

One of the most important ideas in operating systems is that programs do not interact directly with physical memory.

Instead, every program sees its own **virtual address space**.

The processor translates each virtual address into a physical address before accessing memory.

Conceptually, the process resembles the following.

```text
Program
    |
Virtual Address
    |
    v
Page Tables
    |
    v
Physical Address
    |
    v
RAM
```

The translation occurs automatically in hardware.

Neither the compiler nor the application performs this conversion.

Every memory access, every instruction fetch, and every stack operation passes through the paging hardware.

This level of indirection gives the operating system extraordinary flexibility.

Two different virtual addresses may refer to the same physical memory.

Conversely, two adjacent virtual pages may reside in completely different physical locations.

Programs never know the difference.

---

# From Large Pages to Small Pages

During the bootstrap, the kernel used 2 MiB pages.

Large pages greatly simplified initialization because a single page-table entry mapped an enormous region of memory.

For a running operating system, however, large pages are often too coarse.

Suppose an application requests only 4 KiB of memory.

Using a 2 MiB page would waste almost the entire allocation.

The kernel therefore switches to ordinary 4 KiB pages.

```text
Bootstrap

+-------------------------+
|       2 MiB Page        |
+-------------------------+


Runtime

+----+----+----+----+----+
|4KB |4KB |4KB |4KB |4KB |
+----+----+----+----+----+
```

Smaller pages increase the number of page-table entries, but they provide much finer control over memory allocation and protection.

Nearly every modern operating system uses 4 KiB pages as its fundamental allocation unit.

---

# The Four-Level Translation Hierarchy

In 64-bit systems, address translation proceeds through four levels of page tables.

Each level narrows the search until the processor finally reaches the desired physical frame.

Conceptually, the hierarchy appears as follows.

```text
Virtual Address
        |
        v
+--------+
| PML4   |
+--------+
     |
     v
+--------+
| PDPT   |
+--------+
     |
     v
+--------+
| PD     |
+--------+
     |
     v
+--------+
| PT     |
+--------+
     |
     v
Physical Frame
```

Each table contains hundreds of entries.

Rather than storing physical memory directly, an entry usually points to the next table in the hierarchy.

Only the final level identifies the actual physical page.

This design allows enormous virtual address spaces while allocating page tables only where memory is actually used.

---

# Walking the Page Tables

When the processor receives a virtual address, it does not search every page table.

Instead, different groups of bits select one entry from each level.

Conceptually, the processor performs a sequence of lookups.

```text
Virtual Address
        |
        v
Select PML4 Entry
        |
        v
Select PDPT Entry
        |
        v
Select PD Entry
        |
        v
Select PT Entry
        |
        v
Physical Page
```

The operating system performs exactly the same traversal whenever it creates or modifies mappings.

One of the central responsibilities of the paging subsystem is therefore to locate the page-table entry corresponding to a particular virtual address.

If intermediate tables do not yet exist, the kernel creates them on demand.

This lazy construction keeps memory usage low while allowing the virtual address space to grow dynamically.

---

# Physical Memory Is a Limited Resource

Virtual memory may appear limitless, but physical memory remains finite.

The operating system therefore requires a method for tracking which physical pages are currently available.

The simplest solution is a bitmap.

Imagine numbering every physical page in the system.

Each page corresponds to one bit.

```text
Frame Bitmap

0 1 1 0 0 1 0 1

0 = Free
1 = Allocated
```

Whenever the kernel allocates a page, the corresponding bit becomes one.

When the page is released, the bit returns to zero.

Searching the bitmap reveals the next available physical frame.

Although simple, this approach is surprisingly efficient and is widely used in educational operating systems.

---

# Mapping Virtual Memory

Creating a mapping involves two independent decisions.

First, the operating system selects a free physical frame.

Second, it decides which virtual address should refer to that frame.

Only after both choices have been made does the kernel update the page tables.

Conceptually,

```text
Free Physical Frame
         |
         v
Update Page Tables
         |
         v
Virtual Address Now Exists
```

Notice that the virtual address and the physical address need not resemble one another.

This separation is the essence of virtual memory.

---

# Page Faults: When Translation Fails

Eventually, software attempts to access memory that has not been mapped.

When this occurs, the processor cannot complete the address translation.

Instead, it raises a **page fault**.

Unlike many processor exceptions, page faults are expected.

Modern operating systems intentionally rely on them.

A page fault does not necessarily indicate that the program has failed.

Instead, it informs the operating system that additional work may be required.

The sequence is straightforward.

```text
Program
    |
Access Memory
    |
    v
Page Present?

   Yes -----------------> Continue

   No
    |
    v
Page Fault
    |
    v
Operating System
```

In this chapter, the kernel simply reports the fault and halts.

Later chapters will handle page faults more intelligently by allocating pages on demand, loading memory from storage, or terminating invalid processes.

---

# Identity Mapping Still Matters

Although the kernel now manages its own page tables, it continues using identity mapping for its own code and data.

This decision keeps the implementation simple.

Every kernel virtual address still refers to the same physical location.

```text
Kernel Virtual Address
          |
          v
Kernel Physical Address
```

Eventually, the kernel will abandon this arrangement and relocate itself into the higher-half address space.

Until then, identity mapping provides a stable foundation while the paging subsystem matures.

---

# Paging Makes the Kernel Dynamic

Before this chapter, the kernel's memory layout was fixed.

Every page table had been constructed during boot.

Every mapping was predetermined.

Nothing changed after initialization.

After this chapter, memory becomes dynamic.

The kernel can create mappings, remove them, allocate physical frames, reclaim unused memory, and detect invalid accesses.

The operating system has moved from simply **using** virtual memory to **managing** it.

This distinction marks one of the most significant milestones in kernel development.

---

# Looking Ahead

The paging subsystem introduced in this chapter provides the foundation for every advanced memory-management feature that follows.

Kernel heaps, user processes, shared memory, copy-on-write, demand paging, and memory protection all depend upon the ability to construct and modify page tables dynamically.

Perhaps more importantly, the kernel now possesses complete control over the processor's view of memory.

From this point forward, memory is no longer defined by the hardware alone. It becomes a resource shaped by software, allowing the operating system to create the illusion that every process owns an independent and contiguous address space, regardless of how physical memory is actually organized.


