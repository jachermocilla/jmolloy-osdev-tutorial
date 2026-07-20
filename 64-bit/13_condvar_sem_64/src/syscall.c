// syscall.c -- Defines the implementation of a system call system.
//              Written for JamesM's kernel development tutorials.
//              Ported to x86-64.

#include "syscall.h"
#include "isr.h"
#include "monitor.h"
#include "thread.h"
#include "proc.h"
#include "paging.h"
#include "sync.h"
#include "semaphore.h"

static registers_t *syscall_handler(registers_t *regs);

// Six arguments, in System V order. Handlers that take fewer simply ignore the
// rest -- the ABI lets us over-supply arguments in registers, which is exactly
// why the 32-bit tutorial's push/call/pop dance is unnecessary here.
typedef u64int (*syscall_fn_t)(u64int, u64int, u64int, u64int, u64int, u64int);

static u64int sys_monitor_write(u64int p)     { monitor_write((char *)p); return 0; }
static u64int sys_monitor_write_hex(u64int p) { monitor_write_hex64(p);   return 0; }
static u64int sys_monitor_write_dec(u64int p) { monitor_write_dec((u32int)p); return 0; }
static u64int sys_getpid(void)                { return getpid(); }
static u64int sys_gettid(void)                { return gettid(); }

// A user thread is create_user_task() with the entry point coming from ring 3
// instead of from main(). Nothing else about it is new -- which is the point.
static u64int sys_thread_create(u64int entry)
{
    return (u64int)create_user_task((void (*)(void))entry);
}

static u64int sys_join(u64int tid)
{
    u64int retval = 0;
    if (thread_join((u32int)tid, &retval) < 0)
        return (u64int)-1;
    return retval;
}

// The sync objects live in the caller's address space, and during a syscall the
// kernel is *in* that address space, so the user pointer dereferences directly.
// No copy, no translation -- the same virtual address means the same object to
// both privilege levels. See the caveat over these stubs in syscall.h.
static u64int sys_mutex_lock(u64int p)   { mutex_lock((mutex_t *)p);       return 0; }
static u64int sys_mutex_unlock(u64int p) { mutex_unlock((mutex_t *)p);     return 0; }
static u64int sys_sem_wait(u64int p)     { sem_wait((semaphore_t *)p);     return 0; }
static u64int sys_sem_post(u64int p)     { sem_post((semaphore_t *)p);     return 0; }

static void *syscalls[] =
{
    &sys_monitor_write,
    &sys_monitor_write_hex,
    &sys_monitor_write_dec,
    &sys_getpid,
    0,              // SYS_FORK  -- handled above, never dispatched here
    0,              // SYS_EXIT  -- handled above
    &sys_gettid,
    &sys_thread_create,
    &sys_join,
    &sys_mutex_lock,
    &sys_mutex_unlock,
    &sys_sem_wait,
    &sys_sem_post,
};
u64int num_syscalls = sizeof(syscalls) / sizeof(syscalls[0]);

void initialise_syscalls()
{
    register_interrupt_handler(0x80, &syscall_handler);
}

static registers_t *syscall_handler(registers_t *regs)
{
    // fork() and exit() are not ordinary functions: they need the interrupt
    // frame itself, not just its argument registers. Handle them here.
    if (regs->rax == SYS_FORK)
    {
        regs->rax = (u64int)fork(regs);
        return regs;
    }
    if (regs->rax == SYS_EXIT)
    {
        // POSIX says exit() ends every thread in the process. This ends the
        // calling thread and lets the process die when the count reaches zero.
        // The difference is invisible until a thread that is not running needs
        // to be stopped, which needs machinery we do not have. It is a choice,
        // not an oversight; the honest version is thread_exit().
        thread_exit(0);   // never returns
        return regs;
    }

    if (regs->rax >= num_syscalls)
    {
        regs->rax = (u64int)-1;
        return regs;
    }

    syscall_fn_t fn = (syscall_fn_t)syscalls[regs->rax];

    // Plain C. No inline assembly, no manual argument marshalling.
    //
    // A real kernel would validate every pointer argument here: is it in user
    // space, is it mapped, is it long enough? We do not, and a hostile ring-3
    // program could hand sys_monitor_write() a kernel address and have the
    // kernel print kernel memory back to it. That is the classic confused
    // deputy, and it is worth knowing that this code has it.
    regs->rax = fn(regs->rdi, regs->rsi, regs->rdx, regs->rcx, regs->r8, regs->r9);

    return regs;
}
