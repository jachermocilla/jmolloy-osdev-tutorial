#!/bin/bash
# mkinitrd.sh -- build initrd.img, then run the kernel with it.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
cd "$HERE"
gcc -Wall -o make_initrd make_initrd.c
./make_initrd test.txt test.txt test2.txt test2.txt
make -C src
echo
echo "run with:  qemu-system-x86_64 -kernel src/kernel -initrd initrd.img"
