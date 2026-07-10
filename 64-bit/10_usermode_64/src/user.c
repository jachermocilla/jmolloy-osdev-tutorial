// user.c -- The ring 3 program.
//
// Everything in this file is placed in the .user_text section, which link.ld
// gives its own page-aligned range. main() then flips the user bit on exactly
// those pages. Nothing else in the kernel becomes reachable from ring 3.
//
// That includes the strings. A literal like "hello" would land in .rodata,
// which is supervisor-only, and the first character read would page-fault.
// Every constant a user program touches must live in a user page.

#include "common.h"
#include "syscall.h"

#define USER_TEXT   __attribute__((section(".user_text")))
#define USER_RODATA __attribute__((section(".user_data")))

USER_RODATA static char msg_a[] = "  [ring 3] hello from pid ";
USER_RODATA static char msg_b[] = ", rsp=";
USER_RODATA static char msg_nl[] = "\n";

USER_TEXT static void user_spin(u64int n)
{
    for (volatile u64int i = 0; i < n; i++)
        ;
}

USER_TEXT void user_task(void)
{
    for (;;)
    {
        u64int rsp;
        asm volatile("mov %%rsp, %0" : "=r"(rsp));

        syscall_monitor_write(msg_a);
        syscall_monitor_write_dec(syscall_getpid());
        syscall_monitor_write(msg_b);
        syscall_monitor_write_hex(rsp);
        syscall_monitor_write(msg_nl);

        user_spin(5000000);
    }
}
