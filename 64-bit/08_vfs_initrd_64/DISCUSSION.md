# Chapter 8: The Virtual Filesystem and the Initial Ramdisk

The kernel has been self-contained until now. Every string it printed, every structure it built, and every instruction it ran was compiled into the kernel image, and giving it a new piece of information meant rebuilding the operating system.

No real system works that way. Programs, configuration, firmware, fonts, and everything else live apart from the kernel, and the kernel needs a way to find them, open them, and read them without caring where they sit.

This chapter builds that in two pieces. The **Virtual Filesystem (VFS)** defines a single interface that every filesystem agrees to present. The **initial ramdisk (initrd)** is a tiny filesystem handed to the kernel in memory at boot, letting it read files before it has any idea how to talk to a disk. Together they are the foundation every later filesystem will stand on.

---

# One Interface, Many Filesystems

The kernel wants to read a configuration file. Where does it live? A FAT partition, an ext2 volume, a ramdisk, a server across a network — and to the code asking for the file, none of that should matter. It wants to open a file and read the bytes.

Without an abstraction, every subsystem that touches a file would need to know about every filesystem that might hold one, and adding a filesystem would mean editing all of them.

```text
Applications
      |
      v
Virtual Filesystem          <- one interface
      |
      +------ initrd        <- many implementations
      |
      +------ FAT
      |
      +------ ext2
      |
      +------ network
```

The VFS is not a filesystem. It is a contract: a set of operations — open, read, write, list a directory, find a name within one — that every filesystem promises to provide, however it likes. The kernel says *what* it wants; the driver decides *how*.

The layering is the first of its kind in this kernel, and it is worth naming. Applications talk only to the VFS. The VFS talks to drivers. Each driver talks to its own format. Supporting a new filesystem means writing one new driver and changing nothing else — which is the entire return on the abstraction.

---

# Everything Is a Node

The VFS flattens the world into one structure. A regular file is a node. So is a directory, a device, a pipe, a symbolic link. Each node carries a name, a length, a type flag, an inode number that means whatever its filesystem wants it to mean — and a set of function pointers.

```text
        fs_node_t
+---------------------------+
| name    "test.txt"        |
| flags   FS_FILE           |
| length  17                |
| inode   0                 |
+---------------------------+
| read    ---> initrd_read  |     the driver's implementation
| write   ---> 0            |     null: this node cannot be written
| readdir ---> 0            |     null: this node is not a directory
| finddir ---> 0            |
+---------------------------+
```

The function pointers are the whole mechanism. Reading a file means calling the node's own `read`, which for an initrd file is a `memcpy` out of RAM and for a disk file would be a request to a driver — and the caller cannot tell the difference. This is virtual dispatch, hand-built in C: the object carries its own methods, and the caller invokes them without knowing the type.

Nodes that lack an operation carry a null pointer for it, and the VFS's wrapper functions check before calling. An initrd file is read-only for free, with no flag and no special case — its `write` is simply not there.

---

# Where the Files Come From

There is a bootstrapping problem hiding here. The kernel cannot read files from a disk until the disk driver is loaded, and the disk driver is a file. Something has to break the circle.

The bootloader does. GRUB understands enough about the boot medium to load more than just the kernel, so it loads a second file — a **module** — into memory alongside it and tells the kernel where it landed.

```text
GRUB
  |  loads kernel
  |  loads initrd.img as a module
  |  builds the multiboot info structure
  v
Physical memory                         Kernel entry
+---------------------+
|  kernel image       |                 RDI -> multiboot info
+---------------------+                        |
|  initrd.img         | <---------------- mods_addr[0].mod_start
+---------------------+
```

By the time `main` runs, the ramdisk is already sitting in RAM, and the kernel's job is not to load it but to *interpret* it. No disk driver, no filesystem on disk, no chicken and egg. The pointer GRUB left in `RDI` — ignored since chapter 2 — is finally read, and the module's start address is where the initrd begins.

Two consequences follow immediately. The placement allocator must be pushed past the end of the module, or the heap will build itself on top of the files. And whatever memory the module occupies must be mapped, which it is, because GRUB loads modules low and the kernel maps low memory.

---

# The Image Format

