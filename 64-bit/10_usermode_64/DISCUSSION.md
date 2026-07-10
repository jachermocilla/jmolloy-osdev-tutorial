This chapter is arguably **the most important conceptual milestone** in the book. Up until now, the operating system has really been a sophisticated kernel that runs only trusted code. This chapter answers the question:

> **How does an operating system safely run someone else's program?**

I would make that the central theme. I also would avoid focusing on the GDT, TSS, or `iret` until after students understand *why* those mechanisms exist.

---

# Chapter 10: User Mode and Loading the First User Program

Up to this point, every instruction executed by the operating system has run with complete control over the processor. Every task created by the scheduler has executed inside the kernel, sharing the same address space and possessing unrestricted access to memory, hardware devices, and processor registers.

Although this has allowed us to develop the operating system itself, it is not how modern computers operate.

Users do not interact directly with the kernel. Instead, they run ordinary programs such as text editors, web browsers, shells, and compilers. These applications are expected to execute without compromising the stability or security of the operating system.

Achieving this goal requires one of the most important ideas in computer systems: **privilege separation**.

This chapter introduces user mode, allowing the processor to distinguish between trusted kernel code and untrusted application code. The operating system will load its first user program from the initial ramdisk, create an execution environment for it, and transfer control to it safely.

For the first time, the kernel will execute software that does not possess complete control over the machine.

---

# Why User Mode Exists

Imagine a computer in which every application executes with unrestricted privileges.

A simple programming error could overwrite the operating system itself.

A malicious application could read passwords stored by another program.

Any process could reprogram hardware devices or disable interrupts.

The result would be a system that is unreliable, insecure, and unstable.

Modern processors prevent these problems by dividing execution into different privilege levels.

Conceptually,

```text
+-------------------------+
|      Kernel Mode        |
|     Full Privileges     |
+-------------------------+

+-------------------------+
|      User Mode          |
|   Restricted Access     |
+-------------------------+
```

The operating system executes in the most privileged mode.

Applications execute with limited privileges.

Whenever an application requires a privileged service, it must ask the kernel to perform the operation on its behalf.

This separation protects both the operating system and other running applications.

---

# The Kernel and Applications Have Different Responsibilities

The distinction between kernel mode and user mode is not merely one of privilege.

The two execution environments serve fundamentally different purposes.

The kernel manages hardware resources.

Applications perform useful work for the user.

Conceptually,

```text
Applications

Text Editor
Shell
Compiler
Browser

        |
        v

Operating System Kernel

Memory Management
Scheduling
Filesystems
Drivers

        |
        v

Hardware
```

Applications should not manipulate hardware directly.

Instead, they request services from the kernel, which decides whether those requests are permitted and carries them out safely.

This organization allows many applications to share the same hardware without interfering with one another.

---

# Processor Privilege Levels

The x86 architecture defines several privilege levels, traditionally known as **rings**.

Although four rings exist, modern operating systems typically use only two.

```text
Ring 0

Kernel


Ring 3

Applications
```

Ring 0 possesses unrestricted access to processor instructions, memory management, interrupt handling, and hardware devices.

Ring 3 executes with significant restrictions.

Certain instructions cannot be executed.

Protected memory cannot be accessed.

Hardware devices remain under the exclusive control of the operating system.

Whenever an application attempts an operation beyond its privileges, the processor automatically transfers control back to the kernel.

This hardware-enforced protection forms the basis of operating system security.

---

# Loading a Program

The previous chapter introduced the Virtual Filesystem and the initial ramdisk.

Those components now become immediately useful.

Instead of embedding every executable directly inside the kernel, the operating system loads a program from the initrd.

Conceptually,

```text
initrd

+----------------------+
| User Program         |
+----------------------+

        |
        v

Kernel Loader

        |
        v

User Memory
```

The loader locates the program within the filesystem, copies it into the process's address space, and prepares it for execution.

Although the example program is extremely simple, the loading process establishes the foundation for every executable that will run in the operating system.

---

# Every Process Needs Its Own Execution Environment

Loading executable code into memory is only the beginning.

Before the processor can execute a user program, the operating system must construct an execution environment for it.

Every process requires

* executable code,
* a stack,
* processor registers,
* and an initial instruction pointer.

Conceptually,

```text
+---------------------------+
| Executable Code           |
+---------------------------+
|           ...             |
+---------------------------+
|        User Stack         |
+---------------------------+
```

When execution begins, the processor expects these components to be present.

The operating system is responsible for creating them before transferring control to the application.

---

# Crossing the Boundary Between Kernel and User Mode

