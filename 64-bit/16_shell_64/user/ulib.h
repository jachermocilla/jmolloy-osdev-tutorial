// ulib.h -- The tiny standard library a ring 3 program has instead of libc.
//
// A program on the initrd links against nothing (-nostdlib): no printf, no
// strcmp, no getline. It has the int $0x80 syscalls in syscall.h and whatever
// it writes for itself. Two things every interactive program needs are a way to
// compare a word and a way to read a line, so they live here once, as inline
// functions, and sh.c, prog.c and hello.c all include them. This is the "do not
// repeat yourself" of chapter 15's prog.c, which grew its own copy of read_line
// before there was a second program to share one.

#ifndef ULIB_H
#define ULIB_H

#include "syscall.h"

// Return 1 if the two strings are equal, 0 otherwise. The whole of strcmp that a
// shell actually uses is the equality test, so that is all this is.
static inline int streq(const char *a, const char *b)
{
    while (*a && *a == *b)
    {
        a++;
        b++;
    }
    return *a == *b;
}

// Read one line from the keyboard into buf, echoing each character as it lands
// and honouring backspace. Stops at Enter or when buf is one short of full, and
// always leaves buf null-terminated. Returns the length.
//
// The echo is the program's job, not the driver's: syscall_getchar() hands back
// a byte and nothing appears on screen until we write it. That split is chapter
// 14's, and it is why a password prompt can read a key without showing it.
static inline int read_line(char *buf, int max)
{
    int n = 0;
    for (;;)
    {
        char c = (char)syscall_getchar();

        if (c == '\n')
            break;

        if (c == '\b')                 // Backspace: erase one, on screen and in buf.
        {
            if (n > 0)
            {
                n--;
                syscall_monitor_write("\b \b");
            }
            continue;
        }

        if (n < max - 1)               // Leave room for the null terminator.
        {
            char echo[2];
            echo[0] = c;
            echo[1] = 0;
            syscall_monitor_write(echo);
            buf[n++] = c;
        }
    }
    buf[n] = 0;
    return n;
}

#endif // ULIB_H
