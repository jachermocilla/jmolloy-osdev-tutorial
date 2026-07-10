// task.h -- Defines the structures and prototypes needed to multitask.
//           Written for JamesM's kernel development tutorials.
//           Redesigned for x86-64.

#ifndef TASK_H
#define TASK_H

#include "common.h"
#include "isr.h"

#define KERNEL_STACK_SIZE 0x4000     // 16 KiB per task.

typedef struct task
{
    u32int id;              // Process ID.
    u64int rsp;             // Saved pointer to this task's registers_t frame.
    u64int kstack;          // Base of this task's kernel stack (for freeing).
    u64int kstack_top;      // Top of it. Loaded into TSS.rsp0 on every switch.
    struct task *next;      // The next task in the ready queue.
} task_t;

// Initialises the tasking system, turning the current thread of control into
// task 1.
void initialise_tasking();

// Creates a new task which begins executing at `entry`. Returns its pid.
// The task runs in ring 0, in the kernel's address space, on a stack of its own.
int create_task(void (*entry)(void));

// Creates a task that begins executing at `entry` in ring 3, on a user stack of
// its own. `entry` must live in the .user_text section.
int create_user_task(void (*entry)(void));

// Drops the *current* task into ring 3 at `entry`, on the given user stack.
void switch_to_user_mode(void (*entry)(void), u64int user_stack_top);

// The pid of the currently running task.
u32int getpid();

// Give up the rest of this timeslice. Raises INT_YIELD, which lands in the
// same scheduler the timer uses.
void task_yield();

// The scheduler. Registered against the timer IRQ and against INT_YIELD.
//
// It is handed the frame of the task that was interrupted and returns the frame
// of the task that should run next. Returning is the context switch.
registers_t *schedule(registers_t *regs);

#endif
