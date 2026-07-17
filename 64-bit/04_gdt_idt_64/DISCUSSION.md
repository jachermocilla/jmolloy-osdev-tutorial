# Chapter 4: Protecting the Processor

The previous chapter produced visible output. The kernel booted, made its way through protected mode into 64-bit long mode, and printed text on the screen. That was a milestone, and the processor underneath it was still running with almost none of the machinery a modern operating system depends on.

Divide by zero, or execute an invalid instruction, and the kernel had no way to respond. The processor was perfectly capable of enforcing memory protection and privilege levels, and nothing had told it how.

This chapter introduces two of the most important data structures in the x86 architecture: the **Global Descriptor Table (GDT)**, which defines the execution environments the processor knows about, and the **Interrupt Descriptor Table (IDT)**, which tells the processor where to go when something exceptional happens. Every protected-mode operating system is built on top of the pair.

---

# From a Program to an Operating System

An ordinary application waits its turn and runs its instructions. A kernel cannot afford that kind of passivity, because the processor keeps encountering events that demand immediate attention — some raised by the running program, others by hardware outside the chip. In every case the processor must know where to transfer control, and whether the transfer is permitted.

```
                CPU
                 |
      -----------------------
      |                     |
 Normal Instructions   Exceptional Event
      |                     |
      |                     v
      |              Look up handler
      |              in the IDT
      |                     |
      -----------------------
                 |
          Continue execution
```

Without that information, the processor cannot recover from an error or hear from a device. The rest of the chapter is about supplying it.

---

# Segmentation and the Role of the GDT

Long before paging, x86 protected memory by **segmentation**: rather than one flat array of bytes, memory was described as a set of logical regions called **segments**, each with a starting address, a size, a set of access permissions, and a privilege level. The processor checked those descriptions on every access.

```
Memory

+-------------------------------+
| Kernel Code                   |
+-------------------------------+
| Kernel Data                   |
+-------------------------------+
| User Code                     |
+-------------------------------+
| User Data                     |
+-------------------------------+
```

Programs named a segment and an offset within it instead of naming a physical address, and the processor did the rest.

Long mode has quietly hollowed this out. A 64-bit processor ignores the base and the limit of a code or data segment entirely: every segment starts at zero and spans the whole address space, whatever the descriptor says, and the memory protection the segments once provided now comes from paging. What survives are the access bits — whether a segment holds code or data, and which privilege level may use it — and those are enough to matter, because the processor still refuses to execute without a valid Global Descriptor Table. The table is required, most of its fields are decoration, and the few that remain are the ones this chapter cares about.

---

# The Global Descriptor Table

Each entry describes one segment: where it begins, how large it is, whether it holds executable code or writable data, and which ring is allowed to touch it.

```
Global Descriptor Table

+-------+------------------+
| Entry | Purpose          |
+-------+------------------+
| 0     | Null Descriptor  |
| 1     | Kernel Code      |
| 2     | Kernel Data      |
| 3     | User Code        |
| 4     | User Data        |
+-------+------------------+
```

Viewed as bit fields the descriptors look forbidding, and their job is simple enough to state in a sentence: they define the processor's view of memory.

The first entry deserves attention, because it describes nothing at all. The **null descriptor** exists because the hardware insists on it; leave it out and the processor faults during initialization rather than telling you what you forgot. It occupies a slot in the table as a reserved value, never as a usable segment.

The kernel's own entries are the two the processor will spend all its time in: selector `0x08` for kernel code and `0x10` for kernel data. Those numbers are not arbitrary. A selector is a byte offset into the table, and entries are eight bytes wide, which is why the first real descriptor is 8 and the second is 16. When a later chapter prints a fault's code segment and shows `0x08`, that is the table built here answering.

---

# Privilege Rings

The GDT brings with it the idea of **privilege levels**. The architecture defines four rings, and operating systems use two of them.

```
          Ring 0
      Operating System

            |
            |

          Ring 3
      User Programs
```

Ring 0 has unrestricted access to the processor, and the kernel runs there. Ring 3 has the least, and applications run there. Rings 1 and 2 exist, were meant for device drivers, and are almost universally skipped — partly because two levels are enough, and partly because paging's protection bits only distinguish supervisor from user anyway.

Enforcement is the hardware's job, not the kernel's. Code in ring 3 cannot decide to read privileged memory or execute a privileged instruction; the check happens in silicon, on every access, and every transition between rings must go through a controlled entrance. This is a large part of why an operating system survives a crashing application.

---

# Why the Processor Needs an Interrupt Table

Programs divide by zero, instructions reference memory that is not there, and devices demand attention. When any of it happens, the processor must abandon the instruction stream it was running and start executing kernel code — which raises the only interesting question: where should it jump?

The answer is the Interrupt Descriptor Table, which holds one entry for every interrupt and exception the processor recognizes. It works like a telephone directory.

```
Interrupt Number

0  ---> Divide Error
1  ---> Debug Exception
2  ---> Non-maskable Interrupt
3  ---> Breakpoint
...
31 ---> Reserved CPU Exception
```

