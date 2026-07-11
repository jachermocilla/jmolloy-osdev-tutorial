# Appendix C - Task Execution

## A Capstone Where Structures, Pointers, Function Pointers, and the Language Boundary Meet

Every chapter so far has taught one idea and pointed at the kernel code that needs it. This appendix does something different. It introduces almost no new C. Instead, it takes a single subsystem — the one that lets a kernel run more than one thread of control — and shows how the ideas you already know combine to build it.

The subsystem is the multitasking code of the 64-bit tutorial, found at

```text
64-bit/09_multitasking_64
```

and its core is a single file, `task.c`, of roughly one hundred and twenty lines. That file gives the kernel a preemptive round-robin scheduler, kernel threads that each own a stack, and a voluntary `task_yield()`. It is short not because it does little, but because it rests on everything the book has already built.

Read this appendix as a capstone. Its purpose is not to teach scheduling theory — Chapter 15 §15.8 already warned that policy is a subject for other books — but to demonstrate a claim this book has made repeatedly: **when the data structure is right, the algorithm nearly disappears.** Nowhere in the tutorial is that claim proved more convincingly than here.

Wherever a topic was covered earlier, the chapter is named. Keep `task.c`, `task.h`, and `isr.h` open beside you.

---

## Learning Objectives

After completing this capstone, you should be able to

* explain what a "thread of control" is in terms of processor state,
* recognize the register frame as a complete, restartable description of execution,
* read the `task_t` structure and the ready queue as a linked list,
* explain why, in this design, a context switch is a single `mov` instruction,
* read the three-line scheduler and say what each line does,
* explain how `create_task` manufactures a thread that has never run,
* describe how cooperative and preemptive switching reach the same code path,
* articulate why the correct data structure deleted hundreds of lines of fragile code.

---

## C.1 Why This Is a Capstone

Look at what the execution code actually uses, and notice that you have seen all of it:

* The register frame is a **structure describing hardware** — Chapter 2, and specifically the `registers_t` type introduced in Chapter 5 §5.5.
* The scheduler receives a **pointer** to that frame so it can act on live state — Chapter 3 §3.9, and the reasoning of Chapter 5 §5.6.
* The scheduler is dispatched through a **function pointer** — Chapter 5 §5.7.
* The ready queue is a **self-referential structure forming a linked list** — the same shape as the free list of Chapter 8 §8.6.
* Each task's stack comes from the **heap allocator** — Chapter 8 §8.11.
* The switch itself lives on the **boundary between C and assembly** — Chapter 10.

No new syntax appears. What appears is fluency: function pointers that return pointers to structures, a frame that is written by assembly and read by C, a linked list walked in interrupt context. This is the book's material used at full strength, which is exactly what a *mastering* text should end on.

The spine of the appendix is a single sentence, and it is worth holding in mind from the start: *understand the frame, and the scheduler writes itself.*

---

## C.2 What "Execution" Means to a Processor

Chapter 10 §10.10 described a context switch abstractly, as preserving Process A and restoring Process B. This appendix makes that concrete, so begin by asking what, precisely, must be preserved.

At any instant, a running thread of control is completely described by a small amount of processor state:

* the general-purpose registers, which hold its working values,
* the stack pointer, which locates its stack,
* the instruction pointer, which says what runs next,
* the flags register, which holds condition bits and the interrupt-enable flag.

That is the whole of it. If you could capture those values, freeze them, and later put them back exactly, the thread would resume as though nothing had happened — it could not tell it had ever stopped. This is the illusion Chapter 10 §10.10 named: each task believes it owns the processor.

The entire multitasking system is built on one observation about that list of state. **You have already seen every item on it saved to memory, together, in one place — by the interrupt machinery of Chapter 5.**

---

## C.3 The Register Frame Is the Thread

Recall from Chapter 5 §5.4 what happens when an interrupt fires. The processor pushes some state automatically, an assembly stub pushes the rest, and the C handler is handed a pointer to the result. In the 64-bit kernel that result is `registers_t`, now complete for long mode:

