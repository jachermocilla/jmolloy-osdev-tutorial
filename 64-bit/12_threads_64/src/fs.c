// fs.c -- Defines the interface for and structures relating to the virtual file system.
//         Written for JamesM's kernel development tutorials.
//         Ported to x86-64.

#include "fs.h"

fs_node_t *fs_root = 0; // The root of the filesystem.

u64int read_fs(fs_node_t *node, u64int offset, u64int size, u8int *buffer)
{
    if (node->read != 0)
        return node->read(node, offset, size, buffer);
    else
        return 0;
}

u64int write_fs(fs_node_t *node, u64int offset, u64int size, u8int *buffer)
{
    if (node->write != 0)
        return node->write(node, offset, size, buffer);
    else
        return 0;
}

void open_fs(fs_node_t *node, u8int read, u8int write)
{
    if (node->open != 0)
        node->open(node);
}

void close_fs(fs_node_t *node)
{
    if (node->close != 0)
        node->close(node);
}

struct dirent *readdir_fs(fs_node_t *node, u64int index)
{
    if ((node->flags & 0x7) == FS_DIRECTORY && node->readdir != 0)
        return node->readdir(node, index);
    else
        return 0;
}

fs_node_t *finddir_fs(fs_node_t *node, char *name)
{
    if ((node->flags & 0x7) == FS_DIRECTORY && node->finddir != 0)
        return node->finddir(node, name);
    else
        return 0;
}
