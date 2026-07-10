# Mastering C for Kernel Development

## A Practical Guide to Reading and Writing 64-Bit Operating System Code

### Table of Contents

* **Introduction**

---

### Part I — Foundations

**ch01.md** — Fixed-Width Integers, Types, and Portability

Understanding integer types, architecture-independent programming, hexadecimal notation, signed and unsigned arithmetic, integer overflow, addresses as data, and the basic data representations used throughout the kernel.

**ch02.md** — Structures, Memory Layout, and Hardware Interfaces

Structures, alignment, padding, packed structures, hardware-defined layouts, register representations, and why binary compatibility matters.

**ch03.md** — Pointers, Memory, and Addressing

Pointers, pointer arithmetic, arrays, casting, memory-mapped I/O, `volatile`, addresses versus values, and understanding memory through kernel examples.

**ch04.md** — Functions, the Stack, and Program Execution

Function calls, stack frames, recursion, parameter passing, local variables, return values, stack growth, and the execution model of C.

---

### Part II — Systems Programming in C

**ch05.md** — Function Pointers, Interrupt Handlers, and Callback Design

Function pointers, dispatch tables, interrupt handlers, callback mechanisms, and dynamic control flow.

**ch06.md** — Dynamic Memory Management

The placement allocator, kernel heap, paging-aware allocation, `kmalloc()`, memory ownership, and allocation strategies.

**ch07.md** — The Build System

Compilation, preprocessing, assembling, linking, linker scripts, compiler flags, ELF files, and how a kernel becomes executable.

**ch08.md** — Defensive Programming and Debugging

Assertions, panic handlers, logging, debugging strategies, QEMU, GDB, and systematic approaches to diagnosing kernel failures.

---

### Part III — From C to the Machine

**ch09.md** — Macros, Bit Manipulation, and Low-Level C Idioms

The C preprocessor, compile-time constants, macros, bit masks, register manipulation, inline functions, and common kernel programming techniques.

**ch10.md** — Linking C and Assembly

Calling conventions, the ABI, stack frames, register preservation, inline assembly, interrupt stubs, and context switching.

**ch11.md** — Building Reusable Kernel Code

Header files, source files, declarations, definitions, include guards, separate compilation, modules, Makefiles, and linker organization.

**ch12.md** — The Freestanding C Environment

Hosted versus freestanding C, writing your own runtime library, implementing common functions, startup code, and life without the standard library.

---

### Part IV — Becoming a Systems Programmer

**ch13.md** — Reading and Writing Kernel Code

Developing strategies for exploring a large codebase, tracing execution, understanding subsystem boundaries, and navigating unfamiliar source files.

**ch14.md** — Debugging a Kernel

Kernel debugging with QEMU and GDB, interpreting crashes, panic handlers, assertions, debugging memory errors, and disciplined debugging practices.

**ch15.md** — Thinking Like a Kernel Programmer

Design philosophy, common C idioms, layered architectures, modular design, defensive programming, mechanism versus policy, and engineering patterns found in modern kernels.

**ch16.md** — Learning by Experiment

A systematic workflow for studying kernel code through experimentation, observation, hypothesis testing, version control, and continuous learning.

---

## Appendix (Suggested)

**appendix-a.md** — Common x86-64 Data Types and Registers

**appendix-b.md** — ASCII Table, Hexadecimal, and Binary Reference

**appendix-c.md** — GDB Cheat Sheet

**appendix-d.md** — QEMU Command-Line Reference

**appendix-e.md** — Common GCC Compiler Flags for Kernel Development

**appendix-f.md** — Suggested Reading

* *The C Programming Language* (Kernighan & Ritchie)
* *Expert C Programming: Deep C Secrets* (Peter van der Linden)
* *Modern C* (Jens Gustedt)
* *Operating Systems: Three Easy Pieces*
* Intel® 64 and IA-32 Architectures Software Developer's Manual
* AMD64 Architecture Programmer's Manual
* Linux Kernel Documentation
* OSDev Wiki

---

## Recommended Reading Order

Read the chapters sequentially.

For each chapter:

1. Read the corresponding chapter in this book.
2. Read the matching `README.md` in the operating-system tutorial.
3. Build the kernel.
4. Run it in QEMU.
5. Modify one piece of code.
6. Observe the result.
7. Record what you learned.

This book is designed to be read alongside the operating-system tutorial. The source code is your laboratory. The chapters explain the language and the engineering decisions behind it. The combination of careful reading and deliberate experimentation will develop the skills needed to understand and extend modern operating systems.

