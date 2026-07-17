# Chapter 6: Taking Control of Virtual Memory

In Chapter 3, the kernel entered 64-bit long mode by enabling paging. At that time, paging was little more than a bootstrap mechanism. The assembly code constructed a simple identity map using large 2 MiB pages, allowing the processor to execute 64-bit code without requiring the kernel to understand virtual memory.

That arrangement was enough to boot the system, and it was never meant to survive past boot. The page tables were created entirely in assembly, their structure was fixed, and the kernel had no ability to create new mappings, reclaim memory, or respond intelligently to page faults. Paging existed, but the operating system did not control it.

This chapter changes that. Instead of relying on tables created during boot, the kernel takes ownership of virtual memory. It learns how to construct page tables dynamically, allocate physical memory frames, establish new virtual mappings, and respond when software touches unmapped memory.

The transition matters. Memory management stops being something configured once during startup and becomes a subsystem that the kernel actively manages for the lifetime of the machine.

---

# Why the Bootstrap Page Tables Are Not Enough

The tables created during boot solved exactly one problem: they allowed the processor to enter long mode. Every address in the first gigabyte of memory was mapped directly to the same physical address.

```text
Virtual Address
        |
        v
Physical Address
```

This identity mapping was ideal for initialization, because existing code required no modification. It is also a dead end. With a fixed map, the kernel cannot allocate new pages, cannot give different processes separate address spaces, cannot protect one region of memory from another, and cannot recover gracefully when software reads an address that does not exist.

Paging must therefore become a service the operating system manages, rather than a structure the bootloader leaves behind.

---

# Virtual Memory Is an Illusion

Programs never touch physical memory. Each one sees its own **virtual address space**, and the processor translates every virtual address into a physical address before the memory is accessed.

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

The translation happens in hardware. Neither the compiler nor the application performs the conversion; every memory access, instruction fetch, and stack operation passes through the paging hardware without knowing it.

That single layer of indirection gives the operating system enormous freedom. Two virtual addresses may refer to the same physical memory. Two adjacent virtual pages may live in physical locations far apart. Programs cannot tell.

---

# From Large Pages to Small Pages

The bootstrap used 2 MiB pages because a single entry mapped an enormous region, which kept the assembly short. For a running system, that granularity is too coarse. An application asking for 4 KiB of memory would receive a 2 MiB page and waste almost all of it.

The kernel therefore switches to ordinary 4 KiB pages.

```text
Bootstrap

+-------------------------+
|       2 MiB Page        |
+-------------------------+


Runtime

+----+----+----+----+----+
|4KiB|4KiB|4KiB|4KiB|4KiB|
+----+----+----+----+----+
```

Smaller pages mean more page-table entries, and in exchange they give much finer control over allocation and protection. Nearly every modern operating system treats the 4 KiB page as its fundamental unit of memory.

---

# The Four-Level Translation Hierarchy

On 64-bit systems, address translation passes through four tables. Each one narrows the search until the processor reaches the physical frame:

- the **Page Map Level 4 (PML4)**, the root table, whose address the processor keeps in **Control Register 3 (CR3)**
- the **Page Directory Pointer Table (PDPT)**
- the **Page Directory (PD)**
- the **Page Table (PT)**, the only level that names a page of real memory

```text
Virtual Address
        |
        v
+-------------------------------+
| PML4  (Page Map Level 4)      |
+-------------------------------+
     |
     v
+-------------------------------+
| PDPT  (Page Directory         |
|        Pointer Table)         |
+-------------------------------+
     |
     v
+-------------------------------+
| PD    (Page Directory)        |
+-------------------------------+
     |
     v
+-------------------------------+
| PT    (Page Table)            |
+-------------------------------+
     |
     v
Physical Frame
```

Each table is itself one 4 KiB page holding 512 entries of eight bytes. An entry in the first three levels holds the physical address of the table below it, together with a handful of permission bits; only an entry in the PT holds the address of a page of data. Because a table is created only when something in its range is mapped, a program may be handed a 256 TiB address space while the kernel pays for the few tables it actually uses.

---

# Walking the Page Tables

Given a virtual address, the processor does not search anything. Different groups of bits within the address *are* the indexes, one for each level:

```text
 63       48 47    39 38    30 29    21 20    12 11         0
+-----------+--------+--------+--------+--------+-----------+
| sign ext. |  PML4  |  PDPT  |   PD   |   PT   |  offset    |
+-----------+--------+--------+--------+--------+-----------+
       16        9        9        9        9        12
```

Nine bits select one of 512 entries, which is why each level consumes exactly nine bits. Four such fields plus a twelve-bit offset account for 48 bits, and the top sixteen must repeat bit 47. An address that breaks that rule is *non-canonical*, and the hardware rejects it outright.

The walk itself is four lookups and an addition:

```text
Virtual Address
        |
        v
CR3 --> PML4 [ bits 47:39 ] --> PDPT
        PDPT [ bits 38:30 ] --> PD
        PD   [ bits 29:21 ] --> PT
        PT   [ bits 20:12 ] --> Physical Frame
                                     +
                                offset [ bits 11:0 ]
                                     |
                                     v
                              Physical Address
```

The kernel performs the same traversal in software whenever it creates or inspects a mapping, which is why `get_page()` is the heart of the paging subsystem. When an intermediate table is missing, the kernel allocates it on the spot. This lazy construction keeps memory use proportional to the address space actually in use.

---

# A Translation, Step by Step

Watching the hardware translate one address makes the hierarchy concrete. Take this virtual address:

