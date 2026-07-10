# Chapter 11: Process Creation with `fork()` and Private Address Spaces

In the previous chapter, the operating system learned how to execute programs in user mode. Multiple applications could now run without possessing kernel privileges, and the processor enforced the boundary between user space and kernel space.

Despite this important achievement, every task still shared the same user memory. Although the scheduler could switch between different execution contexts, all processes effectively operated within a single address space.

Real operating systems go much further.

Each process receives its own private view of memory. Two programs may use exactly the same virtual addresses without interfering with one another because those addresses refer to different physical memory. This illusion of exclusive ownership is one of the defining features of modern virtual memory.

This chapter introduces that capability. The operating system will create independent address spaces for each process and implement one of the most influential system calls in Unix: **`fork()`**.

---

# Why Every Process Needs Its Own Memory

Imagine two programs sharing exactly the same memory.

If one program modifies a global variable, the other immediately observes the change.

If one process accidentally overwrites its stack, another process may crash.

If one application corrupts its heap, every other running application becomes vulnerable.

Such a system would be unreliable and nearly impossible to manage.

Instead, each process is given its own virtual address space.

Conceptually,

```text
               Virtual Address

            0x40000000

Process A  -------------> Physical Frame A

Process B  -------------> Physical Frame B
```

Although both programs use the same virtual address, the memory management unit translates each reference to a different physical page.

From the perspective of each process, it appears as though it owns the entire machine.

This abstraction is one of the greatest strengths of virtual memory.

---

# The Address Space Is Divided into Two Regions

One important observation simplifies the design of the operating system.

Although user programs require private memory, the kernel does not.

Every process should execute the same kernel code, use the same device drivers, and access the same interrupt handlers.

Duplicating the kernel for every process would waste enormous amounts of memory.

Instead, the operating system divides the address space into two regions.

```text
+----------------------------------+
|           Kernel Space           |
|      Shared by All Processes     |
+----------------------------------+

+----------------------------------+
|            User Space            |
|      Private Per Process         |
+----------------------------------+
```

The lower portion of the address space belongs to the process.

The upper portion belongs to the operating system.

Whenever the scheduler switches from one process to another, only the user portion changes.

The kernel remains mapped exactly the same way for every process.

This design greatly simplifies scheduling, interrupt handling, and system calls because the kernel always executes within a familiar environment.

---

# Sharing the Kernel, Copying the User

The operating system therefore follows a simple principle:

> Share everything that belongs to the kernel. Duplicate everything that belongs to the user.

Conceptually,

```text
                 Kernel

       +----------------------+
       | Shared Code & Data   |
       +----------------------+
             ^          ^
             |          |
        Process A   Process B

       +----------+ +----------+
       | User Mem | | User Mem |
       +----------+ +----------+
```

Kernel page tables are shared among all processes.

User pages are copied whenever a new process is created.

Although this approach consumes more memory than advanced techniques such as Copy-on-Write, it is considerably easier to understand and provides complete process isolation.

Once this design is mastered, more sophisticated optimizations become natural extensions rather than conceptual leaps.

---

# The Meaning of `fork()`

The `fork()` system call occupies a unique place in Unix-like operating systems.

Unlike most functions, `fork()` does not create a new program.

Instead, it creates a second copy of the currently running process.

Immediately after the call, two nearly identical processes exist.

Both continue executing from exactly the same instruction.

Both possess the same registers.

Both have identical stacks.

Both initially contain the same program data.

The only observable difference is the value returned by `fork()`.

Conceptually,

```text
           Parent Process

                  |
               fork()
                  |
         +--------+--------+
         |                 |
         v                 v

     Parent           Child
```

From this point onward, the two processes execute independently.

---

# One Call, Two Return Values

One of the most surprising aspects of `fork()` is that it appears to return twice.

The parent process receives the process identifier of its newly created child.

The child process receives zero.

Conceptually,

```text
            fork()

        /             \

Parent               Child

return PID          return 0
```

This behavior allows both processes to determine their identity immediately after the call.

A typical application can therefore distinguish between parent and child using a simple conditional statement.

Although unusual at first, this design elegantly supports concurrent execution without requiring separate program entry points.

---

# Cloning an Address Space

Creating a new process involves much more than allocating a task structure.

The operating system must construct a completely new address space.

Conceptually,

