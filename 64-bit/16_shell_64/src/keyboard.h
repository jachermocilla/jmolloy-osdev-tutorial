// keyboard.h -- A PS/2 keyboard driver.
//               Written for JamesM's kernel development tutorials.
//               New in chapter 14.

#ifndef KEYBOARD_H
#define KEYBOARD_H

#include "common.h"

// The keyboard is chapter 13's bounded buffer with one end wired to hardware.
// The producer is not a thread this time -- it is the IRQ1 interrupt handler,
// which reads a scancode the instant a key moves and drops the resulting
// character into a ring. The consumer is whatever thread calls keyboard_getchar,
// which blocks on the ring's "characters available" semaphore until the handler
// posts it.
//
// The whole chapter turns on one asymmetry between those two ends. A thread
// that finds the ring full can wait for room. The interrupt handler cannot: it
// has no thread to put to sleep, and an interrupt that blocks wedges the
// machine. So the handler gets only the half of the semaphore that never waits
// -- it may post, never wait -- and when the ring is full it drops the key.
// See DISCUSSION.md.

// Register the IRQ1 handler. The PIC already has every line unmasked (see
// descriptor_tables.c), so there is nothing else to switch on.
void init_keyboard(void);

// Return the next character typed. Blocks -- with no spin -- until one exists.
// The driver delivers bytes and nothing more; echoing a typed character to the
// screen is the caller's policy, not the driver's.
char keyboard_getchar(void);

#endif // KEYBOARD_H
