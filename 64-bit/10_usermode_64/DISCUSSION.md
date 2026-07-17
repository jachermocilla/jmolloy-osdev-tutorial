# Chapter 10: User Mode and the System Call Boundary

Every instruction this kernel has executed has run with complete control of the machine. Every task the scheduler created ran inside the kernel, in the kernel's address space, free to touch any byte of memory, any hardware port, and any processor register.

That is not how a computer is supposed to work. People do not run kernels; they run editors, shells, browsers, and compilers, and those programs are expected to do their jobs without being able to take the system down with them — whether by mistake or on purpose.

The idea that makes this possible is **privilege separation**, and this chapter builds it. The processor learns to distinguish trusted kernel code from untrusted application code, a task is dropped into the untrusted side, and the only thing it can still do is ask the kernel for help.

For the first time, the kernel will run software that does not control the machine.

---

# Why User Mode Exists

Consider a machine where every program runs with full privileges. One bad pointer overwrites the operating system. Any program can read another's memory, and so any program can read another's passwords. Any program can reprogram a device, mask an interrupt, or halt the processor. Nothing is reliable, because everything is trusted.

Processors solve this by refusing to trust. Execution happens at a privilege level, and the level decides what is permitted.

```text
        Ring 0 -- the kernel
        +--------------------------------------------+
        | every instruction, every port, every page  |
        +--------------------------------------------+
                            ^
                            |  system calls, interrupts, exceptions
                            v
        Ring 3 -- applications
        +--------------------------------------------+
        | ordinary instructions, user pages only     |
        +--------------------------------------------+
```

The architecture defines four rings and almost every operating system uses two, for a practical reason: paging's protection bits distinguish only supervisor from user, so the middle rings have nothing to enforce with.

The division is not only about safety; it is about responsibility. The kernel owns the hardware — memory, scheduling, filesystems, devices — and applications do the work people care about. When an application needs something the hardware guards, it does not reach for it. It asks, and the kernel decides.

---

# Ring 3 Is Three Constants

Chapter 9 ended by claiming the scheduler was already most of the way to processes. This is where that gets paid out.

A task is a saved frame; `iretq` restores one; and `iretq` reads the privilege level out of the code selector in the frame it pops. So the frame that starts a task in ring 3 is the frame that starts a task in ring 0 with three fields changed.

```text
create_task                      create_user_task
-----------------------          -----------------------
frame->cs      = 0x08;    -->    frame->cs      = 0x1B;   user code, RPL 3
frame->ss      = 0x10;    -->    frame->ss      = 0x23;   user data, RPL 3
frame->userrsp = kstack;  -->    frame->userrsp = ustack; a stack in user pages
frame->rip     = entry;          frame->rip     = entry;
frame->rflags  = 0x202;          frame->rflags  = 0x202;
```

Those selectors are not new either. `0x1B` is entry 3 of the Global Descriptor Table with a requested privilege level of 3 in the low bits; `0x23` is entry 4, likewise. Chapter 4 built both descriptors, labelled them "user mode code" and "user mode data," and never used them. They have been sitting in the table for six chapters waiting for a frame to name them.

The scheduler is not modified and does not care. It hands frames to the stub, the stub restores whatever it is given, and a frame with `CS.RPL = 3` in it resumes in ring 3 exactly as readily as one with `0x08` resumes in ring 0. Preemption of a user task is preemption of any other task.

---

# Returning From an Interrupt That Never Happened

There is no instruction that means "switch to user mode." There is no far jump in long mode to fake it with. The only way down a privilege level is to *return* from an interrupt, so the kernel builds the frame the return expects and executes the return.

```text
enter_user_mode(rip, rsp):

    push  0x23      ss        user data, RPL 3
    push  rsp       userrsp   the task's user stack
    push  rflags    IF set, IOPL forced to 0
    push  0x1B      cs        user code, RPL 3
    push  rip       where the program starts
    iretq
                      |
                      v
    CPU pops all five, sees CS.RPL = 3,
    and drops to ring 3 atomically
```

This is chapter 4's `retfq` trick one ring lower: manufacture the evidence of a control transfer that never took place, and let the hardware act on it.

Two details in that frame are load-bearing. The interrupt flag must be set, or the program runs in ring 3 with interrupts off and the timer can never take the processor back — a user program that cannot be preempted is not much of an improvement. And the I/O privilege level is forced to zero, which is what makes `outb` from ring 3 a fault rather than a device write.

