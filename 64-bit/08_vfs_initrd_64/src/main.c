// main.c -- Defines the C-code kernel entry point, calls initialisation routines.
//           Made for JamesM's tutorials <www.jamesmolloy.co.uk>

#include "monitor.h"
#include "descriptor_tables.h"
#include "timer.h"
#include "paging.h"
#include "multiboot.h"
#include "fs.h"
#include "initrd.h"

extern u64int placement_address;

int main(struct multiboot *mboot_ptr)
{
    init_descriptor_tables();
    monitor_clear();

    // Find the location of our initial ramdisk.
    //
    // mboot_ptr has been sitting in RDI, ignored, since chapter 2. This is the
    // first time we read it.
    ASSERT(mboot_ptr->flags & MULTIBOOT_FLAG_MODS);
    ASSERT(mboot_ptr->mods_count > 0);

    // mods_addr is a 32-bit *physical* address, by specification, even here.
    // Widening it to a pointer needs an explicit trip through u64int; the
    // tutorial's `*((u32int*)mboot_ptr->mods_addr)` is an int-to-pointer cast
    // of the wrong width and will not compile clean in 64-bit.
    //
    // It also reads the module list as two bare u32ints. Name the struct.
    struct multiboot_module *mods = (struct multiboot_module *)(u64int)mboot_ptr->mods_addr;
    u64int initrd_location = (u64int)mods[0].mod_start;
    u64int initrd_end      = (u64int)mods[0].mod_end;

    // Don't trample our module with placement accesses, please!
    placement_address = initrd_end;

    // Start paging. The direct map from chapter 7 covers the initrd, since GRUB
    // loads modules into low physical memory.
    initialise_paging();

    // Initialise the initial ramdisk, and set it as the filesystem root.
    fs_root = initialise_initrd(initrd_location);

    monitor_write("initrd at "); monitor_write_hex64(initrd_location);
    monitor_write(" .. ");       monitor_write_hex64(initrd_end);
    monitor_write("\n\n");

    // list the contents of /
    u64int i = 0;
    struct dirent *node = 0;
    while ((node = readdir_fs(fs_root, i)) != 0)
    {
        monitor_write("Found file ");
        monitor_write(node->name);
        fs_node_t *fsnode = finddir_fs(fs_root, node->name);

        if ((fsnode->flags & 0x7) == FS_DIRECTORY)
        {
            monitor_write("\n\t(directory)\n");
        }
        else
        {
            monitor_write("\n\t contents: \"");
            char buf[256];
            u64int sz = read_fs(fsnode, 0, 256, (u8int *)buf);
            u64int j;
            for (j = 0; j < sz; j++)
                monitor_put(buf[j]);
            monitor_write("\"\n");
        }
        i++;
    }

    return 0;
}
