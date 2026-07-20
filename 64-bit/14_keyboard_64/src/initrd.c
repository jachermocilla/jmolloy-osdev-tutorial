// initrd.c -- Defines the interface for and structures relating to the initial ramdisk.
//             Written for JamesM's kernel development tutorials.
//             Ported to x86-64.

#include "initrd.h"
#include "kheap.h"

initrd_header_t *initrd_header;     // The header.
initrd_file_header_t *file_headers; // The list of file headers.
fs_node_t *initrd_root;             // Our root directory node.
fs_node_t *initrd_dev;              // A directory node for /dev, so we can mount devfs later.
fs_node_t *root_nodes;              // List of file nodes.
u32int nroot_nodes;                 // Number of file nodes.

// Where the multiboot module was loaded.
//
// The tutorial instead does `file_headers[i].offset += location`, rewriting the
// on-disk header in place to hold an absolute address. That works only while
// addresses fit in the struct's 32-bit `offset` field. Keep the file's own data
// intact and add the base at read time.
static u64int initrd_location;

struct dirent dirent;

static u64int initrd_read(fs_node_t *node, u64int offset, u64int size, u8int *buffer)
{
    initrd_file_header_t header = file_headers[node->inode];
    if (offset > header.length)
        return 0;
    if (offset + size > header.length)
        size = header.length - offset;
    memcpy(buffer, (u8int *)(initrd_location + header.offset + offset), size);
    return size;
}

static struct dirent *initrd_readdir(fs_node_t *node, u64int index)
{
    if (node == initrd_root && index == 0)
    {
        strcpy(dirent.name, "dev");
        dirent.ino = 0;
        return &dirent;
    }

    // index is unsigned, so index-1 wraps when index is 0. That is how readdir
    // on /dev returns 0 -- by accident. Say it out loud instead.
    if (index == 0 || index - 1 >= nroot_nodes)
        return 0;

    strcpy(dirent.name, root_nodes[index-1].name);
    dirent.ino = root_nodes[index-1].inode;
    return &dirent;
}

static fs_node_t *initrd_finddir(fs_node_t *node, char *name)
{
    if (node == initrd_root && !strcmp(name, "dev"))
        return initrd_dev;

    u32int i;
    for (i = 0; i < nroot_nodes; i++)
        if (!strcmp(name, root_nodes[i].name))
            return &root_nodes[i];
    return 0;
}

fs_node_t *initialise_initrd(u64int location)
{
    initrd_location = location;

    // Initialise the main and file header pointers and populate the root directory.
    initrd_header = (initrd_header_t *)location;
    file_headers  = (initrd_file_header_t *)(location + sizeof(initrd_header_t));

    // Initialise the root directory.
    initrd_root = (fs_node_t *)kmalloc(sizeof(fs_node_t));
    memset((u8int *)initrd_root, 0, sizeof(fs_node_t));
    strcpy(initrd_root->name, "initrd");
    initrd_root->flags   = FS_DIRECTORY;
    initrd_root->readdir = &initrd_readdir;
    initrd_root->finddir = &initrd_finddir;

    // Initialise the /dev directory (required!)
    initrd_dev = (fs_node_t *)kmalloc(sizeof(fs_node_t));
    memset((u8int *)initrd_dev, 0, sizeof(fs_node_t));
    strcpy(initrd_dev->name, "dev");
    initrd_dev->flags   = FS_DIRECTORY;
    initrd_dev->readdir = &initrd_readdir;
    initrd_dev->finddir = &initrd_finddir;

    nroot_nodes = initrd_header->nfiles;
    root_nodes  = (fs_node_t *)kmalloc(sizeof(fs_node_t) * nroot_nodes);
    memset((u8int *)root_nodes, 0, sizeof(fs_node_t) * nroot_nodes);

    // For every file...
    u32int i;
    for (i = 0; i < nroot_nodes; i++)
    {
        ASSERT(file_headers[i].magic == 0xBF);

        strcpy(root_nodes[i].name, (char *)file_headers[i].name);
        root_nodes[i].length = file_headers[i].length;
        root_nodes[i].inode  = i;
        root_nodes[i].flags  = FS_FILE;
        root_nodes[i].read   = &initrd_read;
    }
    return initrd_root;
}
