# Chapter 8: The Virtual Filesystem and the Initial Ramdisk

Until this point, the kernel has been almost entirely self-contained. Every string displayed on the screen, every data structure created during initialization, and every piece of executable code has been built directly into the kernel image. If the kernel needs additional information, the only option is to recompile the entire operating system.

Real operating systems do not work this way.

Applications, configuration files, device firmware, fonts, icons, and countless other resources are stored separately from the kernel itself. The operating system must therefore provide a mechanism for locating, opening, and reading files regardless of where those files physically reside.

This chapter introduces that mechanism.

Rather than implementing a complete disk-based filesystem immediately, we begin with two simpler components. The first is a **Virtual Filesystem (VFS)**, which defines a common interface for all filesystems. The second is an **initial ramdisk (initrd)**, a small filesystem loaded into memory during boot that allows the kernel to access files before permanent storage devices are available.

Together, these components establish the foundation upon which every future filesystem will be built.

---

# Why a Virtual Filesystem?

Suppose the kernel wishes to read a configuration file.

Where is that file located?

It might reside on a FAT partition.

It might be stored on an ext2 filesystem.

It could exist inside a ramdisk.

It might even be obtained from a network server.

From the perspective of the application requesting the file, none of these details should matter.

The application simply wants to open a file and read its contents.

Without a VFS, every kernel subsystem would need separate code for every supported filesystem.

```text
Application

     |
     +------ FAT Driver
     |
     +------ ext2 Driver
     |
     +------ Ramdisk Driver
     |
     +------ Network Driver
```

As additional filesystems are added, the complexity grows rapidly.

Instead, the kernel introduces an abstraction layer.

```text
Application
      |
      v
Virtual Filesystem
      |
      +------ FAT
      |
      +------ ext2
      |
      +------ Ramdisk
      |
      +------ Network
```

The Virtual Filesystem becomes the common interface between applications and storage devices.

Applications no longer care how files are stored.

They interact only with the VFS.

---

# What Is a Virtual Filesystem?

A Virtual Filesystem is not a filesystem itself.

Instead, it is an interface that every filesystem agrees to implement.

Every filesystem provides operations such as

* opening a file,
* reading data,
* writing data,
* listing directory contents, and
* locating files by name.

The implementation differs from one filesystem to another, but the operations remain the same.

This is one of the most common examples of abstraction in operating systems.

The kernel separates **what** it wants to do from **how** the operation is performed.

This design allows new filesystems to be added without modifying the rest of the operating system.

---

# Everything Is Represented as a Node

The VFS simplifies the world by representing every filesystem object using the same abstraction.

Whether the object is a regular file, a directory, a device, or something else entirely, the kernel views it as a **filesystem node**.

Conceptually,

```text
Filesystem Object

        |
        v

+----------------+
|   VFS Node     |
+----------------+

Regular File
Directory
Device
Pipe
```

Every node carries information describing the object and provides operations appropriate for that object.

Some nodes support reading.

Others support writing.

Directories support listing their contents.

The kernel interacts with the node without knowing the underlying implementation.

---

# Why Function Pointers?

Different filesystems perform the same operations in different ways.

Reading from a ramdisk is very different from reading from a hard disk.

Nevertheless, the kernel wants to invoke the same operation regardless of the filesystem.

Conceptually,

```text
Read File
     |
     v
 VFS Node
     |
     +------ Ramdisk Read
     |
     +------ FAT Read
     |
     +------ ext2 Read
```

The node itself determines which implementation should execute.

This approach resembles object-oriented programming, although it is implemented entirely in C.

Each filesystem provides its own behavior while presenting the same interface to the rest of the kernel.

---

# Bootstrapping the Operating System

A practical question immediately arises.

If the kernel has not yet initialized disk drivers, how can it load files?

The answer is surprisingly simple.

The bootloader places a small filesystem directly into memory before transferring control to the kernel.

This memory-resident filesystem is called the **initial ramdisk**, or **initrd**.

Conceptually,

