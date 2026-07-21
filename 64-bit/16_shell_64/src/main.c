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

// The kernel's ring 3 bootstrap, in user.c. It execs "sh" from the initrd.
extern void start_shell(void);

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

    // Drop a thread into ring 3 that immediately execs "sh" from the initrd.
    // From here on the kernel is a service, not a driver: it runs whatever the
    // shell forks and execs, and does nothing on its own initiative.
    monitor_write("starting shell from the initrd...\n\n");
    create_user_task(&start_shell);

    // The boot thread has nothing left to do. Yield forever so the scheduler
    // runs the user processes and the idle thread.
    for (;;)
        thread_yield();

    return 0;
}
