# Chapter 8 (The VFS and the initrd) — 64-bit Port

This is JamesM's chapter 8, ported to x86-64. It builds a small virtual
filesystem layer and an initial ramdisk driver, and reads the initrd out of a
GRUB module.

**Read `07_heap_64/README.md` first.**

Chapter 7's advice was "widen your types." Chapter 8 is where that advice becomes
dangerous, because for the first time your kernel reads bytes that **something
else wrote**.

```
08_vfs_initrd_64/
├── make_initrd.c       ← host program; writes initrd.img
├── mkinitrd.sh         ← build + run helper
├── test.txt, test2.txt
└── src/
    ├── boot.s, link.ld, gdt.s, interrupt.s, isr.*, timer.*  ← unchanged
    ├── descriptor_tables.*, monitor.*, kheap.*, paging.*    ← unchanged since ch7
    ├── ordered_array.*                                      ← unchanged
    ├── common.c/.h     ← strcpy and strcat finally fixed
    ├── multiboot.h     ← every field stays 32 bits
    ├── fs.c/.h         ← the VFS; offsets widen
    ├── initrd.c/.h     ← the driver; on-disk fields do NOT widen
    └── main.c          ← reads mboot_ptr for the first time
```

Eighteen of the twenty-seven source files carry over untouched.

---

## Building and running

```bash
cd 08_vfs_initrd_64
bash mkinitrd.sh          # or: chmod +x mkinitrd.sh && ./mkinitrd.sh
qemu-system-x86_64 -kernel src/kernel -initrd initrd.img
```

```
initrd at 0x111000 .. 0x11232e

Found file dev
        (directory)
Found file test.txt
         contents: "Hello, VFS world!"
Found file test2.txt
         contents: "My filename is test2.txt!"
```

It also boots from the repo's `floppy-initrd.img`, whose `menu.lst` already has
a `module /initrd` line. Copy `src/kernel` to `/kernel` and `initrd.img` to
`/initrd` on the image, then `qemu-system-x86_64 -fda floppy-initrd.img -boot a`.

---

## The rule this chapter teaches

> **Widen your in-memory types freely. Never widen a type that describes bytes on
> a disk, on a wire, or in a hardware register.**

Chapter 7 said "when your pointers double, so does every array of pointers." That
was about memory you own. This chapter is about memory somebody else laid out,
and the rule inverts.

Two structures in this chapter are contracts with an outside party:

| Struct | Written by | Read by |
|---|---|---|
| `struct multiboot` | GRUB, before your kernel ran | `main.c` |
| `initrd_file_header_t` | `make_initrd.c`, on your build machine | `initrd.c` |

Neither may change size. Both are tempting to "fix."

---

## The initrd header

```c
typedef struct
{
    u8int  magic;    // Magic number, for error checking.
    s8int  name[64]; // Filename.
    u32int offset;   // Offset of the file, from the start of the initrd.
    u32int length;   // Length of the file.
} initrd_file_header_t;
```

`unsigned int` is four bytes under both the i386 and the x86-64 System V ABIs, so
this struct is **76 bytes on both**, with `offset` at byte 68. Verify it yourself:

```
32-bit:  sizeof=76  magic@0  name@1  offset@68  length@72
64-bit:  sizeof=76  magic@0  name@1  offset@68  length@72
```

Now do the "obvious" thing and widen `offset` and `length` to `u64int`:

```
64-bit:  sizeof=88  offset@72  length@80
```

The struct grew by twelve bytes and both fields moved. The file on disk did not.
Your kernel now strides 88 bytes per entry through an array of 76-byte entries,
and reads `offset` out of the middle of the *next* file's name.

Here is what it actually reads, decoded from a real `initrd.img`:

```
correct:   entry0: name='test.txt'   offset=0x1304  length=17
widened:   entry0: name='test.txt'   offset=0x736574bf00000011
```

Look at `0x736574bf`. That is `0xBF` — the next entry's magic byte — followed by
`t`, `e`, `s`: the first three characters of `test2.txt`. You are reading the
next header as a pointer.

### And then something specifically 64-bit happens

`initrd_read` computes `initrd_location + header.offset`, which is
`0x111000 + 0x736574bf00000011` = `0x736574bf00111011`.

Bits 48–63 of that are not a sign-extension of bit 47. The address is
**non-canonical**, and the CPU refuses it *before consulting the MMU*. You do not
get a page fault. You get `#GP` — vector 13:

