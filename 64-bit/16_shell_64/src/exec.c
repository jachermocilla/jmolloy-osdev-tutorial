// exec.c -- exec(): turn the calling process into a program loaded from a file.
//           Written for JamesM's kernel development tutorials.
//           New in chapter 15.

#include "exec.h"
#include "fs.h"
#include "paging.h"
#include "proc.h"
#include "thread.h"
#include "sync.h"
#include "kheap.h"
#include "common.h"

// Ring 3 selectors, the same constants create_user_task() hands to iretq.
#define USER_CS  0x1B
#define USER_SS  0x23

// The user stack layout is thread.c's, repeated here because exec builds a
// stack in a fresh address space rather than through create_user_task().
#define USER_STACK_BASE  0x0000700000000000UL
#define USER_STACK_SIZE  0x4000
#define USER_STACK_FOR(tid) (USER_STACK_BASE + (u64int)(tid) * 0x10000UL)

// Map one fresh, zeroed, user-accessible, writable page into the *currently
// loaded* address space. exec switches CR3 to the new space first, so this maps
// into it. It is map_user_page() in miniature, spelled out because exec needs
// to zero the page before copying a partial last page of the file over it.
static void map_one(u64int vaddr)
{
    page_t *page = get_page(vaddr, 1, (page_table_t *)current_pml4_phys);
    alloc_frame(page, 0 /* user */, 1 /* writable */);
    invlpg(vaddr);
    memset((u8int *)vaddr, 0, 0x1000);
}

int do_exec(registers_t *frame, const char *path)
{
    // Everything that can fail happens first, before a single destructive act.
    // If the file is not there, the caller must be left exactly as it was.
    fs_node_t *node = finddir_fs(fs_root, (char *)path);
    if (!node)
        return -1;

    u64int size = node->length;
    u8int *buf = (u8int *)kmalloc(size);
    read_fs(node, 0, size, buf);

    u64int f = irq_save();

    // A fresh address space: the kernel, shared; no user pages at all. The old
    // image -- the caller's code, data, and stack -- is about to become
    // unreachable, and that is the point of exec.
    u64int old_pml4 = current_thread->proc->pml4_phys;
    u64int new_pml4 = new_kernel_address_space(kernel_process.pml4_phys);

    // Move into it now, while we still hold the kernel stack that the switch
    // does not disturb. The kernel image, the direct map, the heap, and this
    // very stack are all shared into the new space, so execution steps across
    // the CR3 load without noticing. From here, USER_LOAD_BASE and the stack
    // pages we map are visible to the CPU immediately.
    switch_pml4_phys(new_pml4);
    current_thread->proc->pml4_phys = new_pml4;

    // Lay the program down. A flat binary is loaded whole at its link address;
    // the tail of the final page past `size` was zeroed by map_one(), which is
    // the closest a flat binary comes to having a .bss.
    u64int npages = (size + 0xFFF) / 0x1000;
    for (u64int i = 0; i < npages; i++)
        map_one(USER_LOAD_BASE + i * 0x1000);
    memcpy((u8int *)USER_LOAD_BASE, buf, size);
    kfree(buf);

    // A fresh user stack for this thread's id.
    u64int base = USER_STACK_FOR(current_thread->tid);
    for (u64int a = base; a < base + USER_STACK_SIZE; a += 0x1000)
        map_one(a);
    u64int ustack_top = base + USER_STACK_SIZE;

    // Rewrite the frame the syscall will return through. Instead of resuming
    // the caller after its int $0x80, the iretq will drop into the loaded
    // program at its entry, in ring 3, on the new stack. exec is, in the end,
    // loading an image and pointing the return at it.
    memset((u8int *)frame, 0, sizeof(registers_t));
    frame->rip     = USER_LOAD_BASE;        // entry == load base, by convention
    frame->cs      = USER_CS;
    frame->ss      = USER_SS;
    frame->userrsp = ustack_top;
    frame->rflags  = 0x202;                 // reserved bit 1, plus IF
    frame->rbp     = 0;

    // The old image is now nobody's. Free it -- unless it was the kernel's own
    // never-freed space, which only happens if exec is called straight from a
    // kernel thread rather than from a forked child.
    if (old_pml4 != kernel_process.pml4_phys)
        free_address_space(old_pml4);

    irq_restore(f);
    return 0;       // "returns" through the rewritten frame, into the new program
}
