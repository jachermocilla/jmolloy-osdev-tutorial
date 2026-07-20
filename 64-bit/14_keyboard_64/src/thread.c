// thread.c -- Threads, and the scheduler that runs them.
//             Written for JamesM's kernel development tutorials.
//             New in chapter 12; most of it is chapter 11's task.c, re-sorted.
//
// The context switch itself has not changed since chapter 9, and that is the
// point. Threads are not a new mechanism. The stub still does
//
//     mov rsp, rax        <-- the entire context switch
//
// and the scheduler still just returns a different frame. What changed is
// bookkeeping: which struct owns the address space, and what a thread is
// allowed to be doing when it is not on the CPU.

#include "thread.h"
#include "proc.h"
#include "sync.h"
#include "paging.h"
#include "kheap.h"
#include "monitor.h"
#include "descriptor_tables.h"

volatile thread_t *current_thread = 0;

// The ready queue holds threads that want the CPU and are not on it. The
// running thread is NOT on this queue -- it is on no queue -- which is what
// makes block() two lines instead of a list walk.
static thread_t *ready_head = 0;
static thread_t *ready_tail = 0;

// Every thread ever created, so join() can find a zombie that is on no queue.
static thread_t *all_threads = 0;

// One counter feeds both pids and tids. Two threads must never share a tid,
// because USER_STACK_FOR() below keys a stack address off it, and a process id
// that collides with a thread id would be merely confusing rather than fatal.
// A real kernel numbers them separately and does not place stacks by
// arithmetic. This one is a tutorial, and the alternative is a page allocator
// we do not need yet.
static u32int next_id = 1;

static thread_t *idle_thread = 0;

// Defined in boot.s -- the stack the kernel booted on. It belongs to thread 1.
extern u64int stack_top;

// Defined in timer.c.
extern u32int tick;

// ---------------------------------------------------------------------------
// Queues
// ---------------------------------------------------------------------------

void enqueue_ready(thread_t *t)
{
    t->state = THREAD_READY;
    t->next  = 0;
    if (ready_tail)
        ready_tail->next = t;
    else
        ready_head = t;
    ready_tail = t;
}

static thread_t *dequeue_ready(void)
{
    thread_t *t = ready_head;
    if (!t)
        return 0;
    ready_head = t->next;
    if (!ready_head)
        ready_tail = 0;
    t->next = 0;
    return t;
}

void wq_enqueue(thread_t **head, thread_t *t)
{
    t->next = 0;
    if (!*head)
    {
        *head = t;
        return;
    }
    thread_t *p = *head;
    while (p->next)
        p = p->next;
    p->next = t;
}

thread_t *wq_dequeue(thread_t **head)
{
    thread_t *t = *head;
    if (!t)
        return 0;
    *head = t->next;
    t->next = 0;
    return t;
}

thread_t *find_thread(u32int tid)
{
    for (thread_t *t = all_threads; t; t = t->all_next)
        if (t->tid == tid)
            return t;
    return 0;
}

void registry_add(thread_t *t)
{
    t->all_next = all_threads;
    all_threads = t;
}

u32int alloc_id(void)
{
    return next_id++;
}