```text
Parent

PML4
 |
 +--> User Tables
 |
 +--> Kernel Tables


            clone


Child

PML4
 |
 +--> User Tables (Copy)
 |
 +--> Kernel Tables (Shared)
```

The kernel creates a new top-level page table.

The entries corresponding to kernel memory are copied by reference because every process should execute the same kernel.

The entries corresponding to user memory are duplicated.

Each user page receives a newly allocated physical frame containing the same data as the original.

After cloning completes, the parent and child possess identical memory contents but entirely different physical pages.

From this moment onward, modifications made by one process cannot affect the other.

---

# Context Switching Now Includes Memory

Earlier chapters introduced context switching by saving processor registers and restoring those of another task.

That mechanism remains unchanged.

However, switching between processes now involves another critical component: the page table.

Conceptually,

```text
Scheduler

Save Registers
        |
Load Registers
        |
Load New Page Table
        |
Resume Process
```

Loading a new page table changes the processor's entire view of memory.

The instruction pointer may remain unchanged, yet every virtual address now refers to the memory belonging to a different process.

The processor therefore moves seamlessly between isolated execution environments simply by changing the active page table.

---

# The Illusion of Shared Addresses

One consequence of virtual memory is often surprising to students.

Two processes can use exactly the same addresses while storing completely different information.

Consider a variable named `counter`.

Both parent and child access it using the same virtual address.

Conceptually,

```text
Parent

counter
Virtual 0x40001000
      |
      v
Physical Frame A


Child

counter
Virtual 0x40001000
      |
      v
Physical Frame B
```

Initially, both variables contain the same value because the child's memory was copied from the parent.

As execution continues, each process modifies its own copy.

Although both variables occupy identical virtual addresses, they reside in different physical memory.

This demonstrates that virtual addresses describe locations within a process, not within the machine itself.

---

# Isolation Is the Goal

The true purpose of `fork()` is not duplication.

Its purpose is isolation.

After the child begins executing, the operating system expects the two processes to evolve independently.

One process may allocate additional memory.

Another may terminate.

One may modify data structures.

The other should remain unaffected.

Conceptually,

```text
Before fork()

Parent
Data = 100


After fork()

Parent
Data = 120


Child
Data = 95
```

Although both processes originated from the same state, they quickly diverge.

This independence is fundamental to modern multitasking systems.

Without isolated address spaces, process management would be impossible.

---

# The Cost of Copying

Creating a complete copy of every user page is conceptually simple, but it is also expensive.

Large applications may occupy hundreds of megabytes of memory.

Duplicating all of those pages every time `fork()` executes would consume considerable time and memory.

Modern operating systems therefore employ an optimization known as **Copy-on-Write**.

Instead of copying pages immediately, parent and child temporarily share them as read-only.

Only when one process attempts to modify a page does the operating system create a private copy.

Conceptually,

```text
fork()

        |

Shared Read-Only Page

        |

Write Attempt

        |

Create Private Copy
```

This optimization dramatically improves the performance of process creation.

However, it depends upon a thorough understanding of page faults and memory management.

For this reason, the implementation presented in this chapter deliberately performs complete copies. The simpler design clearly illustrates how process isolation works before introducing more advanced techniques.

---

# The Foundation of Process Management

The addition of private address spaces transforms the scheduler introduced in the previous chapter.

Tasks are no longer merely independent execution contexts sharing one memory image.

They become true processes, each possessing its own memory, stack, and execution state.

The operating system can now execute multiple applications simultaneously while ensuring that they remain isolated from one another.

This capability forms the foundation upon which nearly every modern operating system feature is built.

---

# Looking Ahead

The implementation of `fork()` completes one of the most important pieces of operating system infrastructure. Processes now possess independent address spaces, allowing multiple programs to execute concurrently without corrupting one another's memory. The scheduler manages execution, while the memory management subsystem provides the illusion that every process owns its own machine.

The implementation presented in this chapter intentionally favors clarity over efficiency. Every user page is copied during process creation, making the behavior easy to understand and debug. Future chapters will refine this design through optimizations such as Copy-on-Write, executable loading with `exec()`, demand paging, and more sophisticated process management.

Together, user mode and `fork()` establish the classic process model found in Unix and Unix-like operating systems. From this point onward, the kernel is capable of supporting independent applications that execute concurrently, remain isolated in memory, and interact with the operating system through well-defined interfaces. This marks another major step toward a complete general-purpose operating system.