```
recieved interrupt: 13
recieved interrupt: 13
recieved interrupt: 13
...
```

In 32-bit, a garbage pointer gives you a page fault at a garbage address, and the
address tells you something. In 64-bit, a garbage pointer usually gives you a
general protection fault and `CR2` is never even loaded. **`#GP` in a 64-bit
kernel almost always means "you built an address that is not canonical," and it
is one of the most useful diagnostics on the platform.** Learn to read it.

(This is the answer to chapter 6's exercise 6, arriving unbidden.)

### Make the compiler do this for you

```c
_Static_assert(sizeof(initrd_file_header_t) == 76, "on-disk layout changed");
_Static_assert(__builtin_offsetof(initrd_file_header_t, offset) == 68, "offset moved");
_Static_assert(__builtin_offsetof(initrd_file_header_t, length) == 72, "length moved");
```

With those in place, the widened struct does not boot and then misbehave. It does
not compile:

```
initrd.h:44:1: error: static assertion failed: "initrd_file_header_t layout changed"
initrd.h:46:1: error: static assertion failed: "offset moved"
initrd.h:47:1: error: static assertion failed: "length moved"
```

The same assertions live in `make_initrd.c`, so whichever side you compile first
catches the mismatch. Three lines. Put them on every on-disk struct you ever
write.

---

## Don't rewrite the file's own data

The tutorial does this:

```c
// Edit the file's header - currently it holds the file offset relative to the
// start of the ramdisk. We want it relative to the start of memory.
file_headers[i].offset += location;
```

It mutates the loaded image in place, so that `offset` becomes an absolute
address. That fits in a `u32int` today, and would not if GRUB loaded the module
above 4 GiB — which Multiboot 2 permits.

Keep the file's data as the file wrote it. Remember the base separately:

```c
static u64int initrd_location;
...
memcpy(buffer, (u8int *)(initrd_location + header.offset + offset), size);
```

---

## `struct multiboot` is 32-bit forever

```c
struct multiboot { u32int flags; ... u32int mods_count; u32int mods_addr; ... };
```

Every field, by specification. `mods_addr` is a 32-bit **physical** address even
on a 64-bit machine. It cannot be otherwise: GRUB filled the struct in while the
CPU was in 32-bit protected mode.

So reading it needs an explicit widening. The tutorial writes:

```c
u32int initrd_location = *((u32int*)mboot_ptr->mods_addr);      // 32-bit only
```

That is an int-to-pointer cast of the wrong width, and it will not compile clean
in 64-bit. It also treats the module list as two anonymous `u32int`s. Name the
struct and go through `u64int` deliberately:

```c
struct multiboot_module *mods = (struct multiboot_module *)(u64int)mboot_ptr->mods_addr;
u64int initrd_location = (u64int)mods[0].mod_start;
u64int initrd_end      = (u64int)mods[0].mod_end;
```

The 32-bit-ness of the Multiboot 1 info structure is one of the main reasons
Multiboot 2 exists. If you added the Multiboot 2 header from the GRUB kit, its
tag-structured info table gives you 64-bit module addresses.

Two more things worth doing here. Check the flag before trusting the field:

```c
ASSERT(mboot_ptr->flags & MULTIBOOT_FLAG_MODS);
ASSERT(mboot_ptr->mods_count > 0);
```

And note that `mboot_ptr` has been sitting in `RDI`, ignored, since chapter 2.
This is the first time anything reads it.

---

## `strcpy` has been broken since chapter 3

Chapter 3's notes flagged this. Chapter 8 is the first chapter that *calls* it, so
now it must be fixed. Here is the tutorial's version:

```c
char *strcpy(char *dest, const char *src)
{
    do { *dest++ = *src++; } while (*src != 0);
}
```

The test happens *after* the increment. Copying `"dev"`: it writes `d`, `e`, `v`,
then sees `*src == 0` and stops — **without writing the terminating NUL**. It
also never returns `dest`, falling off the end of a non-`void` function.

```
tutorial strcpy("dev") -> dev#####   <-- no NUL terminator was written
```

You can watch the tutorial patching around its own bug:

```c
strcpy(dirent.name, "dev");
dirent.name[3] = 0;                                       // <- here
...
strcpy(dirent.name, root_nodes[index-1].name);
dirent.name[strlen(root_nodes[index-1].name)] = 0;        // <- and here
```

Two call sites, two manual NULs. A third call — `strcpy(initrd_root->name,
"initrd")` — gets no such patch, so the root node's name runs on into whatever
`kmalloc` left behind.

Fix the function, delete the patches:

```c
char *strcpy(char *dest, const char *src)
{
    char *ret = dest;
    while ((*dest++ = *src++) != 0)
        ;
    return ret;
}
```

`strcat` is worse. It contains `*dest = *dest++;` — reading and modifying `dest`
with no sequence point between, which is undefined behaviour — and it returns a
pointer to the end of the string rather than to its beginning.

---

## Bugs in `make_initrd.c`

It is a host program, so none of this affects the kernel. All of it affects you.

- **`int main(char argc, char **argv)`.** `argc` as a `char`. Pass 128 files and
  find out.
- **`headers[64]` is never initialised**, and all 64 entries are written to the
  file. Your `initrd.img` contains up to 4.5 KiB of whatever was on the stack —
  which is both an information leak and a source of non-reproducible builds.
- **No `#include <string.h>` or `<stdlib.h>`**, so `strcpy` and `malloc` are
  implicitly declared. Same 64-bit pointer-truncation hazard described in
  chapter 3, in a program that actually runs on a 64-bit host.
- **`fopen(..., "r")` and `"w"`** rather than `"rb"` / `"wb"`.
- **No bound on `nheaders`**, no check that a filename fits in 64 bytes.
- `malloc(off)` is allocated, never used, and freed.

---

## What the VFS layer looks like in 64-bit

Almost identical, which is the point. `fs_node_t` never leaves memory, so widen
freely:

```c
typedef u64int (*read_type_t)(struct fs_node*, u64int offset, u64int size, u8int*);
```

Offsets and lengths become 64-bit, so a file may exceed 4 GiB. `inode`, `mask`,
`uid`, `gid` and `flags` stay 32-bit because nothing needs more.

The function-pointer table in `fs_node_t` doubles in size — seven pointers at 8
bytes rather than 4 — taking `sizeof(fs_node_t)` from 176 to 208. Nothing depends
on it. That is the difference between a struct you own and a struct you don't.

One small honesty fix in `initrd_readdir`:

```c
if (index - 1 >= nroot_nodes) return 0;    // tutorial
```

`index` is unsigned, so `index - 1` wraps to a huge number when `index` is zero,
and the comparison happens to return 0. That is how `readdir` on `/dev` returns
nothing — by accident. Write what you mean:

```c
if (index == 0 || index - 1 >= nroot_nodes) return 0;
```

---

## Things to try

1. **Widen the on-disk struct on purpose.** Comment out the `_Static_assert`s,
   change `offset` to `u64int`, and boot. Count the `#GP`s. Then put the
   assertions back and watch the build fail instead. This is the single most
   valuable ten minutes in this chapter.

2. **Print the non-canonical address.** In the widened build, print
   `initrd_location + header.offset` before the `memcpy`. Check by hand whether
   bits 48–63 sign-extend bit 47. Then work out which byte of which struct field
   each hex digit came from.

3. **Make the initrd 64-bit clean, properly.** Bump the on-disk `offset` and
   `length` to explicit `uint64_t` in *both* `make_initrd.c` and `initrd.h`,
   update the `_Static_assert`s to the new size, and rebuild both. It works —
   because you changed the contract on both sides at once. That is the difference
   between a format change and a bug.

4. **Restore the old `strcpy`** and delete `dirent.name[3] = 0`. Watch the
   directory listing print `dev` followed by garbage. Now you know what the
   tutorial's manual NULs were hiding.

5. **Read the memory map.** `mboot_ptr->mmap_addr` and `mmap_length` describe
   real physical memory. Chapter 7 hardcodes `PHYS_MEM_SIZE` to 16 MiB. Replace
   it. The direct map and the frames bitset will both scale.

---

## Next

Chapter 9 is multitasking, and it is the chapter the original tutorial gets most
wrong. `read_eip()`'s `pop eax; jmp eax` trick, the `switch_task()` inline asm
that clobbers the register GCC put `ebp` in, and a context switch that saves only
four registers — none of it survives contact with a modern compiler, and the
64-bit version should be *designed*, not translated.

Before you start, notice what you now have that the tutorial does not: an
interrupt frame passed by pointer (chapter 4), so a timer callback can rewrite
`RIP` and `RSP` directly. That is the whole scheduler. You do not need
`read_eip()` at all.
