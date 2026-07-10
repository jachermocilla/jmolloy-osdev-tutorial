# Chapter 9 (Multitasking) — 64-bit Redesign

This is JamesM's chapter 9, **redesigned** rather than ported. It gives you a
preemptive round-robin scheduler, kernel threads with their own stacks, and a
voluntary `yield()`.

**Read `08_vfs_initrd_64/README.md` first.**

Chapter 6 was a redesign because the *data structure* did not survive. This one
is a redesign because the *idea* does not survive. It is also the shortest
chapter so far, and the one where your kernel gets noticeably better.

```
09_multitasking_64/
├── make_initrd.c, test*.txt, mkinitrd.sh
└── src/
    ├── task.c/.h       ← new; ~120 lines, and that is the whole scheduler
    ├── isr.c/.h        ← handlers now RETURN a frame
    ├── interrupt.s     ← one new instruction: `mov rsp, rax`
    ├── timer.c         ← no longer owns IRQ0
    ├── descriptor_tables.*  ← one new gate, for int $0x81
    ├── paging.c/.h     ← page_fault() signature follows isr_t
    ├── boot.s          ← exports stack_top
    ├── main.c          ← the demo
    └── everything else ← unchanged since chapter 8 (17 files)
```

Note what is **not** here: no `process.s`, no `read_eip()`, no `move_stack()`,
no `clone_directory()`, no `copy_page_physical()`. Roughly 250 lines of the
original are simply deleted.

---

## Building and running

```bash
cd 09_multitasking_64
bash mkinitrd.sh
qemu-system-x86_64 -kernel src/kernel -initrd initrd.img
```

```
Found file dev
Found file test.txt
Found file test2.txt

tasking up. pid = 1
created pids 2 and 3

[A pid=1 tick=1]
  [B pid=2]
    [C pid=3 yielding]
[A pid=1 tick=3]
  [B pid=2]
  [B pid=2]
[A pid=1 tick=6]
...
```

Three tasks, interleaved by a 50 Hz timer.

---

## What the original does, and why none of it is needed

Here is `switch_task()` from the 32-bit tutorial, condensed:

```c
eip = read_eip();            // pop eax; jmp eax
if (eip == 0x12345) return;  // "am I the task that just got resumed?"
current_task->eip = eip;
current_task->esp = esp;
current_task->ebp = ebp;
current_task = current_task->next;
asm volatile("mov %0, %%ecx; mov %1, %%esp; mov %2, %%ebp; \
              mov %3, %%cr3; mov $0x12345, %%eax; sti; jmp *%%ecx"
             :: "r"(eip), "r"(esp), "r"(ebp), "r"(current_directory->physicalAddr));
```

Every line of that exists to answer one question: **where do I resume, and with
what stack?**

Chapter 4 already answered it.

Since chapter 4, the interrupt stub has built a `registers_t` on the stack and
handed the C handler a **pointer** to it. That frame is a complete description of
a thread of control: fifteen general-purpose registers, `RIP`, `RSP`, `RFLAGS`,
`CS`, `SS`. Everything `iretq` needs.

So switching tasks is not a matter of *setting* registers. It is a matter of
*choosing which frame to restore*.

```c
// isr.h -- handlers return the frame that iretq should restore.
typedef registers_t *(*isr_t)(registers_t *);
```

```nasm
irq_common_stub:
    PUSH_ALL
    mov rdi, rsp
    cld
    call irq_handler
    mov rsp, rax        ; <-- the entire context switch
    POP_ALL
    add rsp, 16
    iretq
```

And the scheduler:

```c
registers_t *schedule(registers_t *regs)
{
    if (!current_task) return regs;

    current_task->rsp = (u64int)regs;      // remember where this task's frame is

    current_task = current_task->next;     // round-robin
    if (!current_task) current_task = ready_queue;

    return (registers_t *)current_task->rsp;
}
```

That is the whole thing. One line saves, one line selects, one line returns. The
`mov rsp, rax` in the stub moves onto the incoming task's kernel stack, `POP_ALL`
loads its registers, and `iretq` loads its `RIP`, `RSP` and `RFLAGS` atomically.

No `read_eip`. No magic 0x12345. No inline asm for GCC to miscompile. The
context switch is a `mov`.

### Why the original had to be so complicated

Because it switches tasks *from ordinary C code*, not from an interrupt. It is
standing in the middle of a function, with a live stack frame, and it has to
manufacture the state it would have had if something else had saved it.

Once you have an interrupt frame, the state has already been saved. Use it.

---

## The three bugs this deletes

I chased these in the 32-bit code before porting it. All three are load-bearing
reasons the original does not run on a current toolchain.

### 1. The inline asm clobbers the new EBP

