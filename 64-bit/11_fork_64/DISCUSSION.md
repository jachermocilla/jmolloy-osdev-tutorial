# Chapter 11: `fork()` and Private Address Spaces

The last chapter taught the processor to distrust. Programs run in ring 3, they cannot touch a supervisor page, and they reach the kernel only through a door the hardware guards.

They still share a room. Every task, kernel and user alike, runs on one set of page tables, and "user pid 3" can read "user pid 4"'s stack — not because of a bug, but because there is only one address space with the user bit flipped on in places. Ring 3 protects the kernel from users. It does nothing to protect users from each other.

This chapter finishes the job. Each process gets page tables of its own, so that two programs can use identical addresses and never meet, and the kernel gains the system call that made Unix Unix: **`fork()`**.

---

# One Address, Two Meanings

If two programs share memory, they share every mistake. A stray pointer in one corrupts the other; a global written by one is read by the other; a heap trampled by one takes down both. Nothing can be reasoned about locally, because nothing is local.

Give each process its own translation of the same numbers and the problem evaporates.

```text
                    virtual 0x107060

   Process A  ----[ A's page tables ]---->  physical frame 0x1F4000
   Process B  ----[ B's page tables ]---->  physical frame 0x2A7000
```

Both programs load from the same address; each gets its own byte. A virtual address is not a location in the machine — it is a location *within a process*, and the tables are what make the difference. This is the illusion of exclusive ownership, and it is the most valuable thing paging buys.

The point of `fork()` is not that it duplicates a process. It is that the duplicate then goes its own way: the parent adds to a variable, the child subtracts from the same variable at the same address, and neither can see the other doing it.

---

# The Address Space Has Two Halves

Users need privacy. The kernel needs the opposite. Every process should run the same kernel code, hit the same handlers, and share the same heap, and copying the kernel per process would be both wasteful and wrong — a heap allocation made by one process must be visible to the kernel while it services another.

The x86-64 address space is already split down the middle, and the split is exactly the one the kernel wants.

```text
  PML4 entry           address range              contents

  511  ---+
          |            0xFFFF800000000000         KERNEL
  256  ---+                and above              heap, kernel stacks, tables
                                                  SHARED by every process
  ------------------------------------------------------------------------
  255  ---+
          |            0x0000000000000000         USER
    0  ---+                and up                 code, data, stack
                                                  PRIVATE to each process
```

Entries 0 through 255 of the top-level table map the lower half, where user memory lives. Entries 256 through 511 map the upper half, which in this kernel is all kernel: the heap, every kernel stack, everything `kmalloc` returns.

So the rule for making a new address space fits in one sentence: **share the kernel by reference, copy the user by value.**

"By reference" is literal. The child's top-level table gets a byte-for-byte copy of the parent's upper-half entries, which means both tables point at the *same* second-level tables underneath. Not similar mappings — the same ones. A page the kernel maps after the fork appears in both processes automatically, because there is only one set of kernel tables and both address spaces name it.

That is the invariant chapter 9 asserted when it claimed a context switch was safe across an address-space change, and chapter 10 relied on when it pointed the TSS at a kernel stack. Here it stops being a comment and becomes a loop.

---

# `fork()` Is a Frame and a `memcpy`

The famous strangeness of `fork()` is that it returns twice: once in the parent, with the child's process id, and once in the child, with zero. Written as a mystery, it is a mystery. Written in terms of what this kernel already has, it is an assignment.

`fork()` is a system call, which means the caller's complete state is *already saved* — the interrupt frame from `int $0x80` describes, exactly, a program sitting mid-call waiting for a return value. To make a second process that resumes from the same call, copy that frame onto a new kernel stack and change one field.

```text
  parent's kernel stack              child's kernel stack

  +----------------------+           +----------------------+
  | ss, userrsp, rflags  |  memcpy   | ss, userrsp, rflags  |
  | cs, rip -> after int |  ------>  | cs, rip -> after int |
  | ...                  |           | ...                  |
  | rax = child pid      |           | rax = 0              |  <- the only change
  +----------------------+           +----------------------+
        parent resumes                     child resumes
        returning the pid                  returning zero
```

