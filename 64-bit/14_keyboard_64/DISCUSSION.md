# Chapter 14: The Keyboard, or a Producer That Cannot Wait

Every chapter so far has talked to itself. The scheduler moved threads the kernel created, the mutex guarded memory the kernel owned, the semaphores in chapter 13 passed numbers one part of the kernel made to another part of the same kernel. Nothing outside the machine has ever been heard from.

This chapter opens the door. A key goes down somewhere in the physical world, and a thread inside the kernel wakes up holding the letter. The path between those two events is the whole subject, and almost all of it was built in chapter 13. What is new is one hard rule about who is standing at each end of it.

---

# It Is the Chapter 13 Buffer, With One End Nailed Down

The keyboard driver is a bounded buffer. A producer puts characters in; a consumer takes them out; a counting semaphore tells the consumer when there is something to take. That is chapter 13 exactly, down to the same `sem_wait` and `sem_post`.

The one change is what the producer is. In chapter 13 both ends were threads, peers that took turns because the scheduler said so. Here the consumer is still a thread — whatever code calls `keyboard_getchar` — but the producer is the IRQ1 interrupt handler. It runs not when the scheduler chooses it but when a key moves, at a moment nothing in software picked. The buffer between them is unchanged; the thing feeding it is no longer a thread at all.

```text
    chapter 13                       chapter 14

    producer thread                  IRQ1 handler        <- runs on a keystroke,
        |  sem_post                       |  sem_post        not on a schedule
        v                                 v
    [ ring of slots ]                [ ring of slots ]
        ^                                 ^
        |  sem_wait                       |  sem_wait
    consumer thread                  reader thread
```

Reusing the semaphore was a choice, and it is the choice that makes the chapter short. The blocking, the waking, the "you were told to look again" loop — all of it already works and was already tested. The driver adds a scancode table and an interrupt handler and inherits everything hard.

---

# The Rule: An Interrupt May Post, Never Wait

A thread that produces into a full buffer has a way out — it calls `sem_wait` on the "empty slots" semaphore and sleeps until the consumer makes room. Chapter 13's producer did exactly that.

The interrupt handler has no such move, and the reason is not a rule someone imposed but a fact about what it is. To wait is to be put to sleep, and to be put to sleep is to have a thread that the scheduler can set aside and replace. An interrupt handler is not a thread. It borrowed the stack of whatever happened to be running, and there is no "it" for the scheduler to suspend. A handler that called `block` would suspend its victim, not itself, and the machine would wedge with interrupts disabled and no one to turn them back on.

So the handler gets one half of the semaphore and is forbidden the other. It may `sem_post` — bump the count, wake a sleeper — because posting never blocks. It may never `sem_wait`. And when the ring is full because the reader fell behind, the handler cannot wait for space. It drops the character and returns.

```text
    kbd_push(c):
        if the ring is full:
            return          <- the key is lost. An interrupt cannot wait for room.
        put c in the ring
        sem_post(avail)     <- the only half a handler is allowed to touch
```

Dropping input feels like a bug and is a design. A keyboard that blocked the interrupt until a slow reader caught up would freeze every other interrupt behind it — the timer, the disk, the clock. Losing a keystroke you typed faster than the machine could read is the lesser harm, and every real driver makes the same trade at the same spot. The lesson to carry forward is the shape of it: **the end of a channel that lives in interrupt context can signal, but it can never wait.**

---

# Where Masking Finally Is Locking

The ring is touched by two parties: the reader thread taking a byte, and the IRQ1 handler putting one in. They share mutable state, which in every previous chapter meant a lock. The reader here takes no lock. It masks interrupts.

```text
    keyboard_getchar():
        sem_wait(avail)         // block until a byte exists
        irq_save()              // mask
        take byte from ring
        irq_restore()           // unmask
```

Chapter 12 spent a whole section warning that masking is not locking — that `cli` buys mutual exclusion only by the accident of there being one CPU, and says nothing to a second core. That warning stands. What is different here is the other party. The thing the reader must exclude *is an interrupt*, and masking interrupts excludes an interrupt exactly and by definition. For once the tool fits the job with nothing left over.

