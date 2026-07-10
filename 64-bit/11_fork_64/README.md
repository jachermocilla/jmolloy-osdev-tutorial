# Chapter 11 (fork and Address Spaces) — new for the 64-bit series

This chapter is not in JamesM's original ten. It is the payoff chapter 9
deferred: it gives each process its own address space, and implements `fork()`.

**Read `10_usermode_64/README.md` first.**

Until now every task has shared one set of page tables. "User pid 3" could read
"user pid 4"'s stack, because there was only one address space with the user bit
flipped on. This chapter makes them real processes: `fork()` produces a child
that shares the kernel but gets a private copy of everything in user space.

```
11_fork_64/
├── make_initrd.c, test*.txt, mkinitrd.sh
└── src/
    ├── paging.c/.h     ← clone_address_space, free_address_space, switch_pml4_phys
    ├── task.c/.h       ← per-task pml4, fork(), task_exit(), CR3 reload in schedule()
    ├── syscall.c/.h    ← SYS_FORK, SYS_EXIT
    ├── link.ld         ← .user_data split out, so it can be user-writable
    ├── user.c          ← the fork demo
    ├── main.c          ← publishes user text read-only, user data writable
    └── everything else ← unchanged since chapter 10 (24 files)
```

---

## Building and running

```bash
cd 11_fork_64
bash mkinitrd.sh
qemu-system-x86_64 -kernel src/kernel -initrd initrd.img
```

A ring-3 process forks. Parent and child share a `counter` variable at the same
virtual address, then drive it in opposite directions:

```
  fork() returned 4
  [parent] pid=3 counter=1100
  [child]  pid=4 counter=900
  [parent] pid=3 counter=1200
  [child]  pid=4 counter=800
  [parent] pid=3 counter=1300
  [child]  pid=4 counter=700
```

The parent climbs to 1500, the child falls to 500. They write the *same address*
and never collide. That is the whole point of `fork()` in six lines.

---

## fork() is almost free — here is why

Chapter 9 promised this. Here is `fork()` in full:

```c
int fork(registers_t *parent_frame)
{
    task_t *child = kmalloc(sizeof(task_t));
    child->id         = next_pid++;
    child->kstack     = kmalloc_a(KERNEL_STACK_SIZE);
    child->kstack_top = child->kstack + KERNEL_STACK_SIZE;
    child->pml4_phys  = clone_address_space(current_task->pml4_phys);

    registers_t *cframe = (registers_t *)(child->kstack_top - sizeof(registers_t));
    memcpy(cframe, parent_frame, sizeof(registers_t));
    cframe->rax = 0;                     // fork() returns 0 in the child
    child->rsp = (u64int)cframe;

    /* append child to ready queue */
    return child->id;                    // ...and the child's pid in the parent
}
```

That is it. No `read_eip`, no `move_stack`, no stack-pointer heuristic. The
reason is the same reason chapter 9's context switch was one instruction:
**`fork()` is a syscall, so the caller's complete state is already saved** in the
interrupt frame. To make a child that resumes from the same `int $0x80`, you copy
that frame onto a new kernel stack and change one field — the return value.

The parent's frame is untouched, so `int $0x80` returns the child's pid to it.
The child's frame has `rax = 0`, so the same `int $0x80` returns 0 to the child
when the scheduler first runs it. `pid ? parent : child` falls out for free.

The 32-bit tutorial spends ~250 lines and three subtle bugs reaching this point.
You reach it with a `memcpy` and an assignment, because chapters 4 and 9 built
the frame abstraction that makes it trivial.

---

## The real work: clone_address_space

The address space is where the substance is. The rule is one sentence:

> **Share the kernel by reference; copy user pages.**

```c
u64int clone_address_space(u64int src_pml4_phys)
{
    u64int dst = alloc_zeroed_table();      // a fresh PML4
    for (int i = 0; i < 512; i++)
    {
        if (!src->entries[i].present) continue;

        if (i >= 256)
            dst->entries[i] = src->entries[i];      // upper half: share by reference
        else
            /* lower half: walk it, copying user frames */ ;
    }
    return dst;
}
```

### Why the halves are treated differently

x86-64 splits the canonical address space in two. PML4 entries 256–511 map the
upper half (`0xFFFF800000000000` and up), which in this kernel is **all kernel**:
the heap, kernel stacks, everything `kmalloc` returns. Entries 0–255 map the
lower half, which holds **user** pages.

