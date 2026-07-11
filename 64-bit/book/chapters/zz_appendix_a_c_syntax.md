# Appendix A - C Syntax Essentials

## A Quick Reference for Reading the Tutorial

This appendix collects the C syntax you will actually encounter while reading the tutorial's kernel source. It is not a complete description of the C language, and it does not attempt to teach programming from scratch. Instead, it gathers the essential constructs in one place so that you can look them up quickly. Wherever a topic deserves fuller treatment, the relevant chapter is noted.

---

## A.1 Comments

C provides two comment styles. Both appear throughout the codebase.

```c
// A single-line comment continues to the end of the line.

/* A block comment
   may span several lines. */
```

Comments are removed by the preprocessor before compilation and have no effect on the generated machine code.

---

## A.2 Basic Types and the Kernel's Typedefs

Standard C provides a small set of built-in types.

```c
char   c;   // a single byte
short  s;   // usually 16 bits
int    i;   // usually 32 bits
long   l;   // 64 bits under the LP64 model
void        // "no type"; also used for generic pointers
```

Because exact sizes matter in kernel code, the tutorial defines its own fixed-width names (see Chapter 1).

```c
u8int   b;   // unsigned 8-bit
u16int  w;   // unsigned 16-bit
u32int  d;   // unsigned 32-bit
u64int  q;   // unsigned 64-bit
s32int  n;   // signed 32-bit
```

Prefer these names whenever a value has a hardware-defined width.

---

## A.3 Variables and Constants

A variable is declared with a type and a name, and may be initialized at the same time.

```c
u32int count = 0;
u64int address = 0x100000;
```

Numeric constants may be written in decimal or, more commonly in systems code, in hexadecimal using the `0x` prefix (see Chapter 9). A suffix fixes the constant's type; `U` marks it unsigned and `LL` marks it long long.

```c
0x1000            // hexadecimal
0xFFFFFFFF80000000ULL   // 64-bit unsigned constant
'A'               // a character constant (its numeric code)
"hello"           // a string constant (an array of characters)
```

---

## A.4 Operators

The arithmetic, relational, and logical operators behave as in most languages.

| Category   | Operators                          |
| ---------- | ---------------------------------- |
| Arithmetic | `+`  `-`  `*`  `/`  `%`             |
| Relational | `==`  `!=`  `<`  `>`  `<=`  `>=`    |
| Logical    | `&&`  `\|\|`  `!`                   |

The bitwise and shift operators are used constantly when manipulating flags and hardware registers (see Chapter 9).

| Category | Operators                    |
| -------- | ---------------------------- |
| Bitwise  | `&`  `\|`  `^`  `~`           |
| Shift    | `<<`  `>>`                   |

Note the difference between the logical operators (`&&`, `||`) and the bitwise operators (`&`, `|`); they are not interchangeable. Several shorthand and unary operators also appear regularly.

```c
x += 1;      // compound assignment (also -=, |=, &=, ^=, <<=, >>=)
x++;         // increment (also x--)
flags & 0x4  // test bits without changing them
(u16int *)p  // a cast: reinterpret the type of an expression
sizeof(x)    // the size in bytes of a type or object
cond ? a : b // the conditional (ternary) operator
```

Two operators are specific to memory. The address-of operator `&` yields the location of an object, and the dereference operator `*` accesses the object a pointer refers to (see Section A.7).

---

## A.5 Control Flow

Conditional execution uses `if` and `else`.

```c
if (flags & PAGE_PRESENT) {
    // present
} else {
    // not present
}
```

A `switch` selects among many constant cases; each case usually ends with `break`, and `default` handles the rest.

```c
switch (interrupt_number) {
    case 14:  handle_page_fault(); break;
    default:  handle_generic();    break;
}
```

The three loop forms all appear in kernel code.

```c
for (int i = 0; i < 256; i++) { ... }

while (n--) { ... }

do { ... } while (0);
```

Inside loops, `continue` skips to the next iteration and `break` exits the loop. A function returns with `return`, optionally yielding a value. An intentional infinite loop, often used to halt the processor, is written as follows.

```c
for (;;) { }     // or: while (1) { }
```

---

## A.6 Functions

A function is introduced by a prototype (its interface) and later given a definition (its implementation).

```c
void initialise_paging(void);              // prototype

void initialise_paging(void) {             // definition
    ...
}
```

Parameters are listed with their types, and `void` marks a function that takes no parameters or returns nothing. Two qualifiers appear frequently. `static` limits a function to the file in which it is defined, and `inline` suggests the compiler substitute the body directly at the call site; small hardware routines are often declared `static inline` (see Chapter 4).

```c
static void helper(void) { ... }
static inline u8int inb(u16int port) { ... }
```

