// user.c -- The ring 3 program that demonstrates fork().
//
// As in chapter 10, everything here lives in .user_text so main() can flip the
// user bit on exactly these pages. The private counter that proves the address
// spaces diverged sits in .user_data, which is also user-mapped -- but once
// fork() runs, the child gets its OWN copy of that page, so the two processes'
// writes no longer collide.

#include "common.h"
#include "syscall.h"

#define USER_TEXT   __attribute__((section(".user_text")))
#define USER_DATA   __attribute__((section(".user_data")))

USER_DATA static char m_parent[] = "  [parent] pid=";
USER_DATA static char m_child[]  = "  [child]  pid=";
USER_DATA static char m_shared[] = " counter=";
USER_DATA static char m_fork[]   = "  fork() returned ";
USER_DATA static char m_nl[]     = "\n";

// The shared-then-private page. Both processes see it start at the same value;
// after fork each has a private copy and they diverge.
USER_DATA static volatile u64int counter = 1000;

USER_TEXT static void spin(u64int n) { for (volatile u64int i = 0; i < n; i++); }

USER_TEXT void fork_task(void)
{
    syscall_monitor_write(m_fork);
    u64int pid = syscall_fork();
    syscall_monitor_write_dec(pid);
    syscall_monitor_write(m_nl);

    if (pid == 0)
    {
        // Child. Drive the counter down from 1000.
        for (int i = 0; i < 5; i++)
        {
            counter -= 100;
            syscall_monitor_write(m_child);
            syscall_monitor_write_dec(syscall_getpid());
            syscall_monitor_write(m_shared);
            syscall_monitor_write_dec(counter);
            syscall_monitor_write(m_nl);
            spin(6000000);
        }
        syscall_exit();
    }
    else
    {
        // Parent. Drive the SAME virtual address up from 1000. If fork copied
        // the page, the two never meet.
        for (int i = 0; i < 5; i++)
        {
            counter += 100;
            syscall_monitor_write(m_parent);
            syscall_monitor_write_dec(syscall_getpid());
            syscall_monitor_write(m_shared);
            syscall_monitor_write_dec(counter);
            syscall_monitor_write(m_nl);
            spin(6000000);
        }
        for (;;) spin(9000000);
    }
}
