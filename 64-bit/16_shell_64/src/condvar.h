// condvar.h -- A condition variable.
//              Written for JamesM's kernel development tutorials.
//              New in chapter 13.

#ifndef CONDVAR_H
#define CONDVAR_H

#include "common.h"
#include "sync.h"

// A semaphore carries its own count. A condition variable carries nothing --
// it is a wait queue and two words of code. The count it waits on lives in the
// program, guarded by a mutex the program already holds, and the condition can
// be anything that mutex protects: "the buffer is non-empty", "three workers
// have finished", "the list is sorted". A semaphore can only count. A condition
// variable can wait for a sentence.
//
// The price of that generality is that the condition variable does not know
// what it is waiting for, so it cannot test it. The caller must, and must do so
// in a loop:
//
//     mutex_lock(&m);
//     while (!condition)          // while, never if -- see cond_wait
//         cond_wait(&cv, &m);
//     ... condition now holds, and m is held ...
//     mutex_unlock(&m);
//
// cond_wait does the one thing the caller cannot do for itself: drop the mutex
// and go to sleep as a single indivisible step, so that no signal can slip into
// the gap between "I decided to wait" and "I am waiting".

struct thread;

typedef struct condvar
{
    struct thread *waiters;     // Wait queue. Threads here are BLOCKED.
} condvar_t;

#define COND_INIT { 0 }

void cond_init(condvar_t *c);

// Atomically release `m` and block on `c`. On return the thread has been woken
// and has reacquired `m`. Must be called with `m` held.
void cond_wait(condvar_t *c, mutex_t *m);

// Wake one waiter. If none are waiting the signal is lost -- a condition
// variable has no memory, which is exactly why the caller keeps the real state
// in a mutex-guarded variable and only uses the condvar to sleep on it.
void cond_signal(condvar_t *c);

// Wake every waiter. Each reacquires `m` in turn and re-tests its condition;
// the ones whose condition is still false go straight back to sleep.
void cond_broadcast(condvar_t *c);

#endif // CONDVAR_H
