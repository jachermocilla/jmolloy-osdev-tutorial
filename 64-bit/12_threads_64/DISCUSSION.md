# Chapter 12: Threads, Shared Memory, and Mutual Exclusion

In the previous chapter, the operating system learned to give each process a private view of memory. Two programs could use the same virtual address and never collide, because those addresses referred to different physical frames.

That isolation is expensive, and it is not always what a program wants.

Consider a web server handling a thousand connections, or an editor that saves a file while you keep typing. Each of these needs several things happening at once *within one program*, over one set of data. Isolation would defeat them. A connection handler that cannot see the shared cache is useless.

This chapter introduces the other arrangement. Several threads of control, one address space, one copy of the data.

It also introduces the bill that arrives with it.

---

# The Word "Task" Was Doing Two Jobs

Chapter 9 defined a structure called `task_t`. It held six fields: an identifier, a saved stack pointer, a kernel stack and its top, a page table, and a queue link.

Read that list again and ask a question of each field: **if two threads of one program ran side by side, would they have to agree on this?**

The page table is the only one that answers yes.

Two threads of a program must agree on what memory looks like. They cannot agree on a stack pointer, because a stack pointer *is* where a thread is. Give them one and they are one thread.

```text
        task_t                    process_t            thread_t

    +------------+            +------------+       +------------+
    | id         |            | pid        |       | tid        |
    | rsp        |            | pml4_phys  |       | rsp        |
    | kstack     |   sort     | nthreads   |       | kstack     |
    | kstack_top |  ------>   +------------+       | kstack_top |
    | pml4_phys  |                  ^              | proc  -----+---> process
    | next       |                  |              | state      |
    +------------+                  +--------------+ next       |
                                       many to one +------------+
```

The chapter invents nothing here. It sorts an existing structure into the two things it had always been and gives each a name.

Once the names exist, two system calls that were hard to tell apart become easy:

> A process is an address space. A thread is a thing that runs in one.
>
> `fork()` makes a new address space and a thread to run in it. `thread_create()` makes a thread and reuses the address space it was called from.

The sort pays for itself immediately, in the scheduler. Chapter 11 reloaded the page tables whenever the incoming task's differed from the outgoing one's; the same line now reads through one more pointer.

```text
    if (next->proc->pml4_phys != current_pml4_phys)
        switch_pml4_phys(next->proc->pml4_phys);
```

Two threads of one process share a `process_t`, so the comparison finds them equal and the reload is skipped — not by an optimization, but because the structure says they are the same address space. Most of why threads are cheaper than processes is that one `->`.

---

# One Line, Two Meanings

The demonstration is the same ring-3 program from the last chapter with a single call changed.

```text
    chapter 11:   u64int pid = syscall_fork();
    chapter 12:   u64int tid = syscall_thread_create(&counter_down);
```

Both versions have one variable at one virtual address, and one loop that adds to it and another that subtracts. Chapter 11's output splits and never rejoins — the parent climbs to 1500, the child falls to 500, and neither can see the other, because each wrote its own private copy of the page.

This chapter's output oscillates.

```text
  [up   tid=6] counter=1100
  [down tid=7] counter=1000
  [down tid=7] counter=900
  [up   tid=6] counter=1000
  [up   tid=6] counter=1100
  [down tid=7] counter=1000
```

One number, moving in both directions, because there is one page and two threads writing it. `fork()` gave the child its own copy; `thread_create()` gave the new thread the same one. Everything else about the program is identical, and the output alone tells you which call ran.

The kernel's own greeting changes too, and the change is the chapter in miniature: it now prints `pid = 1, tid = 2`. Eleven chapters printed one number, because a task was a process was a thread. There are two numbers from here on.

Sharing also bites immediately, in the dumbest available place. Chapter 11 placed each user stack by arithmetic on the process id, which was fine while a pid named exactly one thread of control. Two threads of one process would be handed the same stack and destroy each other within a few instructions. The fix is four characters — index by thread id instead — and it is worth noticing how quietly the old code became wrong.

---

# What Sharing Costs

Isolation had one great virtue: it made interference impossible. Take it away and interference becomes the default.