The initrd is a file format invented for this tutorial, and its virtue is that you can hold all of it in your head. There is no compression, no directory tree, no permissions, no timestamps. It is a count, a table, and the bytes.

```text
offset
0x0000  +-------------------------------+
        |  nfiles   (u32)               |   how many entries are real
0x0004  +-------------------------------+
        |  header[0]        76 bytes    |
0x0050  +-------------------------------+
        |  header[1]        76 bytes    |
0x009C  +-------------------------------+
        |  header[2..63]                |   always present, zeroed
        |      62 x 76 bytes            |
0x1304  +-------------------------------+
        |  file 0 contents              |
        +-------------------------------+
        |  file 1 contents              |
        +-------------------------------+
        |  ...                          |
        +-------------------------------+
```

The header table is a fixed 64 entries whether the image holds two files or sixty-four, so the data always begins at the same place: 4 bytes of count plus 64 × 76 bytes of table is 4868, or `0x1304`. That wastes almost 5 KiB on a two-file image, and it buys the builder an offset it can compute before it has read anything.

Each entry describes one file.

```text
byte    0     1                                65   68      72      76
       +-----+----------------------------------+---+-------+-------+
       |magic| name[64]                         |pad| offset| length|
       +-----+----------------------------------+---+-------+-------+
        0xBF  NUL-padded, 63 chars max          \   \        \
                                                 \   \        `- u32, bytes
                                                  \   `- u32, from image start
                                                   `- 3 bytes of alignment
```

Look at those three padding bytes, because they are the most instructive thing in the format. Nobody put them there. The name ends at byte 65, `offset` is a four-byte field, and the compiler must place it on a four-byte boundary, so it inserts three bytes of nothing and starts `offset` at 68. That padding is not an implementation detail — it is *part of the file*, as much as the magic byte is, and any program that wants to read this image must reproduce it exactly.

The `magic` byte, `0xBF`, marks an entry as real. `offset` is measured from the start of the image rather than from anything absolute, which matters: the driver adds the load address at read time and never writes to the image. The file's own bytes stay the file's own bytes. (The original tutorial instead walks the table once at startup and rewrites every `offset` into an absolute address in place — which works only as long as an address fits in a 32-bit field, and stops working the moment it does not.)

Here is a real image, the one the build script produces, decoded:

```text
0x0000   02 00 00 00                        nfiles = 2

0x0004   BF                                  magic
0x0005   74 65 73 74 2E 74 78 74 00 ...      name   = "test.txt"
0x0045   00 00 00                            padding
0x0048   04 13 00 00                         offset = 0x1304
0x004C   11 00 00 00                         length = 17

0x0050   BF                                  magic
0x0051   74 65 73 74 32 2E 74 78 74 00 ...   name   = "test2.txt"
0x0091   00 00 00                            padding
0x0094   15 13 00 00                         offset = 0x1315
0x0098   19 00 00 00                         length = 25

0x009C   00 00 ... (62 zeroed entries) ...

0x1304   "Hello, VFS world!"                 17 bytes
0x1315   "My filename is test2.txt!"         25 bytes
0x132E   end of image
```

Everything is little-endian, because the machine is. File 0's data begins at `0x1304` and runs 17 bytes to `0x1315`, where file 1 begins — the contents are simply concatenated, in table order, with no padding and no terminator. The image is 4910 bytes, of which 4864 are an almost entirely empty table.

The format's limits are worth stating plainly, because each one is a design decision you can see in the bytes. Sixty-four files, because the table is fixed. Sixty-three characters of name, because the field is 64 and needs its terminator. Four gigabytes per file, because `length` is a `u32`. No directories at all: the name field holds a name, not a path, and every file lives in the root. A real initrd is a gzipped cpio archive that solves all of this and would take a chapter of its own to parse; this one takes forty lines and teaches the same lesson.

Note also what the format does *not* contain: the `/dev` directory the kernel shows you. That node is manufactured in memory at startup so a device filesystem has somewhere to mount later. The VFS is free to present nodes that exist nowhere on any medium, and this is the first hint of it.

---

# Reading a File

The driver's setup pass is a loop over the table: check the magic, copy the name into a fresh node, record the length, set the type to a file, point `read` at its own function, and — the key move — store the table index as the node's `inode`.

```text
initrd.img in RAM                     nodes on the heap

