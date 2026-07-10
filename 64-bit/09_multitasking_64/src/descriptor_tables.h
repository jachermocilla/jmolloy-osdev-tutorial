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