Each entry stores the address of a handler rather than a phone number, and the processor looks up the number itself before transferring control. The kernel's part is finished before the first interrupt ever arrives: fill in the table, tell the processor where it lives, and wait.

---

# The Life of an Exception

Following one exception from start to finish explains the whole mechanism. Suppose the processor meets a division by zero.

```
Program
   |
   v
Divide by Zero
   |
   v
CPU detects exception
   |
   v
Consult IDT
   |
   v
Assembly ISR
   |
   v
C Exception Handler
   |
   v
Return to interrupted code
```

Three things in that sequence are worth pausing on. The processor detects the error, not the operating system. The processor consults the IDT, not the operating system. And control reaches C only after passing through a small assembly wrapper, because only assembly can touch the registers directly and issue the interrupt return instruction.

Some exceptions arrive carrying an **error code**, a value the processor pushes to describe what went wrong; a page fault uses it to report whether the page was missing or the access was forbidden. Most exceptions push nothing. The kernel must know which is which for every vector it installs, since a wrapper that guesses wrong leaves the stack off by eight bytes and every field the handler reads is the neighbour of the one it wanted.

---

# Why Assembly Wrappers Are Necessary

Students reasonably ask why the handler cannot just be a C function. The answer is that an interrupt arrives in the middle of somebody else's work.

Picture a program halfway through a calculation, its intermediate values sitting in registers, when an interrupt fires. The processor starts running the handler immediately, on the same registers. Should the handler use them without putting them back, the interrupted program resumes with corrupted data and no indication of why. So every handler begins by saving the processor's state and ends by restoring it, and a C compiler, which knows nothing about interrupt frames or the interrupt return instruction, cannot be trusted to arrange either.

```
Before Interrupt

Registers
Stack
Flags
Instruction Pointer

        |
        v

Assembly Wrapper  (save state)

        |
        v

C Handler

        |
        v

Assembly Wrapper  (restore state)

        |
        v

Continue Program
```

To the interrupted program, execution paused for an instant and then carried on. The illusion holds only as long as the wrapper is exact.

The saved state is more than a backup, and this is the part that pays off later. The registers on the stack are what the interrupt return instruction will reload, which makes them a live copy rather than an archive. Hand the C handler a pointer to that copy and the handler can change where the program resumes, or what value it finds in a register. A page-fault handler maps the missing page and returns to re-run the very instruction that failed. A scheduler rewrites the saved instruction pointer and stack pointer, and the program that resumes is a different program entirely. The wrapper holds no operating system logic; it translates between the processor and the C kernel, and it hands the kernel the steering wheel.

---

# Software Interrupts as a Testing Tool

No keyboard, timer, or disk has been configured yet, so the kernel raises its own interrupts with the `int` instruction and watches what happens. Deliberately triggering a known exception confirms that the table was loaded correctly, that the processor reached the intended handler, that the wrapper preserved the state, and that the C handler received the information it expected — all without a single device attached.

The test is only as good as the paths it exercises, which is worth remembering here. Firing `int $3` proves the no-error-code path works and says nothing whatever about the error-code path, whose stack layout differs and which can be silently wrong until a real fault arrives to prove it.

---

# The Relationship Between the GDT and the IDT

The two tables arrive together and answer different questions.

> **GDT:** what execution environments exist?

> **IDT:** what should happen when something interrupts execution?

```
GDT
Defines execution environment
(Code, Data, Privilege)

            +

IDT
Defines interrupt destinations
(Exceptions, Interrupts)

            =

Protected Operating System
```

They meet in the IDT entries. Each one names not only a handler address but the code segment the handler should run in — a selector from the GDT — so an interrupt is both a jump and a change of environment. Neither table is optional before the kernel can attempt anything more ambitious.

---

# Descriptor Tables in Long Mode

The 64-bit processor keeps both tables and changes what it reads from them.

Segment descriptors are still eight bytes and still nearly all ignored, as described above; the one bit that matters is the flag marking a code segment as 64-bit, and setting it together with the old 32-bit operand-size flag is an illegal combination the processor punishes with a fault on first use rather than a diagnostic.

IDT entries doubled to sixteen bytes, mostly to hold a 64-bit handler address, and picked up a new field along the way: an index into a table of known-good stacks. Leave it zero and interrupts behave as they always have. Set it and the processor switches to a stack you nominated, unconditionally, which is the mechanism that lets a double-fault handler survive a stack overflow that has already destroyed the stack it would otherwise have used.

The wrapper feels the change most. Long mode deleted the single instructions that pushed and popped all the general-purpose registers, so the fifteen of them are saved by hand, in an order that must mirror the C structure describing them exactly, since a register out of place produces no error at all — only wrong values, restored to the wrong places.

---

# Looking Ahead

The processor's basic protections are now in place. The kernel understands segmentation, privilege levels, and exceptions, and — more to the point — it can react when something unexpected happens instead of hanging.

The next chapter opens the door to **hardware interrupts**. Instead of answering only for its own mistakes, the operating system starts hearing from external devices: the programmable interval timer, and eventually the keyboard.

The kernel stops running in isolation from here, one interrupt at a time.