```text
0x00007F5C2E8A9018
```

Write it in binary and cut it into the five fields:

```text
0000000000000000 011111110 101110000 101110100 010101001 000000011000
   sign ext.        PML4      PDPT       PD        PT        offset
                    254        368       372       169       0x018
```

Bits 63 through 48 are all zero and match bit 47, so the address is canonical. The four indexes are 254, 368, 372, and 169. An entry is eight bytes wide, so an index of 254 sits 254 × 8 = 0x7F0 bytes into its table.

Suppose the kernel's tables contain the following entries. Each value packs a physical address in bits 51:12 with the present and read/write bits at the bottom, so an entry of `0x11F003` means *the table below me starts at 0x11F000, it is present, and it is writable*.

| Table | Base       | Index | Entry address | Entry value  |
|-------|------------|-------|---------------|--------------|
| PML4  | 0x10E000   | 254   | 0x10E7F0      | 0x0011F003   |
| PDPT  | 0x11F000   | 368   | 0x11FB80      | 0x00120003   |
| PD    | 0x120000   | 372   | 0x120BA0      | 0x00121003   |
| PT    | 0x121000   | 169   | 0x121548      | 0x002AB003   |

The processor now does the following.

1. Read CR3, which holds 0x10E000, the physical base of the PML4.
2. Read entry 254 at 0x10E7F0. It contains 0x0011F003. The present bit is set, so the walk continues at the PDPT located at 0x11F000.
3. Read entry 368 at 0x11FB80. It contains 0x00120003, sending the walk to the PD at 0x120000.
4. Read entry 372 at 0x120BA0. It contains 0x00121003, sending the walk to the PT at 0x121000.
5. Read entry 169 at 0x121548. It contains 0x002AB003. This is the final level, so the address it holds is the physical frame itself: 0x2AB000, or frame number 0x2AB.
6. Add the offset. 0x2AB000 + 0x018 gives the answer.

```text
Virtual   0x00007F5C2E8A9018
Physical  0x00000000002AB018
```

Four memory reads to satisfy one memory access is an expensive way to fetch a byte, which is why the processor caches finished translations in the Translation Lookaside Buffer and consults the tables only on a miss.

Notice how little the two addresses have to do with each other. Only the low twelve bits survive, because the offset never takes part in the walk; a page begins at a multiple of 4 KiB in both address spaces, and translation replaces the page, never the position within it.

The walk also has an early exit. Had entry 254 of the PML4 been zero, the processor would have stopped at step 2 with nothing to follow, and the remaining indexes would never have been read. That failure is the subject of a later section.

---

# Physical Memory Is a Limited Resource

Virtual memory may look limitless; the RAM behind it is not. The kernel needs a way to remember which physical pages are already spoken for, and the simplest one is a bitmap. Number every physical page in the machine, and give each one a bit.

```text
Frame Bitmap

0 1 1 0 0 1 0 1

0 = Free
1 = Allocated
```

Allocating a page sets its bit; releasing the page clears it; scanning for the first zero finds the next free frame. The scheme is crude, and it is fast, compact, and used by real systems, which is a fair return for a few lines of code.

---

# Mapping Virtual Memory

Creating a mapping means making two decisions that have nothing to do with each other. The kernel picks a free physical frame, and it picks the virtual address that should refer to that frame. Only when both are settled does it write the page tables.

```text
Free Physical Frame
         |
         v
Update Page Tables
         |
         v
Virtual Address Now Exists
```

The worked example above shows how far apart the two can be: an address near 128 TiB backed by a frame at 2.7 MiB. That freedom to pair any virtual address with any physical frame is the whole point of virtual memory.

---

# Page Faults: When Translation Fails

Sooner or later, software reads an address that the walk cannot complete, and the processor raises a **page fault**.

Page faults are expected. Modern operating systems depend on them, and a fault does not mean the program has misbehaved; it means the operating system has been asked to do something before the access can proceed.

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

The processor supplies two pieces of evidence: the faulting address, left in CR2, and an error code whose bits say whether the page was present, whether the access was a write, whether the processor was in user mode, and whether it was an instruction fetch. Together they let the handler tell a missing page apart from a protection violation.

This chapter's handler prints both and halts. Later chapters put the mechanism to work by allocating pages on demand, loading them from storage, or killing the offending process.

---

# Identity Mapping Still Matters

The kernel keeps mapping its own code and data to matching physical addresses, even now that it builds the tables itself. Every kernel virtual address still names the physical location with the same number.

```text
Kernel Virtual Address
          |
          v
Kernel Physical Address
```

The convenience is real: a page table's physical address, dug out of an entry, can be dereferenced as a pointer without conversion, which is exactly what the four-line walk in `get_page()` relies on. That convenience is also a debt. When the kernel eventually relocates into the higher half of the address space, every place that treats the two as interchangeable must be revisited.

---

# Looking Ahead

Everything that follows leans on this chapter. Kernel heaps, user processes, shared memory, copy-on-write, demand paging, and memory protection all reduce to constructing and modifying page tables at runtime, which the kernel can now do.

Before this chapter, the kernel's memory layout was fixed at boot and never changed. It can now create mappings and tear them down, hand out physical frames and reclaim them, and notice when software reaches for memory that is not there. The operating system has moved from **using** virtual memory to **managing** it.

Memory is no longer defined by the hardware alone. It has become a resource shaped by software, which is what lets the kernel promise every process a private, contiguous address space, however scattered the physical memory behind it turns out to be.