Two threads increment a counter. Each does what looks like one operation.

```text
    counter = counter + 1
```

The processor sees three.

```text
    load    counter -> register
    add     1
    store   register -> counter
```

Between the load and the store, a timer interrupt can arrive, and the scheduler can hand the CPU to the other thread, which runs all three steps and stores its result. When the first thread resumes, it stores a value computed from a number that is now stale. Both threads incremented. The counter moved once.

```text
    Thread A                Thread B            counter

    load 0                                         0
       |                    load 0                 0
       |                    add  1                 0
       |                    store 1                1
    add  1                                         1
    store 1                                        1     <-- B's work is gone
```

Both threads executed correctly, in an order the scheduler was entitled to choose. The bug is that the program permitted an interleaving it could not survive.

This is a **race condition**, and its defining property is that the program keeps running while quietly ceasing to be trustworthy. The answer changes while the code stands still.

---

# The Race You Cannot See Is Still There

A student who writes the code above and runs it will usually see the right answer.

The reason is arithmetic. The timer in this kernel fires fifty times a second, so a thread holds the processor for twenty milliseconds. The window between the load and the store is a few nanoseconds wide. The interrupt lands inside that window roughly one time in a million, and the loop is not run a million times.

This is not a hypothetical. The first version of this chapter's demo held the window open for fifty spin iterations, and with the lock commented out it printed the correct total three times in a row. A critical section a microsecond wide, on a 20-millisecond timeslice, is not interrupted rarely — it is not interrupted.

Widen the window and the failure appears on demand. With the read and the write pulled twenty thousand iterations apart, and the lock still commented out, four consecutive runs of the same binary:

```text
  counter = 519, expected 800  LOST UPDATES
  counter = 533, expected 800  LOST UPDATES
  counter = 400, expected 800  LOST UPDATES
  counter = 400, expected 800  LOST UPDATES
```

Wrong, and wrong by a different amount each time — which is the signature. A deterministic bug gives the same wrong answer; a race gives a different one, because the scheduler is making a different choice on every run.

Speed the timer up and the failure appears more often. Add a second processor and it appears constantly, because a second processor is not sampling the window fifty times a second — it is inside the window whenever it wants to be.

Had the demo shipped with the narrow window, a reader could have removed the lock, seen the right answer, and drawn precisely the wrong conclusion. That is worth carrying past this chapter, because it generalizes badly:

> The reason your program has never raced is not evidence that it cannot.

---

# Mutual Exclusion

The counter needs one guarantee: while a thread is between its load and its store, no other thread may touch the same variable. The region needing that guarantee is a **critical section**, and the guarantee is **mutual exclusion**.

There are two ways to obtain it, and this kernel has been quietly relying on the weaker one for four chapters.

**Turn off interrupts.** On a machine with one processor, a thread loses the CPU only when an interrupt takes it. Mask interrupts and the thread cannot be interrupted, so it cannot be interleaved, so the critical section is safe.

What that buys is mutual exclusion by side effect — a property of the machine having one processor rather than of the program asking for anything. The distinction becomes urgent the moment a second processor exists: the other processor was never interrupted, so masking says nothing to it. Both processors read the counter, both store, and one increment vanishes exactly as before.

**Use an atomic instruction.** `lock xchg` reads and writes memory as one indivisible transaction that no other processor can split. It is the real thing, and it costs a bus lock.

The kernel in this chapter has one processor and uses the cheap version. What matters is knowing which sentence stops being true when a second core boots.

---

# Masking Is Not Locking

The last three chapters guarded their critical sections like this:

```text
    asm volatile("cli");
    ...
    asm volatile("sti");
```

That is wrong, and it has been wrong since chapter 9. `sti` does not restore the interrupt flag — it *sets* it. Call one of those functions from a context where interrupts were already masked, and on the way out it hands them back behind its caller's back, ending a critical section the caller believes it is still standing in. The caller is then interrupted in the middle of something it had declared indivisible, and nothing anywhere reports it.

The fix is ten lines: read the flags register before masking, and put the saved value back afterwards rather than assuming what it was.

