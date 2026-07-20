// main.c -- Defines the C-code kernel entry point, calls initialisation routines.
//           Made for JamesM's tutorials <www.jamesmolloy.co.uk>

#include "common.h"
#include "monitor.h"
#include "descriptor_tables.h"
#include "timer.h"
#include "paging.h"
#include "multiboot.h"
#include "fs.h"
#include "initrd.h"
#include "thread.h"
#include "proc.h"
#include "keyboard.h"
#include "syscall.h"

extern u64int placement_address;
extern u32int tick;

// Provided by link.ld: the page-aligned bounds of the .user_text section.
extern u64int user_text_start;
extern u64int user_text_end;
extern u64int user_data_start;
extern u64int user_data_end;

// The ring 3 program, in user.c.
extern void thread_task(void);

// ---------------------------------------------------------------------------
// The keyboard demo, in ring 0
// ---------------------------------------------------------------------------
//
// Read one line and echo it. The interesting part is not on the screen -- it is
// what the CPU does between keystrokes. keyboard_getchar() blocks, so while this
// loop waits for a key the boot thread is off the ready queue entirely and the
// idle thread has the processor halted. The machine is asleep until you touch a
// key, at which point the IRQ1 handler wakes this thread and the loop runs one
// more turn. No polling, no spin.
//
// Echo lives here, in the reader, not in the driver. The keyboard driver hands
// out bytes; whether and how they appear on screen is policy, and policy is the
// caller's to set. A password prompt would read the same bytes and echo none.

static void keyboard_demo(void)
{
    char line[128];
    u32int n = 0;

    monitor_write("keyboard: type a line and press Enter.\n");
    monitor_write("> ");

    for (;;)
    {
        char c = keyboard_getchar();

        if (c == '\n')
        {
            monitor_put('\n');
            break;
        }
        if (c == '\b')
        {
            if (n > 0) { n--; monitor_write("\b \b"); }     // erase on screen too
            continue;
        }
        if (n < (u32int)sizeof(line) - 1)
        {
            line[n++] = c;
            monitor_put(c);
        }
    }

    line[n] = 0;
    monitor_write("kernel read: ");
    monitor_write(line);
    monitor_write("\n\n");
}

// Hand .user_text to ring 3: readable and executable, but not writable.
static void publish_user_text(void)
{
    u64int tlo = (u64int)&user_text_start, thi = (u64int)&user_text_end;
    u64int dlo = (u64int)&user_data_start, dhi = (u64int)&user_data_end;
    monitor_write(".user_text "); monitor_write_hex64(tlo);
    monitor_write(" .. ");        monitor_write_hex64(thi); monitor_write("\n");
    monitor_write(".user_data "); monitor_write_hex64(dlo);
    monitor_write(" .. ");        monitor_write_hex64(dhi); monitor_write("\n");
    for (u64int a = tlo; a < thi; a += 0x1000)
        make_page_user(a, 0 /* read-only: code and constants */);
    for (u64int a = dlo; a < dhi; a += 0x1000)
        make_page_user(a, 1 /* writable */);
}

int main(struct multiboot *mboot_ptr)
{
    init_descriptor_tables();
    monitor_clear();

    ASSERT(mboot_ptr->flags & MULTIBOOT_FLAG_MODS);
    ASSERT(mboot_ptr->mods_count > 0);
    struct multiboot_module *mods = (struct multiboot_module *)(u64int)mboot_ptr->mods_addr;
    placement_address = (u64int)mods[0].mod_end;

    initialise_paging();
    fs_root = initialise_initrd((u64int)mods[0].mod_start);

    u64int i = 0;
    struct dirent *node = 0;
    while ((node = readdir_fs(fs_root, i)) != 0)
    {
        monitor_write("Found file "); monitor_write(node->name); monitor_write("\n");
        i++;
    }
    monitor_write("\n");

    init_timer(50);

    // The kernel process owns the address space we booted in. Nothing else may
    // ever free it, which is why it is a static rather than a kmalloc.
    kernel_process.pid       = alloc_id();
    kernel_process.pml4_phys = current_pml4_phys;

    initialise_tasking();

    monitor_write("tasking up. pid = "); monitor_write_dec(getpid());
    monitor_write(", tid = ");           monitor_write_dec(gettid());
    monitor_write("\n\n");

    initialise_syscalls();
    init_keyboard();
    publish_user_text();

    // First in ring 0: the kernel reads a line straight from the driver.
    keyboard_demo();

    // Then the same keyboard one privilege level down. The ring 3 program below
    // reads keys through syscall_getchar() and echoes them, and it owns the
    // keyboard from here on -- the boot thread has read its line and steps
    // aside.
    int u = create_user_task(&thread_task);
    monitor_write("keyboard now belongs to ring 3 (tid ");
    monitor_write_dec(u); monitor_write("). Type away.\n\n");

    // Nothing more for the boot thread to do. Yield forever, so the scheduler
    // spends the processor on the ring 3 reader and the idle thread and the
    // screen stays clean for typing.
    for (;;)
        thread_yield();

    return 0;
}
