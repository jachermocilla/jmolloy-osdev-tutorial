// initrd.h -- Defines the interface for and structures relating to the initial ramdisk.
//             Written for JamesM's kernel development tutorials.
//             Ported to x86-64.

#ifndef INITRD_H
#define INITRD_H

#include "common.h"
#include "fs.h"

// ===========================================================================
// ON-DISK FORMAT. DO NOT WIDEN THESE FIELDS.
//
// These structs describe bytes that make_initrd.c wrote to a file. They are a
// contract between two programs, and the file does not know or care that the
// kernel reading it is now 64-bit.
//
// `unsigned int` is 4 bytes under both the i386 and the x86-64 System V ABIs,
// so leaving these as u32int keeps the layout identical. Change either to
// u64int and the struct grows from 76 to 88 bytes, `offset` moves from byte 68
// to byte 72, and the kernel reads the file's length where its offset should
// be -- with no warning from anyone.
//
// This is the general rule, and it is worth learning here where it is cheap:
// widen your *in-memory* types freely; never widen a type that describes bytes
// on a disk, on a wire, or in a hardware register.
// ===========================================================================
typedef struct
{
    u32int nfiles; // The number of files in the ramdisk.
} initrd_header_t;

typedef struct
{
    u8int magic;     // Magic number, for error checking.
    s8int name[64];  // Filename.
    u32int offset;   // Offset of the file, from the start of the initrd.
    u32int length;   // Length of the file.
} initrd_file_header_t;

// Belt and braces: make the compiler check the layout for us. If someone
// "helpfully" widens a field, the build stops here rather than at runtime.
_Static_assert(sizeof(initrd_header_t) == 4,       "initrd_header_t layout changed");
_Static_assert(sizeof(initrd_file_header_t) == 76, "initrd_file_header_t layout changed");
_Static_assert(__builtin_offsetof(initrd_file_header_t, name)   == 1,  "name moved");
_Static_assert(__builtin_offsetof(initrd_file_header_t, offset) == 68, "offset moved");
_Static_assert(__builtin_offsetof(initrd_file_header_t, length) == 72, "length moved");

// Initialises the initial ramdisk. It gets passed the address of the multiboot
// module, and returns a completed filesystem node.
fs_node_t *initialise_initrd(u64int location);

#endif
