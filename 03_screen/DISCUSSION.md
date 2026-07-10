# Understanding a Minimal 64-bit Kernel: Printing "Hello, World!"

The first successful operating system kernel often prints a single message to the screen:

> Hello, world!

Although this output appears simple, producing it requires a remarkable amount of preparation. Before a single character can be displayed, the processor must be transformed from the environment provided by the bootloader into a fully operational 64-bit execution environment. Most of the code in this stage is therefore devoted not to printing text, but to preparing the processor itself.

This chapter explains the concepts behind that transition. Rather than focusing on individual instructions, we examine the ideas that make the bootstrap work. Understanding these concepts is far more valuable than memorizing the assembly code because every modern 64-bit operating system follows essentially the same sequence during startup.

---

# The Journey from Power-On to a Running Kernel

When a computer is powered on, the processor does not immediately begin executing your operating system. Instead, a sequence of software components gradually prepares the machine.

```
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

The firmware initializes the hardware and eventually transfers control to a bootloader. The bootloader locates the kernel on disk, loads it into memory, and jumps to its entry point.

At this point, your kernel finally begins executing.

However, the processor is **not yet in 64-bit mode**.

This surprises many students. Since the processor is physically a 64-bit processor, it seems natural to assume that it begins executing 64-bit instructions. In reality, the processor always starts in a simpler compatibility state. The operating system is responsible for enabling the advanced features required for modern execution.

---

# The Environment Provided by the Bootloader

The bootloader gives the kernel a predictable starting point. This simplifies operating system development because the kernel does not need to understand every detail of the firmware.

The initial environment has several important characteristics.

* The processor executes in **32-bit protected mode**.
* Paging is disabled.
* A stack is not guaranteed.
* The bootloader provides information about available memory and hardware.
* Interrupt handling has not yet been configured by the kernel.

Conceptually, the processor begins in the following state.

```
Processor State

Mode        : 32-bit Protected Mode
Paging      : Disabled
Interrupts  : Disabled
Stack        : Kernel must create one
```

Although this environment is sufficient for running ordinary 32-bit programs, it is insufficient for a modern 64-bit operating system.

The kernel must perform the remaining initialization itself.

---

# Why Switching to 64-bit Is Necessary

Modern operating systems take advantage of features that do not exist in 32-bit mode.

Among the most important are:

* vastly larger virtual address spaces,
* additional processor registers,
* improved calling conventions,
* more efficient instruction encoding,
* support for modern processor features.

The difference between 32-bit and 64-bit execution is much more significant than simply changing the size of integers.

For the processor, entering long mode fundamentally changes how memory is translated, how instructions are decoded, and how software interacts with the hardware.

---

# Long Mode Cannot Be Entered Directly

One of the most important ideas in x86-64 architecture is that long mode cannot simply be turned on with a single switch.

Instead, the processor must pass through several carefully ordered stages.

```
Protected Mode
       |
       |
       v
Build Page Tables
       |
       v
Enable Long Mode
       |
       v
Enable Paging
       |
       v
Compatibility Mode
       |
       v
Far Jump
       |
       v
64-bit Long Mode
```

Each stage prepares the processor for the next one.

Skipping any step prevents the processor from entering long mode successfully.

---

# Why Paging Becomes Mandatory

Students often encounter paging long before they understand why it exists.

In earlier chapters, paging may have seemed like an optional memory-management technique.

In x86-64, it is no longer optional.

Long mode **requires paging**.

This design decision reflects the way modern processors manage memory. Every instruction executed in long mode assumes that addresses are translated through page tables.

Without those page tables, the processor simply cannot operate.

This means the kernel must construct an initial virtual memory system before it can execute a single 64-bit instruction.

---

# Identity Mapping: Making the Transition Simple

The first page tables created by this tutorial perform a very simple mapping known as **identity mapping**.

Identity mapping means that every virtual address initially refers to the same physical address.

```
Virtual Memory          Physical Memory

0x00000000  --------->  0x00000000
0x00001000  --------->  0x00001000
0x000B8000  --------->  0x000B8000
0x00100000  --------->  0x00100000
```

Nothing appears to change.

The processor now translates addresses through the paging hardware, but every address still points to exactly the same location.

This greatly simplifies the bootstrap because existing code continues to work without modification.

The VGA framebuffer remains at the familiar address.

The kernel remains at its original load address.

No relocation is required.

Identity mapping serves as a temporary bridge between physical memory and the virtual memory system that the kernel will construct later.

---

# The Four-Level Paging Hierarchy

Unlike 32-bit processors, which typically use a two-level page table structure, x86-64 introduces a hierarchy consisting of four levels.

```
PML4
 │
 ▼
PDPT
 │
 ▼
Page Directory
 │
 ▼
Page Table
 │
 ▼