```c
typedef struct registers
{
    // Pushed by us, in interrupt.s.
    u64int r15, r14, r13, r12, r11, r10, r9, r8;
    u64int rbp, rdi, rsi, rdx, rcx, rbx, rax;

    // Pushed by the ISR stub: interrupt number and error code (or a dummy 0).
    u64int int_no, err_code;

    // Pushed by the processor automatically.
    u64int rip, cs, rflags, userrsp, ss;
} registers_t;
```

Read this structure the way Chapter 13 §13.8 insists — data before algorithms — because everything else follows from it. Compare its fields against the list in §C.2: fifteen general-purpose registers, `rip` (the instruction pointer), `userrsp` (the stack pointer), `rflags` (the flags). Every item that describes a thread of control is here, in one contiguous block of memory.

That is the key realization, and it deserves to be stated plainly:

> A `registers_t` frame is not merely information *about* a thread. It *is* the thread, in the only form the processor cares about. Everything needed to stop a thread and later restart it is already sitting in this structure, on the stack, the moment the handler runs.

Chapter 5 §5.6 explained why the handler receives a *pointer* to the frame rather than a copy: a copy would let the handler read the state but never change what the processor restores on the way out. Under the System V AMD64 ABI (Chapter 10 §10.4), passing this 176-byte structure by value would force the caller to copy it into a fresh stack slot, and the handler would be editing a corpse. The pointer keeps the handler connected to the live frame — the one `iretq` will actually reload. Hold on to that fact; the scheduler depends entirely on it.

---

## C.4 A Task Is Almost Nothing

If the frame already captures a thread, then a "task" needs to store shockingly little. Here is the whole of it, from `task.h`:

```c
typedef struct task
{
    u32int id;              // Process ID.
    u64int rsp;             // Saved pointer to this task's registers_t frame.
    u64int kstack;          // Base of this task's kernel stack (for freeing).
    struct task *next;      // The next task in the ready queue.
} task_t;
```

Four fields. Read them with Chapter 2 in hand, because this is a structure used exactly as Chapter 2 §2.3 described — a blueprint for a small record — and with Chapter 8 in hand, because the `next` field makes it a node in a linked list, the same construction as the heap's free list (§8.6).

The most important field is `rsp`. It is not a copy of the task's registers; it is a *pointer to where that task's frame lives* on the task's own kernel stack. When a task is not running, its complete state sits frozen in a `registers_t` on its stack, and `task_t::rsp` remembers the address. The task structure is a bookmark, not a photograph.

Two globals tie the tasks together:

```c
volatile task_t *current_task = 0;
volatile task_t *ready_queue  = 0;
```

`ready_queue` is the head of the linked list; `current_task` points at whichever task is running now. Both are `volatile` for the reason Chapter 3 §3.12 gave: they are changed inside an interrupt handler, asynchronously with respect to ordinary code, and the compiler must not cache them in a register across that boundary.

That is the entire data model. A list of small structures, each holding a pointer to a saved frame. Everything from here on is a consequence of getting these two paragraphs right.

---

## C.5 The Context Switch Is a `mov`

Now the payoff. Since a task's state already lives in a frame, and the handler already holds a pointer to a frame, switching tasks cannot require *setting* registers one at a time. It requires only *choosing which frame to restore*. Watch the assembly stub do it, from `interrupt.s`:

```nasm
irq_common_stub:
    PUSH_ALL                 ; save the outgoing task's 15 registers
    mov rdi, rsp             ; RDI = pointer to the frame (ABI first argument)
    call irq_handler         ; C decides what runs next; returns a frame in RAX
    mov rsp, rax             ; <-- the entire context switch
    POP_ALL                  ; load the incoming task's 15 registers
    add rsp, 16              ; discard int_no and err_code
    iretq                    ; load its RIP, RSP, RFLAGS, CS, SS atomically
```

Read it as Chapter 10 §10.12 taught — ask what enters, what leaves, what state changes — and the shape is clear. `PUSH_ALL` finishes building the outgoing task's frame on its stack. `mov rdi, rsp` hands the C handler a pointer to that frame, obeying the calling convention of Chapter 10 §10.4. The handler returns, in `RAX`, a pointer to *some* frame. Then one instruction does the work:

```nasm
    mov rsp, rax
```