GCC 13 compiles that `asm volatile` block to:

```
mov  -0x14(%ebp),%eax   ; %0 = eip -> EAX
mov  -0xc(%ebp),%edx    ; %1 = esp -> EDX
mov  -0x10(%ebp),%ecx   ; %2 = ebp -> ECX   <-- GCC picked ECX
cli
mov  %eax,%ecx          ; "mov %0, %%ecx"   <-- overwrites the saved EBP
mov  %edx,%esp
mov  %ecx,%ebp          ; ebp = eip, not ebp
```

The template hardcodes `ECX` as scratch but never declares it clobbered, so GCC
allocated the `ebp` *variable* into it. The resumed task runs with a frame
pointer equal to its instruction pointer, and the next `leave; ret` returns to
nowhere. This is a documented GCC-4.8+ bug on the OSDev wiki.

### 2. EBX is not saved, and modern GCC needs it

`switch_task()` saves ESP, EBP, EIP and CR3. Nothing else. With PIE — the default
since GCC 6 — globals are reached through EBX, which holds the GOT base. A
forked child resumes with EBX holding a page-directory physical address and
page-faults on its first global access.

The fix in 32-bit is `-fno-pie`. The fix here is structural: `PUSH_ALL` saves
all fifteen registers, because an interrupt has to.

### 3. `read_eip`'s `pop eax; jmp eax`

Correct, and completely dependent on the compiler not having inlined the call,
not having used a tail call, and not having a shadow stack. It is a trick, and it
is unnecessary once `regs->rip` exists.

---

## What replaces `fork()`

Here is the honest part. **I did not port `fork()`, and you should not either.**

Look at what `fork()` means in chapter 9 of the original: every task runs in ring
0; there is exactly one stack; `initialise_tasking()` calls `move_stack()`, which
relocates the kernel stack to `0xE0000000` and then walks it **rewriting any
value that happens to look like a stack pointer**:

```c
if (( old_stack_pointer < tmp ) && (tmp < initial_esp))
    *tmp2 = tmp + offset;      // hope this was a pointer and not data
```

That is a heuristic, applied to memory, in a kernel. It cannot be made correct.
It exists because a `fork()` that copies a stack to a *different virtual address*
invalidates every pointer into that stack, and the tutorial has no other way to
fix them up.

Real `fork()` avoids the problem entirely: parent and child have **separate
address spaces**, so the child's stack lives at the *same virtual address*, backed
by different physical frames. No pointer needs fixing. But that requires a user
address space to clone — which is chapter 10.

So chapter 9 gets what chapter 9 can correctly have:

```c
int create_task(void (*entry)(void));   // a kernel thread, with its own stack
u32int getpid();
void task_yield();
```

`create_task` hand-builds the frame that `iretq` will restore the first time the
task runs. It is the only place in the kernel that manufactures one, and it reads
like a description of what a thread *is*:

```c
registers_t *frame = (registers_t *)(stack_top_addr - sizeof(registers_t));
memset((u8int *)frame, 0, sizeof(registers_t));

frame->rip     = (u64int)entry;
frame->cs      = 0x08;              // kernel code segment
frame->ss      = 0x10;              // kernel data segment
frame->rflags  = 0x202;             // reserved bit 1, plus IF
frame->userrsp = stack_top_addr;    // its own stack
frame->rbp     = 0;                 // terminate any backtrace here

task->rsp = (u64int)frame;
```

Chapter 10 can build `fork()` on top of this without touching the scheduler.

---

## `task_yield()`, and a nice symmetry

Voluntary yielding needs to reach the same scheduler the timer reaches — which
means it needs an interrupt frame. So raise one:

```c
#define INT_YIELD 0x81
void task_yield() { asm volatile("int %0" :: "i"(INT_YIELD)); }
```

Add `ISR_NOERRCODE 129` to `interrupt.s`, one `idt_set_gate(129, ...)`, and
register `schedule` against it. Preemption and cooperation now travel the same
code path, which is exactly what you want: only one thing in the kernel knows how
to switch tasks.

Note this also required `mov rsp, rax` in `isr_common_stub`, not just in
`irq_common_stub`. Symmetry, and now `int $0x81` costs nothing extra.

---

## One design decision worth stating

**All tasks share one address space, and each has its own kernel stack.**

That ordering matters. The alternative — separate address spaces, one stack VA —
is what forces the original into `move_stack()`. With shared mappings, every task
stack is visible from every address space, so `mov rsp, rax` is safe no matter
which CR3 is loaded.

That is not a shortcut. It is the invariant every real kernel maintains: *kernel
mappings are identical in every address space*. When chapter 10 gives each task
its own PML4, the CR3 reload drops into `schedule()` right here —