Note also what the code deliberately does not do: it loads the data segment registers by hand but leaves `ss` alone. `iretq` loads `ss` from the frame, and it must arrive there with a privilege level of 3 or the return itself faults.

---

# The Kernel Stack Must Change Hands

An interrupt arrives while a user program is running. The handler cannot run on the stack the program was using — it may be too small, it may be unmapped, it may be a pointer the program invented on purpose to see what the kernel does with it. The kernel must land on a stack it owns.

The processor does this itself, and it needs to be told where to go. That is what the **Task State Segment (TSS)** is for.

```text
  ring 3 running                      ring 0 handling
  +-----------------+                 +------------------+
  | user stack      |   interrupt     | kernel stack     |
  | 0x700000033f98  |  ----------->   | task->kstack_top |
  +-----------------+                 +------------------+
                          ^
                          |
                    CPU reads rsp0 from the TSS
                    and switches stacks before
                    pushing anything
```

The 32-bit TSS was 104 bytes of saved registers, because the 386 could switch tasks in hardware by swapping one TSS for another. Long mode deleted that machinery, and the structure that remains is almost entirely a table of stack pointers. One field matters here: `rsp0`, the stack the processor loads on entry to ring 0. The rest of the table is the interrupt stack mechanism chapter 4 mentioned — stacks the processor switches to unconditionally, which is how a double-fault handler survives the stack overflow that caused it.

Because the field names *the* kernel stack rather than *a* kernel stack, it must be updated every time the scheduler changes tasks. Point it at the wrong task's stack and the next interrupt from ring 3 saves its frame on top of somebody else's, which is the kind of bug that corrupts a task that was not even running.

One more field earns its place: the I/O map base is set past the end of the segment, meaning there is no I/O bitmap, meaning every port is denied to ring 3. Combined with an IOPL of zero, hardware access is closed.

---

# Everything Ring 3 Touches Must Live in a User Page

Chapter 6 put a `user` bit in every page-table entry, and until now it has protected nothing, since everything ran in ring 0 and the bit was never consulted. It becomes real here, and it is stricter than it first appears.

The user program is not loaded from anywhere — it is compiled into the kernel, into its own page-aligned section, and the kernel flips the user bit on exactly those pages before starting the task.

```text
  kernel image                       page permissions

  .text        kernel code           supervisor, exec
  .rodata      kernel strings        supervisor
  .data/.bss   kernel state          supervisor, write
  ------------------------------------------------------
  .user_text   the ring 3 program    USER, exec, not writable
  .user_data   its strings           USER
  ------------------------------------------------------
  user stack   0x700000030000..      USER, write

              everything else remains invisible to ring 3
```

"Exactly those pages" is not a figure of speech. A string literal in the user program would ordinarily land in `.rodata`, which is supervisor-only, and reading its first character from ring 3 is a page fault. The strings must be moved into a user section by hand, and they are.

The same trap has a sharper edge in the system call stubs. They are `static inline`, which is a *hint* — at low optimization, or when the compiler simply decides otherwise, the stub is emitted as a real function in `.text`, and the ring 3 program calls into a supervisor page and faults on the instruction fetch. The fault is correct; the bug is that the code was there at all. `always_inline` turns the wish into a requirement, and copies the stub into the user's own page.

The general rule is worth carrying out of this chapter: when code or data must live in a particular section, the compiler's cooperation is not optional, and `inline` does not ask for it.

---

# Coming Back: the System Call

A user program that can only touch its own pages cannot print. Everything it wants — the screen, a file, its own process id — belongs to the kernel, so it has to ask, and asking means crossing back into ring 0 through a door the kernel controls.

That door is an interrupt. The program puts a number in a register and executes `int $0x80`; the processor switches to the kernel stack named by the TSS, saves a frame, and lands in the handler, at ring 0, on memory the user cannot touch.

```text
  ring 3                                            ring 0

  rax = 0  (SYS_MONITOR_WRITE)
  rdi = pointer to the string
  int $0x80  ------------------------------------>  stub saves a frame
                                                    (on the kernel stack,
                                                     via TSS rsp0)
                                                          |
                                                          v
                                                    syscall_handler(regs)
                                                      fn = syscalls[regs->rax]
                                                      regs->rax = fn(regs->rdi,
                                                                     regs->rsi, ...)
                                                          |
  rax = return value  <---------------------------  iretq restores the frame
```

