# Chapter 9: Multitasking and the CPU Scheduler

Up to this point, the operating system has behaved like a single-threaded program. After booting, the kernel executes one instruction after another along a single execution path. If a function enters an infinite loop, the remainder of the system stops executing because there is no mechanism for running anything else.

Real operating systems behave very differently.

While a user edits a document, the operating system continues handling keyboard input, updating the display, receiving network packets, writing files to disk, and servicing timer interrupts. Although a single processor can execute only one instruction at any instant, the operating system creates the illusion that many activities are occurring simultaneously.

This illusion is known as **multitasking**.

The purpose of this chapter is to build the mechanism that makes multitasking possible. The kernel will learn how to pause one execution context, resume another, and repeatedly alternate between them so quickly that multiple tasks appear to run at the same time.

---

# One Processor, Many Tasks

A common misconception is that multitasking requires multiple processors.

In reality, a single processor can support many independent tasks simply by switching between them.

Imagine three kernel threads.

```text
Task A

AAAAAAAAAAAAAAAAAAAA


Task B

BBBBBBBBBBBBBBBBBBBB


Task C

CCCCCCCCCCCCCCCCCCCC
```

If the processor executed each task until completion before starting the next, the system would spend a long time waiting before later tasks received any processor time.

Instead, the scheduler divides processor time into small intervals.

```text
Time

|A|B|C|A|B|C|A|B|C|...
```

Each task executes briefly before another task is selected.

Because these switches occur many times per second, the user perceives all three tasks as executing concurrently.

The processor is not executing multiple instructions simultaneously.

Rather, it is switching between execution contexts rapidly enough to create the illusion of parallel execution.

---

# What Is a Task?

A task represents an independent execution context.

Each task has its own processor state, including its registers, stack, and instruction pointer.

Conceptually,

```text
Task

+----------------------+
| Registers            |
| Stack                |
| Instruction Pointer  |
| Scheduler State      |
+----------------------+
```

The program code itself may be shared between tasks.

What distinguishes one task from another is its execution state.

If two tasks execute the same function, they nevertheless possess different stacks and different register values.

Each task therefore progresses independently.

---

# Saving the Processor State

Suppose the scheduler wishes to stop executing Task A and begin executing Task B.

The processor cannot simply jump to another function.

It must first preserve Task A exactly as it currently exists.

Conceptually,

```text
Running Task

        |
        v

Save Registers
Save Stack Pointer
Save Instruction Pointer

        |
        v

Resume Later
```

Later, when Task A is scheduled again, these values are restored, allowing execution to continue as though nothing had happened.

From the perspective of the running task, it appears as if execution merely paused for an instant.

---

# Interrupts Make Context Switching Possible

Earlier chapters introduced timer interrupts.

At first, they merely counted ticks.

Now they serve a much more important purpose.

Every timer interrupt provides an opportunity for the scheduler to decide whether another task should execute.

The sequence is straightforward.

```text
Running Task

      |
      v

Timer Interrupt

      |
      v

Scheduler

      |
      +------ Continue Current Task

      |
      +------ Switch to Another Task
```

This approach is known as **preemptive multitasking** because the operating system interrupts the currently running task rather than waiting for it to surrender the processor voluntarily.

Without timer interrupts, a task that never returned from a function could monopolize the processor indefinitely.

---

# Context Switching

The act of moving from one task to another is called a **context switch**.

A context switch consists of two complementary operations.

First, the current task is suspended.

Second, another task is resumed.

Conceptually,

```text
Task A

Running

     |
     v

Save State

     |
     v

Load State

     |
     v

Task B

Running
```

Notice that no computation is copied from one task to another.

Only the processor state changes.

The code remains exactly where it was in memory.

The processor simply begins executing a different instruction stream.

---

# The Interrupt Frame Becomes the Task

One of the advantages of the 64-bit implementation is that interrupt handling already preserves the complete processor state.

When an interrupt occurs, the processor and the interrupt stub together construct a complete snapshot of the running task.

Conceptually,

```text
Interrupt

      |
      v

Saved Register Frame

      |
      v

Scheduler
```

Instead of manually saving dozens of registers, the scheduler simply decides which saved register frame should be restored when the interrupt returns.

