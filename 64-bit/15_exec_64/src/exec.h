// exec.h -- exec(): turn the calling process into a program loaded from a file.
//           Written for JamesM's kernel development tutorials.
//           New in chapter 15.

#ifndef EXEC_H
#define EXEC_H

#include "common.h"
#include "isr.h"

// The virtual address a flat binary is both linked at and loaded at. A flat
// binary has no header to say where its entry is, so the convention is the only
// thing that can: entry == load base, which is why the program's _start must be
// the first byte of the image (see user/prog.ld).
//
// It sits at 1 GiB, above the 16 MiB direct map and below the user stacks at
// 0x7000_0000_0000. Loading it inside the direct map would collide a user page
// with the kernel's own view of that physical frame; 1 GiB is clear of
// everything.
#define USER_LOAD_BASE  0x40000000UL

// Replace the calling process's image with the program named by `path`, loaded
// from the initrd. On success it does not return to the caller: it rewrites the
// syscall's interrupt frame so the returning iretq lands in the new program, in
// ring 3, on a fresh stack. Returns -1 (and leaves the caller intact) only if
// the file cannot be found.
//
// Must be called from within a syscall -- it needs the caller's frame.
int do_exec(registers_t *frame, const char *path);

#endif // EXEC_H
