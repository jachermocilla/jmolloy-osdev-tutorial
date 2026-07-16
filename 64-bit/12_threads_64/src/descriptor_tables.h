//
// descriptor_tables.h - Defines the interface for initialising the GDT and IDT.
//                       Also defines needed structures.
//                       Based on code from Bran's kernel development tutorials.
//                       Rewritten for JamesM's kernel development tutorials.
//                       Ported to x86-64.
//

#ifndef DESCRIPTOR_TABLES_H
#define DESCRIPTOR_TABLES_H

#include "common.h"

// Initialisation function is publicly accessible.
void init_descriptor_tables();

// Allows the kernel stack in the TSS to be changed. Must be called on every
// switch to a task that might return to ring 3.
void set_kernel_stack(u64int stack);

// One GDT entry. Still eight bytes wide in long mode.
//
// The base and limit fields are *ignored* for code and data segments in
// 64-bit mode: every segment is flat, spanning the whole address space. We
// keep the fields (and gdt_set_gate's signature) so the code still looks like
// the tutorial's, but the only bits that matter now are `access` and the
// L (long-mode) flag in the high nibble of `granularity`.
struct gdt_entry_struct
{
    u16int limit_low;           // The lower 16 bits of the limit.
    u16int base_low;            // The lower 16 bits of the base.
    u8int  base_middle;         // The next 8 bits of the base.
    u8int  access;              // Access flags, determine what ring this segment can be used in.
    u8int  granularity;         // Low nibble: limit bits 16-19. High nibble: G, D/B, L, AVL.
    u8int  base_high;           // The last 8 bits of the base.
} __attribute__((packed));

typedef struct gdt_entry_struct gdt_entry_t;

// The operand for lgdt. The base is now 64 bits wide.
struct gdt_ptr_struct
{
    u16int limit;
    u64int base;
} __attribute__((packed));

typedef struct gdt_ptr_struct gdt_ptr_t;

// A system-segment descriptor -- sixteen bytes, not eight.
//
// In long mode a TSS descriptor is *twice* the size of a code or data
// descriptor, because its base is a full 64-bit address. It therefore occupies
// two consecutive GDT slots. This is why gdt_entries[] has 7 elements for 5
// usable segments.
struct tss_descriptor_struct
{
    u16int limit_low;
    u16int base_low;
    u8int  base_middle;
    u8int  access;       // 0x89: present, type 9 = available 64-bit TSS
    u8int  granularity;
    u8int  base_high;
    u32int base_upper;   // bits 32-63 of the base
    u32int reserved;
} __attribute__((packed));

typedef struct tss_descriptor_struct tss_descriptor_t;

// The 64-bit Task State Segment. 104 bytes, and it shares almost nothing with
// its 32-bit ancestor.
//
// There is no hardware task switching in long mode, so every general-purpose
// register field is gone. What remains is a table of stack pointers:
//
//   rsp0..rsp2 -- the stack to load when the CPU enters ring 0/1/2 from a
//                 less privileged ring. Only rsp0 matters to us.
//   ist1..ist7 -- the Interrupt Stack Table. A gate whose `ist` field is
//                 nonzero switches to one of these *unconditionally*, even
//                 within ring 0. This is what makes a double-fault handler
//                 survivable after a stack overflow. (See chapter 4.)
struct tss_entry_struct
{
    u32int reserved0;
    u64int rsp0;
    u64int rsp1;
    u64int rsp2;
    u64int reserved1;
    u64int ist1;
    u64int ist2;
    u64int ist3;
    u64int ist4;
    u64int ist5;
    u64int ist6;
    u64int ist7;
    u64int reserved2;
    u16int reserved3;
    u16int iomap_base;   // >= limit disables the I/O permission bitmap entirely
} __attribute__((packed));

typedef struct tss_entry_struct tss_entry_t;

_Static_assert(sizeof(tss_entry_t) == 104, "64-bit TSS must be 104 bytes");
_Static_assert(sizeof(tss_descriptor_t) == 16, "TSS descriptor must be 16 bytes");

// An interrupt gate. Sixteen bytes in long mode, not eight: the handler
// address is 64 bits, and there is a new IST field.
struct idt_entry_struct
{
    u16int base_lo;             // Bits 0-15 of the handler address.
    u16int sel;                 // Kernel code segment selector.
    u8int  ist;                 // Bits 0-2: Interrupt Stack Table index. Rest zero.
    u8int  flags;               // Type and attributes.
    u16int base_mid;            // Bits 16-31 of the handler address.
    u32int base_hi;             // Bits 32-63 of the handler address.
    u32int always0;             // Reserved, must be zero.
} __attribute__((packed));

typedef struct idt_entry_struct idt_entry_t;

// The operand for lidt. The base is now 64 bits wide.
struct idt_ptr_struct
{
    u16int limit;
    u64int base;
} __attribute__((packed));

typedef struct idt_ptr_struct idt_ptr_t;

// These extern directives let us access the addresses of our ASM ISR handlers.
extern void isr0 ();
extern void isr1 ();
extern void isr2 ();
extern void isr3 ();
extern void isr4 ();
extern void isr5 ();
extern void isr6 ();
extern void isr7 ();
extern void isr8 ();
extern void isr9 ();
extern void isr10();
extern void isr11();
extern void isr12();
extern void isr13();
extern void isr14();
extern void isr15();
extern void isr16();
extern void isr17();
extern void isr18();
extern void isr19();
extern void isr20();
extern void isr21();
extern void isr22();
extern void isr23();
extern void isr24();
extern void isr25();
extern void isr26();
extern void isr27();
extern void isr28();
extern void isr29();
extern void isr30();
extern void isr31();
extern void isr128();   // the syscall gate
extern void isr129();

extern void irq0 ();
extern void irq1 ();
extern void irq2 ();
extern void irq3 ();
extern void irq4 ();
extern void irq5 ();
extern void irq6 ();
extern void irq7 ();
extern void irq8 ();
extern void irq9 ();
extern void irq10();
extern void irq11();
extern void irq12();
extern void irq13();
extern void irq14();
extern void irq15();

#endif // DESCRIPTOR_TABLES_H