The dispatcher is one line of ordinary C, and the reason is the calling convention. The System V ABI passes the first six arguments in registers, the interrupt frame already captured every register, so the arguments are simply there — a function-pointer call with six registers read out of the frame. The 32-bit tutorial has to hand-write pushes and pops in assembly, because 32-bit code passes arguments on the stack and the stack in question belongs to ring 3, which the kernel must not touch.

The frame does double duty. It carries the arguments in, and the handler writes the return value into `regs->rax` on the way out, so the value the user program finds in `RAX` after its `int` is a value the kernel wrote into a saved frame that `iretq` then restored. The mechanism from chapter 4 — a handler modifying the live frame — is now the calling convention of an operating system.

---

# One Gate, Not Forty-Nine

The gate for vector `0x80` is marked as reachable from ring 3. Every other gate is not, and the difference matters enough to state plainly.

A gate's descriptor carries a privilege level of its own, and it decides who may reach that vector with an `int` instruction. The original tutorial sets ring 3 on *every* gate in the table, which hands a user program a set of tools it should never have:

```text
  int $0x0e   forge a page fault
  int $0x08   forge a double fault
  int $0x20   forge a timer tick -- and drive the scheduler by hand
```

That last one is worth sitting with. A ring 3 program that can raise the timer vector can call the scheduler whenever it likes, with a frame of its choosing.

Marking only the syscall gate closes it, and the denial comes from the processor rather than from any check the kernel writes. A ring 3 `int $0x0e` does not run the page-fault handler; it raises a general protection fault whose error code says, in effect, *you asked for vector 14 and you are not allowed to*.

---

# What the Hardware Actually Refuses

A line of text printed from ring 3 proves the transition works. It proves nothing about isolation, and isolation is the entire point, so it is worth naming exactly what a hostile task is prevented from doing — and by whom.

```text
  ring 3 attempts                    the CPU's answer

  write to kernel memory       ->    #PF, error 0x7 (present|write|user)
                                     the page is supervisor-only

  outb to a hardware port      ->    #GP
                                     IOPL is 0 and the I/O bitmap is empty

  int $0x0e to forge a fault   ->    #GP
                                     the gate's privilege level is 0
```

Not one of those refusals is a check in the kernel's code. The kernel's whole contribution was setting some bits correctly — a user bit here, an IOPL there, a gate privilege level — and the hardware does the enforcing on every instruction, at no cost, without being asked. That is what makes the boundary trustworthy: a check the kernel performs can be forgotten, and a check the processor performs cannot.

---

# The Isolation This Does Not Yet Give

Being honest about the gaps is more useful than celebrating the milestone.

All tasks still share one address space. Ring 3 is kept out of kernel pages by the user bit rather than by having a different set of page tables, and two user tasks are kept apart only because they were handed different stack addresses. Nothing stops one from reading the other's stack — both are user pages in the same address space. Real isolation needs a page table per process and a `CR3` reload in the scheduler, which is the hole chapter 9 deliberately left open.

The system call boundary has a second gap, and it is the classic one. The handler takes a pointer from a user register and dereferences it, on the kernel's behalf, with the kernel's privileges. A hostile program can pass the address of kernel memory to a call that prints strings, and the kernel will read it and print it — happily, correctly, and catastrophically. The processor cannot help here, because the processor is doing what ring 0 asked. Every pointer that crosses this boundary must be validated by the kernel: in user space, mapped, long enough. This kernel does not do it, and knowing the name of the failure — a **confused deputy** — is most of learning to avoid it.

---

# Looking Ahead

The operating system has changed jobs. Everything up to now was infrastructure the kernel built for itself: interrupts, paging, a heap, a filesystem, a scheduler. With ring 3 running, the kernel becomes a *platform* — software it did not write runs on it, with fewer privileges than it has, reaching hardware only through an interface the kernel defines and the processor enforces.

The program here is trivial, and the execution model around it is the one every modern system uses. What remains is to make it general: an executable format so programs can be loaded from the filesystem instead of linked into the kernel, an address space per process, `fork` and `exec`, and a faster door than `int $0x80`.

The threshold has been crossed regardless. This is no longer a kernel that manages itself. It is an environment that runs and protects other software, which is what a general-purpose operating system is for.