If `RAX` holds the same frame that came in, the stub unwinds the interrupt normally and the same task resumes — this is the ordinary interrupt return of Chapter 5. But if `RAX` holds a *different* task's frame, on a *different* stack, then `POP_ALL` loads that task's registers and `iretq` loads its instruction pointer, stack pointer, and flags. The processor walks away as that other task. The switch was a single move.

This is why Chapter 10 §10.8 mattered: only `iretq`, not a C `return`, can reload `RIP`, `RSP`, and `RFLAGS` together and atomically. C cannot express the final step; assembly must. But notice how *little* assembly — one `mov` beyond the interrupt handling you already had.

The mechanism reaches into C through a function-pointer type that Chapter 5 §5.7 prepared you for, now sharpened:

```c
typedef registers_t *(*isr_t)(registers_t *);
```

Read this declaration carefully, using Appendix A §A.10 if the syntax resists. In Chapter 5 a handler received a frame and returned nothing. Here a handler receives a pointer to a frame *and returns a pointer to a frame*. That return value is not decoration; it is load-bearing. It is the frame the stub moves into `RSP`. A handler that wants to change what resumes — a scheduler, a page-fault handler — now has a way to say so.

---

## C.6 The Scheduler in Three Lines

With the frame and the task list understood, the scheduler is almost anticlimactic. Here it is in full, from `task.c`:

```c
registers_t *schedule(registers_t *regs)
{
    if (regs->int_no == IRQ0)
        tick++;

    if (!current_task)
        return regs;

    // Save the frame we were handed. It lives on the outgoing task's own
    // kernel stack, so there is nothing to copy -- we just remember where it is.
    current_task->rsp = (u64int)regs;

    // Round-robin.
    current_task = current_task->next;
    if (!current_task)
        current_task = ready_queue;

    return (registers_t *)current_task->rsp;
}
```

Strip the timer bookkeeping and the guard, and three lines remain, each doing exactly one job:

* **Save.** `current_task->rsp = (u64int)regs;` records where the outgoing task's frame lives. There is no copying — the frame is already sitting on that task's stack, so the scheduler only writes down its address. This is the bookmark of §C.4 being placed.
* **Select.** `current_task = current_task->next;` advances to the next task in the list, wrapping back to `ready_queue` at the end. That single sentence is the whole of the scheduling *policy*: round-robin, nothing more. (What a real scheduler adds here — priorities, fairness, sleeping — is the subject Chapter 15 §15.8 deliberately leaves to other books.)
* **Return.** `return (registers_t *)current_task->rsp;` hands back the incoming task's frame. The stub's `mov rsp, rax` does the rest.

One line saves, one line selects, one line returns. This is the sentence from §C.1 made real: because the frame was the right data structure, the algorithm collapsed to almost nothing. Chapter 15 §15.5 called this "data first," and Chapter 13 §13.8 called it "understand data before algorithms." Here is the proof that the advice pays.

Note also the pointer casts, `(u64int)regs` and `(registers_t *)current_task->rsp`. These are the address-as-data idea of Chapter 1 §1.8 and the casting of Chapter 3 §3.10: an address is stored as an integer in the task structure and interpreted back as a frame pointer when needed.

---

## C.7 Manufacturing a Thread That Has Never Run

The scheduler can switch to any task whose frame is ready. But a brand-new task has never been interrupted, so no frame exists for it yet. `create_task` builds one by hand — and it is the only place in the kernel that manufactures a frame from nothing, which makes it worth reading as a definition of what a thread *is*.

```c
int create_task(void (*entry)(void))
{
    asm volatile("cli");

    task_t *task = (task_t *)kmalloc(sizeof(task_t));
    task->id     = next_pid++;
    task->next   = 0;

    // A fresh kernel stack, page-aligned so we can spot overruns easily.
    task->kstack = kmalloc_a(KERNEL_STACK_SIZE);
    u64int stack_top_addr = task->kstack + KERNEL_STACK_SIZE;

    // Hand-build the frame that iretq will restore the first time this task runs.
    registers_t *frame = (registers_t *)(stack_top_addr - sizeof(registers_t));
    memset((u8int *)frame, 0, sizeof(registers_t));

    frame->rip     = (u64int)entry;    // where the task begins executing
    frame->cs      = 0x08;             // kernel code segment
    frame->ss      = 0x10;             // kernel data segment
    frame->rflags  = 0x202;            // reserved bit 1, plus IF: interrupts on
    frame->userrsp = stack_top_addr;   // its own stack
    frame->rbp     = 0;                // terminate any backtrace here

    task->rsp = (u64int)frame;

    // Append to the ready queue.
    task_t *tmp = (task_t *)ready_queue;
    while (tmp->next)
        tmp = tmp->next;
    tmp->next = task;

    asm volatile("sti");
    return task->id;
}
```

