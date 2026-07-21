// multiboot.h -- Declares the multiboot info structure.
//                Made for JamesM's tutorials <www.jamesmolloy.co.uk>
//                Ported to x86-64.

#ifndef MULTIBOOT_H
#define MULTIBOOT_H

#include "common.h"

#define MULTIBOOT_FLAG_MEM     0x001
#define MULTIBOOT_FLAG_DEVICE  0x002
#define MULTIBOOT_FLAG_CMDLINE 0x004
#define MULTIBOOT_FLAG_MODS    0x008
#define MULTIBOOT_FLAG_AOUT    0x010
#define MULTIBOOT_FLAG_ELF     0x020
#define MULTIBOOT_FLAG_MMAP    0x040
#define MULTIBOOT_FLAG_CONFIG  0x080
#define MULTIBOOT_FLAG_LOADER  0x100
#define MULTIBOOT_FLAG_APM     0x200
#define MULTIBOOT_FLAG_VBE     0x400

// DO NOT WIDEN THESE FIELDS.
//
// This is not our struct. The Multiboot 1 specification fixes every field at
// exactly 32 bits, and the bootloader has already written the bytes before we
// get control. `mods_addr` is a 32-bit *physical* address, and it is 32 bits
// even on a 64-bit machine -- which is one reason Multiboot 2 exists.
//
// Turning these into u64int would silently shift every field after `flags` and
// you would read the memory map where you expected the module list.
struct multiboot
{
    u32int flags;
    u32int mem_lower;
    u32int mem_upper;
    u32int boot_device;
    u32int cmdline;
    u32int mods_count;
    u32int mods_addr;
    u32int num;
    u32int size;
    u32int addr;
    u32int shndx;
    u32int mmap_length;
    u32int mmap_addr;
    u32int drives_length;
    u32int drives_addr;
    u32int config_table;
    u32int boot_loader_name;
    u32int apm_table;
    u32int vbe_control_info;
    u32int vbe_mode_info;
    u32int vbe_mode;
    u32int vbe_interface_seg;
    u32int vbe_interface_off;
    u32int vbe_interface_len;
}  __attribute__((packed));

// One entry of the module list that mods_addr points at. Also 32-bit, also
// fixed by the specification.
struct multiboot_module
{
    u32int mod_start;
    u32int mod_end;
    u32int string;
    u32int reserved;
} __attribute__((packed));

#endif
