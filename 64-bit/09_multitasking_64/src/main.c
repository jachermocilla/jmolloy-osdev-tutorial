// main.c -- Defines the C-code kernel entry point, calls initialisation routines.
//           Made for JamesM's tutorials <www.jamesmolloy.co.uk>

#include "monitor.h"
#include "descriptor_tables.h"
#include "timer.h"
#include "paging.h"
#include "multiboot.h"
#include "fs.h"
#include "initrd.h"
#include "task.h"

extern u64int placement_address;
extern u32int tick;

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

static void task_c(void)
{
    for (int i = 0; i < 3; i++)
    {
        monitor_write("    [C pid="); monitor_write_dec(getpid());
        monitor_write(" yielding]\n");
        task_yield();          // give up the rest of the timeslice, voluntarily
        spin(6000000);
    }
    monitor_write("    [C exiting the loop; it will spin from here]\n");
    for (;;) spin(8000000);
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

    int b = create_task(&task_b);
    int c = create_task(&task_c);
    monitor_write("created pids "); monitor_write_dec(b);
    monitor_write(" and "); monitor_write_dec(c); monitor_write("\n\n");

    for (;;)
    {
        monitor_write("[A pid="); monitor_write_dec(getpid());
        monitor_write(" tick="); monitor_write_dec(tick); monitor_write("]\n");
        spin(4000000);
    }

    return 0;
}
