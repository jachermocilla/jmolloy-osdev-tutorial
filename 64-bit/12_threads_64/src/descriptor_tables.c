//
// descriptor_tables.c - Initialises the GDT and IDT, and defines the 
//                       default ISR and IRQ handler.
//                       Based on code from Bran's kernel development tutorials.
//                       Rewritten for JamesM's kernel development tutorials.
//

#include "common.h"
#include "descriptor_tables.h"
#include "isr.h"

// Lets us access our ASM functions from our C code.
extern void gdt_flush(u64int);
extern void tss_flush();
extern void idt_flush(u64int);

// Internal function prototypes.
static void init_gdt();
static void init_idt();
static void write_tss();

// Extern the ISR handler array so we can nullify them on startup.
extern isr_t interrupt_handlers[];
static void gdt_set_gate(s32int,u32int,u32int,u8int,u8int);
static void idt_set_gate(u8int,u64int,u16int,u8int);

gdt_entry_t gdt_entries[7];   // 5 segments + a 16-byte TSS descriptor
tss_entry_t tss_entry;
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

    // Nullify all the interrupt handlers.
    memset((u8int *)&interrupt_handlers, 0, sizeof(isr_t)*256);

}

static void init_gdt()
{
    gdt_ptr.limit = (sizeof(gdt_entry_t) * 7) - 1;
    gdt_ptr.base  = (u64int)&gdt_entries;

    // Granularity high nibble: bit 7 = G, bit 6 = D/B, bit 5 = L, bit 4 = AVL.
    // A 64-bit code segment sets L and *clears* D/B -- 0xA0, not 0xC0. Setting
    // both is illegal and faults on the first far jump.
    gdt_set_gate(0, 0, 0, 0, 0);                // Null segment
    gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xAF); // Kernel code segment (L=1)
    gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0xCF); // Kernel data segment
    gdt_set_gate(3, 0, 0xFFFFFFFF, 0xFA, 0xAF); // User mode code segment (L=1)
    gdt_set_gate(4, 0, 0xFFFFFFFF, 0xF2, 0xCF); // User mode data segment
    write_tss();                                // Slots 5 and 6.

    gdt_flush((u64int)&gdt_ptr);
    tss_flush();
}

// Set the value of one GDT entry.
// Initialise our task state segment structure and its descriptor.
static void write_tss()
{
    u64int base  = (u64int)&tss_entry;
    u64int limit = sizeof(tss_entry) - 1;

    memset((u8int *)&tss_entry, 0, sizeof(tss_entry));

    // The kernel stack to load when the CPU enters ring 0 from ring 3.
    // schedule() rewrites this on every context switch.
    tss_entry.rsp0 = 0;

    // Setting iomap_base beyond the segment limit means "no I/O bitmap", so
    // ring 3 gets #GP on any in/out instruction. Combined with IOPL=0 in
    // RFLAGS, user mode cannot touch an I/O port.
    tss_entry.iomap_base = sizeof(tss_entry);

    // The descriptor straddles gdt_entries[5] and gdt_entries[6].
    tss_descriptor_t *d = (tss_descriptor_t *)&gdt_entries[5];
    d->limit_low   = limit & 0xFFFF;
    d->base_low    = base & 0xFFFF;
    d->base_middle = (base >> 16) & 0xFF;
    d->access      = 0x89;                    // present | type 9 (64-bit TSS, available)
    d->granularity = (limit >> 16) & 0x0F;    // G=0: limit counts bytes
    d->base_high   = (base >> 24) & 0xFF;
    d->base_upper  = (u32int)(base >> 32);
    d->reserved    = 0;
}

void set_kernel_stack(u64int stack)
{
    tss_entry.rsp0 = stack;
}

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

    memset((u8int *)&idt_entries, 0, sizeof(idt_entry_t)*256);

    // Remap the irq table.
    // The PIC is a 1981-vintage 8259A and knows nothing about long mode.
    // This code is byte-for-byte identical to the 32-bit version.
    outb(0x20, 0x11);
    outb(0xA0, 0x11);
    outb(0x21, 0x20);
    outb(0xA1, 0x28);
    outb(0x21, 0x04);
    outb(0xA1, 0x02);
    outb(0x21, 0x01);
    outb(0xA1, 0x01);
    outb(0x21, 0x0);
    outb(0xA1, 0x0);

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
    idt_set_gate(32, (u64int)irq0, 0x08, 0x8E);
    idt_set_gate(33, (u64int)irq1, 0x08, 0x8E);
    idt_set_gate(34, (u64int)irq2, 0x08, 0x8E);
    idt_set_gate(35, (u64int)irq3, 0x08, 0x8E);
    idt_set_gate(36, (u64int)irq4, 0x08, 0x8E);
    idt_set_gate(37, (u64int)irq5, 0x08, 0x8E);
    idt_set_gate(38, (u64int)irq6, 0x08, 0x8E);
    idt_set_gate(39, (u64int)irq7, 0x08, 0x8E);
    idt_set_gate(40, (u64int)irq8, 0x08, 0x8E);
    idt_set_gate(41, (u64int)irq9, 0x08, 0x8E);
    idt_set_gate(42, (u64int)irq10, 0x08, 0x8E);
    idt_set_gate(43, (u64int)irq11, 0x08, 0x8E);
    idt_set_gate(44, (u64int)irq12, 0x08, 0x8E);
    idt_set_gate(45, (u64int)irq13, 0x08, 0x8E);
    idt_set_gate(46, (u64int)irq14, 0x08, 0x8E);
    idt_set_gate(47, (u64int)irq15, 0x08, 0x8E);

    // The syscall gate. 0xEE = 0x8E | 0x60: an interrupt gate whose DPL is 3,
    // so ring 3 is permitted to invoke it with `int $0x80`.
    //
    // The tutorial instead ORs 0x60 into *every* gate, inside idt_set_gate().
    // That lets user code execute `int $14` to forge a page fault, `int $8` to
    // forge a double fault, or `int $32` to forge a timer tick and drive the
    // scheduler. Only the syscall vector should be reachable from ring 3.
    idt_set_gate(128, (u64int)isr128, 0x08, 0xEE);

    // Software interrupt used by task_yield(). Kernel-only: DPL stays 0.
    idt_set_gate(129, (u64int)isr129, 0x08, 0x8E);

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
    // The caller decides the DPL. See the syscall gate in init_idt().
    idt_entries[num].flags   = flags;
}
