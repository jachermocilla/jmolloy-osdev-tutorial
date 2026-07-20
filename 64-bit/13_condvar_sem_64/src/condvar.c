// condvar.c -- A condition variable.
//              Written for JamesM's kernel development tutorials.
//              New in chapter 13.

#include "condvar.h"
#include "sync.h"
#include "thread.h"

void cond_init(condvar_t *c)
{
    c->waiters = 0;
}

void cond_wait(condvar_t *c, mutex_t *m)
{
    u64int f = irq_save();

    // Order matters, and this is the whole subtlety of the primitive.
    //
    // Enqueue *before* releasing the mutex. A signaller cannot run until we
    // release m and then yield, and we do neither until we have already put
    // ourselves on the queue. So there is no instant in which the mutex is free
    // -- inviting a signal -- but this thread is not yet findable. The dropped
    // mutex and the enqueued thread happen inside one masked region, which is
    // what "atomically release and block" means on one CPU.
    //
    // Get this order wrong -- release first, enqueue second -- and a signal
    // that lands in the gap wakes nobody, because nobody is on the queue yet;
    // the waiter enqueues immediately after and sleeps forever waiting for a
    // signal that already came. That is the lost-wakeup bug, and it is a race
    // the masking here does not save you from, because the two threads are the
    // waiter and the signaller, not the load and the store.
    wq_enqueue(&c->waiters, (thread_t *)current_thread);

    mutex_unlock(m);        // let a signaller make progress
    block();                // IF is off (f was masked); the scheduler moves on

    irq_restore(f);
    mutex_lock(m);          // reacquire before returning, as the caller expects
}

void cond_signal(condvar_t *c)
{
    u64int f = irq_save();

    thread_t *t = wq_dequeue(&c->waiters);
    if (t)
        unblock(t);

    irq_restore(f);
}

void cond_broadcast(condvar_t *c)
{
    u64int f = irq_save();

    thread_t *t;
    while ((t = wq_dequeue(&c->waiters)) != 0)
        unblock(t);

    irq_restore(f);
}

// A condition variable is deliberately forgetful. cond_signal with no waiter
// does nothing, and that is not a defect -- it is why the caller must hold the
// state in a real variable. A semaphore remembers a post that arrives early; a
// condition variable does not remember a signal that arrives early. If you
// reach for a condvar and find yourself wishing it counted, the thing you
// wanted was a semaphore.
