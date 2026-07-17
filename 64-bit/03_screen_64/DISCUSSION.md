# Chapter 3: Reaching Long Mode, and Printing One Line

The first thing a kernel usually says is:

> Hello, world!

Thirteen characters, and almost none of the work is in producing them. Before a single character can appear, the processor has to be carried from the environment the bootloader left it in to a working 64-bit machine, and nearly every line of this chapter's assembly exists to do that carrying.

This chapter is about the ideas behind that transition rather than the instructions that implement it. The ideas are the durable part: every 64-bit operating system on this architecture performs the same sequence, in the same order, for the same reasons.

---

# From Power-On to a Running Kernel

A processor does not wake up running your kernel. Several layers of software hand it along.

```text
+----------------+
| Power On       |
+----------------+
        |
        v
+----------------+
| Firmware       |
| (BIOS / UEFI)  |
+----------------+
        |
        v
+----------------+
| Bootloader     |
| (GRUB)         |
+----------------+
        |
        v
+----------------+
| Your Kernel    |
+----------------+
```

Firmware brings the hardware up and hands off to a bootloader; the bootloader finds the kernel, loads it into memory, and jumps to its entry point. That is where this chapter begins.

The processor is not in 64-bit mode when it arrives.

This surprises people, reasonably. The chip is a 64-bit chip, so why is it not executing 64-bit instructions? Because compatibility is worth more to the industry than convenience is to you: an x86 processor still starts in a mode a 1985 operating system would recognise, and every advanced feature since is something software must ask for. Long mode is opt-in, and asking for it is your job.

---

# What the Bootloader Hands You

The value of a bootloader that follows a specification is that the kernel knows exactly what it is walking into.

```text
Processor state at the kernel's entry point

Mode        32-bit protected mode
Paging      OFF
Interrupts  masked
Stack       none -- the kernel must supply its own
Registers   one pointer to a structure describing the machine
```

That last item is the bootloader's gift: a table of what memory exists, what modules were loaded, and what the firmware reported. The kernel stashes it and ignores it for five chapters, until chapter 8 needs to find a file.

The rest is a perfectly good environment for a 32-bit program and useless for a 64-bit one.

---

# The Chicken and the Egg

Here is the whole problem of this chapter in two facts.

The processor arrives with **paging off**. Long mode **requires paging** — not as a recommendation or an optimization, but as a precondition. There is no 64-bit mode without address translation; every instruction executed in long mode assumes its addresses pass through page tables.

So the kernel must build a page table before it can run a single 64-bit instruction, which means building it in 32-bit assembly, which means the most 64-bit-specific structure in the system is constructed by code that cannot use 64-bit registers.

You cannot recompile your way out of this. `-m64` produces instructions the processor will not decode yet. The bootstrap has to be written in the mode you are leaving, to construct the thing that lets you leave it.

---

# Identity Mapping

The tables the bootstrap builds do the least interesting thing a page table can do: they map every address to itself.

```text
Virtual Memory          Physical Memory

0x00000000  --------->  0x00000000
0x00001000  --------->  0x00001000
0x000B8000  --------->  0x000B8000
0x00100000  --------->  0x00100000
```

That is the point. The paging hardware switches on and nothing moves. The kernel stays at the megabyte mark where it was loaded, the video buffer stays at `0xB8000`, no address in any existing line of code needs revisiting, and no relocation is required.

Identity mapping is a bridge — a way to satisfy the processor's demand for page tables without yet having to think about virtual memory. Chapter 6 will take it seriously. Today it exists to be ignored.

---

# Four Levels, and the Shortcut

A 64-bit processor translates addresses through four tables, each one 4096 bytes holding 512 entries of eight bytes.

```text
PML4  (Page Map Level 4)
  |
  v
PDPT  (Page Directory Pointer Table)
  |
  v
PD    (Page Directory)
  |
  v
PT    (Page Table)
  |
  v
Physical Memory
```

Filling all four levels to cover a gigabyte would mean thousands of entries, and the bootstrap does not need that kind of precision. There is a shortcut: set the page-size bit in a page-directory entry and the walk stops one level early, with that single entry mapping a **2 MiB page** directly.

```text
4 KiB pages                       one 2 MiB page

+----+----+----+----+----+        +-------------------------+
|Page|Page|Page|Page|Page| ...    |        One Page         |
+----+----+----+----+----+        +-------------------------+
   512 entries per table            1 entry
```

So the bootstrap builds three tables, not four. One top-level table pointing at one second-level table pointing at one directory, whose 512 entries each map 2 MiB — and 512 × 2 MiB is exactly one gigabyte, identity-mapped, in a loop of four instructions.

