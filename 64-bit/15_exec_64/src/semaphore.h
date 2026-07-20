// semaphore.h -- A counting semaphore.
//                Written for JamesM's kernel development tutorials.
//                New in chapter 13.

#ifndef SEMAPHORE_H
#define SEMAPHORE_H

#include "common.h"

// A semaphore is a mutex that can count. The mutex in chapter 12 held one bit:
// taken or free. A semaphore holds a number, and that number is the whole
// difference. `sem_wait` waits for it to be positive and then spends one;
// `sem_post` adds one and wakes a waiter. Initialise it to 1 and you have the
// mutex back; initialise it to N and N threads may pass before the (N+1)th
// waits.
//
// The count is what a mutex could never express. A mutex answers "is this one
// thing free?"; a semaphore answers "are there any of these left?", which is
// the question the bounded buffer in main.c actually needs to ask twice -- once
// about empty slots, once about full ones.

struct thread;

typedef struct semaphore
{
    volatile s64int count;      // How many may pass without waiting.
    struct thread *waiters;     // Wait queue. Threads here are BLOCKED.
} semaphore_t;

// A static initialiser, so a semaphore can live in .user_data as plain data --
// which is exactly how the ring-3 demo shares one between threads.
#define SEM_INIT(n) { (n), 0 }

void sem_init(semaphore_t *s, s64int count);

// Spend one unit. Blocks while the count is zero, and never returns until it
// has decremented the count itself.
void sem_wait(semaphore_t *s);

// Add one unit and wake one waiter, if any.
void sem_post(semaphore_t *s);

#endif // SEMAPHORE_H