```c
    // All tasks share the kernel address space in this chapter, so there is no
    // CR3 to reload. When chapter 10 gives each task its own address space, the
    // switch goes here -- and it is safe precisely because every kernel stack is
    // mapped identically in every address space.
    return (registers_t *)current_task->rsp;
```

— and nothing else changes.

---

## Verifying it, properly

Watching `[A]` and `[B]` alternate proves the scheduler runs. It does not prove
the scheduler is *correct*. A context switch that dropped `R12` would look
identical.

So: compute a long, register-hungry checksum three times with interrupts **off**,
to get ground truth. Then compute the same three checksums in three preemptible
tasks, and compare.

```c
static u64int checksum(u64int seed)
{
    volatile u64int local_marker = 0xA5A5A5A5A5A5A5A5UL;   // canary on our stack
    u64int a = seed, b = 1, c = 2, d = 3, e = 5, f = 7;
    for (u64int i = 0; i < 40000000; i++)
    {
        a = a * 6364136223846793005UL + 1442695040888963407UL;
        b ^= a >> 13; c += b; d ^= c << 7; e += d; f ^= e >> 3;
    }
    if (local_marker != 0xA5A5A5A5A5A5A5A5UL) return 0xDEAD;
    return a ^ b ^ c ^ d ^ e ^ f;
}
```

Six live 64-bit values across a hot loop is more than the six callee-saved
registers, so GCC keeps some in `RBX`, `R12`–`R15` and spills the rest. Any
register lost across a switch, or any stack aliasing between tasks, changes the
answer.

```
uniprocessor truth: 0x38f6acff1f4c9713 0xa2892cba9d60a346 0xdd4a6434c7229319
  task1 rsp=0x10dfc0
  task2 rsp=0xffff800000084fe8
  task3 rsp=0xffff800000089fe8

preempted results:  0x38f6acff1f4c9713 0xa2892cba9d60a346 0xdd4a6434c7229319
ticks elapsed: 110
MATCH: no register or stack corruption across preemption
```

And the interrupt census over the run:

```
   1377 v=20      timer
   2542 v=81      task_yield
```

Nearly four thousand context switches, no faults of any kind, and three
bit-identical checksums.

Read the stack addresses too. Task 1 runs on `boot.s`'s `.bss` stack at
`0x10dfc0`. Tasks 2 and 3 have their own 16 KiB stacks up in the heap at
`0xffff8000_00084fe8` and `0xffff8000_00089fe8` — 0x5000 apart, which is the
16 KiB stack plus the heap's 32-byte header and footer rounded to a page. Three
tasks, three stacks, no overlap.

---

## Things to try

1. **Break `POP_ALL`.** Swap two `pop`s in `interrupt.s`. The `[A]`/`[B]` demo
   still runs happily. The checksum test fails immediately. Keep both.

2. **Delete the `cli` in `create_task`.** Then create tasks from inside a task,
   under load. The ready queue is a linked list being walked by the scheduler in
   interrupt context while you append to it.

3. **Overflow a stack.** `create_task` page-aligns each 16 KiB stack, so an
   overrun runs into the heap block below. Give a task a deep recursion and watch
   `ASSERT(header->magic == HEAP_MAGIC)` fire in `kfree`. Now put an unmapped
   guard page below each stack instead, and get a clean page fault at the exact
   moment of overflow.

4. **Add a `sleep(ticks)`.** Give `task_t` a `wake_at` field, skip tasks whose
   time has not come, and `hlt` when nothing is runnable. This is the first step
   toward a scheduler that is not a busy loop.

5. **Count switches.** Add a `switches` counter to `schedule()`, print it against
   `tick`. Work out why yields outnumber timer interrupts by roughly two to one
   in the verification run above. (Hint: look at what task 1 does once it has
   finished its own checksum.)

---

## Next

Chapter 10 is user mode, and you are in an unusually good position for it.

What you need: a TSS with `RSP0` pointing at the current task's kernel stack; a
ring-3 code and data segment (you built those in chapter 4 and never used them);
`iretq` into `CS = 0x1B`, `SS = 0x23`; and `syscall`/`sysret` or an `int 0x80`
gate with DPL 3.

Two things you already have that the original does not. First, `create_task`
already manufactures an `iretq` frame — pointing it at ring 3 means changing two
constants and `RFLAGS`. Second, `schedule()` has a one-line hole where the CR3
reload belongs, and per-address-space `fork()` slots straight into it.

And when you write `fork()`, notice that chapter 7's direct map means you can
copy a page frame with an ordinary `memcpy` through its direct-map address. The
original disables paging to do this, in `copy_page_physical`, with interrupts off
and a stack pointer that is briefly invalid. You do not have to.
