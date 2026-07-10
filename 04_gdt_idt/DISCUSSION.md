I actually think this chapter benefits from **restructuring** rather than following the source-code order.

The code is organized around files (`gdt.s`, `descriptor_tables.c`, `interrupt.s`, etc.), but students don't naturally think that way. They think in terms of **questions**:

1. *Why do we suddenly need a GDT?*
2. *What problem does an IDT solve?*
3. *What actually happens when an exception occurs?*
4. *How do all these pieces work together?*

Following *On Writing Well*, the chapter should teach one idea at a time, with each section answering one question before introducing the next. The code then becomes evidence of the concepts rather than the focus of the discussion.

I would write the chapter something like this.

---

# Chapter 4: Protecting the Processor

The previous chapter produced visible output. The kernel booted, entered protected mode, and printed text on the screen. Although this was an important milestone, the processor was still operating with almost none of the mechanisms required by a modern operating system.

If something went wrong—for example, if the processor divided by zero or attempted to execute an invalid instruction—the kernel had no way of responding. Likewise, although the processor was capable of enforcing memory protection and privilege levels, none of those facilities had been configured.

This chapter introduces two of the most important data structures in the x86 architecture:

* the **Global Descriptor Table (GDT)**, which defines the processor's execution environment, and
* the **Interrupt Descriptor Table (IDT)**, which tells the processor how to respond when exceptional events occur.

Together, these structures form the foundation upon which every protected-mode operating system is built.

---

# From a Program to an Operating System

An ordinary application waits for the processor to execute its instructions.

An operating system cannot afford to be so passive.

The processor constantly encounters events that require immediate attention. Some originate from the currently executing program, while others originate from external hardware. In every case, the processor must know where to transfer control and under what conditions that transfer is allowed.

Without this mechanism, the processor has no way to recover from errors or communicate with devices.

Conceptually, the processor operates as shown below.

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

The remainder of this chapter explains how the operating system provides the information required for these decisions.

---

# Segmentation and the Role of the GDT

One of the earliest memory protection mechanisms in the x86 architecture is **segmentation**.

Instead of treating memory as one continuous array of bytes, segmentation divides memory into logical regions called **segments**.

Each segment has its own

* starting address,
* size,
* access permissions, and
* privilege level.

The processor consults these descriptions whenever software accesses memory.

Conceptually, segmentation can be viewed as follows.

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

Rather than referring directly to physical addresses, programs refer to these predefined segments.

Although modern operating systems rely primarily on paging for memory management, segmentation remains an essential part of entering protected mode. Every processor executing in protected mode requires a valid Global Descriptor Table.

---

# The Global Descriptor Table

The GDT is simply a table containing descriptions of every segment known to the processor.

Each entry specifies

* where the segment begins,
* how large it is,
* whether it contains executable code or writable data,
* and which privilege level is allowed to access it.

Conceptually, the table resembles the following.

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

Although the descriptors appear complicated when viewed as bit fields, their purpose is straightforward. They define the processor's view of memory.

One entry deserves special attention.

The first descriptor is always the **null descriptor**.

It intentionally describes no memory at all. Hardware expects this entry to exist, and omitting it typically causes the processor to fail immediately during initialization. Although it occupies space in the table, it serves as a reserved value rather than a usable segment.

---

# Privilege Rings

One of the most important ideas introduced by the GDT is the concept of **privilege levels**.

Modern processors recognize four privilege rings.

```
          Ring 0
      Operating System

            |
            |

          Ring 3
      User Programs
```

Ring 0 possesses unrestricted access to the processor. The kernel executes here.

Ring 3 provides the least privilege. Ordinary applications execute in this environment.

Rings 1 and 2 exist in the architecture but are rarely used by modern operating systems.

The processor enforces these privilege levels automatically. Software executing in Ring 3 cannot simply decide to access privileged memory or execute privileged instructions. Every transition between privilege levels must occur through carefully controlled mechanisms.

This hardware enforcement is one of the fundamental reasons operating systems remain stable even when applications crash.

---

# Why the Processor Needs an Interrupt Table

