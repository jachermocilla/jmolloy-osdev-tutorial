// timer.c -- Initialises the PIT, and handles clock updates.
//            Written for JamesM's kernel development tutorials.

#include "timer.h"
#include "isr.h"
#include "monitor.h"

u32int tick = 0;

void init_timer(u32int frequency)
{
    // Firstly, register our timer callback.
    // NOTE: we no longer register a handler here. task.c registers schedule()
    // against IRQ0, and schedule() bumps `tick`. Two handlers cannot share one
    // vector, and the scheduler must be the one that gets the frame.

    // The value we send to the PIT is the value to divide its input clock
    // (1193180 Hz) by, to get our required frequency. The divisor must fit
    // into 16 bits, so the slowest tick the PIT can produce is
    // 1193180 / 65535 = 18.2 Hz, and the fastest is 1193180 Hz.
    //
    // The tutorial's own comment warns about this and then ignores it: calling
    // init_timer(10) yields a divisor of 119318 (0x1D216), whose low 16 bits
    // are 0xD216 = 53782 -- so the PIT quietly runs at 22.2 Hz instead. Clamp.
    u32int divisor = 1193180 / frequency;
    if (divisor > 0xFFFF) divisor = 0xFFFF;   // ~18.2 Hz, the slowest possible
    if (divisor == 0)     divisor = 1;

    // Send the command byte.
    outb(0x43, 0x36);

    // Divisor has to be sent byte-wise, so split here into upper/lower bytes.
    u8int l = (u8int)(divisor & 0xFF);
    u8int h = (u8int)( (divisor>>8) & 0xFF );

    // Send the frequency divisor.
    outb(0x40, l);
    outb(0x40, h);
}