Sharing the upper half by reference — literally copying the 8-byte PML4 entry, so
both address spaces point at the *same* PDPT — means the two processes see
identical kernel mappings, and any kernel mapping made *after* the fork (a new
heap page, the child's own kernel stack) appears in both automatically. That is
the invariant chapter 9 named when it said `mov rsp, rax` was safe across a CR3
switch: **the kernel is identical in every address space.** Chapter 11 is where
that stops being a comment and becomes a `for` loop.

The lower half is walked page by page. At each leaf:

```c
if (pte->user) {
    u64int frame = alloc_phys_frame();                     // a private frame
    memcpy((void*)frame, (void*)(pte->frame << 12), 0x1000); // copy the data
    dst_pte = *pte;  dst_pte.frame = frame >> 12;
} else {
    dst_pte = *pte;                                         // share kernel frame
}
```

### Where the direct map earns its keep

Look at that `memcpy`. It copies frame to frame **by physical address**, with no
temporary mappings, no disabling of paging. That is only possible because of
chapter 7's direct map: physical address `P` is readable at virtual address `P`.
`pte->frame << 12` is a physical address *and* a valid pointer.

The 32-bit tutorial cannot do this. Its `copy_page_physical` **disables paging**,
copies, and re-enables it — with interrupts off and, for a few instructions, an
`ESP` that points at an unmapped address. It is the single most dangerous
sequence in the whole 32-bit codebase, and here it is a one-line `memcpy`.

Every page table `clone_address_space` allocates comes from `alloc_phys_frame`,
i.e. straight from the frame bitset, i.e. the direct-mapped low region — so its
physical address equals the address you write it at, and CR3 gets the right
number with no `virt_to_phys`. This is the same reason kernel page tables were
allocated from the placement allocator back in chapter 6.

---

## The CR3 reload chapter 9 left a hole for

Chapter 9's `schedule()` ended with a comment where the address-space switch
belonged. Here it is:

```c
if (current_task->pml4_phys != current_pml4_phys)
    switch_pml4_phys(current_task->pml4_phys);
```

Three things make this safe to do in the middle of an interrupt:

1. The **kernel stack** the scheduler is standing on is in the upper half, shared
   by reference, so it is mapped identically before and after the reload.
2. The **kernel code** about to run (`POP_ALL`, `iretq`) is likewise shared.
3. `set_kernel_stack` has already pointed `TSS.rsp0` at the incoming task's kernel
   stack, so the *next* ring-3 interrupt lands correctly.

Reload CR3 when the kernel half differs between the two address spaces, and the
CPU keeps executing without noticing, because nothing it can currently see
changed. Only the lower half — which the kernel is not touching — flips.

---

## What "different address space" actually means — measured

The demo prints each process's CR3 alongside the address of `counter`:

```
  fork() returned 4
  cr3=0x116000 &counter=0x107060      <- parent
  cr3=0x229000 &counter=0x107060      <- child
```

Same virtual address, `0x107060`. Different CR3. Two page tables translating one
virtual address to two different physical frames. That single pair of lines is
the definition of a process, and it is why the parent's `counter += 100` and the
child's `counter -= 100` never interfere.

---

## Teardown, and the leak test

`free_address_space` is `clone` run backwards: free every private user frame and
every table the child privately owns, and touch neither the shared kernel frames
nor the shared upper-half tables.

```c
void free_address_space(u64int pml4_phys)
{
    for (int i = 0; i < 256; i++)               // lower half only
        if (present) free_table(sub_tree, ...);  // frees user frames + tables
    clear_frame(pml4_phys);                      // upper half is shared: leave it
}
```

Correctness here is invisible to the eye, so measure it. 200 cycles of clone +
free, counting the frame bitset before and after:

```
churn: cloning+freeing address spaces
  free frames at start: 3546
  free frames after 200 clone+free: 3546
  NO LEAK
```

Exactly balanced. Free one shared kernel frame by mistake and this number would
drift downward and the kernel would corrupt within a few forks; free too little
and it drifts upward. It does neither.

---

## Two honest limitations

**`task_exit` leaks the kernel stack and the `task_t`.** You cannot free the
stack you are standing on, nor the struct you are reading, from within the dying
task. The *address space* — the expensive part — is reclaimed; the two small
fixed-size allocations are not. A real kernel hands them to a reaper thread that
runs in a different context. `task.c` says so at the point it happens.

**Every fork copies the lower-half page-table structure, including the identity
map.** Because chapter 10 put user code in `.user_text` inside PML4 entry 0 —
right next to the kernel's identity map — that entry contains both user and
kernel pages, so it must be walked and partly copied on every fork. The identity
map's ~10 tables get duplicated each time. It is correct but wasteful, and the
fix is the higher-half migration this series has flagged since chapter 6: put the
kernel entirely in the upper half, and the lower half becomes user-only and
cheap to clone. `exec()` is the natural place to do it.

---

## Things to try

1. **Prove isolation the hard way.** Have the child write a sentinel to `counter`
   and `exit`. Have the parent, which forked *before* the child ran, print
   `counter`. It should still read the parent's value, never the child's. If it
   reads the child's, your clone shared a frame it should have copied.

2. **Fork a fork.** Have the child fork again. Three processes, one `counter`
   variable, three private copies. Confirm three distinct CR3s.

3. **Find the wasteful copy.** Add a `frames copied` counter to
   `clone_address_space`, print it per fork, and account for every page. Most of
   them are the identity map's tables. This is the number the higher-half
   migration drives to nearly zero.

4. **Copy-on-write.** The natural next step, and the reason `exec()` matters. Map
   the user frames read-only and *shared* on fork instead of copying, add a
   per-frame reference count, and let `page_fault()` do the copy lazily on the
   first write. Re-run the demo: `frames copied` at fork time drops from N to 0,
   and the copies happen one at a time, only for pages actually written. This is
   the first time `page_fault()` does something other than panic.

5. **Break the halves.** Change `clone_address_space` to share entry 0 by
   reference instead of walking it. Watch the parent and child fight over the
   same `counter` — because now they share the frame the lower half was supposed
   to copy. Then put it back.

---

## Where this leaves the series

You now have real processes: separate address spaces, `fork()`, preemptive
scheduling across them, and a clean teardown. Combined with chapter 10's ring-3
isolation, that is most of what "an operating system" means.

The path from here, in order of payoff:

- **`exec()` and an ELF loader.** Load a program from the initrd into a fresh
  address space. This is where the `.user_text` hack finally dies and the lower
  half becomes user-only — which retires the wasteful clone above and unlocks a
  clean higher-half kernel.
- **Copy-on-write**, per exercise 4. `fork` + `exec` is the motivation: right now
  a `fork` immediately followed by `exec` copies every user page and then throws
  them all away. COW makes that free.
- **`wait()` and a reaper**, which fixes `task_exit`'s leak and gives you the last
  piece of the Unix process model.

Each builds directly on this chapter. None needs anything the 64-bit series has
not already put in place.
