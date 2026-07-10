// main.c -- Defines the C-code kernel entry point, calls initialisation routines.
//           Made for JamesM's tutorials <www.jamesmolloy.co.uk>

#include "monitor.h"
#include "descriptor_tables.h"
#include "timer.h"
#include "paging.h"
#include "kheap.h"

int main(struct multiboot *mboot_ptr)
{
    init_descriptor_tables();
    monitor_clear();

    // Before the heap exists, kmalloc() is the placement allocator: it hands
    // out identity-mapped memory just past the end of the kernel.
    u64int a = kmalloc(8);

    initialise_paging();

    // Afterwards, the same call is served by the heap, up in the higher half.
    u64int b = kmalloc(8);
    u64int c = kmalloc(8);

    monitor_write("a: ");   monitor_write_hex64(a);
    monitor_write("\nb: "); monitor_write_hex64(b);
    monitor_write("\nc: "); monitor_write_hex64(c);

    // b and c are adjacent: 16-byte header + 8 bytes of data + 16-byte footer.
    monitor_write("\nc - b: "); monitor_write_hex64(c - b);

    // The heap is not identity-mapped. This is the first time in the tutorial
    // that a virtual address and its physical address differ.
    u64int phys;
    u64int v = kmalloc_p(8, &phys);
    monitor_write("\nv: "); monitor_write_hex64(v);
    monitor_write(" -> phys "); monitor_write_hex64(phys);

    kfree((void *)c);
    kfree((void *)b);

    // Freeing b then c coalesces them back into one hole, so d gets b's address.
    u64int d = kmalloc(12);
    monitor_write("\nd: "); monitor_write_hex64(d);
    monitor_write(d == b ? "   (reused b)\n" : "   (did NOT reuse b)\n");

    return 0;
}