The parent's frame is never touched, so its `int $0x80` returns whatever the handler put in `rax` — the child's pid. The child's frame says zero, so when the scheduler first picks it, `iretq` restores it and the same instruction returns zero. There are not two returns from one call. There are two frames, differing in one field, and each returns once.

Everything else `fork` does is bookkeeping: a `task_t`, a kernel stack, a cloned address space, and an append to the ready queue. There is no `read_eip`, no stack-pointer heuristic, no assembly. The 32-bit tutorial spends two hundred and fifty lines and several subtle bugs arriving here, because it is trying to duplicate a thread of control from inside ordinary C, where no one has saved the state for it. Chapters 4 and 9 already did.

---

# Cloning an Address Space

The substance is in the tables. A clone allocates a fresh top-level table and walks the parent's, entry by entry, treating the two halves differently.

```text
  parent PML4                          child PML4

  [511] --------------------------.
  [...]  upper half, kernel        `-->  [511]  same 8 bytes, same tables
  [256] --------------------------'      [...]  SHARED by reference
                                         [256]
  ------------------------------         ------------------------------
  [255]                                  [255]
  [...]  lower half, user                [...]  walked, page by page
  [  0] --------------.                  [  0]
                       \
                        \      for each leaf entry:
                         \
                          +--> user page?   allocate a new frame,
                          |                 copy 4096 bytes into it,
                          |                 point the child's entry at it
                          |
                          +--> kernel page? copy the entry as-is
```

Above the halfway line, an entry is copied and the work stops there — eight bytes, and two processes now share an entire subtree of kernel mappings. Below it, every level is rebuilt and every user frame is duplicated, so the child ends with identical *contents* at identical *addresses* backed by entirely different physical memory. From that instant the two cannot reach each other.

Notice the test at the leaf. A page is copied because its `user` bit is set, not because of where it sits, which matters in this kernel: chapter 10 put the ring-3 program in a section inside the very first PML4 entry, alongside the kernel's identity map, so that one entry contains both kinds of page and the clone must sort them out one leaf at a time.

---

# Where the Direct Map Earns Its Keep

Look again at what the copy is: `memcpy` from one physical frame to another, using the physical addresses out of the page-table entries as pointers.

```text
  src_pte->frame << 12   ==  a physical address
                         ==  a valid pointer, because of the direct map
  memcpy(dst_phys, src_phys, 4096);
```

That works only because chapter 7 arranged for physical address *P* to be readable at virtual address *P*. The number in the page-table entry is simultaneously the frame's physical address and a pointer the kernel can dereference, so copying a frame is one library call.

The 32-bit tutorial cannot do this, and what it does instead is worth knowing about. Its `copy_page_physical` **disables paging**, copies, and turns paging back on — with interrupts off, and with a stack pointer that for a few instructions refers to an address that no longer means anything. It is the most dangerous sequence in that codebase, and the whole of it is deleted here by a decision made four chapters ago.

The same convenience runs through the rest of the clone. Every table it allocates comes from the frame bitmap in the direct-mapped low region, so the address it writes to is the address `CR3` needs, and there is no conversion to get wrong.

---

# The Hole in the Scheduler

Chapter 9 left two lines' worth of comment where the address-space switch belonged. Filling it in is anticlimactic:

```text
    current_task = current_task->next;              /* choose */

    if (current_task->pml4_phys != current_pml4_phys)
        switch_pml4_phys(current_task->pml4_phys);  /* the address space */

    return (registers_t *)current_task->rsp;        /* the context switch */
