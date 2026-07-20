# Chapter 13 (Condition Variables and Semaphores) — new for the 64-bit series

This chapter is not in JamesM's original ten, and it adds no new mechanism. It takes the block-and-wake from chapter 12 and arranges the same parts two more ways: a semaphore, which is the mutex with a counter in place of its single bit, and a condition variable, which is the mutex's wait queue with the counter removed and handed back to the program.

**Read `12_threads_64/README.md` first.** Everything here stands on `block`, `unblock`, and the wait queue that chapter built.

```
13_condvar_sem_64/
├── make_initrd.c, test*.txt, mkinitrd.sh
└── src/
    ├── semaphore.c/.h  ← NEW: counting semaphore (sem_wait/sem_post)
    ├── condvar.c/.h    ← NEW: condition variable (cond_wait/signal/broadcast)
    ├── sync.c/.h       ← unchanged: irq_save/irq_restore, mutex_t
    ├── syscall.c/.h    ← + SYS_MUTEX_LOCK/UNLOCK, SYS_SEM_WAIT/POST
    ├── main.c          ← the bounded buffer, twice: semaphores, then condvars
    ├── user.c          ← the ring 3 producer/consumer, over semaphores
    └── everything else ← unchanged since chapter 12
```

`thread.c`, `proc.c`, `paging.c`, `kheap.c`, the scheduler, and `registers_t` are all untouched. That is the headline again: the scheduler learns nothing new. The two new primitives are arrangements of machinery chapter 12 already proved correct, which is why `sync.c` itself did not have to change — `semaphore.c` and `condvar.c` sit beside it and call the same `block`/`unblock`/`wq_*` it does.

---

## What changed, and why

**`semaphore.c/.h` — the mutex with a counter.** `sem_wait` waits for the count to be positive and spends one; `sem_post` adds one and wakes a waiter. The wait is the mutex's `while` loop verbatim — a woken thread re-tests, because being woken is being told to look, not being handed the count. The one new property is that a post with no waiter is *remembered* in the count, which is exactly what a bounded buffer needs.

**`condvar.c/.h` — the wait queue with no counter.** It holds no state; the condition lives in a program variable guarded by a mutex. `cond_wait` releases that mutex and blocks as one indivisible step, then reacquires on the way out. The subtlety is the order: it enqueues the sleeper *before* releasing the mutex, so no signaller can slip into the gap and signal an empty queue. Reverse those two lines and you have the classic lost wakeup — see the comment in `cond_wait` and the diagram in `DISCUSSION.md`.

**`main.c` — the bounded buffer, written twice.** One producer, one consumer, a ring of four slots. The first version uses two counting semaphores (`empty` and `full`); the second uses a hand-kept count and two condition variables. Same numbers in, same sum out; the difference is where the count lives. The consumer is deliberately slower than the producer, so the ring fills and you can watch the producer block.

**`user.c` + four syscalls — the same buffer in ring 3.** The mutex and semaphores move into the shared user-data page, and ring 3 operates on them through `syscall_mutex_lock/unlock` and `syscall_sem_wait/post`. The kernel dereferences the user pointer directly, because during a syscall it is in the caller's address space. That trust is a real hole, flagged in `syscall.h` and `DISCUSSION.md`: the wait queues are kernel pointers sitting in user-writable memory. A grown-up kernel hands out opaque handles instead; this one takes the shortcut in daylight.

---

## Building and running

```bash
cd 13_condvar_sem_64/src
make
cd ..
bash mkinitrd.sh
qemu-system-x86_64 -kernel src/kernel -initrd initrd.img
```

The build assembles with `nasm`, compiles with `gcc -std=gnu11 -ffreestanding`, links an ELF64 image, and rewrites the container to ELF32 so a Multiboot 1 loader will jump into it — identical to every chapter since the port began.

Representative output:

```
tasking up. pid = 1, tid = 2

semaphore: 1 producer, 1 consumer, ring of 4
  produced 1
  produced 2
    consumed 1
  produced 3
  produced 4
  produced 5
    consumed 2
  produced 6
    consumed 3
  produced 7
    consumed 4
    consumed 5
  produced 8
    consumed 6
    consumed 7
    consumed 8
  sum = 36, expected 36  OK

condvar: the same buffer, count kept by hand
  produced 1
  produced 2
    consumed 1
  ...
  sum = 36, expected 36  OK

user thread tid 8
  [ring3] producer tid 9
  [ring3] produced 1
  [ring3] produced 2
  [ring3] consumed 1
  ...
  [ring3] sum = 36
```

The exact interleaving of `produced` and `consumed` lines changes from run to run — that is the scheduler choosing, and it is meant to. What does *not* change is the sum. The buffer is guarded, so every value is produced once and consumed once regardless of order, and the total is always 36. A deterministic answer riding on a nondeterministic schedule is the whole promise of the chapter: the primitives make the interleaving safe without making it fixed.

Watch the semaphore run for the gap where several `produced` lines land in a row and then stop until a `consumed` line appears. That pause is the producer blocked on `empty` — a thread waiting on a condition a mutex could not have expressed, and burning no CPU while it waits.

---

## A note on the two versions

Keeping both the semaphore and the condition-variable buffer in `main.c` is deliberate, and DRY would say to cut one. The point survives only in the comparison: the semaphore folds the slot count into itself, the condition variable makes you keep the count by hand and hold more code to do it. The semaphore is sharper for counting; the condition variable is general, and worth its extra lines the moment the thing you wait for stops being a number. Reading the two side by side is how you learn which to reach for, so both stay.
