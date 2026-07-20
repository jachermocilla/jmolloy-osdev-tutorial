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
#include "semaphore.h"
#include "condvar.h"
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
// The bounded buffer
// ---------------------------------------------------------------------------
//
// One producer makes the numbers 1..ITEMS and drops them in a ring of QN slots.
// One consumer takes them out and adds them up. The buffer is smaller than the
// run, on purpose, so neither thread can finish in one breath: the producer
// fills the ring and has to wait for room, the consumer empties it and has to
// wait for an item. Both waits are the point.
//
// A mutex alone cannot express either wait. A mutex answers "may I touch the
// buffer?"; it has no way to say "wait until the buffer is non-full". That
// second question is what a semaphore is for, and the buffer needs to ask it
// twice -- once counting empty slots, once counting full ones.
//
// The consumer is deliberately slower than the producer, so the ring runs full
// and you can watch the producer block. Speed it up instead and you watch the
// consumer block on an empty ring. The two semaphores hold the invariant either
// way: a thread that would overrun the buffer sleeps instead, and the machine
// spends the time on the thread that can actually make progress.

#define QN     4        // Slots in the ring. Smaller than ITEMS, so it fills.
#define ITEMS  8        // How many numbers the producer makes.

// -- Version 1: two counting semaphores ------------------------------------

static u64int      sq[QN];
static u32int      sq_head = 0, sq_tail = 0;
static mutex_t     sq_lock  = MUTEX_INIT;
static semaphore_t sq_empty = SEM_INIT(QN);     // Empty slots. Starts full of room.
static semaphore_t sq_full  = SEM_INIT(0);      // Full slots. Starts with none.
static volatile u64int sq_sum = 0;

static void sem_producer(void *arg)
{
    (void)arg;
    for (u64int i = 1; i <= ITEMS; i++)
    {
        sem_wait(&sq_empty);            // Wait for a free slot; spend it.
        mutex_lock(&sq_lock);
        sq[sq_head] = i;
        sq_head = (sq_head + 1) % QN;
        monitor_write("  produced "); monitor_write_dec(i); monitor_write("\n");
        mutex_unlock(&sq_lock);
        sem_post(&sq_full);             // Announce a full slot.
        spin(1000000);
    }
    thread_exit(0);
}

static void sem_consumer(void *arg)
{
    (void)arg;
    for (u64int i = 0; i < ITEMS; i++)
    {
        sem_wait(&sq_full);             // Wait for a full slot; spend it.
        mutex_lock(&sq_lock);
        u64int v = sq[sq_tail];
        sq_tail = (sq_tail + 1) % QN;
        sq_sum += v;
        monitor_write("    consumed "); monitor_write_dec(v); monitor_write("\n");
        mutex_unlock(&sq_lock);
        sem_post(&sq_empty);            // Announce a free slot.
        spin(4000000);                  // Slower than the producer: the ring fills.
    }
    thread_exit(0);
}

static void semaphore_demo(void)
{
    monitor_write("semaphore: 1 producer, 1 consumer, ring of ");
    monitor_write_dec(QN); monitor_write("\n");

    int p = thread_create(&sem_producer, 0);
    int c = thread_create(&sem_consumer, 0);
    thread_join(p, 0);
    thread_join(c, 0);

    u64int expect = ITEMS * (ITEMS + 1) / 2;
    monitor_write("  sum = "); monitor_write_dec(sq_sum);
    monitor_write(", expected "); monitor_write_dec(expect);
    monitor_write(sq_sum == expect ? "  OK\n\n" : "  WRONG\n\n");
}

// -- Version 2: the same buffer on a condition variable --------------------
//
// Read this against the version above. The semaphore carried the count of full
// and empty slots inside itself. Here the count is an ordinary variable,
// `cq_n`, and the mutex guards it like any other shared state. The condition
// variables carry nothing; they are only somewhere to sleep.
//
// This is more code to do the same job, which is the lesson, not a mark
// against it. The condvar earns its extra lines when the condition is something
// no count can express -- "the buffer holds a value greater than 100", say, or
// "every worker has checked in". The semaphore version cannot be written for
// those. This one changes by one word in the `while`.

static u64int   cq[QN];
static u32int   cq_head = 0, cq_tail = 0, cq_n = 0;
static mutex_t  cq_lock     = MUTEX_INIT;
static condvar_t cq_notfull  = COND_INIT;       // Signalled when a slot frees.
static condvar_t cq_notempty = COND_INIT;       // Signalled when an item lands.
static volatile u64int cq_sum = 0;

static void cond_producer(void *arg)
{
    (void)arg;
    for (u64int i = 1; i <= ITEMS; i++)
    {
        mutex_lock(&cq_lock);
        while (cq_n == QN)                       // while, not if. See cond_wait.
            cond_wait(&cq_notfull, &cq_lock);
        cq[cq_head] = i;
        cq_head = (cq_head + 1) % QN;
        cq_n++;
        monitor_write("  produced "); monitor_write_dec(i); monitor_write("\n");
        cond_signal(&cq_notempty);
        mutex_unlock(&cq_lock);
        spin(1000000);
    }
    thread_exit(0);
}

static void cond_consumer(void *arg)
{
    (void)arg;
    for (u64int i = 0; i < ITEMS; i++)
    {
        mutex_lock(&cq_lock);
        while (cq_n == 0)                        // while, not if. See cond_wait.
            cond_wait(&cq_notempty, &cq_lock);
        u64int v = cq[cq_tail];
        cq_tail = (cq_tail + 1) % QN;
        cq_n--;
        cq_sum += v;
        monitor_write("    consumed "); monitor_write_dec(v); monitor_write("\n");
        cond_signal(&cq_notfull);
        mutex_unlock(&cq_lock);
        spin(4000000);
    }
    thread_exit(0);
}

static void condvar_demo(void)
{
    monitor_write("condvar: the same buffer, count kept by hand\n");

    int p = thread_create(&cond_producer, 0);
    int c = thread_create(&cond_consumer, 0);
    thread_join(p, 0);
    thread_join(c, 0);

    u64int expect = ITEMS * (ITEMS + 1) / 2;
    monitor_write("  sum = "); monitor_write_dec(cq_sum);
    monitor_write(", expected "); monitor_write_dec(expect);
    monitor_write(cq_sum == expect ? "  OK\n\n" : "  WRONG\n\n");
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

    // A bounded buffer, in ring 0, two ways: first with a pair of counting
    // semaphores, then with condition variables and a count kept by hand. Both
    // print the same numbers and reach the same sum; the difference is where
    // the count lives.
    semaphore_demo();
    condvar_demo();

    // The same producer/consumer one privilege level down, over the syscall
    // interface. The synchronisation objects now live in a shared user page and
    // the kernel operates on them through pointers ring 3 hands it.
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