```text
    flags = irq_save();      pushfq; pop flags; cli
    ...                      the critical section
    irq_restore(flags);      push flags; popfq
```

Nesting now works, because each region restores the state it found rather than the state it wishes were there. The general shape of this bug is worth recognising: a function that sets a global instead of restoring it is fine until it has a caller.

And the reason it belongs in this chapter rather than in chapter 9 is that this is where the vocabulary arrives to say what went wrong. *Mask interrupts* and *take a lock* are different verbs. The first is a statement about the hardware you happen to be running on. The second is a claim your program makes about itself.

---

# Waiting Without Spinning

A lock that is held must make its next claimant wait. There are two ways to wait.

**Spin.** Loop, re-reading the lock until it is free. The waiting thread burns its entire timeslice discovering, several million times, that the answer is still no.

**Block.** Tell the scheduler you are not runnable, and let it choose someone else. The thread that holds the lock now gets the processor sooner, which is the only event that can end the wait.

```text
    spinning                     blocking

    holder    [====]             holder    [====]
    waiter    [xxxx]             waiter    [                ]
    holder        [====]         holder        [====]
    waiter        [xxxx]         other         [====]
                                                     ^
    x = wasted                                       waiter wakes
```

Blocking is better here for a reason worth stating plainly: the waiter has nothing to do, and the machine has work available. Spinning is not always wrong — on a multiprocessor, where the lock holder is running on another core right now and will release in fifty nanoseconds, spinning beats two context switches. On one processor it is never right, because the holder cannot possibly release while the waiter holds the CPU.

Blocking has a price. It requires a scheduler that can be told about states other than "wants to run."

It also requires the woken thread to be suspicious. The lock is not handed over — a thread is woken and told to try again, and between the wake and the moment it actually runs, any other thread may have taken the lock. So the wait is a loop and never an `if`:

```text
    while (m->locked)        not: if (m->locked)
    {
        wq_enqueue(&m->waiters, current_thread);
        block();
    }
    m->locked = 1;
```

Being woken is being told to look again.

---

# A Thread Is Always Somewhere

Chapter 9's scheduler knew one fact about each task: its position in a circular list. Every task was runnable, always, so a list was enough.

Blocking makes that false. A thread waiting on a mutex is not runnable, and the scheduler must not choose it. A thread that has exited is not runnable, and never will be, but its exit value has to survive until someone collects it.

Each thread therefore carries a state.

```text
                   thread_create()
                         |
                         v
        +------------> READY <------------+
        |                |                |
        |            scheduled            | unblock()
        |                |                |
        |                v                |
        +-- preempted  RUNNING --------> BLOCKED
                         |     block()
                    thread_exit()
                         |
                         v
                      ZOMBIE
                         |
                     join()
                         |
                         v
                       freed
```

One rule holds the design together:

> A thread is on exactly one queue, or it is running and on none.

READY means on the ready queue. BLOCKED means on some mutex's wait queue. RUNNING means on the processor, on no queue at all. ZOMBIE means finished, on nothing, waiting to be collected.

The rule earns its keep by making blocking trivial. Because a running thread was never on the ready queue, blocking does not have to remove it from anywhere. It marks itself unrunnable and yields, and the scheduler simply does not put it back.

The rule also explains a field that otherwise looks redundant. Every thread carries a second link, threading it onto a registry of all threads that exist, and the obvious economy — reuse the queue link, one list is surely enough — would break the invariant on the first zombie. A zombie is on no queue, and `join()` still has to find it. Threading the registry through the same pointer would put a thread on two lists at once, which is exactly the thing the rule forbids. Two links, two purposes, and the invariant survives.

Every serious bug in a scheduler is this rule being broken: a thread on two queues, or on none by accident. A thread stranded on no queue never runs again, and no fault fires to say so — the machine has no opinion about threads it was never asked to schedule.

---

# Someone Has to Be Willing to Do Nothing

A scheduler that can block threads can block all of them.

Every earlier chapter was safe from this by construction, because every task was permanently runnable. Round-robin could not come up empty-handed. Now the last runnable thread can block on a mutex, and the scheduler is asked for a thread to run when there are none.