nfiles = 2
header[0] test.txt  ---------------->  fs_node_t { name="test.txt", inode=0 }
header[1] test2.txt ---------------->  fs_node_t { name="test2.txt", inode=1 }
                                       fs_node_t { name="dev", FS_DIRECTORY }
```

That inode is how a node finds its way back to its own header. When `read` is called, the driver uses `node->inode` to index the table, adds the load address to the header's `offset`, clamps the request against the header's `length`, and copies. Reading a file from this filesystem is one `memcpy` and some arithmetic — which is exactly the point, since the interface it presents is indistinguishable from one wrapping a disk.

`finddir` walks the node array comparing names, and `readdir` returns them by position. Both are linear scans over a handful of entries, and both are as fast as this filesystem will ever need to be.

---

# External Data Has Fixed Layouts

The last chapter's advice was to widen everything: pointers doubled, so arrays of pointers doubled, and `u32int` became `u64int` across the kernel without much thought. This chapter is where that advice turns dangerous, because the kernel is now reading bytes that another program wrote.

Two structures here are contracts with outsiders. The multiboot information came from GRUB, which ran before the kernel existed and was compiled years ago by someone else. The initrd headers came from `make_initrd.c`, which ran on the build machine. Neither of those programs will be recompiled because the kernel changed, and neither will consult the kernel about layout.

The file-header struct is 76 bytes because `unsigned int` is four bytes under both the i386 and the x86-64 ABIs. Widen `offset` and `length` to 64 bits — the obvious, tidy, wrong thing — and the struct becomes 88 bytes, `offset` moves from byte 68 to byte 72, and nothing anywhere complains.

```text
The file says:            The kernel now thinks:

+----+------+---+--+--+   +----+------+---+----+----+
|0xBF| name |pad|of|ln|   |0xBF| name |pad| offset  |   88 bytes,
+----+------+---+--+--+   +----+------+---+----+----+   striding into
 76 bytes per entry                              ^      the next entry
                                                 |
                              reads 0x736574bf here: 0xBF, 't', 'e', 's'
                              -- the next header's magic and name
```

The kernel strides 88 bytes through an array of 76-byte records, and the second entry it reads is the middle of the first. The number it pulls out as an offset, `0x736574bf`, is the next file's magic byte followed by `t`, `e`, `s` — the beginning of `test2.txt`, interpreted as an address. It will then read a file from wherever that lands.

The rule the chapter is teaching is small enough to memorize and general enough to last a career:

> Widen your in-memory types freely. Never widen a type that describes bytes on a disk, on a wire, or in a hardware register.

The VFS node is internal, so its offsets and lengths widen to 64 bits without a second thought, and a file may be larger than 4 GiB. The initrd header is external, so every field stays exactly the width the format says. Both structs live in the same chapter, in adjacent files, and the difference between them is not the processor — it is who wrote the bytes.

The defence is cheap: assert the layout at compile time, in both programs, so that a helpful widening stops the build instead of corrupting a read.

---

# The Ramdisk Is Temporary

The initrd is a bridge, not a destination. It carries just enough to get the kernel to the point where it can talk to real storage, and then it has done its job.

```text
Bootloader
     |
     v
Kernel + initrd in memory
     |
     v
Initialize drivers          (from files in the initrd)
     |
     v
Mount the real filesystem
     |
     v
Abandon the initrd
```

Real systems do exactly this, and for exactly this reason: the driver for your disk cannot live on your disk. The ramdisk exists to break that circle and then to get out of the way.

---

# Looking Ahead

The kernel's attention has shifted. The last few chapters asked where an object should live while the system runs; this one asks where information should be kept so that it can be found again — and the two questions turn out to be the same question over different timescales.

The initrd will be replaced, and the VFS will not. Later chapters can add a disk driver, a real filesystem, a deeper directory tree, and the ability to load programs from storage, and every one of them arrives as a new set of function pointers behind an interface the rest of the kernel already speaks.

That is the principle worth taking from this chapter, and it is older and larger than operating systems: separate what something does from how it does it, and you can replace the how without anyone noticing.