```

Reloading the top-level page table in the middle of an interrupt handler sounds reckless. It is safe for three reasons, and all three were arranged in advance.

The kernel stack the scheduler is standing on lives in the upper half, shared by reference, so it is mapped at the same address before and after the reload. The kernel code about to run — the register pops, the `iretq` — is likewise upper-half and likewise unchanged. And the TSS has already been pointed at the incoming task's kernel stack, so the next interrupt from ring 3 lands where it should.

Nothing the processor can currently see changes. Only the lower half flips, and the kernel is not looking at the lower half. The instruction that switches the entire meaning of memory is safe precisely because the half that matters at that moment is identical in both.

---

# What "Different Address Space" Means, Measured

The demo prints, from each process, the address of a shared variable and the value in `CR3`:

```text
  fork() returned 4
  cr3=0x116000  &counter=0x107060      <- parent
  cr3=0x229000  &counter=0x107060      <- child
```

One address. Two sets of page tables. Two physical frames. That pair of lines is the definition of a process, and everything else in the chapter exists to produce it.

Then the two processes drive that address in opposite directions, and never collide:

```text
  [parent] pid=3 counter=1100        [child] pid=4 counter=900
  [parent] pid=3 counter=1200        [child] pid=4 counter=800
  [parent] pid=3 counter=1300        [child] pid=4 counter=700
```

Both started from 1000. Both write `counter`. Neither can see the other's arithmetic, because the same nine hex digits mean different memory to each of them.

---

# Tearing It Down

Freeing an address space is the clone run backwards, and it has to be exactly as asymmetric. Walk the lower half, free every private user frame and every table the process owned alone, and touch nothing above the line — those tables belong to every other process too.

The failure modes here are silent in both directions. Free one shared kernel frame by mistake and the kernel corrupts a few forks later, somewhere else entirely. Free too little and the machine leaks memory slowly enough that nothing looks wrong until it does. Neither shows up on screen.

So the correctness of teardown is not something to look at. It is something to count:

```text
  free frames at start:                 3546
  free frames after 200 clone+free:     3546
```

Two hundred address spaces created and destroyed, and the frame bitmap ends exactly where it started. That number is the test.

---

# What This Costs

Copying every user page is honest and expensive. A real program occupies megabytes, and duplicating all of it on every `fork` is time and memory spent on data the child may never read — especially since the most common thing a child does is immediately replace its entire memory image with a different program.

The standard cure is **copy-on-write**: at fork, share the frames read-only instead of copying them, and let the first write take a page fault, at which point the kernel makes a private copy of that one page and lets the instruction retry.

```text
  fork          both processes point at the same frame, marked read-only
     |
  write attempt in either      ->  page fault
     |
  kernel copies that one page, marks it writable, resumes the instruction
```

The reason this chapter does not do it is worth stating: the copy is easy to trust, and copy-on-write is easy to get subtly wrong. Once whole-page copying is understood, the optimization becomes an obvious refinement instead of a leap — and it would be the first time in this tutorial that `page_fault()` does something other than panic.

Two of this implementation's limits deserve naming out loud rather than being discovered.

`task_exit` leaks. It reclaims the address space, which is the expensive part, and it cannot free the kernel stack it is standing on or the structure it is reading from. A real kernel hands both to a reaper thread running in another context; this one leaves two small fixed-size allocations behind and admits it at the point where it happens.

Every fork also duplicates page tables it has no use for. Because the user program lives in the same top-level entry as the kernel's identity map, that entry cannot be shared by reference and must be walked, and its ten-odd tables are rebuilt on every clone. The fix is the higher-half migration this series has been pointing at since chapter 6: move the kernel entirely above the line, and the lower half becomes user-only and cheap to clone.

---

# Looking Ahead

The kernel now has processes in the full sense. Each has a private address space, they are created by the call Unix has used since 1970, they are preemptively scheduled across those address spaces, and they clean up after themselves. Together with ring 3, that is most of what the word "operating system" means.

Three things follow naturally, and each depends on this one. `exec()` and an executable loader would let a program come from the filesystem rather than being linked into the kernel — which retires the `.user_text` arrangement, hands the lower half entirely to users, and makes the wasteful clone above disappear. Copy-on-write would make `fork` cheap, and it is motivated most by `exec`, since the pair together currently copies every page and then discards it. And `wait()` with a reaper would fix the leak and complete the Unix process model.

None of them needs anything this series has not already built.
