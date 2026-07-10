// task.c -- Implements the multitasking system.
//           Written for JamesM's kernel development tutorials.
//           Redesigned for x86-64.
//
// The 32-bit tutorial performs a context switch by reading EIP with a `pop eax;
// jmp eax` trick, stuffing a magic 0x12345 into EAX so the resumed task can
// recognise itself, and then setting ESP/EBP/EIP with hand-written inline asm.
// That asm is famously miscompiled by GCC >= 4.8, which allocates the `ebp`
// variable into the very register the template uses as scratch.
//
// None of it is necessary. Since chapter 4 our interrupt stub has passed the C
// handler a *pointer* to the register frame it is about to restore. A frame is a
// complete description of a thread of control: every register, RIP, RSP, and
// RFLAGS. Switching tasks means nothing more than handing the stub a different
// frame, on a different stack, and letting iretq do its job.
//
//   irq_common_stub:
//       PUSH_ALL
//       mov rdi, rsp
//       call irq_handler
//       mov rsp, rax        <-- the entire context switch
//       POP_ALL
//       add rsp, 16
//       iretq

#include "task.h"
#include "paging.h"
#include "kheap.h"
#include "monitor.h"

volatile task_t *current_task = 0;
volatile task_t *ready_queue = 0;

u32int next_pid = 1;

// Defined in boot.s -- the stack the kernel booted on. It belongs to task 1.
extern u64int stack_top;

// Defined in timer.c.
extern u32int tick;

void initialise_tasking()
{
    asm volatile("cli");

    // The thread of control we are already running on becomes task 1. Its rsp
    // is left at zero; the first timer interrupt will fill it in when it saves
    // the frame it was handed.
    current_task = ready_queue = (task_t *)kmalloc(sizeof(task_t));
    current_task->id     = next_pid++;
    current_task->rsp    = 0;
    current_task->kstack = (u64int)&stack_top;
    current_task->next   = 0;

    register_interrupt_handler(IRQ0, &schedule);
    register_interrupt_handler(INT_YIELD, &schedule);

    asm volatile("sti");
}

int create_task(void (*entry)(void))
{
    asm volatile("cli");

    task_t *task = (task_t *)kmalloc(sizeof(task_t));
    task->id     = next_pid++;
    task->next   = 0;

    // A fresh kernel stack, page-aligned so we can spot overruns easily.
    task->kstack = kmalloc_a(KERNEL_STACK_SIZE);
    u64int stack_top_addr = task->kstack + KERNEL_STACK_SIZE;

    // Hand-build the frame that iretq will restore the first time this task is
    // scheduled. This is the only place in the kernel that manufactures one,
    // and it is worth reading beside registers_t in isr.h.
    registers_t *frame = (registers_t *)(stack_top_addr - sizeof(registers_t));
    memset((u8int *)frame, 0, sizeof(registers_t));

    frame->rip     = (u64int)entry;
    frame->cs      = 0x08;              // kernel code segment
    frame->ss      = 0x10;              // kernel data segment
    frame->rflags  = 0x202;             // reserved bit 1, plus IF: interrupts on
    frame->userrsp = stack_top_addr;    // the task's own stack, 16-byte aligned
    frame->rbp     = 0;                 // terminate any backtrace here

    task->rsp = (u64int)frame;

    // Append to the ready queue.
    task_t *tmp = (task_t *)ready_queue;
    while (tmp->next)
        tmp = tmp->next;
    tmp->next = task;

    asm volatile("sti");
    return task->id;
}

registers_t *schedule(registers_t *regs)
{
    if (regs->int_no == IRQ0)
        tick++;

    if (!current_task)
        return regs;

    // Save the frame we were handed. It lives on the outgoing task's own kernel
    // stack, so there is nothing to copy -- we just remember where it is.
    current_task->rsp = (u64int)regs;

    // Round-robin.
    current_task = current_task->next;
    if (!current_task)
        current_task = ready_queue;

    // All tasks share the kernel address space in this chapter, so there is no
    // CR3 to reload. When chapter 10 gives each task its own address space, the
    // switch goes here -- and it is safe precisely because every kernel stack is
    // mapped identically in every address space.
    return (registers_t *)current_task->rsp;
}

u32int getpid()
{
    return current_task->id;
}

void task_yield()
{
    asm volatile("int %0" :: "i"(INT_YIELD));
}
