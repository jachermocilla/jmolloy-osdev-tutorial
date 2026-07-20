// user.c -- The ring 3 program that reads the keyboard.
//
// Chapter 12's user program made a second thread; chapter 13's ran a
// producer/consumer over semaphores. This one just listens. It calls
// syscall_getchar() in a loop, echoes each key, and prints the whole line back
// when you press Enter.
//
// Nothing here spins or polls. syscall_getchar() traps into the kernel, the
// kernel blocks this thread on the keyboard's semaphore, and the scheduler
// stops choosing it until the IRQ1 handler posts a character. A ring 3 program
// that waits on hardware costs no CPU, and it never learns how -- the entire
// mechanism lives on the far side of one system call. That is the input half a
// shell needs, and chapter 15 will give it the other half: the ability to run
// what you type.

#include "common.h"
#include "syscall.h"

#define USER_TEXT   __attribute__((section(".user_text")))
#define USER_DATA   __attribute__((section(".user_data")))

USER_DATA static char m_hello[]  = "  [ring3] over to you -- type, Enter echoes the line.\n  $ ";
USER_DATA static char m_you[]    = "\n  [ring3] you typed: ";
USER_DATA static char m_prompt[] = "\n  $ ";
USER_DATA static char m_bs[]     = "\b \b";
USER_DATA static char one[2]     = { 0, 0 };
USER_DATA static char line[128];

// Echo a single character. syscall_monitor_write takes a string, so a key is
// echoed as a two-byte string with the character and a terminating zero.
USER_TEXT static void uputc(char c)
{
    one[0] = c;
    one[1] = 0;
    syscall_monitor_write(one);
}

USER_TEXT void thread_task(void)
{
    u32int n = 0;

    syscall_monitor_write(m_hello);

    for (;;)
    {
        char c = (char)syscall_getchar();

        if (c == '\n')
        {
            line[n] = 0;
            syscall_monitor_write(m_you);
            syscall_monitor_write(line);
            syscall_monitor_write(m_prompt);
            n = 0;
            continue;
        }
        if (c == '\b')
        {
            if (n > 0) { n--; syscall_monitor_write(m_bs); }
            continue;
        }
        if (n < (u32int)sizeof(line) - 1)
        {
            line[n++] = c;
            uputc(c);
        }
    }
}
