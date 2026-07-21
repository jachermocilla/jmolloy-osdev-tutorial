// thread.h -- Threads: the half of chapter 11's task_t that is not the address
//             space.
//             Written for JamesM's kernel development tutorials.
//             New in chapter 12.

#ifndef THREAD_H
#define THREAD_H

#include "common.h"
#include "isr.h"
#include "proc.h"

#define KERNEL_STACK_SIZE 0x4000     // 16 KiB per thread.

typedef enum
{
    THREAD_READY,       // On the ready queue, waiting for a turn.
    THREAD_RUNNING,     // On the CPU. On no queue at all.
    THREAD_BLOCKED,     // On some wait queue. Not runnable until unblocked.
    THREAD_ZOMBIE       // Finished, but not yet joined. Holds a return value.
} thread_state_t;

// One invariant governs every bug in this chapter:
//
//     A thread is on exactly one queue, or it is running and on none.
//
// `next` means "the ready queue" or "this mutex's waiters" or "nothing",
// never two at once. `all_next` is a separate link precisely so that the
// registry does not violate this -- a zombie is on no queue, and join() still
// has to be able to find it.
typedef struct thread
{
    u32int tid;
    u32int pid;                 // The owning process's id, copied here at birth.
                                // Redundant with proc->pid while we live -- but a
                                // zombie outlives its process_t (thread_exit frees
                                // it), and wait() still has to name which process
                                // this corpse was. So the pid rides on the thread.
    u64int rsp;                 // Saved registers_t frame. Unchanged from task_t.
    u64int kstack;              // Base of this thread's kernel stack.
    u64int kstack_top;          // Top of it. Loaded into TSS.rsp0 on every switch.

    process_t *proc;            // Which address space we run in.
    thread_state_t state;

    struct thread *next;        // Whichever *one* queue we are on.
    struct thread *all_next;    // The registry. Every thread, forever.

    struct thread *joiner;      // Thread blocked in join() on us, if any.
    u64int retval;
    u32int detached;            // Nobody will join us; we free ourselves.
} thread_t;

extern volatile thread_t *current_thread;

// Turns the thread of control we are already running on into thread 1 of the
// kernel process, creates the idle thread, and registers the scheduler.
void initialise_tasking(void);

// Create a thread in the *current* process. The entire difference from fork():
// this inherits the address-space pointer where fork() clones the tables.
//
// `arg` lands in RDI, which is all "passing an argument to a thread" is once
// you are already building the frame by hand.
int thread_create(void (*entry)(void *), void *arg);

// Chapter 9's create_task(), still here, now visibly a two-line wrapper. It was
// always making threads -- it shared current_pml4_phys and said so in a comment.
// The chapter did not have the noun yet.
int create_task(void (*entry)(void));

// A thread in the current process that begins life in ring 3, on a user stack
// of its own. `entry` must live in the .user_text section.
int create_user_task(void (*entry)(void));

// Drops the *current* thread into ring 3 at `entry`, on the given user stack.
void switch_to_user_mode(void (*entry)(void), u64int user_stack_top);

// Map a fresh user stack for tid and return its top. Exposed because the
// SYS_THREAD_CREATE path needs it.
u64int build_user_stack(u32int tid);

u32int getpid(void);        // The current *process's* id.
u32int gettid(void);        // The current *thread's* id. Chapter 11 had one number.

// Give up the rest of this timeslice. Raises INT_YIELD, which lands in the same
// scheduler the timer uses.
void thread_yield(void);

// The scheduler. Registered against the timer IRQ and against INT_YIELD.
registers_t *schedule(registers_t *regs);

// Finish. Wakes any joiner, drops the process refcount, and never returns.
void thread_exit(u64int retval);

// Wait for `tid` to finish, collect its return value, and free it. Returns -1
// if there is no such thread, if it is detached, or if it is us.
int thread_join(u32int tid, u64int *retval);

// Nobody will join `tid`; it should clean up after itself. Detached threads
// still leak their kernel stack -- see the comment in thread_exit().
int thread_detach(u32int tid);

// Wait for the process `pid` to finish, collect its exit value into *status,
// and free its last thread. This is thread_join() one level up: join() waits on
// a thread by tid within our own process; proc_wait() waits on a child process
// by pid across the fork boundary. It assumes the child has a single thread,
// which every forked process in this book does. Returns -1 if no such process
// exists or it is us.
int proc_wait(u32int pid, u64int *status);

// ---------------------------------------------------------------------------
// Scheduler primitives. sync.c is the only other file that needs these.
// ---------------------------------------------------------------------------

void enqueue_ready(thread_t *t);            // Append. Sets state to READY.
void wq_enqueue(thread_t **head, thread_t *t);
thread_t *wq_dequeue(thread_t **head);

void block(void);                           // BLOCKED, then yield. Call with IF off.
void unblock(thread_t *t);                  // READY, back on the ready queue.

thread_t *find_thread(u32int tid);
void registry_add(thread_t *t);             // Add to the all-threads list.
u32int alloc_id(void);                      // One counter for pids and tids.

#endif // THREAD_H
