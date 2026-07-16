// user.c -- The ring 3 program that demonstrates threads.
//
// Read this beside chapter 11's user.c. They are the same program. The counter
// starts at the same value, one side drives it up, the other drives it down,
// and the only line that differs is which syscall makes the second thread of
// control:
//
//     chapter 11:   u64int pid = syscall_fork();
//     chapter 12:   u64int tid = syscall_thread_create(&counter_down);
//
// Chapter 11's output diverges: 1100, 1200, 1300 on one side and 900, 800, 700
// on the other, because fork() gave the child its own copy of the page the
// counter lives on. Two programs, each convinced it owns the number.
//
// This one converges. There is one counter, at one physical address, and both
// threads are writing it. The number staggers up and down and ends up wherever
// the interleaving left it. That is the whole of what "shares an address space"
// means, and it costs one call to see.

#include "common.h"
#include "syscall.h"

#define USER_TEXT   __attribute__((section(".user_text")))
#define USER_DATA   __attribute__((section(".user_data")))

USER_DATA static char m_up[]     = "  [up   tid=";
USER_DATA static char m_down[]   = "  [down tid=";
USER_DATA static char m_shared[] = "] counter=";
USER_DATA static char m_spawn[]  = "  spawned tid ";
USER_DATA static char m_joined[] = "  joined; it returned ";
USER_DATA static char m_nl[]     = "\n";

// The shared page. In chapter 11 fork() copied it and the two processes stopped
// being able to see each other. Nothing copies it here.
USER_DATA static volatile u64int counter = 1000;

USER_TEXT static void spin(u64int n) { for (volatile u64int i = 0; i < n; i++); }

USER_TEXT static void counter_down(void)
{
    for (int i = 0; i < 5; i++)
    {
        counter -= 100;
        syscall_monitor_write(m_down);
        syscall_monitor_write_dec(syscall_gettid());
        syscall_monitor_write(m_shared);
        syscall_monitor_write_dec(counter);
        syscall_monitor_write(m_nl);
        spin(6000000);
    }
    syscall_exit();
}

USER_TEXT void thread_task(void)
{
    u64int tid = syscall_thread_create(&counter_down);
    syscall_monitor_write(m_spawn);
    syscall_monitor_write_dec(tid);
    syscall_monitor_write(m_nl);

    for (int i = 0; i < 5; i++)
    {
        counter += 100;
        syscall_monitor_write(m_up);
        syscall_monitor_write_dec(syscall_gettid());
        syscall_monitor_write(m_shared);
        syscall_monitor_write_dec(counter);
        syscall_monitor_write(m_nl);
        spin(6000000);
    }

    // Note what this does *not* need: a wait queue in user space, a flag to
    // poll, a spin. join() blocks us in the kernel and the scheduler simply
    // stops choosing us until the other thread exits.
    u64int rv = syscall_join(tid);
    syscall_monitor_write(m_joined);
    syscall_monitor_write_dec(rv);
    syscall_monitor_write(m_nl);

    for (;;) spin(9000000);
}