Nearly every line ties back to an earlier chapter:

* `kmalloc` and `kmalloc_a` allocate the task structure and a page-aligned stack — Chapter 8 §8.11. The stack is aligned so an overflow lands on a page boundary you can detect, a small piece of the defensive thinking of Chapter 15 §15.9.
* `stack_top_addr - sizeof(registers_t)` is pointer arithmetic (Chapter 3 §3.7) placing the frame at the very top of the new stack, because a stack grows downward.
* `memset(...)` zeroes the frame; recall from Chapter 12 §12.6 that in a freestanding kernel you supply `memset` yourself. Zeroing first means every register the task starts with is a defined value.
* The assignments then write only the fields that matter. `rip` is set to `entry`, so the task begins executing at the function you passed in. `cs` and `ss` are the kernel segments from Chapter 2. `rflags = 0x202` sets the interrupt-enable bit so the task is preemptible the instant it starts — one of the flag constructions of Chapter 9 §9.8. `userrsp` points the task at its own stack. `rbp = 0` stops a debugger's backtrace cleanly at the task's origin.
* `entry` itself arrives as `void (*entry)(void)`, a function pointer (Chapter 5 §5.7) — the task's body is passed the same way an interrupt handler is registered.

When the scheduler later selects this task and the stub does `mov rsp, rax; POP_ALL; iretq`, the processor loads these hand-written values and jumps to `entry` with a clean stack. The task cannot tell that its "resume" is actually its birth. That is the elegance: *starting* a thread and *resuming* a thread are the same operation, because both are just restoring a frame.

The `cli`/`sti` pair around the body disables interrupts while the ready queue is modified. The queue is a linked list being walked by the scheduler in interrupt context; appending to it without protection is a race, and Chapter 15 §15.9's defensive habit applies.

---

## C.8 Turning the Running Kernel Into Task One

There is a chicken-and-egg question hiding here. The scheduler switches *between* tasks, but when the kernel first boots there are no tasks — only the ordinary thread of control running `main`. `initialise_tasking` resolves this by declaring that existing thread to be task one:

```c
void initialise_tasking()
{
    asm volatile("cli");

    current_task = ready_queue = (task_t *)kmalloc(sizeof(task_t));
    current_task->id     = next_pid++;
    current_task->rsp    = 0;                  // filled in by the first interrupt
    current_task->kstack = (u64int)&stack_top; // the boot stack from boot.s
    current_task->next   = 0;

    register_interrupt_handler(IRQ0, &schedule);
    register_interrupt_handler(INT_YIELD, &schedule);

    asm volatile("sti");
}
```

Two details are worth pausing on.

First, `current_task->rsp` is left at zero. The running thread has no saved frame yet, because it has not been interrupted. That is fine: the very first timer interrupt will enter the stub, build a frame on the boot stack, and `schedule` will fill in `rsp` with `(u64int)regs` — the save line of §C.6. The kernel's original thread of control quietly acquires a frame the first time it is preempted.

Second, and central to the whole design, the scheduler is installed as an ordinary interrupt handler through a **function pointer**:

```c
register_interrupt_handler(IRQ0, &schedule);
```

This is Chapter 5 §5.9 — event dispatching through a table of function pointers — used to plug scheduling into the interrupt system. `IRQ0` is the timer (defined as 32 in `isr.h`), so every timer tick now runs `schedule`, and every tick is an opportunity to switch tasks. Preemption is nothing more than the timer's handler being the scheduler.

---

## C.9 Cooperation and Preemption Share One Path

Preemption happens *to* a task when the timer fires. Sometimes a task wants to give up the processor *voluntarily* — it has nothing to do and would rather let others run. The obvious temptation is to write a second switching routine for the voluntary case. The design refuses that temptation, and the refusal is instructive.

