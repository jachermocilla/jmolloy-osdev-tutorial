// proc.c -- Processes: address spaces with a refcount.
//           Written for JamesM's kernel development tutorials.
//           New in chapter 12; fork() is chapter 11's, moved and thinned.

#include "proc.h"
#include "thread.h"
#include "sync.h"
#include "paging.h"
#include "kheap.h"

// Defined in paging.c.
extern u64int current_pml4_phys;

// Never freed, so the refcount in thread_exit() can never take it away.
// initialise_paging() has already run by the time anything reads pml4_phys, and
// initialise_tasking() fills it in.
process_t kernel_process = { 0, 0, 0 };

process_t *proc_create(u64int pml4_phys)
{
    process_t *p = (process_t *)kmalloc(sizeof(process_t));
    p->pid       = alloc_id();
    p->pml4_phys = pml4_phys;
    p->nthreads  = 0;
    return p;
}

int fork(registers_t *parent_frame)
{
    u64int f = irq_save();

    // A new address space, and a process to own it.
    process_t *child_proc = proc_create(clone_address_space(current_thread->proc->pml4_phys));

    // One thread. Note the number: a four-threaded process that forks gets a
    // child with *one* thread and a copy of the other three's memory -- which
    // includes any mutex they happened to be holding at the instant of the
    // call. Those mutexes are now locked forever, by threads that do not exist,
    // in an address space that will deadlock the first time anyone touches
    // them. This is real POSIX behaviour, it is why pthread_atfork() exists,
    // and it is the best argument in this book for why fork() and threads are
    // an uneasy pair. We have one thread per process in the demo, so we get
    // away with it. Say so rather than hide it.
    child_proc->nthreads = 1;

    thread_t *child = (thread_t *)kmalloc(sizeof(thread_t));
    child->tid        = alloc_id();
    child->pid        = child_proc->pid;    // The child's own process id.
    child->state      = THREAD_READY;
    child->proc       = child_proc;
    child->kstack     = kmalloc_a(KERNEL_STACK_SIZE);
    child->kstack_top = child->kstack + KERNEL_STACK_SIZE;
    child->joiner     = 0;
    child->retval     = 0;
    child->detached   = 0;                  // The parent will wait() and reap us.
                                            // Chapter 15 set this to 1 and let the
                                            // parent idle; wait() is what changes.
    child->next       = 0;

    // The child resumes exactly where the parent is now -- returning from this
    // same int $0x80 -- so it needs a copy of the parent's interrupt frame,
    // sitting on the child's own kernel stack. The one field that differs is
    // the return value: fork() returns 0 in the child.
    registers_t *cframe = (registers_t *)(child->kstack_top - sizeof(registers_t));
    memcpy((u8int *)cframe, (u8int *)parent_frame, sizeof(registers_t));
    cframe->rax = 0;
    child->rsp = (u64int)cframe;

    registry_add(child);
    enqueue_ready(child);

    irq_restore(f);

    // The parent's own return value: the child's pid.
    return child_proc->pid;
}
