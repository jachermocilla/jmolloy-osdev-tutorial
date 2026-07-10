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
#include "task.h"
#include "syscall.h"

extern u64int placement_address;
extern u32int tick;

// Provided by link.ld: the page-aligned bounds of the .user_text section.
extern u64int user_text_start;
extern u64int user_text_end;

// The ring 3 program, in user.c.
extern void user_task(void);

// Each task prints its pid in its own column, so you can watch the scheduler
// interleave them.
static void spin(u32int n) { for (volatile u32int i = 0; i < n; i++); }

static void task_b(void)
{
    for (;;)
    {
        monitor_write("  [B pid="); monitor_write_dec(getpid()); monitor_write("]\n");
        spin(4000000);
    }
}

// Hand .user_text to ring 3: readable and executable, but not writable.
static void publish_user_text(void)
{
    u64int lo = (u64int)&user_text_start;
    u64int hi = (u64int)&user_text_end;
    monitor_write(".user_text "); monitor_write_hex64(lo);
    monitor_write(" .. ");        monitor_write_hex64(hi); monitor_write("\n");
    for (u64int a = lo; a < hi; a += 0x1000)
        make_page_user(a, 0 /* not writeable */);
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

    // Prove chapter 8 still works before we start moving the CPU around.
    u64int i = 0;
    struct dirent *node = 0;
    while ((node = readdir_fs(fs_root, i)) != 0)
    {
        monitor_write("Found file "); monitor_write(node->name); monitor_write("\n");
        i++;
    }
    monitor_write("\n");

    init_timer(50);
    initialise_tasking();

    monitor_write("tasking up. pid = "); monitor_write_dec(getpid()); monitor_write("\n");

    initialise_syscalls();
    publish_user_text();

    int b = create_task(&task_b);              // ring 0
    int c = create_user_task(&user_task);      // ring 3
    monitor_write("created kernel pid "); monitor_write_dec(b);
    monitor_write(", user pid "); monitor_write_dec(c); monitor_write("\n\n");

    for (;;)
    {
        monitor_write("[A pid="); monitor_write_dec(getpid());
        monitor_write(" tick="); monitor_write_dec(tick); monitor_write("]\n");
        spin(4000000);
    }

    return 0;
}
