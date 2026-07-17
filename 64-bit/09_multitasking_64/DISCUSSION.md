# Chapter 9: Multitasking and the CPU Scheduler

The kernel has behaved like a single-threaded program since it booted. It runs one instruction after another down one path, and a function that loops forever stops the operating system, because there is nothing else for the processor to do.

Real systems are not like that. While someone edits a document, the machine is also handling keystrokes, redrawing the screen, taking network packets, writing to disk, and counting timer ticks. One processor executes one instruction at a time, and the operating system nonetheless presents the appearance of many things happening at once.

That appearance is **multitasking**, and this chapter builds the mechanism behind it: pausing one thread of control, resuming another, and alternating between them fast enough that nobody notices the seams.

The mechanism turns out to be small. Everything it needs has been sitting in the kernel since chapter 4.

---

# One Processor, Many Tasks

Multitasking does not need more than one processor. It needs the willingness to stop.

Run each task to completion, and the last one in line waits for everything before it. Chop time into slices instead, and every task makes progress.

```text
Run to completion:

  AAAAAAAAAAAAAAAAAAAABBBBBBBBBBBBBBBBBBBBCCCCCCCCCCCCCCCCCCCC

Time-sliced:

  AAAABBBBCCCCAAAABBBBCCCCAAAABBBBCCCCAAAABBBBCCCCAAAABBBBCCCC
      ^   ^
      |   +-- switch
      +------ switch
```

At fifty switches a second, the interleaving is invisible and all three tasks look simultaneous. Nothing is running in parallel. The processor is changing its mind quickly.

---

# A Task Is a Saved Frame

What makes two tasks different is not their code — they may be running the very same function — but their state: the registers, the stack, and the position in the instruction stream. Suspending a task means writing that state down somewhere; resuming it means putting it back.

The kernel already has a structure that holds exactly that state, and it has had it for five chapters. Every interrupt builds one.

```text
registers_t -- what the CPU and the stub push on every interrupt

+------------------+
| r15 ... r8       |   15 general-purpose registers, pushed by the stub
| rbp, rdi, rsi    |
| rdx, rcx, rbx    |
| rax              |
+------------------+
| int_no, err_code |   pushed by the stub
+------------------+
| rip              |   pushed by the CPU
| cs               |
| rflags           |
| userrsp          |
| ss               |
+------------------+
      176 bytes
```

Read that list as an answer rather than a formality. Where does this thread resume? `rip`. On what stack? `userrsp`. With what flags, in what segment, holding what values? All present. A `registers_t` is a complete description of a thread of control, and `iretq` exists to bring one back to life.

So a task, in this kernel, is a saved frame and a pointer to it.

```text
typedef struct task
{
    u32int id;          // pid
    u64int rsp;         // -> this task's saved registers_t
    u64int kstack;      // the stack it lives on
    struct task *next;  // the ready queue
} task_t;
```

The frame is not copied anywhere. It sits on the task's own kernel stack, exactly where the interrupt left it, and `rsp` remembers the address. Suspending a task costs one store; resuming it costs one load.

---

# Every Task Needs Its Own Stack

Two tasks cannot share a stack. Task A calls a few functions, leaving return addresses and locals behind; Task B is scheduled and begins pushing over them; Task A resumes into wreckage.

So every task is given a stack of its own — sixteen kilobytes from the heap, page-aligned so that an overrun is easy to spot — and the frame that describes the task lives at the top of it.

```text
              task B's kernel stack

  high  +--------------------------+ <- kstack + 0x4000
        | ss       = 0x10          |
        | userrsp  = stack top     |
        | rflags   = 0x202         |
        | cs       = 0x08          |
        | rip      = task_b        |
        | err_code = 0             |
        | int_no   = 0             |
        | rax .. r15               |
        +--------------------------+ <- task->rsp
        |                          |
        |         unused           |
        |                          |
  low   +--------------------------+ <- kstack
```

The stack pointer is therefore not something the scheduler sets. It is something the scheduler *chooses*, by picking which stack to stand on.

---

# The Timer Makes It Involuntary

Chapter 5's timer counted ticks and printed a number. Its real job starts here: every tick is an opportunity to run the scheduler, and an interruption the running task cannot refuse.

That is **preemptive** multitasking, and the alternative is worth naming to see what it costs. Under **cooperative** scheduling, a task keeps the processor until it gives it up, and one task that forgets to yield — or loops forever, or waits on something that never arrives — freezes the machine. Preemption takes the decision away from the task and gives it to the hardware.

This kernel does both, and the pleasant surprise is that they are the same code path.

```text
  timer fires (IRQ0)          task calls task_yield()  ->  int $0x81
          |                                 |
          +----------------+----------------+
                           |
                    interrupt stub
                           |
                        schedule()
```

A voluntary yield is a software interrupt, and it lands in the same handler the timer does. The scheduler cannot tell the difference and does not need to: what reaches it either way is a frame, and what it does either way is choose the next one.

One consequence deserves a note, because it looks like a regression. The timer no longer owns IRQ0 — the scheduler does, and it increments the tick counter itself. One vector holds one handler, and the handler that runs on a timer interrupt must be the one that gets the frame.

---

# The Switch Itself

Since chapter 4, the interrupt stub has passed the C handler a *pointer* to the frame it is about to restore, so that a handler can modify what the interrupted program resumes with. One small change turns that into a scheduler: let the handler **return** a frame, and let the stub restore whatever it gets back.

```text
irq_common_stub:
    PUSH_ALL
    mov rdi, rsp        ; argument: pointer to this task's frame
    cld
    call irq_handler
    mov rsp, rax        ; <-- the context switch
    POP_ALL
    add rsp, 16
    iretq
```