Each entry is the physical address of a 2 MiB frame with three bits set: present, writable, and page-size. Three bits and a shift are the entire memory model of this chapter.

One precaution in the bootstrap is worth more than it looks. The tables live in `.bss`, and a multiboot loader is *supposed* to zero `.bss` before jumping to you. The bootstrap zeroes them anyway, because "supposed to" is not a guarantee, and the failure mode is unforgiving: one leftover byte with the present bit set is a valid mapping to a random physical address, and the processor will find it the instant paging comes on. Three page-sized `memset`s buy immunity from an entire genre of works-on-my-emulator bug.

---

# The Order of Operations

Long mode is not a switch. It is a sequence, and each step is a precondition for the next.

```text
  cpuid          does this processor have long mode at all?
     |
     v
  build tables   three tables, 1 GiB identity-mapped
     |
     v
  CR4.PAE = 1    64-bit page-table entries are PAE's format
     |
     v
  CR3 = &PML4    tell the MMU where the tables are
     |
     v
  EFER.LME = 1   "I intend to enter long mode"
     |
     v
  CR0.PG = 1     paging on -- and the CPU enters compatibility mode
     |
     v
  lgdt + far jmp load a 64-bit code segment
     |
     v
  64-bit long mode
```

The check at the top costs ten instructions and is worth all of them. Skip it, run on a processor without long mode, and the failure is a silent triple fault and a reboot loop — no message, no fault, nothing to debug. Ten instructions buy an error message instead, and an error message is the difference between an afternoon and a minute.

Three of these steps touch registers the chapter has not needed before. `CR4` holds feature switches, and the one being set enables **Physical Address Extension** — long mode's page-table entries are PAE's entries, so the format must be enabled before the tables mean anything. `CR3` holds the physical address of the top-level table. And the long-mode-enable bit is not in a control register at all: it lives in a **model-specific register**, a bank of chip-specific settings reachable only through a dedicated instruction pair that selects a register by number and reads or writes it in two halves.

The order of the last two matters, and the distinction is worth naming. Setting the long-mode-enable bit declares an *intention* and changes nothing. Turning paging on is what acts on it. And the processor answers with a third bit — long-mode *active* — which it sets itself and which is your evidence that the request was granted. After boot, an emulator's monitor shows the register holding `0x500`: bit 8 is the bit you set, bit 10 is the bit the processor set for you.

---

# Compatibility Mode: the Hidden Transition

Textbooks tend to describe protected mode becoming long mode. What actually happens has a step in between.

```text
Protected Mode
        |
        v
Compatibility Mode      paging on, long mode active,
        |               still decoding 32-bit instructions
        v
64-bit Long Mode
```

The moment paging comes on with long mode enabled, the processor *is* in long mode — and it is still executing 32-bit code, because the code segment it is currently running in is a 32-bit one. Everything about memory has changed; nothing about instruction decoding has.

The way out is a far jump into a code segment marked as 64-bit. The Global Descriptor Table is loaded, the jump reloads the code segment register from it, and the processor sees a descriptor with the long-mode bit set. The instruction *after* that jump is the first 64-bit instruction the kernel executes.

Compatibility mode is not a mistake in the architecture. It exists because a processor cannot change what its instructions mean in the middle of an instruction stream, so the change is bound to the only operation that reloads the code segment. The transition needs a door, and the far jump is the door.

---

# Segmentation Is Almost Over

Old x86 leaned on segmentation, dividing programs into code, data, and stack segments with bases and limits the processor checked on every access. That is not how this kernel — or any 64-bit kernel — manages memory.

```text
32-bit                      64-bit

Segmentation                Paging
     +                          +
   Paging               minimal segmentation
```

The table survives, and most of what it used to say is now ignored: bases and limits mean nothing for code and data in long mode, and every segment spans everything. What is left is the handful of bits the processor still reads — whether a descriptor is code or data, which privilege level may use it, and whether it is 64-bit.

The whole descriptor table for this chapter is three entries: a mandatory null, a code segment, and a data segment. Paging does the rest, which is exactly the arrangement chapter 6 will start exploiting.

---

# Arriving in C

Once the far jump lands, the hard part is over and the kernel can call an ordinary C function.

```text
Assembly Bootstrap
        |
   check the CPU
        |
   build page tables
        |
   enable PAE, paging, long mode
        |
   far jump
        |
        v
      main()
```

The last few instructions before that call are a lesson in themselves. A 32-bit kernel hands its arguments to C on the stack, which is why the original of this file pushes the bootloader's pointer before calling. The 64-bit calling convention puts the first six integer arguments in registers instead, so the pointer goes into a register and nothing is pushed at all.