```text
GRUB

 |
 | Loads Kernel
 |
 | Loads initrd
 |
 v

Physical Memory

+----------------------+
| Kernel               |
+----------------------+
| initrd               |
+----------------------+
```

When the kernel begins executing, both the kernel image and the initial ramdisk already exist in memory.

No disk driver is required.

The kernel simply interprets the memory region containing the ramdisk.

---

# The Initial Ramdisk Is Temporary

It is important to understand that the initrd is not intended to become the operating system's permanent filesystem.

Instead, it provides enough functionality to allow the kernel to continue booting.

Once storage drivers have been initialized, the operating system typically mounts a real filesystem and eventually abandons the initial ramdisk.

The boot sequence therefore resembles the following.

```text
Bootloader

      |
      v

Kernel + initrd

      |
      v

Initialize Drivers

      |
      v

Mount Real Filesystem
```

The initrd acts as a bridge between the bootloader and the fully operational operating system.

---

# Files Stored in Memory

Unlike a disk-based filesystem, the initial ramdisk already resides entirely in RAM.

Reading a file therefore becomes a simple matter of locating the appropriate region of memory.

Conceptually,

```text
initrd

+---------+---------+---------+
| Header  | File A  | File B  |
+---------+---------+---------+

           |
           v

Requested File
```

Although this is much simpler than reading sectors from a storage device, the interface presented to the rest of the kernel remains exactly the same.

Applications do not know whether the file originated from RAM or from a physical disk.

That is precisely the purpose of the VFS.

---

# External Data Has Fixed Layouts

One of the most important lessons in this chapter concerns binary data formats.

Throughout previous chapters, the kernel freely modified its internal data structures while transitioning from 32-bit to 64-bit code.

This flexibility does **not** apply to data originating outside the kernel.

The layout of an initrd image has already been defined.

Likewise, Multiboot information provided by the bootloader follows a predefined binary format.

Conceptually,

```text
Disk Image

Binary Layout
       |
       v

Kernel Must Match Exactly
```

Changing the size of fields or rearranging their order would cause the kernel to misinterpret the data.

For this reason, structures describing hardware, boot protocols, network packets, and on-disk formats should remain faithful to their published specifications regardless of the processor architecture.

This distinction between **internal** and **external** data structures is one of the most valuable lessons in systems programming.

---

# The First Layered Architecture

This chapter introduces one of the first examples of layered kernel design.

```text
Applications

        |
        v

Virtual Filesystem

        |
        +------ initrd
        |
        +------ FAT
        |
        +------ ext2
```

Notice that applications communicate only with the VFS.

The VFS communicates with filesystem drivers.

Each driver communicates with its own storage format.

Every layer has a single responsibility.

This separation makes the operating system easier to understand, maintain, and extend.

Adding support for a new filesystem requires only a new driver.

The rest of the kernel remains unchanged.

---

# From Memory Management to Storage Management

The previous chapters focused on managing memory.

The kernel learned how to create virtual address spaces, allocate physical frames, and manage dynamic memory through the kernel heap.

This chapter shifts attention toward persistent data.

Instead of asking, "Where should this object live in memory?" the operating system begins asking, "Where should this information be stored so it can be found later?"

The two questions are closely related.

Memory management determines where data exists while the system is running.

File systems determine where data exists between executions.

Together, they form two of the most fundamental resource-management responsibilities of an operating system.

---

# Looking Ahead

The Virtual Filesystem introduced in this chapter establishes a foundation that extends far beyond the initial ramdisk. Future chapters will replace the temporary in-memory filesystem with real disk-based filesystems, support larger directory hierarchies, and eventually load user programs directly from storage.

Perhaps more importantly, the kernel now possesses a uniform interface for accessing data regardless of where that data resides. Whether a file comes from memory, a hard disk, solid-state storage, or even a remote server, the rest of the operating system interacts with it in exactly the same way.

This separation between interface and implementation is one of the defining principles of operating system design. The Virtual Filesystem embodies that principle, allowing the kernel to evolve from a simple educational system into one capable of supporting many different storage technologies without changing the software that depends upon them.