The scheduler already knows how to switch, but it needs an interrupt frame to do it, because the frame *is* the mechanism (§C.5). So the voluntary path simply raises an interrupt on purpose:

```c
#define INT_YIELD 0x81

void task_yield()
{
    asm volatile("int %0" :: "i"(INT_YIELD));
}
```

This is inline assembly of the kind Chapter 4 §4.3 introduced, here executing a software interrupt. The `int $0x81` instruction pushes a frame exactly as a hardware interrupt would, lands in the same stub, and reaches the same `schedule` — which was registered against `INT_YIELD` right beside `IRQ0` in §C.8. Cooperation and preemption travel one code path.

Chapter 15 §15.10 valued consistency over cleverness, and this is a clean example. There is exactly one thing in the kernel that knows how to switch tasks. The timer reaches it; `yield` reaches it; a future system call could reach it. Every route funnels through the same three-line scheduler, so there is only one place to get right and only one place to look when something is wrong.

---

## C.10 The Line Count That Proves the Point

It is worth seeing what this design replaced, because the contrast is the whole argument of the appendix.

The original 32-bit tutorial switched tasks from ordinary C code rather than from an interrupt. Standing in the middle of a function with a live stack frame, it had to *manufacture* the state an interrupt would have saved. Doing so required reading the instruction pointer with a `pop`/`jmp` trick, stuffing a magic sentinel value into a register so a resumed task could recognize itself, and a block of hand-written inline assembly to set the stack and instruction pointers. That assembly is famously miscompiled by modern GCC, which allocates one of the C variables into the very register the template uses as scratch. Around two hundred and fifty lines of such machinery — including a routine that walked the kernel stack *rewriting any value that happened to look like a stack pointer* — simply do not exist in the 64-bit redesign.

They do not exist because they were never solving a real problem. They were compensating for a missing data structure. Once the register frame is acknowledged as the complete, addressable description of a thread, the questions those lines answered — *where do I resume? with what stack?* — are already answered, in the frame, for free. The algorithm did not get cleverer. The data got honest, and the algorithm evaporated.

This is the lesson Chapter 13 §13.8 and Chapter 15 §15.5 stated as advice. Here it is stated as arithmetic: the right structure was worth roughly two hundred and fifty lines and three genuine bugs. Internalize this, and you will reach for the data structure first for the rest of your career.

---

## C.11 Proving It Correct, Not Merely Alive

A demonstration in which two tasks take turns printing their names *looks* like success, but Chapter 14 §14.2 warns that looking right is not being right. A context switch that quietly dropped one callee-saved register, or let two tasks' stacks overlap, would produce output that looked identical while corrupting state invisibly.

The tutorial verifies the switch the way Chapter 16 §16.4 recommends — predict a ground truth, then test against it. It computes a long, register-hungry checksum three times with interrupts disabled, capturing the answer a correct machine must produce. Then it runs the same three checksums inside three preemptible tasks and compares. If any register is lost across a switch, or any two stacks alias, a checksum changes and the mismatch is caught. A stack canary in each computation guards against silent stack corruption. When the preempted results match the uninterrupted truth bit for bit across thousands of switches, the switch is not merely alive; it is correct.

This is the empirical habit the whole book has argued for. Do not trust a subsystem because it produced plausible output once. Construct a test whose failure you would actually notice, and let the machine try to break it.

---

## C.12 Reading This Code in the Right Order

Chapter 13 §13.6 said to trace execution rather than read files top to bottom. Applied here, the story of a single timer tick is the clearest way in:

```text
timer fires (IRQ0)
        │
        ▼
interrupt stub  (interrupt.s)
        │  PUSH_ALL builds the outgoing frame; RDI points at it
        ▼
irq_handler  (isr.c)
        │  looks up the handler for IRQ0 in the function-pointer table
        ▼
schedule  (task.c)
        │  save current->rsp; advance to next; return its frame
        ▼
back in the stub
        │  mov rsp, rax   ← now on the incoming task's stack
        │  POP_ALL; iretq
        ▼
the next task resumes exactly where it left off
```

