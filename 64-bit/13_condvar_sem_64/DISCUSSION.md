# Chapter 13: Condition Variables and Semaphores

The last chapter built one lock and stopped. A mutex answers a single question — *is this one thing free?* — and answers it well. It is also the only question the previous chapter could ask, and most of the coordination a real program needs cannot be phrased that way.

A consumer waiting for the next item does not want to know whether a lock is free. It wants to know whether there is an item, and to sleep until there is one. A mutex has no word for that. It can protect the buffer while you look, but it cannot wake you when the thing you are looking for arrives.

This chapter adds the two primitives that can. Both are built from machinery that already exists — the block-and-wake from chapter 12, the wait queue, the masked critical section. Nothing new happens in the scheduler. What is new is what these two arrangements of the same parts let a program *say*.

---

# The Machinery Was Already Here

Chapter 12 left three tools lying on the bench: a way to put the running thread to sleep (`block`), a way to make a sleeping thread runnable (`unblock`), and a queue to hold the sleepers (`wq_enqueue`, `wq_dequeue`). The mutex used all three. So does everything in this chapter.

That is worth saying plainly before any code, because it changes what these primitives are. A semaphore is not a new mechanism. It is the mutex's mechanism with a counter where the mutex had a single bit. A condition variable is the same mechanism with the counter taken out entirely and handed back to the program. Three tools, two more ways to arrange them.

The scheduler does not learn a single new fact in this chapter. Every thread that blocks here blocks exactly as a thread blocked on a mutex did: it marks itself unrunnable, puts itself on a queue, and yields. The invariant from chapter 12 still holds without amendment.

> A thread is on exactly one queue, or it is running and on none.

A semaphore's waiters are on the semaphore's queue. A condition variable's are on its queue. The rule never bent, which is the sign that these primitives belong to the design rather than being bolted onto it.

---

# A Mutex That Can Count

Give the mutex's single bit a range and it becomes a semaphore. Where the mutex held *taken* or *free*, the semaphore holds a number: how many threads may pass before the next one has to wait.

```text
    mutex                     semaphore(3)

    locked ∈ {0, 1}           count ∈ {0, 1, 2, 3, ...}

    lock:   wait for 1→0      wait:  wait for count>0, then count--
    unlock: 0→1               post:  count++
```

Initialise the count to 1 and the semaphore *is* a mutex — one thread passes, the second waits. Initialise it to 5 and five threads pass before the sixth blocks, which is exactly what you want guarding a pool of five identical things. The count is the whole idea.

The implementation is the mutex's, with the bit widened:

```text
    sem_wait(s):
        while (s->count == 0)          // the same while, for the same reason
        {
            wq_enqueue(&s->waiters, current);
            block();
        }
        s->count--;

    sem_post(s):
        s->count++;
        wake one waiter, if any
```

The `while` is not decoration, and it is the same `while` the mutex insisted on. A post wakes a waiter; it does not hand it the count. Between the wake and the moment that thread runs, a third thread can call `sem_wait` and take the count back to zero. The woken thread re-tests, finds nothing, and sleeps again. Being woken is being told to look, never being given the thing.

One property separates the semaphore from the mutex, and it is the useful one. A post that arrives when no one is waiting is *remembered* — the count simply goes up, and the next thread to call `sem_wait` passes without blocking. The mutex had nowhere to store an early unlock; the semaphore's count is exactly that storage. A signal that outlives the absence of a listener is the thing a bounded buffer needs, and it is the reason the buffer below is written with semaphores first.

---

# One Number, Two Questions

The demonstration is a bounded buffer: a producer making the numbers 1 through 8, a consumer adding them up, and a ring of four slots between them. The ring is smaller than the run on purpose. Neither thread can finish in one go, so each is forced to wait — the producer for room, the consumer for an item — and the two waits are the entire point.

A mutex alone cannot express either wait. It can keep the two threads from touching the ring at the same instant, and that is necessary, but it has no way to say *stop until there is room*. That sentence needs a count, and the buffer needs it twice:

```text
    empty : how many slots are free   (starts at 4)
    full  : how many slots hold data  (starts at 0)

    producer                         consumer

    sem_wait(empty)  -- claim a slot  sem_wait(full)   -- claim an item
    lock(m)                           lock(m)
      put value                         take value
    unlock(m)                         unlock(m)
    sem_post(full)   -- offer item    sem_post(empty)  -- offer slot
```

The two semaphores are mirror images, and together they form a closed loop: every value the producer puts in raises `full` by one, every value the consumer takes out raises `empty` by one, and the sum of the two counts is always four. The mutex still guards the ring's pointers, because two threads editing `head` and `tail` at once would corrupt them exactly as chapter 12's counter was corrupted. The semaphores do the waiting; the mutex does the excluding. Neither can do the other's job.

Run it and the consumer, made deliberately slower, lets the ring fill. The producer reaches its fifth number, finds `empty` at zero, and sleeps — visibly, as a gap in the output where the "produced" lines stop until a "consumed" line frees a slot. It is a thread waiting for a condition a mutex could not have named, and spending no CPU to do it.

---

# A Condition Variable Waits for a Sentence

A semaphore counts, and counting is all it can do. Some conditions are not counts. *The buffer holds a value greater than one hundred.* *Every worker has reported in.* *The list is finally sorted.* No integer captures those, and a program that needs to wait for one needs something more general than a semaphore.

A condition variable is that thing, and it is more general precisely because it is emptier. It carries no count, no state, nothing but a queue to sleep on. The state it waits upon lives in the program, in an ordinary variable, guarded by a mutex the program already holds. The condition variable's only job is to be somewhere to sleep until that variable might have changed.