---

## A.7 Pointers

A pointer holds the address of an object (see Chapter 3). It is declared with a `*`.

```c
u32int *ptr;              // ptr holds the address of a u32int
u32int  value = 42;

ptr = &value;             // & takes the address of value
*ptr = 100;               // * accesses the object at that address
```

The constant `NULL` represents a pointer that refers to nothing. Pointer arithmetic is scaled by the size of the pointed-to type, so adding one advances by one whole object rather than one byte.

```c
u16int *vga = (u16int *)0xB8000;
vga[1] = 0x0F42;          // same as *(vga + 1); advances two bytes
```

A `void *` is a generic pointer that carries an address without a specific type; it is cast to a concrete type before use.

```c
void *mem = kmalloc(sizeof(page_t));
page_t *page = (page_t *)mem;
```

---

## A.8 Arrays

An array is a contiguous sequence of objects of the same type, indexed from zero.

```c
idt_entry_t idt[256];     // 256 descriptors
idt[0].flags = 0x8E;      // access one element
```

Arrays and pointers are closely related: the array name evaluates to the address of its first element, so `buffer[i]` is equivalent to `*(buffer + i)`.

---

## A.9 Structures

A structure groups related values into one object whose layout in memory is fixed (see Chapter 2). The `typedef struct` form gives the type a convenient name.

```c
typedef struct {
    u16int base_lo;
    u16int sel;
    u8int  flags;
} idt_entry_t;
```

Members are reached with `.` on an object and with `->` through a pointer.

```c
idt_entry_t  entry;
idt_entry_t *p = &entry;

entry.flags = 0x8E;       // via the object
p->flags    = 0x8E;       // via a pointer (same as (*p).flags)
```

When a structure must match a hardware-defined layout exactly, the compiler is told not to insert padding.

```c
typedef struct {
    ...
} __attribute__((packed)) idt_entry_t;
```

---

## A.10 typedef and Function Pointers

`typedef` creates a new name for an existing type; it changes nothing about the generated code and exists only for readability.

```c
typedef unsigned long long u64int;
```

The same mechanism names function-pointer types, which the interrupt subsystem uses to store handlers in a table (see Chapter 5).

```c
typedef void (*isr_t)(registers_t *);

isr_t handlers[256];
handlers[33] = keyboard_handler;   // store a function's address
handlers[33](regs);                // call through the pointer
```

---

## A.11 Type Qualifiers: const and volatile

`const` marks a value that must not be modified. `volatile` tells the compiler that a value may change on its own — for example, a hardware register — so every access must occur exactly as written and must not be optimized away (see Chapter 3).

```c
const u32int limit = 4096;
volatile u32int *status = (volatile u32int *)0xFEE00000;
```

---

## A.12 Storage and Linkage: static and extern

At file scope, `static` keeps a variable or function private to its source file, while `extern` declares that a variable is defined in another file (see Chapter 11).

```c
static u32int placement_address;     // private to this file

extern u32int placement_address;     // defined elsewhere
```

Inside a function, `static` gives a local variable a lifetime that spans the whole program rather than a single call.

---

## A.13 The Preprocessor

The preprocessor runs before compilation and performs textual substitution (see Chapter 11). `#include` inserts the contents of another file.

```c
#include "common.h"
```

`#define` introduces a named constant or a macro. Function-like macros take arguments, and multi-statement macros are commonly wrapped in a `do { ... } while (0)` so they behave like a single statement.

```c
#define PAGE_PRESENT 0x1

#define PANIC(msg) do { panic(msg, __FILE__, __LINE__); } while (0)
```

Include guards prevent a header from being processed twice.

```c
#ifndef COMMON_H
#define COMMON_H
    ...
#endif
```

---

## A.14 GCC Extensions You Will See

The tutorial targets the GCC and Clang compilers and uses a few extensions beyond standard C. Two appear often: `__attribute__(( ... ))` attaches properties to a declaration, such as `packed` on a structure, and the `asm` statement embeds assembly instructions inside a C function (see Chapters 4 and 10).

```c
__attribute__((packed))               // exact memory layout
__attribute__((noreturn))             // function never returns
asm volatile ("cli");                 // an inline assembly instruction
```

---

## Where to Read More

| Topic                         | Chapter |
| ----------------------------- | ------- |
| Types, sizes, and hexadecimal | 1, 9    |
| Structures and packing        | 2       |
| Pointers and memory           | 3       |
| Inline assembly               | 4       |
| Function pointers             | 5       |
| Bitwise operators and flags   | 9       |
| Linking, `static`, `extern`   | 10, 11  |
| The preprocessor and headers  | 11      |