The answer is a thread that is never blocked and never counted: the **idle thread**. It halts the processor until the next interrupt, and it exists purely to give the scheduler something to return.

It is the first component of this kernel that does no work. Its entire purpose is to keep an invariant true, and that is a legitimate reason for code to exist.

---

# Ending a Thread Is Not Simple

A thread that finishes has three things to dispose of, and it cannot dispose of any of them itself.

Its **exit value** must outlive it, because the whole point of returning a value is that someone else reads it.

Its **kernel stack** cannot be freed by the thread that is standing on it. Freeing your own stack is sawing the branch on your side of the cut.

Its **address space** may still be in use by siblings.

The first two are why a finished thread becomes a zombie rather than disappearing. It has stopped running but its remains are still needed, and it takes a *second* thread — one standing on a different stack — to clear them away. That second thread is the one that calls `join()`.

```text
    thread_exit()                    thread_join()

    save exit value                  read exit value
    mark ZOMBIE                      free the kernel stack
    wake the joiner                  free the thread record
    drop the address-space count
    yield forever
```

This is also where chapter 11 pays a debt it acknowledged. Its `task_exit` ended with a confession that it could free neither the stack it was standing on nor the structure it was reading, and leaked both. Nothing has been invented to fix that. The joiner is simply somebody else, on another stack, for whom neither object is load-bearing.

The third disposal is why a process counts its threads. Chapter 11 freed the address space when a task exited, which was correct because no two tasks could share one. Under sharing, the first thread to finish would demolish the memory its siblings are still executing in. A count, decremented on exit, means the last thread out is the one that turns off the lights.

That last thread frees the address space it is at that moment executing inside, which sounds impossible and is routine, for a reason chapter 11 depended on without ever saying: tearing down an address space touches the lower half, and the thread's code and stack live in the upper half — shared by reference, mapped identically in every address space, and untouched by the demolition.

A thread nobody joins is a thread whose remains nobody clears. Real systems either require a join or mark the thread *detached* and hand the corpse to a dedicated reaper. There is no third option in which the problem does not exist.

---

# Two Good Ideas That Dislike Each Other

`fork()` and threads were designed decades apart, by people solving different problems.

`fork()` duplicates the calling thread and the memory it can see. It cannot duplicate the *other* threads, because they are not making the call, and a thread copied while suspended at an arbitrary instruction would resume in a state nobody chose.

So the child is a process with one thread and a full copy of the memory. Including whatever the missing threads were in the middle of.

```text
    Parent                          Child

    thread 1  -- calls fork()       thread 1
    thread 2  -- holds the lock     (gone)
    thread 3  -- waiting on it      (gone)

    [ lock: HELD by thread 2 ]      [ lock: HELD by nobody ]
```

The child's copy of that lock says *held*. The thread that held it does not exist, so it will never be released. The first thread in the child that reaches for it waits forever, for an event that cannot occur.

Every Unix behaves this way, which is why POSIX added `pthread_atfork()` — a mechanism whose whole purpose is to let a program clean up after a bad interaction between two of its own primitives.

Placing `fork()` and threads in adjacent chapters makes the problem visible in a way that reading either one alone does not.

---

# Looking Ahead

Threads complete the concurrency model this book has been building since chapter 9. Processes provide isolation; threads provide sharing; the scheduler moves the processor between them; mutexes make sharing survivable. Together these are the vocabulary in which every operating system after 1970 describes what it is doing.

The implementation here favours the smallest thing that is honestly correct on one processor. The mutex sets its flag with a plain store rather than an atomic exchange, because interrupts are masked and nothing else is executing — a sentence that is true today and describes the machine rather than the code. The kernel heap is still not reentrant, and the chapter survives that by keeping every allocation inside a masked region, which is not a fix but an arrangement that has not yet failed.

Later chapters can pay these debts. Condition variables and semaphores build directly on the block-and-wake machinery. `exec()` gives processes a way to become new programs instead of copies. Copy-on-write makes `fork()` cheap. And a second processor makes several sentences in this chapter false at once, which is the most instructive thing that can happen to a design you thought you understood.
