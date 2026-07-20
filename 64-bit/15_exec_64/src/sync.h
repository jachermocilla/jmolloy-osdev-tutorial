// sync.h -- Interrupt masking done properly, and a mutex built on top of it.
//           Written for JamesM's kernel development tutorials.
//           New in chapter 12.

#ifndef SYNC_H
#define SYNC_H

#include "common.h"

// ---------------------------------------------------------------------------
// Interrupt masking
// ---------------------------------------------------------------------------
//
// Every critical section in chapters 9 to 11 is bracketed by a bare
//
//     asm volatile("cli");
//     ...
//     asm volatile("sti");
//
// That pair has a bug, and it is the kind that never shows up in a demo. The
// `sti` is unconditional: it does not restore the interrupt flag, it *sets* it.
// Call create_task() from a context where interrupts were already off -- from
// inside an interrupt handler, say, or from within another critical section --
// and it hands them back on behind its caller's back. The caller's critical
// section is now not critical, and nothing anywhere says so.
//
// The fix is to save the flag and put it back:

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

// A note worth carrying into the rest of the chapter: this is not a lock. On
// one CPU, masking interrupts is *sufficient* for mutual exclusion, because the
// only thing that can take the CPU away from you is an interrupt. That is a
// fact about the machine, not about the code. The moment a second CPU exists,
// `cli` protects you from nothing -- the other core was never interrupted, it
// was just running. See the note above mutex_lock().

// ---------------------------------------------------------------------------
// Mutex
// ---------------------------------------------------------------------------

struct thread;

typedef struct mutex
{
    volatile u32int locked;
    struct thread *waiters;     // Wait queue. Threads here are BLOCKED.
} mutex_t;

#define MUTEX_INIT { 0, 0 }

void mutex_init(mutex_t *m);
void mutex_lock(mutex_t *m);
void mutex_unlock(mutex_t *m);

#endif // SYNC_H
