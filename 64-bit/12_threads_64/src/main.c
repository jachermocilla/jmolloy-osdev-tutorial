// main.c -- Defines the C-code kernel entry point, calls initialisation routines.
//           Made for JamesM's tutorials <www.jamesmolloy.co.uk>

#include "common.h"
#include "monitor.h"
#include "descriptor_tables.h"
#include "timer.h"
#include "paging.h"
#include "multiboot.h"
#include "fs.h"
#include "initrd.h"
#include "thread.h"
#include "proc.h"
#include "sync.h"
#include "syscall.h"

extern u64int placement_address;
extern u32int tick;

// Provided by link.ld: the page-aligned bounds of the .user_text section.
extern u64int user_text_start;
extern u64int user_text_end;
extern u64int user_data_start;
extern u64int user_data_end;

// The ring 3 program, in user.c.
extern void thread_task(void);

static void spin(u32int n) { for (volatile u32int i = 0; i < n; i++); }

// ---------------------------------------------------------------------------
// The mutex demo
// ---------------------------------------------------------------------------
//
// Two threads, one number, 400 increments each. The read and the write are
// deliberately pulled apart by a spin, so the window between them is wide
// enough for the timer to land in. Without the lock, both threads read the same
// value and both write back the same value plus one, and one increment is
// simply gone.
//
// The spin count is not decoration. At 50 Hz a timeslice is 20 ms, and a
// critical section that takes a microsecond will never be interrupted -- not
// rarely, *never*. Tuned too tight, this demo prints OK with the lock commented
// out and teaches the reader the opposite of the lesson. The window has to be a
// meaningful fraction of a timeslice before the race is even reachable.
//
// Comment out the mutex_lock/mutex_unlock pair and run it. Observed here: 519,
// 533, 400, 400 out of an expected 800 -- wrong, and wrong by a different
// amount from run to run. That is the part worth seeing. A race is not a bug
// that happens; it is a bug that is *allowed* to happen, and the difference
// shows up as your answer changing while your code does not.

#define BUMPS 400

static mutex_t counter_lock = MUTEX_INIT;
static volatile u64int shared_counter = 0;

static void bumper(void *arg)
{
    u64int n = (u64int)arg;

    for (u64int i = 0; i < n; i++)
    {
        mutex_lock(&counter_lock);
        u64int v = shared_counter;
        spin(20000);                    // Widen the window. See above.
        shared_counter = v + 1;
        mutex_unlock(&counter_lock);
    }

    thread_exit(n);                     // The value join() will hand back.
}

static void mutex_demo(void)
{
    monitor_write("mutex: two threads, ");
    monitor_write_dec(BUMPS);
    monitor_write(" increments each\n");

    int a = thread_create(&bumper, (void *)BUMPS);
    int b = thread_create(&bumper, (void *)BUMPS);

    u64int ra = 0, rb = 0;
    thread_join(a, &ra);
    thread_join(b, &rb);

    monitor_write("  joined ");   monitor_write_dec(a);
    monitor_write(" (returned "); monitor_write_dec(ra);
    monitor_write(") and ");      monitor_write_dec(b);
    monitor_write(" (returned "); monitor_write_dec(rb);
    monitor_write(")\n");

    monitor_write("  counter = "); monitor_write_dec(shared_counter);
    monitor_write(", expected "); monitor_write_dec(2 * BUMPS);
    monitor_write(shared_counter == 2 * BUMPS ? "  OK\n\n" : "  LOST UPDATES\n\n");
}

// Hand .user_text to ring 3: readable and executable, but not writable.
static void publish_user_text(void)
{
    u64int tlo = (u64int)&user_text_start, thi = (u64int)&user_text_end;
    u64int dlo = (u64int)&user_data_start, dhi = (u64int)&user_data_end;
    monitor_write(".user_text "); monitor_write_hex64(tlo);
    monitor_write(" .. ");        monitor_write_hex64(thi); monitor_write("\n");
    monitor_write(".user_data "); monitor_write_hex64(dlo);
    monitor_write(" .. ");        monitor_write_hex64(dhi); monitor_write("\n");
    for (u64int a = tlo; a < thi; a += 0x1000)
        make_page_user(a, 0 /* read-only: code and constants */);
    for (u64int a = dlo; a < dhi; a += 0x1000)
        make_page_user(a, 1 /* writable: the counter lives here */);
}

int main(struct multiboot *mboot_ptr)
{
    init_descriptor_tables();
    monitor_clear();

    ASSERT(mboot_ptr->flags & MULTIBOOT_FLAG_MODS);
    ASSERT(mboot_ptr->mods_count > 0);
    struct multiboot_module *mods = (struct multiboot_module *)(u64int)mboot_ptr->mods_addr;
    placement_address = (u64int)mods[0].mod_end;

    initialise_paging();
    fs_root = initialise_initrd((u64int)mods[0].mod_start);

    u64int i = 0;
    struct dirent *node = 0;
    while ((node = readdir_fs(fs_root, i)) != 0)
    {
        monitor_write("Found file "); monitor_write(node->name); monitor_write("\n");
        i++;
    }
    monitor_write("\n");

    init_timer(50);

    // The kernel process owns the address space we booted in. Nothing else may
    // ever free it, which is why it is a static rather than a kmalloc.
    kernel_process.pid       = alloc_id();
    kernel_process.pml4_phys = current_pml4_phys;

    initialise_tasking();

    monitor_write("tasking up. pid = "); monitor_write_dec(getpid());
    monitor_write(", tid = ");           monitor_write_dec(gettid());
    monitor_write("\n\n");

    initialise_syscalls();
    publish_user_text();

    // Threads, a lock, and a join -- all in ring 0, all in this address space.
    mutex_demo();

    // The same argument one privilege level down. Compare chapter 11's output.
    int u = create_user_task(&thread_task);
    monitor_write("user thread tid "); monitor_write_dec(u); monitor_write("\n\n");

    for (;;)
    {
        monitor_write("[main tid="); monitor_write_dec(gettid());
        monitor_write(" tick=");     monitor_write_dec(tick); monitor_write("]\n");
        spin(4000000);
    }

    return 0;
}
