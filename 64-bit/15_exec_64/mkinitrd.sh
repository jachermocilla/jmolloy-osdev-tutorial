#!/bin/bash
set -e
HERE=$(cd "$(dirname "$0")" && pwd); cd "$HERE"

# Build the initrd packer.
gcc -Wall -o make_initrd make_initrd.c

# Build the ring 3 program exec() will load. It is freestanding, non-PIC, linked
# at USER_LOAD_BASE, and flattened to a raw binary -- no ELF header, so its first
# byte is _start (see user/prog.ld).
UCFLAGS="-std=gnu11 -m64 -nostdlib -nostdinc -fno-builtin -fno-stack-protector \
         -fno-pie -fno-pic -ffreestanding -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -Wall"
gcc $UCFLAGS -Isrc -c user/prog.c -o user/prog.o
ld -T user/prog.ld -o user/prog.elf user/prog.o
objcopy -O binary user/prog.elf user/prog.bin

# Pack the text files and the program (named "prog") into the initrd.
./make_initrd test.txt test.txt test2.txt test2.txt user/prog.bin prog

make -C src
echo; echo "run with:  qemu-system-x86_64 -kernel src/kernel -initrd initrd.img"