```text
    mutex_lock(&m);
    while (!condition)              // the condition is the program's, not the cv's
        cond_wait(&cv, &m);
    ... condition holds, and m is held ...
    mutex_unlock(&m);
```

The generality has a price, and the price is that the condition variable cannot test the condition, because it does not know what the condition is. The caller must test it, and must test it in a loop. A woken thread has been told only that the variable *might* now be true; it re-checks, and if a faster thread got there first and made it false again, it waits once more. The `while` is the same `while` as ever, one primitive higher.

`cond_wait` does the one step the caller cannot do alone: it releases the mutex and goes to sleep as a single indivisible action. That indivisibility matters more than it looks.

---

# The Gap That Loses a Wakeup

Suppose `cond_wait` released the mutex first and enqueued the sleeper second. Between those two acts the mutex is free, so another thread can lock it, change the variable, and call `cond_signal` — signalling a queue the waiter has not yet joined. The signal wakes nobody. The waiter then enqueues and sleeps, waiting for an event that already happened and will not happen again.

```text
    releases m first                 enqueues first (correct)

    T1: unlock(m)                    T1: enqueue self
    T2: lock(m); change; signal      T1: unlock(m)
        (signal finds empty queue)   T2: lock(m); change; signal
    T1: enqueue self; block              (signal finds T1, wakes it)
        (sleeps forever)             T1: block, then wake
```

This is the **lost wakeup**, and it is a genuine race — not the load-versus-store race of chapter 12, but a race between the waiter and the signaller. Masking interrupts does not save you from it, because both parties are threads and the bug is in the order of their steps, not in an interrupt landing between two instructions.

The fix is order. Enqueue before releasing the mutex, all inside one masked region:

```text
    cond_wait(c, m):
        enqueue self on c->waiters
        mutex_unlock(m)
        block()
        mutex_lock(m)              -- reacquire before returning
```

Because the thread is on the queue before the mutex is free, there is no instant when a signaller can run and find the queue empty while this thread intends to wait. The release and the enqueue happen together, and *together* is what "atomically release and block" means on one processor: a masked region nothing else can interrupt. On a second core the masking would not be enough and the enqueue-then-unlock order would still be exactly right — the order is the real defence, and the masking only makes it indivisible here.

---

# The Same Buffer, Written the Other Way

The bounded buffer appears twice in `main.c`, once with semaphores and once with condition variables, and reading them side by side is the chapter in one glance.

The semaphore version keeps its counts inside the semaphores. `empty` and `full` *are* the counts of free and filled slots; the program never sees a number.

The condition-variable version keeps an ordinary integer, `n`, guarded by the mutex, and two condition variables that hold nothing:

```text
    producer                         consumer

    lock(m)                          lock(m)
    while (n == QN)                  while (n == 0)
        cond_wait(notfull, m)            cond_wait(notempty, m)
    put value; n++                   take value; n--
    signal(notempty)                 signal(notfull)
    unlock(m)                        unlock(m)
```

It is more code to reach the same output, and that is the lesson rather than a complaint. The condition variable earns its extra lines when the condition is something no count could express. Change `while (n == 0)` to `while (n == 0 || peek() <= 100)` and the consumer now waits for a full slot *holding a large value* — a wait no semaphore can be initialised to perform. The semaphore is the sharper tool for counting, and blunt for everything else; the condition variable is general, and pays for its generality in lines. A reader who has written both knows which to reach for, and that knowledge is the whole reason both are here.

---

# Ring 3 Gets the Same Primitives

The chapter ends where chapter 12 ended, one privilege level down. The producer and consumer become a ring-3 program, and the mutex and two semaphores move into the shared user-data page beside the buffer they guard. User code never touches their internals — it hands their addresses to the kernel through four new system calls, and the kernel does the blocking and waking on its behalf.

A user thread that must wait does not spin. `syscall_sem_wait` traps into the kernel, which puts the thread to sleep exactly as the in-kernel version did, and the scheduler stops choosing it until a `syscall_sem_post` from the other thread wakes it. The waiting costs no processor time, and the mechanism that makes that true is invisible from ring 3 — which is the definition of a system call doing its job.

This exposure comes with a hole worth naming rather than hiding. The semaphore and mutex objects live in user-*writable* memory, and their wait queues are *kernel* pointers. A hostile ring-3 program can overwrite a queue and steer the kernel into a bad address. A finished kernel keeps these objects on its own side of the wall and hands user space an opaque handle — an integer that names a kernel object without exposing it. This kernel trusts the pointer, exactly as `syscall_join` in chapter 12 trusted its argument, and for the same honest reason: the machinery to do it safely is a later chapter's, and it is better to run the shortcut in daylight than to pretend the wall is finished.

---

# Looking Ahead

Two primitives, no new scheduler. That is the shape of this chapter, and it is worth carrying forward as a habit of reading: when a system offers a dozen synchronisation objects, most of them are the same block-and-wake wearing different clothes. Barriers, read-write locks, and the events an operating system fires at its drivers are all this chapter's parts in new arrangements.

The debts chapter 12 named are still open, and they are the same debts. The semaphore's `count--` is a plain decrement, correct only because interrupts are masked and one processor means one thread of execution. The condition variable's release-and-block is indivisible for the same reason. Both sentences describe the machine and not the code, and both stop being true the moment a second core can run `sem_wait` at the same instant — where the count needs `lock xadd` and the masked region needs a real spinlock underneath it.

The next capability the kernel is missing is not another lock. It is `exec()` — the ability for a process to stop being a copy of its parent and become a new program, loaded from the filesystem chapter 8 built and has not yet been asked to run anything from. With it, `fork()` finally has a partner, and the read-only initrd becomes something the kernel executes rather than merely lists.
