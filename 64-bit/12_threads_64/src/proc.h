// proc.h -- What a process is, once threads exist to take the rest away.
//           Written for JamesM's kernel development tutorials.
//           New in chapter 12.

#ifndef PROC_H
#define PROC_H

#include "common.h"
#include "isr.h"

// Chapter 11's task_t held six fields. Exactly one of them -- pml4_phys -- was
// a property of the *program*: everything else described a thread of control.
// Nothing here is an addition. This struct and thread_t are task_t, sorted.
//
// The test for which side a field belongs on: if two threads of one program
// must agree on it, it belongs here. If each needs its own, it belongs in
// thread_t. `rsp` fails that test immediately, which is the whole point.
typedef struct process
{
    u32int pid;
    u64int pml4_phys;       // The address space. The reason this struct exists.

    // How many threads are still running in that address space. The last one
    // out frees it. Chapter 11's task_exit() freed unconditionally, which was
    // correct only because no two tasks could share a space. They can now, and
    // an unconditional free is a thread shooting its own siblings.
    u32int nthreads;
} process_t;

// The address space every kernel thread runs in. Statically allocated and never
// freed, so the refcount below can never reach zero for it.
extern process_t kernel_process;

// Allocate a process_t around an existing address space. nthreads starts at 0;
// the caller adds its own thread.
process_t *proc_create(u64int pml4_phys);

// fork(). Clones the caller's address space and the caller's interrupt frame,
// producing a new process with exactly one thread, which resumes from the same
// syscall with a return value of 0. Returns the child's pid to the parent.
// Must be called from within a syscall -- it needs the caller's frame.
int fork(registers_t *parent_frame);

#endif // PROC_H
