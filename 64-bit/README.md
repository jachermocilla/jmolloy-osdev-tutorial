# 64-bit port of JMolloy's OSDev tutorial

Made with AI assistance

It is highly recommended that you go through the original 32-bit version of the tutorial first 
before doing the materials here.

The kernel is built as C11 with GNU extensions (`-std=gnu11`), pinned in every Makefile rather 
than left to whatever the compiler defaults to that year.

The port also fixes bugs carried over from the original tutorial. Each one is explained in the 
chapter where it appears.

Chapters 11 and 12 are new. Chapter 11 gives each process its own address space and implements 
`fork()`. Chapter 12 separates a task into a process and a thread, adds `thread_create()`, and 
protects shared memory with a mutex.

In each tutorial, there is a README.md file that discusses the porting process. There is also 
a DISCUSSION.md that talks about the concepts.