Errors are inevitable.

Programs divide by zero.

Instructions reference invalid memory.

Hardware devices request attention.

When any of these events occur, the processor must immediately stop executing the current instruction stream and begin executing kernel code.

The obvious question becomes:

**Where should it jump?**

The answer is the Interrupt Descriptor Table.

The IDT contains one entry for every interrupt and exception recognized by the processor.

Conceptually, it resembles a telephone directory.

```
Interrupt Number

0  ---> Divide Error
1  ---> Debug Exception
2  ---> Non-maskable Interrupt
3  ---> Breakpoint
...
31 ---> Reserved CPU Exception
```

Instead of storing phone numbers, however, each entry stores the address of an interrupt handler.

Whenever an interrupt occurs, the processor consults this table before transferring control.

---

# The Life of an Exception

One of the easiest ways to understand interrupt handling is to follow a single exception from beginning to end.

Suppose the processor encounters a divide-by-zero operation.

The sequence proceeds as follows.

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

Several observations are worth making.

First, the processor—not the operating system—detects the error.

Second, the processor itself consults the IDT.

Finally, control reaches ordinary C code only after passing through a small assembly-language wrapper.

This separation exists because only assembly language has direct control over processor registers and the interrupt return instruction.

---

# Why Assembly Wrappers Are Necessary

Students often ask why interrupt handlers cannot simply be written in C.

The answer lies in the processor's expectations.

When an interrupt occurs, the processor immediately begins modifying the execution state. Registers must be preserved, stack frames must be constructed, and the interrupt must eventually be terminated with a special return instruction.

Ordinary C functions know nothing about these requirements.

The assembly wrapper therefore performs three essential tasks.

1. Save the processor state.
2. Call the high-level C handler.
3. Restore the original state before returning.

Conceptually,

```
CPU
 |
 v
Assembly Wrapper
 |
 v
C Handler
 |
 v
Assembly Wrapper
 |
 v
Interrupted Program
```

The assembly code is therefore not where operating system logic resides.

It simply acts as a translator between the processor and the C kernel.

---

# Preserving the Processor State

Imagine that an interrupt occurs while a program is performing a calculation.

The processor immediately begins executing the interrupt handler.

If the handler modifies the program's registers without restoring them, the interrupted program resumes execution with corrupted data.

For this reason, every interrupt handler begins by saving the processor's execution state.

```
Before Interrupt

Registers
Stack
Flags
Instruction Pointer

        |

        v

Interrupt Handler

        |

        v

Restore Everything

        |

        v

Continue Program
```

From the program's perspective, it appears as though execution paused briefly and then resumed normally.

This illusion is only possible because the handler faithfully preserves the processor state.

---

# Software Interrupts as a Testing Tool

At this stage of the tutorial, no keyboard, timer, or disk interrupts have been configured.

Instead, the kernel deliberately generates software interrupts.

This allows exception handling to be tested without relying on external hardware.

By intentionally triggering well-known exceptions, the developer can verify that

* the IDT has been loaded correctly,
* the processor reaches the correct interrupt handler,
* the assembly wrapper preserves processor state, and
* the C exception handler receives the expected information.

Testing with software interrupts provides confidence that the interrupt subsystem is functioning before hardware devices are introduced.

---

# The Relationship Between the GDT and the IDT

Although introduced together, the GDT and IDT solve different problems.

The GDT answers the question

> **What execution environments exist?**

The IDT answers the question

> **What should happen when something interrupts execution?**

Their relationship can be summarized as follows.

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

Both tables are required before the operating system can safely execute more advanced features.

---

# Looking Ahead

This chapter establishes the processor's basic protection mechanisms. The kernel now understands memory segmentation, privilege levels, and processor exceptions. More importantly, it has acquired the ability to react when unexpected events occur.

The next chapter extends this foundation by introducing **hardware interrupts**. Instead of responding only to processor-generated exceptions, the operating system will begin communicating with external devices such as the programmable interval timer and the keyboard.

From this point forward, the kernel will no longer execute in isolation. It will begin interacting with the outside world, one interrupt at a time.

---