Transitioning from kernel mode to user mode is fundamentally different from calling an ordinary function.

A function call remains within the same privilege level.

Entering user mode changes the processor's protection level.

Conceptually,

```text
Kernel Mode

      |
      v

Privilege Transition

      |
      v

User Mode
```

The processor performs several actions simultaneously during this transition.

It changes the current privilege level.

It begins using a user stack.

It loads the application's instruction pointer.

From that moment onward, the processor executes the application with restricted privileges.

The kernel has intentionally surrendered direct control until an interrupt, exception, or system call returns execution to the operating system.

---

# Returning to the Kernel

Applications do not remain permanently in user mode.

Eventually they require operating system services.

Perhaps they wish to read a file.

Perhaps they need additional memory.

Perhaps the timer interrupt occurs.

In every case, execution returns to the kernel.

Conceptually,

```text
Kernel

      |
      v

User Program

      |
      +------ System Call

      |
      +------ Interrupt

      |
      +------ Exception

      |
      v

Kernel
```

The operating system therefore serves as the central authority that coordinates all interaction between applications and hardware.

User programs execute independently, but the kernel remains responsible for managing the entire system.

---

# Why the Processor Needs a Kernel Stack

Suppose an interrupt occurs while a user program is running.

The processor cannot safely continue using the application's stack.

The application could corrupt it, intentionally or accidentally.

Instead, the processor immediately switches to a trusted kernel stack before executing the interrupt handler.

Conceptually,

```text
User Stack

      |
Interrupt
      |
      v

Kernel Stack

      |
Interrupt Handler
```

This guarantees that the operating system always executes using memory under its own control.

The mechanism responsible for identifying the correct kernel stack is the **Task State Segment (TSS)**.

Although the TSS has many historical fields dating back to early versions of the x86 architecture, modern operating systems primarily use it to specify which kernel stack should be activated whenever execution transitions from user mode to kernel mode.

---

# Memory Protection Becomes Meaningful

Earlier chapters introduced virtual memory, but every kernel thread still shared the same address space.

User mode finally gives memory protection practical significance.

Conceptually,

```text
+---------------------------+
| Kernel Memory             |
| Accessible Only by Kernel |
+---------------------------+

+---------------------------+
| User Program              |
| User Memory               |
+---------------------------+
```

Applications execute within their own regions of virtual memory.

Kernel memory remains protected.

If an application attempts to access memory reserved for the operating system, the processor immediately raises an exception.

The operating system can then terminate the offending process without affecting the remainder of the system.

This isolation is one of the defining characteristics of modern operating systems.

---

# The Beginning of Process Isolation

Kernel threads introduced multitasking by allowing multiple execution contexts to share the processor.

User mode introduces a second, equally important concept: **isolation**.

Two applications should not interfere with one another.

One program should not overwrite another's memory.

A faulty application should not crash the operating system.

Conceptually,

```text
+----------------------+
| Process A            |
+----------------------+

+----------------------+
| Process B            |
+----------------------+

+----------------------+
| Operating System     |
+----------------------+
```

Although these processes still share certain kernel resources, the processor now distinguishes between trusted kernel execution and untrusted application execution.

Future chapters will strengthen this separation further by giving each process its own virtual address space.

---

# The Operating System Becomes a Platform

This chapter changes the role of the operating system.

Earlier chapters focused on building kernel infrastructure: interrupt handling, paging, memory allocation, filesystems, and scheduling. Those components enabled the operating system to function internally.

With the introduction of user mode, the operating system begins serving another purpose. It becomes a platform upon which other software can execute.

Applications are no longer part of the kernel.

They become clients of the kernel.

The operating system now exists not only to manage hardware but also to provide a secure and controlled environment in which independent programs can run.

This transition marks one of the defining moments in operating system development.

---

# Looking Ahead

Introducing user mode fundamentally changes the architecture of the operating system. The kernel is no longer the only software executing on the processor. It now manages programs that execute with fewer privileges, communicate through controlled interfaces, and depend upon the operating system for access to hardware and other protected resources.

The user program loaded in this chapter is intentionally simple, but it establishes the execution model used by every modern operating system. Future chapters will extend this foundation by introducing system calls, independent virtual address spaces, executable file formats such as ELF, process creation through operations such as `fork()` and `exec()`, and mechanisms for communication between processes.

With user mode in place, the operating system has crossed an important threshold. It is no longer merely a kernel capable of managing itself. It has become an environment capable of running and protecting other software, fulfilling one of the central purposes of every general-purpose operating system.

