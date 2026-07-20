//
// isr.c -- High level interrupt service routines and interrupt request handlers.
//          Part of this code is modified from Bran's kernel development tutorials.
//          Rewritten for JamesM's kernel development tutorials.
//          Ported to x86-64.
//

#include "common.h"
#include "isr.h"
#include "monitor.h"

isr_t interrupt_handlers[256];

void register_interrupt_handler(u8int n, isr_t handler)
{
    interrupt_handlers[n] = handler;
}

// This gets called from our ASM interrupt handler stub.
registers_t *isr_handler(registers_t *regs)
{
    if (interrupt_handlers[regs->int_no] != 0)
    {
        isr_t handler = interrupt_handlers[regs->int_no];
        return handler(regs);
    }

    monitor_write("recieved interrupt: ");
    monitor_write_dec((u32int)regs->int_no);
    monitor_put('\n');
    return regs;
}

// This gets called from our ASM interrupt handler stub.
registers_t *irq_handler(registers_t *regs)
{
    // Send an EOI (end of interrupt) signal to the PICs.
    if (regs->int_no >= 40)
    {
        outb(0xA0, 0x20);       // Reset signal to slave.
    }
    outb(0x20, 0x20);           // Reset signal to master.

    if (interrupt_handlers[regs->int_no] != 0)
    {
        isr_t handler = interrupt_handlers[regs->int_no];
        return handler(regs);
    }
    return regs;
}