Physical Memory
```

Each level narrows the search until the processor finally identifies the physical memory location corresponding to a virtual address.

Although this hierarchy appears complicated at first, it offers enormous scalability.

The operating system can manage enormous virtual address spaces while allocating page tables only where they are needed.

---

# Large Pages Simplify the Bootstrap

Building complete page tables using ordinary 4 KiB pages requires thousands of entries.

Fortunately, the processor also supports **large pages**.

Instead of mapping memory in small 4 KiB pieces, the kernel can map entire 2 MiB regions with a single entry.

```
4 KiB Pages

+----+----+----+----+----+
|Page|Page|Page|Page|Page|
+----+----+----+----+----+


2 MiB Page

+-------------------------+
|        One Page         |
+-------------------------+
```

Using large pages dramatically reduces the amount of initialization required during boot.

Since this kernel only needs the first gigabyte of memory, large pages are an ideal choice.

---

# Compatibility Mode: The Hidden Transition

One concept that often confuses beginners is **compatibility mode**.

Many textbooks describe the processor as moving directly from protected mode into long mode.

The reality is more subtle.

After paging is enabled, the processor enters an intermediate execution state.

```
Protected Mode
        |
        v
Compatibility Mode
        |
        v
Long Mode
```

Compatibility mode already uses the new paging system, but it continues executing 32-bit instructions.

Only after a far jump loads a 64-bit code segment does the processor begin decoding instructions as true 64-bit instructions.

This intermediate state exists because changing the execution mode of a processor is too significant to perform instantaneously.

Compatibility mode provides a safe transition between the two architectures.

---

# The Role of the Global Descriptor Table

Earlier x86 processors relied heavily on segmentation.

Programs were divided into code segments, data segments, stack segments, and many others.

Modern 64-bit operating systems no longer use segmentation for memory management.

Instead, paging performs almost all address translation.

The Global Descriptor Table still exists, but its role is much smaller.

```
32-bit

Segmentation
      +
Paging


64-bit

Paging
   +
Minimal Segmentation
```

The processor still requires descriptors that identify executable code and writable data, but most of the complexity of segmentation disappears.

This simplification is one reason modern operating systems rely almost entirely on paging.

---

# Entering the C Kernel

Once the processor reaches true long mode, the difficult part is finished.

The kernel can finally begin executing ordinary C code.

Conceptually, the transition looks like this.

```
Assembly Bootstrap
        |
        |
Processor Initialization
        |
Memory Initialization
        |
Enable Long Mode
        |
        v
main()
```

From this point onward, kernel development becomes much more familiar.

The operating system can initialize devices, configure interrupts, manage memory, and eventually schedule processes.

The bootstrap exists solely to reach this point safely.

---

# Calling Conventions Change in 64-bit Systems

Moving to 64-bit mode changes more than the processor registers.

It also changes how functions communicate.

In 32-bit systems, function arguments are traditionally placed on the stack.

```
Stack

Argument 3
Argument 2
Argument 1
Return Address
```

In the 64-bit System V ABI used by Linux and GCC, the first several arguments are placed directly into registers.

```
Register File

RDI  -> First Argument
RSI  -> Second Argument
RDX  -> Third Argument
RCX  -> Fourth Argument
```

Passing arguments in registers reduces memory accesses and improves performance.

It also explains why assembly code written for 32-bit kernels cannot simply be recompiled for 64-bit systems.

The interface between assembly and C has fundamentally changed.

---

# Why the Screen Driver Does Not Change

One surprising aspect of this example is that the screen driver remains almost identical to the 32-bit version.

The reason lies in the identity mapping created during initialization.

Since virtual addresses initially equal physical addresses, the VGA text buffer still appears at the same address as before.

```
Virtual Address
      |
      v
0xB8000
      |
      v
Physical VGA Memory
```

The driver neither knows nor cares that paging is active.

From its perspective, nothing has changed.

This illustrates one of the central goals of virtual memory.

Properly designed abstractions allow software to continue working even when the underlying hardware becomes significantly more sophisticated.

---

# The Bootstrap Is the Foundation

Students often become discouraged by the complexity of the bootstrap.

Unlike later kernel components, almost every line appears mysterious.

This is normal.

The bootstrap is unique because it performs tasks that occur only once during the lifetime of the operating system. Later subsystems such as process scheduling, interrupt handling, virtual memory management, and file systems all operate in an environment that the bootstrap has already prepared.

The processor transition therefore represents one of the steepest learning curves in operating system development.

Fortunately, it is also one of the shortest.

Once the processor reaches `main()`, the operating system behaves much more like an ordinary C program. Every subsequent chapter builds upon the stable execution environment established here.

Understanding this transition is therefore one of the most important milestones in learning how modern operating systems are constructed. It marks the point where the kernel moves beyond the bootloader's limited environment and begins taking complete control of the machine.

