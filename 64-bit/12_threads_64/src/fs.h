// fs.h -- Defines the interface for and structures relating to the virtual file system.
//         Written for JamesM's kernel development tutorials.
//         Ported to x86-64.

#ifndef FS_H
#define FS_H

#include "common.h"

#define FS_FILE        0x01
#define FS_DIRECTORY   0x02
#define FS_CHARDEVICE  0x03
#define FS_BLOCKDEVICE 0x04
#define FS_PIPE        0x05
#define FS_SYMLINK     0x06
#define FS_MOUNTPOINT  0x08 // Is the file an active mountpoint?

struct fs_node;

// These typedefs define the type of callbacks - called when read/write/open/close
// are called.
//
// Offsets and sizes widen to u64int. Unlike the initrd's on-disk header, this
// struct never leaves memory, so we are free to make a file bigger than 4 GiB.
typedef u64int (*read_type_t)(struct fs_node*,u64int,u64int,u8int*);
typedef u64int (*write_type_t)(struct fs_node*,u64int,u64int,u8int*);
typedef void (*open_type_t)(struct fs_node*);
typedef void (*close_type_t)(struct fs_node*);
typedef struct dirent * (*readdir_type_t)(struct fs_node*,u64int);
typedef struct fs_node * (*finddir_type_t)(struct fs_node*,char *name);

typedef struct fs_node
{
    char name[128];     // The filename.
    u32int mask;        // The permissions mask.
    u32int uid;         // The owning user.
    u32int gid;         // The owning group.
    u32int flags;       // Includes the node type. See #defines above.
    u32int inode;       // Device-specific; lets a filesystem identify files.
    u64int length;      // Size of the file, in bytes.
    u32int impl;        // An implementation-defined number.
    read_type_t read;
    write_type_t write;
    open_type_t open;
    close_type_t close;
    readdir_type_t readdir;
    finddir_type_t finddir;
    struct fs_node *ptr; // Used by mountpoints and symlinks.
} fs_node_t;

struct dirent
{
    char name[128]; // Filename.
    u32int ino;     // Inode number. Required by POSIX.
};

extern fs_node_t *fs_root; // The root of the filesystem.

u64int read_fs(fs_node_t *node, u64int offset, u64int size, u8int *buffer);
u64int write_fs(fs_node_t *node, u64int offset, u64int size, u8int *buffer);
void open_fs(fs_node_t *node, u8int read, u8int write);
void close_fs(fs_node_t *node);
struct dirent *readdir_fs(fs_node_t *node, u64int index);
fs_node_t *finddir_fs(fs_node_t *node, char *name);

#endif