A mutex would be the wrong instrument, and wrong twice. The handler could not acquire it, because acquiring a held mutex means waiting, and the handler may not wait. And if the reader held the mutex when a key arrived, the handler would find it locked by a thread that cannot run until the handler returns — a deadlock built out of a lock that was never needed. The right tool is the blunt one: mask the interrupt while you touch the thing the interrupt also touches. On one processor that is complete, and the sentence that makes it complete — "there is one CPU, so masking the interrupt is the whole of the exclusion" — is the same sentence, again, that a second core will someday falsify.

---

# The Driver Hands You Bytes and No Opinions

`keyboard_getchar` returns a character and does nothing else. It does not print it. The letter that appears on the screen as you type this chapter's demo is put there by the demo, not by the driver, and the split is deliberate.

Echo is policy. A shell echoes what you type; a password prompt reads the same keys and shows nothing; a game reads arrow keys that were never meant to appear as text at all. A driver that printed every key it delivered would force one of those behaviours on all of them. So the driver delivers the byte and stops, and the reader decides what the byte means. The demo happens to echo, because a line editor should; that decision lives in `main.c`, twelve lines away from the driver that has no view on it.

This is the same instinct as chapter 13's mutex, which handed the lock to nobody and let the woken threads race. A primitive that does less is a primitive more things can be built from.

---

# Ring 3 Gets It Through One Door

The reader can be a kernel thread or a user program, and the demo is both. In ring 0 the boot thread calls `keyboard_getchar` directly. In ring 3 a user program calls `syscall_getchar`, which traps into the kernel and calls the very same function.

From ring 3 the wait is invisible. `syscall_getchar` looks like a call that takes a while to return; behind the trap, the kernel blocks the calling thread on the keyboard semaphore and the scheduler stops choosing it until the IRQ1 handler posts a byte. A user program waits on physical hardware and spends no processor doing it, and it never learns the mechanism — the blocking, the wait queue, the interrupt — because all of that is on the kernel's side of the door. Hiding that machinery is what a system call is for.

One system call is the entire ring-3 cost of this chapter. The keyboard needed no new page mapping, no shared object, no pointer for the kernel to trust — a keystroke is a value the kernel already holds and simply returns. Contrast chapter 13, whose ring-3 semaphores had to live in a page both sides could reach; input is easier, because it flows one way.

---

# Scancodes Are a Lookup, Not a Lesson

The handler reads a byte from port 0x60 and turns it into a letter. The byte is a *scancode*, a number naming a key by its position, and it arrives twice per press: once when the key goes down and once, with the top bit set, when it comes up. Two small tables map the downward codes to characters, one for unshifted keys and one for shifted, and the shift keys themselves are the only ones whose release the handler bothers to notice, because they set a mode rather than produce a letter.

None of this is deep, and the driver keeps it shallow on purpose — no keypad, no function keys, no layouts beyond US. Those are more table, not more idea. The idea worth keeping is that the hardware speaks in positions and the driver translates to meaning, and that the translation is the one part of a keyboard driver that has nothing to do with concurrency at all.

---

# Looking Ahead

The kernel can now read a line. That sentence is smaller than it sounds and larger than it looks. Smaller, because the machinery underneath it is entirely chapter 13's, wearing an interrupt where a thread used to be. Larger, because reading a line is half of a shell, and a shell is the thing that finally lets a person drive the operating system instead of watching it.

The other half is missing, and it is the next chapter. A shell reads a command and then *runs* it, and running a program that is not a copy of the shell is something this kernel still cannot do. `fork()` from chapter 11 makes a copy; nothing yet makes a process into a *new* program. `exec()` is that operation — it reads a binary from the filesystem chapter 8 built and has not yet been asked to execute anything from, tears down the caller's memory, and jumps into the new image. With it, the line this chapter learned to read becomes a command the next one can obey.

The debts are unchanged and unpaid. The ring is safe because one processor means masking is exclusion; the semaphore's count is a plain decrement for the same reason. Both sentences describe the machine and not the code, and both wait for the second core that will end this book by making several of them false at once.