This greatly simplifies the implementation.

The interrupt mechanism naturally provides exactly the information required for multitasking.

Rather than inventing a new representation for a task, the scheduler reuses the processor state already saved during interrupt handling.

---

# Round-Robin Scheduling

Many scheduling algorithms exist.

Some consider task priorities.

Others estimate processor usage or attempt to improve cache performance.

The scheduler implemented in this chapter is intentionally much simpler.

Tasks execute in a circular sequence.

```text
Task A

      |
      v

Task B

      |
      v

Task C

      |
      +------------+
                   |
                   v

                Task A
```

After each timer interrupt, the scheduler advances to the next task in the list.

Eventually every task receives processor time.

This strategy is known as **round-robin scheduling**.

Although simple, it demonstrates the fundamental ideas behind preemptive multitasking while remaining easy to understand.

---

# Every Task Needs Its Own Stack

Imagine two tasks sharing the same stack.

Task A calls several functions before the scheduler switches to Task B.

If Task B begins using the same stack, it overwrites Task A's return addresses and local variables.

When Task A resumes, its execution state has been destroyed.

For this reason, every task receives its own stack.

```text
+-----------+
| Task A    |
| Stack     |
+-----------+

+-----------+
| Task B    |
| Stack     |
+-----------+

+-----------+
| Task C    |
| Stack     |
+-----------+
```

The scheduler changes the stack pointer whenever it switches tasks.

Each task therefore resumes execution using its own private call stack.

This separation is essential for correct multitasking.

---

# Cooperative and Preemptive Scheduling

There are two broad approaches to multitasking.

In a **cooperative** system, tasks voluntarily surrender the processor.

```text
Task

Running

      |
      v

Yield

      |
      v

Scheduler
```

If a task never yields, every other task waits indefinitely.

In a **preemptive** system, timer interrupts force the scheduler to run at regular intervals.

```text
Task

Running

      |
      v

Timer Interrupt

      |
      v

Scheduler
```

The operating system developed in this chapter supports both approaches.

Tasks may voluntarily yield the processor, but timer interrupts ensure that no task can monopolize the CPU indefinitely.

This combination provides fairness while remaining relatively simple to implement.

---

# Shared Address Space

Although the scheduler now supports multiple execution contexts, all kernel threads still execute within the same virtual address space.

Conceptually,

```text
Shared Kernel Memory

+---------------------------+
| Kernel Code               |
| Kernel Heap               |
| Global Variables          |
+---------------------------+

     |      |      |

   Task A Task B Task C
```

Every task sees the same memory.

Only the processor state differs.

This greatly simplifies communication between kernel threads, but it also means that every task has unrestricted access to kernel memory.

Future chapters will introduce user processes with independent address spaces, allowing memory protection between executing programs.

---

# The Scheduler as a Resource Manager

This chapter introduces another fundamental responsibility of the operating system.

Earlier chapters managed memory and storage.

The scheduler manages an entirely different resource: **processor time**.

Just as the memory manager decides which process receives physical memory, the scheduler decides which task receives the CPU.

Conceptually,

```text
Applications

       |
       v

CPU Scheduler

       |
       v

Processor
```

The scheduler therefore acts as the operating system's traffic controller.

It determines who runs, when they run, and for how long.

Every multitasking operating system, regardless of its complexity, performs this basic function.

---

# Looking Ahead

The multitasking system introduced in this chapter represents a major turning point in the development of the operating system. The kernel is no longer limited to a single execution path. It can now maintain multiple independent execution contexts and share the processor among them through periodic context switches.

At present, every task executes with full kernel privileges and shares the same address space. This is sufficient for implementing kernel threads, but it does not yet provide the isolation required for running independent programs safely.

The next chapter will build directly upon this scheduler by introducing user-mode execution, separate virtual address spaces, and system calls. Together, these additions will transform kernel threads into true operating system processes, bringing the kernel much closer to the architecture of modern multitasking operating systems.

This chapter also reinforces an important principle that will appear throughout the remainder of the book: an operating system is fundamentally a resource manager. Earlier chapters managed memory and storage. Here, the kernel learns to manage time itself by deciding which task receives the processor and when that task must yield to another. That ability lies at the heart of every modern operating system.