`mov rsp, rax` is the whole thing. Normally the handler returns the frame it was handed, the instruction changes nothing, and the interrupted task resumes. When the scheduler has chosen someone else, the returned pointer is a frame on a *different stack*, the instruction lands the processor on it, and everything that follows unwinds the other task instead.

Following a timer tick from end to end:

```text
Task A is running
        |
        v
timer fires
        |
CPU pushes  ss, userrsp, rflags, cs, rip        onto A's kernel stack
stub pushes int_no, err_code, and 15 registers  onto A's kernel stack
        |
        v
schedule(regs)
        |
        |   A->rsp = regs           remember where A's frame is
        |   current = A->next       round-robin
        |   return B->rsp           hand back B's frame
        v
mov rsp, rax        now standing on B's kernel stack
POP_ALL             B's registers
add rsp, 16
iretq               B's rip, userrsp, rflags -- atomically
        |
        v
Task B is running, resuming exactly where it was interrupted
```

Nothing was copied. No register was assigned. The scheduler saved a pointer, followed a pointer, and returned a pointer, and the machinery that already existed to resume an interrupted program resumed a different one instead.

It is worth appreciating how much this deletes. Switching tasks from ordinary C code — which is what the original tutorial does — means standing in the middle of a live function and manufacturing, by hand, the state something else would have saved for you: read the instruction pointer with a trick, stash the stack and base pointers, plant a magic value so the resumed task can recognise itself, then set every register with inline assembly and jump. Perhaps two hundred and fifty lines, most of them fragile, some of them miscompiled by modern compilers. Switch from inside an interrupt and the state is already saved. The context switch is a `mov`.

---

# Creating a Task from Nothing

A task can only be resumed if it has a frame to be resumed from, and a brand-new task has never been interrupted. So `create_task` forges the evidence: it allocates a stack, writes a `registers_t` at the top of it, and fills in the fields as though this task had been interrupted a moment ago at the first instruction of its entry function.

```text
frame->rip     = entry;            resume here
frame->cs      = 0x08;             kernel code segment
frame->ss      = 0x10;             kernel data segment
frame->rflags  = 0x202;            reserved bit, plus IF: interrupts enabled
frame->userrsp = stack_top;        its own stack, 16-byte aligned
frame->rbp     = 0;                a backtrace stops here
everything else = 0
```

The first time the scheduler picks this task, `iretq` restores that frame and the processor "returns" to a place it has never been. The lie is undetectable, because `iretq` has no way of knowing whether the frame it pops was pushed by an interrupt or written by a `memset` and six assignments.

Two of those fields repay attention. `rflags` must have the interrupt flag set, or the task will resume with interrupts disabled and never be preempted again — the first task scheduled would be the last. And `rbp` is zeroed deliberately, so that a debugger walking the frame chain has somewhere to stop.

The task the kernel is already running needs no forging. `initialise_tasking` allocates a `task_t` for the current thread of control, calls it task 1, and leaves its `rsp` at zero; the first timer interrupt hands the scheduler a real frame, and the field fills itself in.

---

# Round-Robin

The scheduling policy is the least interesting part of the chapter, which is the point. Tasks sit in a singly-linked queue, and the scheduler takes the next one, wrapping at the end.

```text
ready_queue
    |
    v
  +---+     +---+     +---+
  | A |---->| B |---->| C |----+ next == 0
  +---+     +---+     +---+    |
    ^                          |
    +--------------------------+
        wrap to ready_queue
```

Every task gets a turn, in order, for one tick each. There are no priorities, no accounting of how much processor a task has used, no attempt to keep a task on the core that holds its cache lines warm, and no notion that some tasks are more urgent than others. Real schedulers are largely made of those concerns.

What round-robin does have is the property that makes it a good teacher: it is obviously fair and obviously correct, so when the interleaving on screen looks wrong, the bug is in the switch and not in the policy.

---

# One Address Space, Three Tasks

Every task here is a **kernel thread**. All three run in ring 0, and all three share one address space.

```text
             one set of page tables

  +-----------------------------------------+
  | kernel code | heap | globals | initrd   |
  +-----------------------------------------+
        ^             ^              ^
        |             |              |
      Task A        Task B        Task C
      (own stack)   (own stack)   (own stack)
```

The tasks differ in processor state and in nothing else. They share every global, every heap allocation, and every page, which makes communication between them trivial and protection between them nonexistent: any task can scribble on any other task's stack, and the hardware will help it do so.

This is why the switch needs no `CR3` reload — there is only one set of page tables, so there is nothing to reload. When chapter 10 gives each process its own address space, the reload goes into `schedule`, in the two lines between choosing the next task and returning its frame. It will work for a reason worth noticing now: every kernel stack is mapped at the same address in every address space, so the frame the scheduler returns is still valid after the tables change underneath it.

---

# Looking Ahead

The kernel has learned to manage a third resource. Earlier chapters divided memory among the things that need it, and storage among the things that persist; this chapter divides **time** among the things that want to run. Deciding who runs, when, and for how long is the oldest job an operating system has.

What exists now is a scheduler and kernel threads — enough to run several things at once, and not enough to run them safely. Every task holds full privileges and can reach every byte the kernel can.

The next chapter closes that gap. User mode, separate address spaces, and system calls turn these threads into processes: contexts that cannot see each other's memory and must ask the kernel for anything they want. The scheduler will not change much. It is already handing out frames, and a frame with a user-mode `cs` in it resumes in ring 3 just as readily as this one resumes in ring 0.
