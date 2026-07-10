// syscall.c -- Defines the implementation of a system call system.
//              Written for JamesM's kernel development tutorials.
//              Ported to x86-64.

#include "syscall.h"
#include "isr.h"
#include "monitor.h"
#include "task.h"

static registers_t *syscall_handler(registers_t *regs);

// Six arguments, in System V order. Handlers that take fewer simply ignore the
// rest -- the ABI lets us over-supply arguments in registers, which is exactly
// why the 32-bit tutorial's push/call/pop dance is unnecessary here.
typedef u64int (*syscall_fn_t)(u64int, u64int, u64int, u64int, u64int, u64int);

static u64int sys_monitor_write(u64int p)     { monitor_write((char *)p); return 0; }
static u64int sys_monitor_write_hex(u64int p) { monitor_write_hex64(p);   return 0; }
static u64int sys_monitor_write_dec(u64int p) { monitor_write_dec((u32int)p); return 0; }
static u64int sys_getpid(void)                { return getpid(); }

static void *syscalls[] =
{
    &sys_monitor_write,
    &sys_monitor_write_hex,
    &sys_monitor_write_dec,
    &sys_getpid,
};
u64int num_syscalls = sizeof(syscalls) / sizeof(syscalls[0]);

void initialise_syscalls()
{
    register_interrupt_handler(0x80, &syscall_handler);
}

static registers_t *syscall_handler(registers_t *regs)
{
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
