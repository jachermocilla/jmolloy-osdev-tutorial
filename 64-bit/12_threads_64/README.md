# Chapter 12 (Threads and Mutual Exclusion) — new for the 64-bit series

This chapter is not in JamesM's original ten. It is chapter 11 read backwards:
`fork()` gives a program a new address space, and `thread_create()` gives it a
new thread in the one it already has.

**Read `11_fork_64/README.md` first.**

The surprise is that you have had threads since chapter 9. `create_task()` set
`pml4_phys` to `current_pml4_phys` and the comment beside it said *kernel
threads share one space*. The mechanism was there. The noun was missing, and so
was everything that has to be true before sharing memory is survivable.

```
12_threads_64/
├── make_initrd.c, test*.txt, mkinitrd.sh
└── src/
    ├── thread.c/.h    ← thread_t, the scheduler, create/exit/join
    ├── proc.c/.h      ← process_t, fork(), the address-space refcount
    ├── sync.c/.h      ← irq_save/irq_restore, mutex_t
    ├── usermode.s     ← was process.s (see "The rename")
    ├── syscall.c/.h   ← SYS_GETTID, SYS_THREAD_CREATE, SYS_JOIN
    ├── main.c         ← the mutex demo
    ├── user.c         ← the ring 3 thread demo
    ├── task.c/.h      ← gone; sorted into thread.* and proc.*
    └── everything else ← unchanged since chapter 11 (25 files)
```

`boot.s`, `link.ld`, `paging.c`, `kheap.c` and `isr.h` are untouched. That is the
headline. `registers_t` does not change and neither does the context switch —
threads are bookkeeping laid over the machinery chapter 9 already built.

---

## Building and running

```bash
cd 12_threads_64
bash mkinitrd.sh
qemu-system-x86_64 -kernel src/kernel -initrd initrd.img
```

```
tasking up. pid = 1, tid = 2

mutex: two threads, 400 increments each
  joined 4 (returned 400) and 5 (returned 400)
  counter = 800, expected 800  OK

user thread tid 6
  spawned tid 7
  [up   tid=6] counter=1100
  [down tid=7] counter=1000
  [down tid=7] counter=900
  [up   tid=6] counter=1000
  [up   tid=6] counter=1100
  [down tid=7] counter=1000
  joined; it returned 0
```

Two things in that output are the chapter.

`pid = 1, tid = 2`. Chapter 11 printed one number, because a task was a process
was a thread. There are two numbers now and there will be two from here on.

**The counter oscillates.** Run chapter 11 again and watch its counter split:
the parent climbs to 1500, the child falls to 500, and neither can see the other.
Same program, same variable, same virtual address. One line differs:

```c
chapter 11:   u64int pid = syscall_fork();
chapter 12:   u64int tid = syscall_thread_create(&counter_down);
```

`fork()` gave the child a private copy of the page. `thread_create()` gives the
new thread the same page. Everything else in `user.c` is unchanged, and the
output tells you which call ran.

---

## The sort

`task_t` held six fields:

```c
u32int id;              u64int rsp;             u64int kstack;
u64int kstack_top;      u64int pml4_phys;       struct task *next;
```

Ask each one: *if two threads of a program ran side by side, would they have to
agree on this?* Only `pml4_phys` says yes. That is the cut.

```c
typedef struct process {          typedef struct thread {
    u32int pid;                       u32int tid;
    u64int pml4_phys;                 u64int rsp;
    u32int nthreads;                  u64int kstack, kstack_top;
} process_t;                          process_t *proc;
                                      thread_state_t state;
                                      struct thread *next;      // one queue
                                      struct thread *all_next;  // the registry
                                      struct thread *joiner;
                                      u64int retval;
                                  } thread_t;
```

Nothing is invented. Six fields go left or right, and `schedule()` gains one
dereference:

```c
if (next->proc->pml4_phys != current_pml4_phys)
    switch_pml4_phys(next->proc->pml4_phys);
```

