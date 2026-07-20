// user.c -- The ring 3 program that demonstrates semaphores.
//
// Read this beside chapter 12's user.c. That one had two threads scribbling a
// single counter with nothing between them, so the reader could watch a shared
// address space in the raw. This one puts a rule on the sharing: a bounded
// buffer, a producer, a consumer, and the two semaphores that keep them from
// treading on each other.
//
// The synchronisation objects -- one mutex, two semaphores -- live in the
// shared .user_data page, the same page the buffer lives on. Ring 3 never
// touches their internals; it hands their addresses to the kernel through
// syscalls, and the kernel does the blocking and waking. A user-space thread
// that must wait does not spin: syscall_sem_wait puts it to sleep in the kernel
// and the scheduler stops choosing it until a syscall_sem_post elsewhere wakes
// it. The waiting costs no CPU, and none of the machinery that makes that true
// is visible from here -- which is the point of a system call.

#include "common.h"
#include "sync.h"
#include "semaphore.h"
#include "syscall.h"

#define USER_TEXT   __attribute__((section(".user_text")))
#define USER_DATA   __attribute__((section(".user_data")))

#define UQN     4       // Slots in the ring.
#define UITEMS  8       // Numbers the producer makes.

USER_DATA static char m_prod[]  = "  [ring3] produced ";
USER_DATA static char m_cons[]  = "  [ring3] consumed ";
USER_DATA static char m_spawn[] = "  [ring3] producer tid ";
USER_DATA static char m_done[]  = "  [ring3] sum = ";
USER_DATA static char m_nl[]    = "\n";

// The shared buffer and the objects that guard it. Every field here is written
// by both threads and by the kernel on their behalf, and it is all correct
// because the mutex serialises the buffer and the semaphores count the slots.
USER_DATA static volatile u64int ubuf[UQN];
USER_DATA static volatile u32int uhead = 0;
USER_DATA static volatile u32int utail = 0;
USER_DATA static mutex_t     ulock  = MUTEX_INIT;
USER_DATA static semaphore_t uempty = SEM_INIT(UQN);    // Free slots.
USER_DATA static semaphore_t ufull  = SEM_INIT(0);      // Filled slots.
USER_DATA static volatile u64int usum = 0;

USER_TEXT static void spin(u64int n) { for (volatile u64int i = 0; i < n; i++); }

USER_TEXT static void ring3_producer(void)
{
    for (u64int i = 1; i <= UITEMS; i++)
    {
        syscall_sem_wait(&uempty);          // Wait for room.
        syscall_mutex_lock(&ulock);
        ubuf[uhead] = i;
        uhead = (uhead + 1) % UQN;
        syscall_monitor_write(m_prod);
        syscall_monitor_write_dec(i);
        syscall_monitor_write(m_nl);
        syscall_mutex_unlock(&ulock);
        syscall_sem_post(&ufull);           // Announce an item.
        spin(2000000);
    }
    syscall_exit();
}

USER_TEXT void thread_task(void)
{
    u64int ptid = syscall_thread_create(&ring3_producer);
    syscall_monitor_write(m_spawn);
    syscall_monitor_write_dec(ptid);
    syscall_monitor_write(m_nl);

    // This thread is the consumer, and it is the slower of the two, so the ring
    // fills and the producer ends up waiting on uempty -- a wait you can see in
    // the output, where several "produced" lines stop appearing until a
    // "consumed" line frees a slot.
    for (u64int i = 0; i < UITEMS; i++)
    {
        syscall_sem_wait(&ufull);           // Wait for an item.
        syscall_mutex_lock(&ulock);
        u64int v = ubuf[utail];
        utail = (utail + 1) % UQN;
        usum += v;
        syscall_monitor_write(m_cons);
        syscall_monitor_write_dec(v);
        syscall_monitor_write(m_nl);
        syscall_mutex_unlock(&ulock);
        syscall_sem_post(&uempty);          // Announce a free slot.
        spin(6000000);
    }

    syscall_join(ptid);
    syscall_monitor_write(m_done);
    syscall_monitor_write_dec(usum);        // 1 + 2 + ... + 8 = 36.
    syscall_monitor_write(m_nl);

    for (;;) spin(9000000);
}
