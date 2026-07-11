# Mastering C for Kernel Development

## A Practical Guide to Reading and Writing 64-Bit Operating System Code

> A companion textbook for the 64-bit Operating System Tutorial.

---

# Table of Contents

## Introduction

* [Introduction](introduction.md)

---

# Part I — Foundations

* [Chapter 1 — Types and Portability](./chapters/ch01.md)
* [Chapter 2 — Structures, Packing, and Hardware Interaction](ch02.md)
* [Chapter 3 — Pointers and Memory](ch03.md)
* [Chapter 4 — Inline Assembly and Talking to Hardware](ch04.md)

---

# Part II — Systems Programming in C

* [Chapter 5 — Interrupts, Function Pointers, and Event-Driven Programming](ch05.md)
* [Chapter 6 — Dynamic Memory Management](ch06.md)
* [Chapter 7 — Paging and Virtual Memory](ch07.md)
* [Chapter 8 — Building a Heap](ch08.md)

---

# Part III — From C to the Machine

* [Chapter 9 — Bit Manipulation and Flags](ch09.md)
* [Chapter 10 — Linking C and Assembly](ch10.md)
* [Chapter 11 — Building Reusable Kernel Code](ch11.md)
* [Chapter 12 — The Freestanding C Environment](ch12.md)

---

# Part IV — Becoming a Systems Programmer

* [Chapter 13 — Reading and Writing Kernel Code](ch13.md)
* [Chapter 14 — Debugging a Kernel](ch14.md)
* [Chapter 15 — Thinking Like a Kernel Programmer](ch15.md)
* [Chapter 16 — Learning by Experiment](ch16.md)

---

# How to Use This Book

This book is intended to be read alongside the 64-bit operating system tutorial.

For each chapter:

1. Read the chapter in this book.
2. Read the corresponding `README.md` in the operating-system tutorial.
3. Build the kernel.
4. Boot it in QEMU.
5. Modify one piece of code.
6. Observe the results.
7. Record what you learned.

Programming is learned by doing. The source code is your laboratory, and each chapter is designed to help you understand not only *what* the kernel does, but *why* it is written that way.

---

# License

See the repository's LICENSE file for licensing information.