static void registry_remove(thread_t *t)
{
    if (all_threads == t)
    {
        all_threads = t->all_next;
        return;
    }
    for (thread_t *p = all_threads; p->all_next; p = p->all_next)
        if (p->all_next == t)
        {
            p->all_next = t->all_next;
            return;
        }
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

// Everything create_task(), create_user_task() and thread_create() have in
// common: a struct, a kernel stack, and a hand-built frame for iretq to
// restore the first time this thread is scheduled. The three constants that
// differ -- cs, ss, and which stack userrsp names -- are the only difference
// between a kernel thread and a user one. That is worth staring at.
static thread_t *thread_alloc(u64int rip, u64int arg, u64int cs, u64int ss)
{
    thread_t *t = (thread_t *)kmalloc(sizeof(thread_t));
    t->tid        = alloc_id();
    t->proc       = (process_t *)current_thread->proc;
    t->kstack     = kmalloc_a(KERNEL_STACK_SIZE);
    t->kstack_top = t->kstack + KERNEL_STACK_SIZE;
    t->state      = THREAD_READY;
    t->next       = 0;
    t->joiner     = 0;
    t->retval     = 0;
    t->detached   = 0;

    registers_t *frame = (registers_t *)(t->kstack_top - sizeof(registers_t));
    memset((u8int *)frame, 0, sizeof(registers_t));
    frame->rip     = rip;
    frame->rdi     = arg;            // System V: the first integer argument.
    frame->cs      = cs;
    frame->ss      = ss;
    frame->rflags  = 0x202;          // Reserved bit 1, plus IF: interrupts on.
    frame->rbp     = 0;              // Terminate any backtrace here.
    // userrsp is filled in by the caller: a kernel thread runs on its own
    // kernel stack, a user thread on a stack that could not be mapped until
    // tid existed.
    t->rsp = (u64int)frame;

    t->proc->nthreads++;
    registry_add(t);
    return t;
}

int thread_create(void (*entry)(void *), void *arg)
{
    u64int f = irq_save();

    thread_t *t = thread_alloc((u64int)entry, (u64int)arg, 0x08, 0x10);
    ((registers_t *)t->rsp)->userrsp = t->kstack_top;
    enqueue_ready(t);

    irq_restore(f);
    return t->tid;
}

// Chapter 9's entry point, preserved. It shared the address space then too.
static void thread_trampoline_void(void *fn)
{
    ((void (*)(void))fn)();
}

int create_task(void (*entry)(void))
{
    return thread_create(&thread_trampoline_void, (void *)entry);
}

// ---------------------------------------------------------------------------
// Ring 3
// ---------------------------------------------------------------------------

extern void enter_user_mode(u64int rip, u64int rsp);

// Each thread gets a user stack of its own, well clear of the direct map.
// Chapter 11 keyed this off the pid, which was fine when a pid named exactly
// one thread of control. Two threads in one process would now land on the same
// stack and destroy each other in about four instructions. Key it off the tid.
#define USER_STACK_BASE  0x0000700000000000UL
#define USER_STACK_SIZE  0x4000
#define USER_STACK_FOR(tid) (USER_STACK_BASE + (u64int)(tid) * 0x10000UL)

u64int build_user_stack(u32int tid)
{
    u64int base = USER_STACK_FOR(tid);
    for (u64int a = base; a < base + USER_STACK_SIZE; a += 0x1000)
        map_user_page(a);
    return base + USER_STACK_SIZE;
}

void switch_to_user_mode(void (*entry)(void), u64int user_stack_top)
{
    set_kernel_stack(current_thread->kstack_top);
    enter_user_mode((u64int)entry, user_stack_top);
}

int create_user_task(void (*entry)(void))
{
    u64int f = irq_save();

    thread_t *t = thread_alloc((u64int)entry, 0, 0x1B, 0x23);
    ((registers_t *)t->rsp)->userrsp = build_user_stack(t->tid);
    enqueue_ready(t);

    irq_restore(f);
    return t->tid;
}

// ---------------------------------------------------------------------------
// Startup
// ---------------------------------------------------------------------------

static void idle_entry(void *unused)
{
    (void)unused;
    for (;;)
        asm volatile("hlt");
}

void initialise_tasking(void)
{
    u64int f = irq_save();

    // The thread of control we are already running on becomes thread 1 of the
    // kernel process. Its rsp is left at zero; the first timer interrupt fills
    // it in when it saves the frame it was handed.
    thread_t *t = (thread_t *)kmalloc(sizeof(thread_t));
    t->tid        = alloc_id();
    t->rsp        = 0;
    t->kstack     = (u64int)&stack_top;
    t->kstack_top = (u64int)&stack_top;
    t->proc       = &kernel_process;
    t->state      = THREAD_RUNNING;
    t->next       = 0;
    t->joiner     = 0;
    t->retval     = 0;
    t->detached   = 1;          // Nobody will ever join the kernel's first thread.
    kernel_process.nthreads++;
    registry_add(t);
    current_thread = t;

    // If this thread ever drops to ring 3, this is the stack the CPU loads when
    // it comes back.
    set_kernel_stack(t->kstack_top);

    // The idle thread exists because the ready queue can now be *empty*. In
    // chapter 11 every task was always runnable, so round-robin could never
    // come up empty-handed. Block the last runnable thread on a mutex and the
    // scheduler has nothing to return -- unless there is one thread that is
    // never blocked and never counted. It is not on the ready queue; it is what
    // you get when the ready queue is gone.
    idle_thread = thread_alloc((u64int)&idle_entry, 0, 0x08, 0x10);
    ((registers_t *)idle_thread->rsp)->userrsp = idle_thread->kstack_top;
    idle_thread->detached = 1;

    register_interrupt_handler(IRQ0, &schedule);
    register_interrupt_handler(INT_YIELD, &schedule);

    irq_restore(f);
    asm volatile("sti");
}

// ---------------------------------------------------------------------------
// The scheduler
// ---------------------------------------------------------------------------

registers_t *schedule(registers_t *regs)
{
    if (regs->int_no == IRQ0)
        tick++;

    if (!current_thread)
        return regs;

    // Save the frame we were handed. It lives on the outgoing thread's own
    // kernel stack, so there is nothing to copy -- we just remember where it is.
    current_thread->rsp = (u64int)regs;

    // Round-robin by rotation: the running thread goes to the tail, the head
    // comes off. A thread that stopped being RUNNABLE while it held the CPU --
    // it blocked, or it exited -- simply is not put back, and by the invariant
    // it is already on whatever queue it belongs on. This is why block() does
    // not need to unlink anything.
    thread_t *prev = (thread_t *)current_thread;
    if (prev != idle_thread && prev->state == THREAD_RUNNING)
        enqueue_ready(prev);

    thread_t *next = dequeue_ready();
    if (!next)
        next = idle_thread;

    next->state = THREAD_RUNNING;
    current_thread = next;

    // Tell the CPU which kernel stack to load if this thread takes an interrupt
    // while it is in ring 3. Forget this and the next syscall from user mode
    // pushes its frame onto whatever RSP0 happened to hold -- usually the
    // *previous* thread's kernel stack, which it is still using.
    set_kernel_stack(next->kstack_top);

    // Load the incoming thread's address space. Two threads of one process have
    // the same pml4_phys, so this is now skipped for the common case rather
    // than by luck -- which is most of why threads are cheaper than processes.
    if (next->proc->pml4_phys != current_pml4_phys)
        switch_pml4_phys(next->proc->pml4_phys);

    return (registers_t *)next->rsp;
}

u32int getpid(void)  { return current_thread->proc->pid; }
u32int gettid(void)  { return current_thread->tid; }

void thread_yield(void)
{
    asm volatile("int %0" :: "i"(INT_YIELD));
}

// ---------------------------------------------------------------------------
// Blocking
// ---------------------------------------------------------------------------
//
// Call with interrupts already masked. block() marks us unrunnable and yields;
// schedule() sees state != RUNNING and does not put us back on the ready queue.
// The caller is responsible for having put us on some *other* queue first --
// otherwise nothing will ever unblock us, and the invariant is broken in the
// one direction that cannot be detected.

void block(void)
{
    current_thread->state = THREAD_BLOCKED;
    thread_yield();
}

void unblock(thread_t *t)
{
    if (t->state != THREAD_BLOCKED)
        return;
    enqueue_ready(t);
}

// ---------------------------------------------------------------------------
// exit and join
// ---------------------------------------------------------------------------

void thread_exit(u64int retval)
{
    u64int f = irq_save();

    thread_t *me = (thread_t *)current_thread;
    me->retval = retval;
    me->state  = THREAD_ZOMBIE;

    if (me->joiner)
        unblock(me->joiner);

    // The refcount. Chapter 11 freed the address space unconditionally here,
    // which was safe only because no two tasks could share one. Now the last
    // thread out turns off the lights.
    //
    // Note we are still *executing* in the space we are freeing. That is fine
    // for exactly one reason: free_address_space() touches the lower half, and
    // our code and our stack are in the kernel half, which is shared by
    // reference in every address space. Chapter 11 relied on this silently.
    if (--me->proc->nthreads == 0 && me->proc != &kernel_process)
    {
        free_address_space(me->proc->pml4_phys);
        kfree(me->proc);
    }

    // A zombie's kernel stack cannot be freed here -- we are standing on it --
    // nor its thread_t, which we are reading. join() frees both, because the
    // joiner is standing somewhere else. A detached thread has no joiner and
    // leaks both; a real kernel hands them to a reaper. The address space, the
    // expensive part, is reclaimed above either way.

    irq_restore(f);
    thread_yield();             // Never returns: schedule() will not pick a zombie.
    for (;;)
        asm volatile("hlt");    // Unreachable. Here so the compiler agrees.
}

int thread_join(u32int tid, u64int *retval)
{
    u64int f = irq_save();

    thread_t *t = find_thread(tid);
    if (!t || t == current_thread || t->detached || t->joiner)
    {
        irq_restore(f);
        return -1;
    }

    if (t->state != THREAD_ZOMBIE)
    {
        t->joiner = (thread_t *)current_thread;
        block();
        // We resume here with interrupts still masked -- our saved frame has
        // IF clear, because block() yielded from inside this critical section.
    }

    if (retval)
        *retval = t->retval;

    // And here is the leak chapter 11 apologised for, closed. We are not on the
    // dying thread's stack, so we can free it.
    registry_remove(t);
    kfree((void *)t->kstack);
    kfree(t);

    irq_restore(f);
    return 0;
}

int thread_detach(u32int tid)
{
    u64int f = irq_save();
    thread_t *t = find_thread(tid);
    if (t)
        t->detached = 1;
    irq_restore(f);
    return t ? 0 : -1;
}