Two threads of one process compare equal here, so the CR3 reload is skipped by
construction rather than by luck. That is most of why threads are cheaper than
processes, and it is one `->`.

---

## thread_create(), beside fork()

```c
thread_t *t = thread_alloc(...);
t->proc = current_thread->proc;                            // thread_create
```

```c
process_t *child = proc_create(clone_address_space(...));  // fork
child->nthreads = 1;
```

Copy a pointer, or clone the page tables. Everything else — the kernel stack, the
hand-built frame, the append to the ready queue — is identical, which is why
`create_task()` survives as a two-line wrapper:

```c
int create_task(void (*entry)(void))
{
    return thread_create(&thread_trampoline_void, (void *)entry);
}
```

`thread_create()` takes an argument where `create_task()` could not. It costs one
field:

```c
frame->rdi = arg;      // System V: the first integer argument
```

You are building the frame by hand anyway. Chapter 9 did not pass an argument
because nobody had asked, not because it was hard.

---

## The demo, and why its spin count is load-bearing

`bumper()` in `main.c` pulls the read and the write apart:

```c
mutex_lock(&counter_lock);
u64int v = shared_counter;
spin(20000);                // widen the window
shared_counter = v + 1;
mutex_unlock(&counter_lock);
```

Comment out the lock and rebuild. Four runs here:

```
counter = 519, expected 800  LOST UPDATES
counter = 533, expected 800  LOST UPDATES
counter = 400, expected 800  LOST UPDATES
counter = 400, expected 800  LOST UPDATES
```

Wrong, and wrong by a different amount each time.

> **The first version of this demo used `spin(50)` and printed `OK` with the lock
> commented out.** Three runs, three passes, no race. At 50 Hz a timeslice is
> 20 ms; a critical section a microsecond wide is not interrupted rarely, it is
> not interrupted. The window has to be a real fraction of a timeslice before the
> race is reachable at all.
>
> Had that shipped, the reader would have removed the lock, seen `OK`, and drawn
> the opposite conclusion. Keep it in mind the next time some code of yours has
> never raced.

---

## irq_save, and the bug in chapters 9 through 11

Every critical section in the last three chapters looks like this:

```c
asm volatile("cli");
...
asm volatile("sti");
```

`sti` sets the interrupt flag rather than restoring it. Call `create_task()` from
a context where interrupts were already masked and it hands them back behind its
caller's back, ending a critical section its caller believes it is still inside.

```c
static inline u64int irq_save(void)
{
    u64int flags;
    asm volatile("pushfq; pop %0; cli" : "=r"(flags) :: "memory");
    return flags;
}

static inline void irq_restore(u64int flags)
{
    asm volatile("push %0; popfq" :: "r"(flags) : "memory", "cc");
}
```

Ten lines, and the cheapest lesson in the chapter: *mask interrupts* and *take a
lock* are different verbs. The first is a fact about having one CPU. The second
is a claim about your program.

---

## The invariant

> **A thread is on exactly one queue, or it is running and on none.**

The ready queue is a plain FIFO and the running thread is not on it. `schedule()`
rotates:

```c
thread_t *prev = current_thread;
if (prev != idle_thread && prev->state == THREAD_RUNNING)
    enqueue_ready(prev);          // still runnable? go to the back

thread_t *next = dequeue_ready(); // take the front
if (!next) next = idle_thread;
```

A thread that stopped being runnable while it held the CPU is simply not put
back. Which makes blocking two lines:

```c
void block(void)
{
    current_thread->state = THREAD_BLOCKED;
    thread_yield();
}
```

No unlink, because there is nothing to unlink from. The caller is responsible for
having put the thread on some *other* queue first — a wait queue — and if it
forgets, nothing will ever wake it and no fault will fire. That is the invariant
being broken in the one direction the machine cannot report.

`all_next` is a second link for exactly this reason. `join()` must find a zombie,
a zombie is on no queue, and threading the registry through `next` would put a
thread on two lists at once.

---

## The idle thread

