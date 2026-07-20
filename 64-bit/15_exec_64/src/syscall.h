// syscall.h -- Defines the interface for and structures relating to the syscall
//              dispatch system.
//              Written for JamesM's kernel development tutorials.
//              Ported to x86-64.

#ifndef SYSCALL_H
#define SYSCALL_H

#include "common.h"

void initialise_syscalls();

// A function pointer needs a name before it can be a macro argument: the
// DEFN_SYSCALL1 template writes `P1 p1`, and `void (*)(void) p1` is not a
// declaration. This is the one place C's declarator syntax leaks into the
// syscall table.
typedef void (*user_entry_t)(void);

#define SYS_MONITOR_WRITE      0
#define SYS_MONITOR_WRITE_HEX  1
#define SYS_MONITOR_WRITE_DEC  2
#define SYS_GETPID             3
#define SYS_FORK               4
#define SYS_EXIT               5
#define SYS_GETTID             6
#define SYS_THREAD_CREATE      7
#define SYS_JOIN               8
#define SYS_MUTEX_LOCK         9
#define SYS_MUTEX_UNLOCK      10
#define SYS_SEM_WAIT          11
#define SYS_SEM_POST          12
#define SYS_GETCHAR           13
#define SYS_EXEC              14

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
DEFN_SYSCALL0(fork,              SYS_FORK)
DEFN_SYSCALL0(exit,              SYS_EXIT)
DEFN_SYSCALL0(gettid,            SYS_GETTID)

// syscall_thread_create() takes the address of a function in .user_text and
// returns a tid. Contrast syscall_fork(), one line above: same shape, same
// cost to call, and the entire difference is whether the kernel clones the
// page tables or copies a pointer to them.
DEFN_SYSCALL1(thread_create,     SYS_THREAD_CREATE,     user_entry_t)

// Returns the joined thread's exit value. A real join() writes it through a
// user pointer and returns a status; that needs the pointer validation
// syscall_handler() still does not do, so this one cheats and says so.
DEFN_SYSCALL1(join,              SYS_JOIN,              u64int)

// Synchronisation, reached from ring 3. Each takes the address of an object
// that lives in the shared .user_data page, and the kernel operates on it in
// place. This is a real hole and worth naming: the mutex's and semaphore's wait
// queues are *kernel* thread pointers stored in *user-writable* memory. A
// hostile ring-3 program can scribble the queue and steer the kernel into a bad
// pointer. A grown-up kernel keeps these objects on its own side and hands user
// space an opaque handle; this one trusts the pointer, exactly as syscall_join
// above trusts its argument, and for the same reason -- the machinery to do it
// safely is a later chapter's.
DEFN_SYSCALL1(mutex_lock,        SYS_MUTEX_LOCK,        void *)
DEFN_SYSCALL1(mutex_unlock,      SYS_MUTEX_UNLOCK,      void *)
DEFN_SYSCALL1(sem_wait,          SYS_SEM_WAIT,          void *)
DEFN_SYSCALL1(sem_post,          SYS_SEM_POST,          void *)

// Block in the kernel until a key is typed, and return it. The whole wait --
// the sleeping, the waking by the IRQ1 handler -- happens on the kernel's side
// of the trap; ring 3 sees a call that simply takes a while to return.
DEFN_SYSCALL0(getchar,           SYS_GETCHAR)

// Turn the calling process into the program named by `path`, loaded from the
// initrd. Returns only on failure (no such file); on success the caller ceases
// to exist and the new program runs in its place. The path pointer is trusted
// unchecked, the same shortcut syscall_handler() has taken since chapter 12.
DEFN_SYSCALL1(exec,              SYS_EXEC,              const char *)

#endif