```text
32-bit                        64-bit

Stack                         Registers

Argument 3                    RDI  first argument
Argument 2                    RSI  second
Argument 1                    RDX  third
Return Address                RCX  fourth
```

This is the first appearance of a difference that will keep appearing. The interface between assembly and C is not a detail of the compiler — it is a contract, it changed completely between the two architectures, and every hand-written stub in the chapters ahead has to honour it.

---

# The Compiler Assumes a World That Does Not Exist

A C compiler targeting x86-64 makes assumptions that are correct everywhere except here, and each one has to be turned off explicitly.

The 64-bit calling convention reserves a **red zone**: 128 bytes below the stack pointer that a leaf function may scribble in without adjusting anything. It is free performance in a normal program, and a trap in a kernel, because an interrupt pushes its frame at the stack pointer and downward — straight through the red zone, over whatever was stashed there, with no indication that anything happened. There is no red zone in 32-bit x86, so there is no 32-bit habit to inherit. The bug does not appear until interrupts do, in the next chapter, and it appears as impossible random corruption.

The compiler also assumes **SSE** exists, because on x86-64 it is part of the base architecture rather than an extension. It will emit vector instructions for something as ordinary as copying a structure, and the first one faults, because nothing has initialised the floating-point unit yet.

And it assumes **position-independent code** is wanted, reaching globals through a register holding a table base. A kernel loaded at a fixed address wants the address.

The theme is worth taking seriously: a freestanding kernel is not a program with the standard library removed. It runs somewhere the compiler's defaults were never designed for, and each default has to be examined rather than inherited.

---

# A 64-bit Kernel in a 32-bit Envelope

The build has a problem that no amount of correct code solves. The Multiboot 1 specification — what GRUB Legacy and `qemu -kernel` implement — knows how to parse ELF32 headers and nothing else. Hand it a 64-bit executable and it declines to load it.

The kernel is, unavoidably, 64-bit code. The resolution is to separate the container from the contents: link as ELF64 so the linker resolves 64-bit relocations properly, then rewrite the *headers* to ELF32 and leave every byte of code and data untouched.

```text
  ld       ------->  kernel64.elf     ELF64 headers, 64-bit code
  objcopy  ------->  kernel           ELF32 headers, the same 64-bit code
                                      |
  the loader reads the headers, copies the sections to 1 MiB, and jumps
```

The loader never learns what it loaded. It reads a description it understands, does what the description says, and transfers control to a mixture of 32-bit and 64-bit machine code it has no opinion about.

This works because every address in this kernel fits in 32 bits — the load address is one megabyte, and a 32-bit ELF field holds it comfortably. It is a trick with an expiry date. Relocate the kernel to the higher half, as a mature x86-64 kernel eventually does, and the addresses stop fitting and the trick stops working.

---

# Why the Screen Driver Does Not Change

The most surprising file in this chapter is the one that was not touched. The screen code is byte-for-byte the 32-bit original.

Identity mapping is why. The video buffer is at `0xB8000` in physical memory, the kernel identity-mapped the low gigabyte, so `0xB8000` is still `0xB8000`, and a pointer to it is a perfectly good 64-bit pointer.

```text
Virtual Address
      |
      v
   0xB8000
      |
      v
Physical VGA Memory
```

The driver does not know paging is on. It cannot tell, and it should not have to, which is what a good abstraction is for: the machine underneath became substantially more sophisticated and the code above did not notice.

The only C change in the entire chapter is two new type definitions for 64-bit integers — and it is worth knowing what did *not* change. A 32-bit integer type is still 32 bits, because the data model these tools use keeps `int` at 32 bits even on a 64-bit machine. What grew is `long`, and, far more consequentially, **pointers**. Almost every difficulty in the chapters ahead traces back to that single sentence.

---

# The Bootstrap Is the Foundation

Beginners find this chapter discouraging, and they are right to: unlike everything after it, nearly every line is mysterious, and the reward for all of it is thirteen characters in a corner of the screen.

The reason is structural. The bootstrap does work that happens once, at the only moment when nothing can be assumed — no memory management, no interrupts, no C, and a processor pretending to be from 1985. Every later subsystem, from the scheduler to the filesystem, runs inside an environment this code created and is allowed to take that environment for granted.

It is the steepest part of the climb, and it is also the shortest. Once the processor reaches `main`, kernel development starts to feel like programming again, and every chapter that follows builds on the ground established here — the point where the kernel stops living in the bootloader's world and starts running the machine.