Follow that path once with your finger on the code, from `interrupt.s` into `isr.c` into `task.c` and back, and the subsystem stops being a collection of files and becomes a single motion. Then read `create_task` to see how a task joins the cycle, and `initialise_tasking` to see how the cycle begins. Read data first (`registers_t`, then `task_t`), mechanism second (the stub), policy last (the three lines of `schedule`). In that order, nothing is mysterious.

---

## Common Mistakes

The most common misunderstanding is to imagine the context switch as a loop that *copies* registers from one task to another. It copies nothing. Each task's registers live in a frame on that task's own stack; switching only changes which stack the processor is standing on. If you find yourself looking for the code that saves fifteen registers into a task structure, stop — that code does not exist, and its absence is the point.

A second mistake is forgetting that the scheduler runs in interrupt context (Chapter 5 §5.10). Anything it touches — the ready queue above all — can be touched between any two instructions of ordinary code. Modifying that list without disabling interrupts, as `create_task` is careful to do, invites a corruption that appears only under load.

A third is treating `current_task` and `ready_queue` as ordinary variables and wondering why a debug build behaves differently from an optimized one. They are `volatile` for a reason (Chapter 3 §3.12); removing that qualifier lets the compiler cache a stale value across the interrupt that changed it.

Finally, students sometimes try to make a resumed task "start" differently from how it "resumes." There is no difference. `create_task` builds a frame, and the first schedule restores it exactly as any later schedule would. If starting and resuming were different operations, the design would have failed.

---

## Practice Exercises

**Exercise 1**

Without looking at §C.5, explain in your own words why `mov rsp, rax` performs a context switch. Your explanation should mention where the outgoing task's state is, where the incoming task's state is, and what `iretq` does. Then verify it against `interrupt.s`.

---

**Exercise 2**

The scheduler's selection policy is a single line: `current_task = current_task->next;`. Describe, in one sentence each, three different policies you could implement by changing only that line, and identify which field you would have to add to `task_t` for each. Do not implement them; the exercise is to see that policy and mechanism are separable (Chapter 15 §15.8).

---

**Exercise 3**

In `create_task`, the frame is placed at `stack_top_addr - sizeof(registers_t)`. Draw the new task's stack and mark where the frame sits. Explain what would go wrong if the frame were placed at `task->kstack` (the *bottom* of the stack) instead.

---

**Exercise 4**

`create_task` sets `frame->rflags = 0x202`. Using Chapter 9, identify which bit enables interrupts and explain what would happen to a task created with `rflags = 0x002` instead. Would it ever be preempted? Would it ever yield?

---

**Exercise 5**

Add a `switches` counter to `schedule()` that increments on every call, and print it alongside `tick`. Predict, before running, whether it will grow faster than, slower than, or at the same rate as `tick`, and explain your prediction using §C.8 and §C.9. Then run it and reconcile.

---

**Exercise 6**

Deliberately break the switch: swap two adjacent `pop` instructions inside `POP_ALL` in `interrupt.s`. Predict what the simple two-task demo will do, then predict what the checksum verification of §C.11 will do. Run both. Explain why one notices the bug and the other does not, connecting your answer to Chapter 14 §14.2.

---

## Appendix Summary

Execution is where the book's threads are tied together. A thread of control is nothing more than processor state, and that state already lives in one place — the `registers_t` frame the interrupt machinery of Chapter 5 builds on every interrupt. A task is a small structure holding a pointer to its saved frame and a link to the next task, a linked list of the same shape you built in Chapter 8. Because the frame is complete, switching tasks is not a matter of setting registers but of choosing which frame to restore, and that choice reduces to one instruction, `mov rsp, rax`, reached through a function pointer that returns a frame. The scheduler that drives it is three lines: save, select, return.

The deeper lesson is the one this appendix exists to deliver. The 64-bit redesign is short — and correct on a modern toolchain where the original is neither — not because its author was cleverer, but because the data structure was chosen honestly. Recognizing the frame as the thread erased hundreds of lines and three real bugs at a stroke. That is the habit worth carrying out of this book: before writing the algorithm, get the data right, and much of the algorithm will turn out to be unnecessary.

You began this book learning what a `u64int` is. You end it watching a linked list of saved frames become the illusion of many programs running at once, built entirely from parts you already understood. That is what mastering C for kernel development looks like.
