// keyboard.c -- A PS/2 keyboard driver.
//               Written for JamesM's kernel development tutorials.
//               New in chapter 14.

#include "keyboard.h"
#include "isr.h"
#include "sync.h"
#include "semaphore.h"
#include "monitor.h"

// ---------------------------------------------------------------------------
// The ring
// ---------------------------------------------------------------------------
//
// One producer (the IRQ1 handler) writes `head`. One consumer (the reader
// thread) writes `tail`. Because there is exactly one of each, and the
// semaphore below orders every hand-off, the two never write the same field --
// which is why the ring needs no mutex of its own. The single reader takes a
// character only after the handler has posted it, so the slot it reads is
// always one the handler has finished writing.

#define KBD_RING 128

static volatile char kbd_ring[KBD_RING];
static volatile u32int kbd_head = 0;    // Written only by the IRQ handler.
static volatile u32int kbd_tail = 0;    // Written only by the reader.

// "Characters available." The reader spends one per keystroke; the handler
// posts one per keystroke. This is chapter 13's counting semaphore, doing
// exactly its chapter-13 job -- the only new thing is who calls post.
static semaphore_t kbd_avail = SEM_INIT(0);

// ---------------------------------------------------------------------------
// Scancode set 1 -> ASCII
// ---------------------------------------------------------------------------
//
// A make code arrives when a key goes down, and the same code with bit 7 set
// arrives when it comes back up. Only two keys here care about the release: the
// shift keys, which set a mode. Everything else acts on the press and ignores
// the release. The tables are sparse on purpose -- keypad, function keys, and
// international layouts are all left out, because none of them teaches anything
// the printable keys do not.

#define SC_LSHIFT   0x2A
#define SC_RSHIFT   0x36

static const char kbdmap[128] =
{
    [0x02]='1',[0x03]='2',[0x04]='3',[0x05]='4',[0x06]='5',[0x07]='6',
    [0x08]='7',[0x09]='8',[0x0A]='9',[0x0B]='0',[0x0C]='-',[0x0D]='=',
    [0x0E]='\b',[0x0F]='\t',
    [0x10]='q',[0x11]='w',[0x12]='e',[0x13]='r',[0x14]='t',[0x15]='y',
    [0x16]='u',[0x17]='i',[0x18]='o',[0x19]='p',[0x1A]='[',[0x1B]=']',
    [0x1C]='\n',
    [0x1E]='a',[0x1F]='s',[0x20]='d',[0x21]='f',[0x22]='g',[0x23]='h',
    [0x24]='j',[0x25]='k',[0x26]='l',[0x27]=';',[0x28]='\'',[0x29]='`',
    [0x2B]='\\',
    [0x2C]='z',[0x2D]='x',[0x2E]='c',[0x2F]='v',[0x30]='b',[0x31]='n',
    [0x32]='m',[0x33]=',',[0x34]='.',[0x35]='/',
    [0x39]=' ',
};

static const char kbdmap_shift[128] =
{
    [0x02]='!',[0x03]='@',[0x04]='#',[0x05]='$',[0x06]='%',[0x07]='^',
    [0x08]='&',[0x09]='*',[0x0A]='(',[0x0B]=')',[0x0C]='_',[0x0D]='+',
    [0x0E]='\b',[0x0F]='\t',
    [0x10]='Q',[0x11]='W',[0x12]='E',[0x13]='R',[0x14]='T',[0x15]='Y',
    [0x16]='U',[0x17]='I',[0x18]='O',[0x19]='P',[0x1A]='{',[0x1B]='}',
    [0x1C]='\n',
    [0x1E]='A',[0x1F]='S',[0x20]='D',[0x21]='F',[0x22]='G',[0x23]='H',
    [0x24]='J',[0x25]='K',[0x26]='L',[0x27]=':',[0x28]='"',[0x29]='~',
    [0x2B]='|',
    [0x2C]='Z',[0x2D]='X',[0x2E]='C',[0x2F]='V',[0x30]='B',[0x31]='N',
    [0x32]='M',[0x33]='<',[0x34]='>',[0x35]='?',
    [0x39]=' ',
};

static volatile u32int shift = 0;

// ---------------------------------------------------------------------------
// The producer: an interrupt, not a thread
// ---------------------------------------------------------------------------

static void kbd_push(char c)
{
    u32int next = (kbd_head + 1) % KBD_RING;

    // Full. The reader has fallen behind, and this is the one place the
    // producer's hands are tied: it cannot wait for room, because it is an
    // interrupt with no thread to suspend. So it drops the key and returns. A
    // thread-producer would have called sem_wait here and slept; an interrupt
    // must not, and the difference is the whole point of the chapter.
    if (next == kbd_tail)
        return;

    kbd_ring[kbd_head] = c;
    kbd_head = next;

    // The half of the semaphore an interrupt is allowed to touch: post never
    // blocks. It bumps the count and, if a reader is asleep on it, makes it
    // runnable. The scheduler picks the woken reader on a later tick; nothing
    // is rescheduled from inside the handler.
    sem_post(&kbd_avail);
}

registers_t *keyboard_handler(registers_t *regs)
{
    // irq_handler() in isr.c has already sent the EOI, so all this routine owes
    // the hardware is a single read of the data port to clear the byte.
    u8int sc = inb(0x60);

    if (sc & 0x80)                      // A release. Only shift cares.
    {
        u8int code = sc & 0x7F;
        if (code == SC_LSHIFT || code == SC_RSHIFT)
            shift = 0;
        return regs;
    }

    if (sc == SC_LSHIFT || sc == SC_RSHIFT)
    {
        shift = 1;
        return regs;
    }

    char c = shift ? kbdmap_shift[sc] : kbdmap[sc];
    if (c)
        kbd_push(c);

    return regs;                        // No context switch: the frame is unchanged.
}

// ---------------------------------------------------------------------------
// The consumer: an ordinary thread
// ---------------------------------------------------------------------------

char keyboard_getchar(void)
{
    // Block until the handler has posted a character. This is where a naive
    // driver would spin on inb(0x64), burning a whole timeslice to learn the
    // answer is still "no key". Here the thread sleeps and the scheduler spends
    // the CPU on work that exists.
    sem_wait(&kbd_avail);

    // Take one byte. Masking here excludes the only other party that touches
    // the ring -- the IRQ1 handler -- and on one processor masking is exactly
    // the right tool for that, because that other party genuinely is an
    // interrupt. Chapter 12 warned that masking is not locking; here the thing
    // being masked out is an interrupt, so for once it is.
    u64int f = irq_save();
    char c = kbd_ring[kbd_tail];
    kbd_tail = (kbd_tail + 1) % KBD_RING;
    irq_restore(f);

    return c;
}

void init_keyboard(void)
{
    sem_init(&kbd_avail, 0);

    // Clear any byte the firmware left sitting in the controller, so the first
    // real keystroke is not preceded by a stale one.
    (void)inb(0x60);

    register_interrupt_handler(33, &keyboard_handler);      // IRQ1 -> vector 33.
}