Every task in chapter 11 was permanently runnable, so `dequeue_ready()` could not
fail. Block the last runnable thread on a mutex and it can:

```c
static void idle_entry(void *unused)
{
    for (;;)
        asm volatile("hlt");
}
```

Never blocked, never on the ready queue, never counted. It is the first code in
this kernel that does no work — it exists to keep an invariant true, which is
reason enough.

---

## The mutex

```c
void mutex_lock(mutex_t *m)
{
    u64int f = irq_save();
    while (m->locked)
    {
        wq_enqueue(&m->waiters, current_thread);
        block();
    }
    m->locked = 1;
    irq_restore(f);
}
```

`while`, not `if`. Between the unlock that wakes you and the moment you actually
run, anyone can take the lock. Being woken is being told to look again.

`m->locked = 1` is a plain store, not `lock xchg`, and that is correct here:
interrupts are off, there is one CPU, and nothing else is executing. The full
comment at the bottom of `sync.c` names the exact sentence that stops being true
when a second core boots.

---

## exit, join, and the leak chapter 11 apologised for

Chapter 11's `task_exit()` ended with a confession:

```c
// We cannot free our own kernel stack while standing on it, nor the task_t
// we are reading. A real kernel hands these to a reaper; we leak them.
```

`join()` closes it, because the joiner is standing on a different stack:

```c
registry_remove(t);
kfree((void *)t->kstack);
kfree(t);
```

`task_exit()` also freed the address space unconditionally — safe when no two
tasks could share one. `nthreads` fixes that:

```c
if (--me->proc->nthreads == 0 && me->proc != &kernel_process)
{
    free_address_space(me->proc->pml4_phys);
    kfree(me->proc);
}
```

You are still executing in the space you just freed. That works for one reason,
which chapter 11 relied on without saying: `free_address_space()` touches the
lower half, and your code and your stack live in the kernel half, shared by
reference in every address space.

---

## USER_STACK_FOR(pid) → USER_STACK_FOR(tid)

Four characters, and the sharing bites in the dumbest possible place. Chapter 11
placed each user stack by arithmetic on the pid, which was fine when a pid named
exactly one thread of control. Two threads of one process would now be handed the
same stack and destroy each other in about four instructions.

---

## The rename

`process.s` is now `usermode.s`, and this is not tidiness.

The Makefile builds `process.o` from `process.s` through the `.s.o` suffix rule.
Add a `process.c` beside it and `make` has two ways to produce `process.o` and
will choose one without mentioning it. The old name was also inaccurate: the file
holds `enter_user_mode` and nothing else, and that function is about privilege
levels.

---

## What this chapter leaves undone, on purpose

**`exit()` ends the calling thread, not the process.** POSIX ends them all. The
difference only shows once a thread that is *not running* has to be stopped,
which needs machinery this kernel does not have. A choice, not an oversight.

**`fork()` from a multi-threaded process is broken.** The child gets one thread
and a copy of the others' memory, including any mutex they were holding — locked
forever, by threads that do not exist. Read the comment in `proc.c`. This is real
POSIX behaviour and it is why `pthread_atfork()` exists.

**`kmalloc()` is still not reentrant.** It never was: IRQ0 can land inside it and
leave the next thread walking a half-linked heap. Every allocation in this chapter
happens with interrupts masked, so the chapter survives — by where the calls
happen to be, which is not a fix.

---

## Open questions

- Backport `irq_save`/`irq_restore` to chapters 9–11? The bug is real in all
  three. Introducing it here gives it a motive; backporting means explaining a
  fix in a chapter with no reason to want one.
- Does `kmalloc` reentrancy belong here or in a chapter of its own?
- Does `process_t` need a thread list? Nothing reads one — the refcount does the
  work. It is needed the moment `exit()` must walk siblings, or `ps` exists.
- `main` prints `pid = 1, tid = 2` because one counter feeds both. Honest, and it
  reads oddly after eleven chapters of `pid = 1`.
