// syscall.h -- Defines the interface for and structures relating to the syscall
//              dispatch system.
//              Written for JamesM's kernel development tutorials.
//              Ported to x86-64.

#ifndef SYSCALL_H
#define SYSCALL_H

#include "common.h"

void initialise_syscalls();

#define SYS_MONITOR_WRITE      0
#define SYS_MONITOR_WRITE_HEX  1
#define SYS_MONITOR_WRITE_DEC  2
#define SYS_GETPID             3

// The user-side entry stubs.
//
// The syscall number goes in RAX and the arguments in RDI, RSI, RDX, RCX, R8,
// R9 -- which is simply the System V AMD64 calling convention. The 32-bit
// tutorial has to shuffle five arguments through EBX/ECX/EDX/ESI/EDI by hand
// because 32-bit cdecl passes on the stack, and a stack in ring 3 is not one
// the kernel may safely dereference.
//
// (Linux substitutes R10 for RCX, because the `syscall` instruction clobbers
// RCX with the return address. `int $0x80` does not, so we can keep RCX.)
#define DEFN_SYSCALL0(fn, num)                                            \
    static inline __attribute__((always_inline)) u64int syscall_##fn(void)                               \
    {                                                                     \
        u64int a;                                                         \
        asm volatile("int $0x80" : "=a"(a) : "a"((u64int)(num)) : "memory"); \
        return a;                                                         \
    }

#define DEFN_SYSCALL1(fn, num, P1)                                        \
    static inline __attribute__((always_inline)) u64int syscall_##fn(P1 p1)                              \
    {                                                                     \
        u64int a;                                                         \
        asm volatile("int $0x80"                                          \
                     : "=a"(a)                                            \
                     : "a"((u64int)(num)), "D"((u64int)(p1))              \
                     : "memory");                                         \
        return a;                                                         \
    }

DEFN_SYSCALL1(monitor_write,     SYS_MONITOR_WRITE,     const char *)
DEFN_SYSCALL1(monitor_write_hex, SYS_MONITOR_WRITE_HEX, u64int)
DEFN_SYSCALL1(monitor_write_dec, SYS_MONITOR_WRITE_DEC, u64int)
DEFN_SYSCALL0(getpid,            SYS_GETPID)

#endif
