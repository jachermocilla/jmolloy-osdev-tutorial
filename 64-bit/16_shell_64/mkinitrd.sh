#!/bin/bash
set -e
HERE=$(cd "$(dirname "$0")" && pwd); cd "$HERE"

# Build the initrd packer.
gcc -Wall -o make_initrd make_initrd.c

# Build every ring 3 program. Each is freestanding, non-PIC, linked at
# USER_LOAD_BASE, and flattened to a raw binary -- no ELF header, so its first
# byte is _start (see user/user.ld). Chapter 15 built one program by hand; there
# are three now, so it is a loop. Add a program by adding its name here.
UCFLAGS="-std=gnu11 -m64 -nostdlib -nostdinc -fno-builtin -fno-stack-protector \
         -fno-pie -fno-pic -ffreestanding -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -Wall"
PROGRAMS="sh hello prog"
for p in $PROGRAMS; do
    gcc $UCFLAGS -Isrc -c user/$p.c -o user/$p.o
    ld -T user/user.ld -o user/$p.elf user/$p.o
    objcopy -O binary user/$p.elf user/$p.bin
done

# Pack the text files and every program into the initrd. A program is packed
# under its bare name ("sh", "hello", "prog"), which is the name the shell execs.
PACK="test.txt test.txt test2.txt test2.txt"
for p in $PROGRAMS; do
    PACK="$PACK user/$p.bin $p"
done
./make_initrd $PACK

make -C src
echo; echo "run with:  qemu-system-x86_64 -kernel src/kernel -initrd initrd.img"
