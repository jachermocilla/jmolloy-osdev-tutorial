//
// descriptor_tables.c - Initialises the GDT and IDT, and defines the 
//                       default ISR and IRQ handler.
//                       Based on code from Bran's kernel development tutorials.
//                       Rewritten for JamesM's kernel development tutorials.
//

#include "common.h"
#include "descriptor_tables.h"

// Lets us access our ASM functions from our C code.
extern void gdt_flush(u64int);
extern void idt_flush(u64int);

// Internal function prototypes.
static void init_gdt();
static void init_idt();
static void gdt_set_gate(s32int,u32int,u32int,u8int,u8int);
static void idt_set_gate(u8int,u64int,u16int,u8int);

gdt_entry_t gdt_entries[5];
gdt_ptr_t   gdt_ptr;
idt_entry_t idt_entries[256];
idt_ptr_t   idt_ptr;

// Initialisation routine - zeroes all the interrupt service routines,
// initialises the GDT and IDT.
void init_descriptor_tables()
{

    // Initialise the global descriptor table.
    init_gdt();
    // Initialise the interrupt descriptor table.
    init_idt();

}

static void init_gdt()
{
    gdt_ptr.limit = (sizeof(gdt_entry_t) * 5) - 1;
    gdt_ptr.base  = (u64int)&gdt_entries;

    // Granularity high nibble: bit 7 = G, bit 6 = D/B, bit 5 = L, bit 4 = AVL.
    // A 64-bit code segment sets L and *clears* D/B -- 0xA0, not 0xC0. Setting
    // both is illegal and faults on the first far jump.
    gdt_set_gate(0, 0, 0, 0, 0);                // Null segment
    gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xAF); // Kernel code segment (L=1)
    gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0xCF); // Kernel data segment
    gdt_set_gate(3, 0, 0xFFFFFFFF, 0xFA, 0xAF); // User mode code segment (L=1)
    gdt_set_gate(4, 0, 0xFFFFFFFF, 0xF2, 0xCF); // User mode data segment

    gdt_flush((u64int)&gdt_ptr);
}

// Set the value of one GDT entry.
static void gdt_set_gate(s32int num, u32int base, u32int limit, u8int access, u8int gran)
{
    gdt_entries[num].base_low    = (base & 0xFFFF);
    gdt_entries[num].base_middle = (base >> 16) & 0xFF;
    gdt_entries[num].base_high   = (base >> 24) & 0xFF;

    gdt_entries[num].limit_low   = (limit & 0xFFFF);
    gdt_entries[num].granularity = (limit >> 16) & 0x0F;
    
    gdt_entries[num].granularity |= gran & 0xF0;
    gdt_entries[num].access      = access;
}

static void init_idt()
{
    idt_ptr.limit = sizeof(idt_entry_t) * 256 -1;
    idt_ptr.base  = (u64int)&idt_entries;

    memset(&idt_entries, 0, sizeof(idt_entry_t)*256);

    idt_set_gate( 0, (u64int)isr0 , 0x08, 0x8E);
    idt_set_gate( 1, (u64int)isr1 , 0x08, 0x8E);
    idt_set_gate( 2, (u64int)isr2 , 0x08, 0x8E);
    idt_set_gate( 3, (u64int)isr3 , 0x08, 0x8E);
    idt_set_gate( 4, (u64int)isr4 , 0x08, 0x8E);
    idt_set_gate( 5, (u64int)isr5 , 0x08, 0x8E);
    idt_set_gate( 6, (u64int)isr6 , 0x08, 0x8E);
    idt_set_gate( 7, (u64int)isr7 , 0x08, 0x8E);
    idt_set_gate( 8, (u64int)isr8 , 0x08, 0x8E);
    idt_set_gate( 9, (u64int)isr9 , 0x08, 0x8E);
    idt_set_gate(10, (u64int)isr10, 0x08, 0x8E);
    idt_set_gate(11, (u64int)isr11, 0x08, 0x8E);
    idt_set_gate(12, (u64int)isr12, 0x08, 0x8E);
    idt_set_gate(13, (u64int)isr13, 0x08, 0x8E);
    idt_set_gate(14, (u64int)isr14, 0x08, 0x8E);
    idt_set_gate(15, (u64int)isr15, 0x08, 0x8E);
    idt_set_gate(16, (u64int)isr16, 0x08, 0x8E);
    idt_set_gate(17, (u64int)isr17, 0x08, 0x8E);
    idt_set_gate(18, (u64int)isr18, 0x08, 0x8E);
    idt_set_gate(19, (u64int)isr19, 0x08, 0x8E);
    idt_set_gate(20, (u64int)isr20, 0x08, 0x8E);
    idt_set_gate(21, (u64int)isr21, 0x08, 0x8E);
    idt_set_gate(22, (u64int)isr22, 0x08, 0x8E);
    idt_set_gate(23, (u64int)isr23, 0x08, 0x8E);
    idt_set_gate(24, (u64int)isr24, 0x08, 0x8E);
    idt_set_gate(25, (u64int)isr25, 0x08, 0x8E);
    idt_set_gate(26, (u64int)isr26, 0x08, 0x8E);
    idt_set_gate(27, (u64int)isr27, 0x08, 0x8E);
    idt_set_gate(28, (u64int)isr28, 0x08, 0x8E);
    idt_set_gate(29, (u64int)isr29, 0x08, 0x8E);
    idt_set_gate(30, (u64int)isr30, 0x08, 0x8E);
    idt_set_gate(31, (u64int)isr31, 0x08, 0x8E);

    idt_flush((u64int)&idt_ptr);
}

static void idt_set_gate(u8int num, u64int base, u16int sel, u8int flags)
{
    idt_entries[num].base_lo  = (base >>  0) & 0xFFFF;
    idt_entries[num].base_mid = (base >> 16) & 0xFFFF;
    idt_entries[num].base_hi  = (base >> 32) & 0xFFFFFFFF;

    idt_entries[num].sel     = sel;
    idt_entries[num].ist     = 0;       // No Interrupt Stack Table switch.
    idt_entries[num].always0 = 0;
    // We must uncomment the OR below when we get to using user-mode.
    // It sets the interrupt gate's privilege level to 3.
    idt_entries[num].flags   = flags /* | 0x60 */;
}
